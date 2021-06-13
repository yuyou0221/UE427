// Copyright Epic Games, Inc. All Rights Reserved.

#include "Streamer.h"
#include "AudioCapturer.h"
#include "VideoCapturer.h"
#include "PlayerSession.h"
#include "VideoEncoderFactory.h"
#include "PixelStreamerDelegates.h"
#include "PixelStreamingEncoderFactory.h"
#include "PixelStreamingSettings.h"
#include "WebRTCLogging.h"

#include "WebSocketsModule.h"


DEFINE_LOG_CATEGORY(PixelStreamer);


bool FStreamer::CheckPlatformCompatibility()
{
	return AVEncoder::FVideoEncoderFactory::Get().HasEncoderForCodec(AVEncoder::ECodecType::H264);
}

FStreamer::FStreamer(const FString& InSignallingServerUrl, const FString& InStreamerId)
	: SignallingServerUrl(InSignallingServerUrl), StreamerId(InStreamerId)
{
	RedirectWebRtcLogsToUnreal(rtc::LoggingSeverity::LS_VERBOSE);

	FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("AVEncoder"));

	// required for communication with Signalling Server and must be called in the game thread, while it's used in signalling thread
	FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");

#if PLATFORM_WINDOWS
	WebRtcSignallingThread = MakeUnique<FThread>(TEXT("PixelStreamerSignallingThread"), [this]() { WebRtcSignallingThreadFunc(); });
#else
	WebRtcSignallingThreadFunc();
#endif
}

FStreamer::~FStreamer()
{
	// stop WebRtc WndProc thread
#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
	PostThreadMessage(WebRtcSignallingThreadId, WM_QUIT, 0, 0);
	UE_LOG(PixelStreamer, Log, TEXT("Exiting WebRTC WndProc thread"));
	WebRtcSignallingThread->Join();
#else
	DeleteAllPlayerSessions();
	WebRtcSignallingThread->Stop();
	PeerConnectionFactory = nullptr;
	rtc::CleanupSSL();
#endif
}

// #endif

void FStreamer::WebRtcSignallingThreadFunc()
{
	// initialisation of WebRTC stuff and things that depends on it should happen in WebRTC signalling thread

#if PLATFORM_WINDOWS || PLATFORM_XBOXONE

	// WebRTC assumes threads within which PeerConnectionFactory is created is the signalling thread
	WebRtcSignallingThreadId = FPlatformTLS::GetCurrentThreadId();
	
	// init WebRTC networking and inter-thread communication
	rtc::WinsockInitializer WSInitialiser;
	if (WSInitialiser.error())
	{
		UE_LOG(PixelStreamer, Error, TEXT("Failed to initialise Winsock"));
		return;
	}

	rtc::Win32SocketServer SocketServer;
	rtc::Win32Thread W32Thread(&SocketServer);
	rtc::ThreadManager::Instance()->SetCurrentThread(&W32Thread);
	rtc::InitializeSSL();

	// WebRTC assumes threads within which PeerConnectionFactory is created is the signalling thread
	PeerConnectionConfig = {};

	auto videoEncoderFactory = std::make_unique<FPixelStreamingVideoEncoderFactory>();
	VideoEncoderFactory = videoEncoderFactory.get();

	PeerConnectionFactory = webrtc::CreatePeerConnectionFactory(
		nullptr, // network_thread
		nullptr, // worker_thread
		nullptr, // signal_thread
		new rtc::RefCountedObject<FAudioCapturer>(), // audio device manager
		webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>(),
		webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>(),
		std::move(videoEncoderFactory),
		std::make_unique<webrtc::InternalDecoderFactory>(),
		nullptr, // audio_mixer
		nullptr); // audio_processing
	check(PeerConnectionFactory);

	// now that everything is ready
	ConnectToSignallingServer();

	// WebRTC window messaging loop
	MSG Msg;
	BOOL Gm;
	while ((Gm = ::GetMessageW(&Msg, NULL, 0, 0)) != 0 && Gm != -1)
	{
		::TranslateMessage(&Msg);
		::DispatchMessage(&Msg);
	}

	// WebRTC stuff created in this thread should be deleted here.
	DeleteAllPlayerSessions();
	PeerConnectionFactory = nullptr;

	rtc::CleanupSSL();

#elif PLATFORM_LINUX
	// WebRTC assumes threads within which PeerConnectionFactory is created is the signalling thread
	WebRtcSignallingThread = TUniquePtr<rtc::Thread>(rtc::Thread::CreateWithSocketServer().release());//MakeUnique<rtc::Thread>(std::make_unique<rtc::PhysicalSocketServer>());
	WebRtcSignallingThread->SetName("WebRtcSignallingThread", nullptr);
	WebRtcSignallingThread->Start();
	// rtc::ThreadManager::Instance()->SetCurrentThread(thread);

	// thread->PostTask(rtc::Location("WebRtcSignallingThreadFunc", __FILE__, __LINE__), [&]()
	// 	{
	rtc::InitializeSSL();

	PeerConnectionConfig = {};

	auto videoEncoderFactory = std::make_unique<FPixelStreamingVideoEncoderFactory>();
	VideoEncoderFactory = videoEncoderFactory.get();

	PeerConnectionFactory = webrtc::CreatePeerConnectionFactory(
		nullptr, // network_thread
		nullptr, // worker_thread
		WebRtcSignallingThread.Get(), // signal_thread
		new rtc::RefCountedObject<FAudioCapturer>(), // audio device manager
		webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>(),
		webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>(),
		std::move(videoEncoderFactory),
		std::make_unique<webrtc::InternalDecoderFactory>(),
		nullptr, // audio_mixer
		nullptr); // audio_processing
	check(PeerConnectionFactory);

	// now that everything is ready
	ConnectToSignallingServer();
	// });
#endif 
}

void FStreamer::ConnectToSignallingServer()
{
	SignallingServerConnection = MakeUnique<FSignallingServerConnection>(SignallingServerUrl, *this, StreamerId);
}

void FStreamer::OnFrameBufferReady(const FTexture2DRHIRef& FrameBuffer)
{
	if (bStreamingStarted && VideoSource)
		VideoSource->OnFrameReady(FrameBuffer);
}

void FStreamer::OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config)
{
	PeerConnectionConfig = Config;
}

void FStreamer::OnOffer(FPlayerId PlayerId, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp)
{
	CreatePlayerSession(PlayerId);
	AddStreams(PlayerId);

	FPlayerSession* Player = GetPlayerSession(PlayerId);
	checkf(Player, TEXT("just created player %s not found"), *PlayerId);

	Player->OnOffer(MoveTemp(Sdp));

	{
		FScopeLock PlayersLock(&PlayersCS);
		for (auto&& PlayerEntry : Players)
			PlayerEntry.Value->SendKeyFrame();
	}
}

void FStreamer::OnRemoteIceCandidate(FPlayerId PlayerId, TUniquePtr<webrtc::IceCandidateInterface> Candidate)
{
	FPlayerSession* Player = GetPlayerSession(PlayerId);
	checkf(Player, TEXT("player %s not found"), *PlayerId);
	Player->OnRemoteIceCandidate(MoveTemp(Candidate));
}

void FStreamer::OnPlayerDisconnected(FPlayerId PlayerId)
{
	UE_LOG(PixelStreamer, Log, TEXT("player %s disconnected"), *PlayerId);
	DeletePlayerSession(PlayerId);
}

void FStreamer::OnSignallingServerDisconnected()
{
	DeleteAllPlayerSessions();
	ConnectToSignallingServer();
}

FPlayerSession* FStreamer::GetPlayerSession(FPlayerId PlayerId)
{
	auto* Player = Players.Find(PlayerId);
	return Player ? Player->Get() : nullptr;
}

void FStreamer::DeleteAllPlayerSessions()
{
	{
		FScopeLock PlayersLock(&PlayersCS);
		while (Players.Num() > 0)
		{
			DeletePlayerSession(Players.CreateIterator().Key());
		}
	}
}

void FStreamer::CreatePlayerSession(FPlayerId PlayerId)
{
	check(PeerConnectionFactory);

	// With unified plan, we get several calls to OnOffer, which in turn calls
	// this several times.
	// Therefore, we only try to create the player if not created already
	{
		FScopeLock PlayersLock(&PlayersCS);
		if (Players.Find(PlayerId))
		{
			return;
		}
	}

	UE_LOG(PixelStreamer, Log, TEXT("Creating player session for PlayerId=%s"), *PlayerId);
	
	// this is called from WebRTC signalling thread, the only thread were `Players` map is modified, so no need to lock it
	bool bOriginalQualityController = Players.Num() == 0; // first player controls quality by default
	TUniquePtr<FPlayerSession> Session = MakeUnique<FPlayerSession>(*this, PlayerId, bOriginalQualityController);
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection = PeerConnectionFactory->CreatePeerConnection(PeerConnectionConfig, webrtc::PeerConnectionDependencies{ Session.Get() });
	check(PeerConnection);
	Session->SetPeerConnection(PeerConnection);

	{
		FScopeLock PlayersLock(&PlayersCS);
		Players.Add(PlayerId) = MoveTemp(Session);
	}
}

void FStreamer::DeletePlayerSession(FPlayerId PlayerId)
{
	FPlayerSession* Player = GetPlayerSession(PlayerId);
	if (!Player)
	{
		UE_LOG(PixelStreamer, VeryVerbose, TEXT("failed to delete player %s: not found"), *PlayerId);
		return;
	}

	bool bWasQualityController = Player->IsQualityController();

	{
		FScopeLock PlayersLock(&PlayersCS);
		Players.Remove(PlayerId);
	}

	// this is called from WebRTC signalling thread, the only thread were `Players` map is modified, so no need to lock it
	if (Players.Num() == 0)
	{
		bStreamingStarted = false;

		// Inform the application-specific blueprint that nobody is viewing or
		// interacting with the app. This is an opportunity to reset the app.
		UPixelStreamerDelegates* Delegates = UPixelStreamerDelegates::GetPixelStreamerDelegates();
		if (Delegates)
			Delegates->OnAllConnectionsClosed.Broadcast();
	}
	else if (bWasQualityController)
	{
		// Quality Controller session has been just removed, set quality control to any of remaining sessions
		TArray<FPlayerId> PlayerIds;
		Players.GetKeys(PlayerIds);
		check(PlayerIds.Num() != 0);
		OnQualityOwnership(PlayerIds[0]);
	}
}

void FStreamer::AddStreams(FPlayerId PlayerId)
{
	FString const StreamId = TEXT("stream_id");
	FString const AudioLabel = FString::Printf(TEXT("audio_label_%s"), *PlayerId);
	FString const VideoLabel= FString::Printf(TEXT("video_label_%s"), *PlayerId);

	FPlayerSession* Session = GetPlayerSession(PlayerId);
	check(Session);
	if (!Session->GetPeerConnection().GetSenders().empty())
		return;  // Already added tracks

	// Create one and only one audio source for Pixel Streaming.
	if (!AudioSource)
		AudioSource = PeerConnectionFactory->CreateAudioSource(cricket::AudioOptions{});

	// Create one and only one VideoCapturer for Pixel Streaming.
	// Video capturuer is actually a "VideoSource" in WebRTC terminology.
	if (!VideoSource)
		VideoSource = new FVideoCapturer();

	// Create video and audio tracks for each Peer/PeerConnection. 
	// These tracks are only thin wrappers around the underlying sources (the sources are shared among all peer's tracks).
	// As per the WebRTC source: "The same source can be used by multiple VideoTracks."
	rtc::scoped_refptr<webrtc::VideoTrackInterface> VideoTrack = PeerConnectionFactory->CreateVideoTrack(TCHAR_TO_UTF8(*VideoLabel), VideoSource);
	rtc::scoped_refptr<webrtc::AudioTrackInterface> AudioTrack = PeerConnectionFactory->CreateAudioTrack(TCHAR_TO_UTF8(*AudioLabel), AudioSource);

	auto const addAudioTrackResult = Session->GetPeerConnection().AddTrack(AudioTrack, { TCHAR_TO_UTF8(*StreamId) });
	if (!addAudioTrackResult.ok())
	{
		UE_LOG(PixelStreamer,
			   Error,
			   TEXT("Failed to add AudioTrack to PeerConnection of player %s. Msg=%s"),
			   *Session->GetPlayerId(),
			   TCHAR_TO_UTF8(addAudioTrackResult.error().message()));
	}

	auto const addVideoTrackResult = Session->GetPeerConnection().AddTrack(VideoTrack, { TCHAR_TO_ANSI(*StreamId) });
	if (!addVideoTrackResult.ok())
	{
		UE_LOG(PixelStreamer,
			   Error,
			   TEXT("Failed to add VideoTrack to PeerConnection of player %s. Msg=%s"),
			   *Session->GetPlayerId(),
				TCHAR_TO_UTF8(addVideoTrackResult.error().message()));
	}
	else
	{
		webrtc::DegradationPreference DegradationPref = PixelStreamingSettings::GetDegradationPreference();
		switch (DegradationPref)
		{
		case webrtc::DegradationPreference::MAINTAIN_FRAMERATE:
			VideoTrack->set_content_hint(webrtc::VideoTrackInterface::ContentHint::kFluid);
			break;
		case webrtc::DegradationPreference::MAINTAIN_RESOLUTION:
			VideoTrack->set_content_hint(webrtc::VideoTrackInterface::ContentHint::kDetailed);
			break;
		default:
			break;
		}
	}

}

void FStreamer::OnQualityOwnership(FPlayerId PlayerId)
{
	checkf(GetPlayerSession(PlayerId), TEXT("player %s not found"), *PlayerId);
	{
		FScopeLock PlayersLock(&PlayersCS);
		for (auto&& PlayerEntry : Players)
		{
			FPlayerSession& Player = *PlayerEntry.Value;
			Player.SetQualityController(Player.GetPlayerId() == PlayerId ? true : false);
		}
	}
}

void FStreamer::SendPlayerMessage(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor)
{
	UE_LOG(PixelStreamer, Log, TEXT("SendPlayerMessage: %d - %s"), static_cast<int32>(Type), *Descriptor);
	{
		FScopeLock PlayersLock(&PlayersCS);
		for (auto&& PlayerEntry : Players)
		{
			PlayerEntry.Value->SendMessage(Type, Descriptor);
		}
	}
}

void FStreamer::SendFreezeFrame(const TArray64<uint8>& JpegBytes)
{
	UE_LOG(PixelStreamer, Log, TEXT("Sending freeze frame to players: %d bytes"), JpegBytes.Num());
	{
		FScopeLock PlayersLock(&PlayersCS);
		for (auto&& PlayerEntry : Players)
		{
			PlayerEntry.Value->SendFreezeFrame(JpegBytes);
		}
	}
	

	CachedJpegBytes = JpegBytes;
}

void FStreamer::SendCachedFreezeFrameTo(FPlayerSession& Player)
{
	if (CachedJpegBytes.Num() > 0)
	{
		UE_LOG(PixelStreamer, Log, TEXT("Sending cached freeze frame to player %s: %d bytes"), *Player.GetPlayerId(), CachedJpegBytes.Num());
		Player.SendFreezeFrame(CachedJpegBytes);
	}
}

void FStreamer::SendUnfreezeFrame()
{
	UE_LOG(PixelStreamer, Log, TEXT("Sending unfreeze message to players"));

	{
		FScopeLock PlayersLock(&PlayersCS);
		for (auto&& PlayerEntry : Players)
		{
			PlayerEntry.Value->SendUnfreezeFrame();
		}
	}
	
	CachedJpegBytes.Empty();
}
