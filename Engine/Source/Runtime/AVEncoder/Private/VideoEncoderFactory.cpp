// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoEncoderFactory.h"
#include "VideoEncoderInputImpl.h"
#include "RHI.h"

#if (PLATFORM_WINDOWS && PLATFORM_DESKTOP) || WITH_CUDA
#include "Encoders/NVENC/NVENC_EncoderH264.h"
#endif

#if PLATFORM_DESKTOP && !PLATFORM_APPLE
#include "Encoders/Amf/Amf_EncoderH264.h"
#endif

#include "Encoders/VideoEncoderH264_Dummy.h"

namespace AVEncoder
{

FCriticalSection			FVideoEncoderFactory::ProtectSingleton;
FVideoEncoderFactory		FVideoEncoderFactory::Singleton;
FThreadSafeCounter			FVideoEncoderFactory::NextID = 4711;

FVideoEncoderFactory& FVideoEncoderFactory::Get()
{
	if (!Singleton.bWasSetup)
	{
		ProtectSingleton.Lock();
		if (!Singleton.bWasSetup)
		{
			Singleton.bWasSetup = true;
			if (!Singleton.bDebugDontRegisterDefaultCodecs)
			{
				Singleton.RegisterDefaultCodecs();
			}
		}
		ProtectSingleton.Unlock();
	}
	return Singleton;
}

void FVideoEncoderFactory::Shutdown()
{
	FScopeLock		Guard(&ProtectSingleton);
	if (Singleton.bWasSetup)
	{
		Singleton.bWasSetup = false;
		Singleton.bDebugDontRegisterDefaultCodecs = false;
		Singleton.AvailableEncoders.Empty();
		Singleton.CreateEncoders.Empty();

	// 
#if defined(AVENCODER_VIDEO_ENCODER_AVAILABLE_NVENC)
		FNVENCCommon::Shutdown();
#endif
	}
}

void FVideoEncoderFactory::Debug_SetDontRegisterDefaultCodecs()
{
	check(!Singleton.bWasSetup);
	Singleton.bDebugDontRegisterDefaultCodecs = true;
}

void FVideoEncoderFactory::Register(const FVideoEncoderInfo& InInfo, const CreateEncoderCallback& InCreateEncoder)
{
	AvailableEncoders.Push(InInfo);
	AvailableEncoders.Last().ID = NextID.Increment();
	CreateEncoders.Push(InCreateEncoder);
}

void FVideoEncoderFactory::RegisterDefaultCodecs()
{

#if defined(AVENCODER_VIDEO_ENCODER_AVAILABLE_NVENC_H264)
	FVideoEncoderNVENC_H264::Register(*this);
#endif
	
#if defined(AVENCODER_VIDEO_ENCODER_AVAILABLE_AMF_H264)
	FVideoEncoderAmf_H264::Register(*this);
#endif

#if defined(AVENCODER_VIDEO_ENCODER_AVAILABLE_H264_DUMMY)
	FVideoEncoderH264_Dummy::Register(*this);
#endif
}

bool FVideoEncoderFactory::GetInfo(uint32 InID, FVideoEncoderInfo& OutInfo) const
{
	for (int32 Index = 0; Index < AvailableEncoders.Num(); ++Index)
	{
		if (AvailableEncoders[Index].ID == InID)
		{
			OutInfo = AvailableEncoders[Index];
			return true;
		}
	}
	return false;
}

bool FVideoEncoderFactory::HasEncoderForCodec(ECodecType CodecType) const
{
	for (const AVEncoder::FVideoEncoderInfo& EncoderInfo : AvailableEncoders)
	{
		if (EncoderInfo.CodecType == CodecType)
		{
			return true;
		}
	}
	return false;
}

TUniquePtr<FVideoEncoder> FVideoEncoderFactory::Create(uint32 InID, const FVideoEncoder::FLayerConfig& config)
{
	// HACK (M84FIX) create encoder without a ready FVideoEncoderInput
	TUniquePtr<FVideoEncoder>	Result;
	for (int32 Index = 0; Index < AvailableEncoders.Num(); ++Index)
	{
		if (AvailableEncoders[Index].ID == InID)
		{
			Result = CreateEncoders[Index]();

			// HACK (M84FIX) work with other RHI
			if (GDynamicRHI->GetName() == FString("D3D11"))
			{
				TSharedRef<FVideoEncoderInput> Input = FVideoEncoderInput::CreateForD3D11(GDynamicRHI->RHIGetNativeDevice(), config.Width, config.Height).ToSharedRef();

				if (Result && !Result->Setup(Input, config))
				{
					Result.Reset();
				}
				break;
			}
			else if (GDynamicRHI->GetName() == FString("D3D12"))
			{
				TSharedRef<FVideoEncoderInput> Input = FVideoEncoderInput::CreateForD3D12(GDynamicRHI->RHIGetNativeDevice(), config.Width, config.Height).ToSharedRef();

				if (Result && !Result->Setup(Input, config))
				{
					Result.Reset();
				}
				break;
			}
		}
	}
	return Result;
}

TUniquePtr<FVideoEncoder> FVideoEncoderFactory::Create(uint32 InID, TSharedPtr<FVideoEncoderInput> InInput, const FVideoEncoder::FLayerConfig& config)
{
	TUniquePtr<FVideoEncoder>		Result;
	if (InInput)
	{
		TSharedRef<FVideoEncoderInput>	Input(InInput.ToSharedRef());
		for (int32 Index = 0; Index < AvailableEncoders.Num(); ++Index)
		{
			if (AvailableEncoders[Index].ID == InID)
			{
				Result = CreateEncoders[Index]();
				if (Result && !Result->Setup(MoveTemp(Input), config))
				{
					Result.Reset();
				}
				break;
			}
		}
	}
	return Result;
}

} /* namespace AVEncoder */
