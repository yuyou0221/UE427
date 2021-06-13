// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemEOSPlus.h"

#include "Misc/ConfigCacheIni.h"

bool FOnlineSubsystemEOSPlus::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	return false;
}

FString FOnlineSubsystemEOSPlus::GetAppId() const
{
	return BaseOSS != nullptr ? BaseOSS->GetAppId() : TEXT("");
}

FText FOnlineSubsystemEOSPlus::GetOnlineServiceName() const
{
	return NSLOCTEXT("OnlineSubsystemEOSPlus", "OnlineServiceName", "EOS_Plus");
}

bool FOnlineSubsystemEOSPlus::Init()
{
	// Get name of Base OSS from config
	FString BaseOSSName;
	GConfig->GetString(TEXT("[OnlineSubsystemEOSPlus]"), TEXT("BaseOSSName"), BaseOSSName, GEngineIni);
	if (BaseOSSName.IsEmpty())
	{
		// Load the native platform OSS name
		GConfig->GetString(TEXT("OnlineSubsystem"), TEXT("NativePlatformService"), BaseOSSName, GEngineIni);
	}
	if (BaseOSSName.IsEmpty())
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOSPlus::Init() failed to find the native OSS!"));
		return false;
	}

	BaseOSS = IOnlineSubsystem::Get(FName(*BaseOSSName));
	if (BaseOSS == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOSPlus::Init() failed to get the platform OSS"));
		return false;
	}
	if (BaseOSS->GetSubsystemName() == EOS_SUBSYSTEM ||
		BaseOSS->GetSubsystemName() == EOSPLUS_SUBSYSTEM)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOSPlus::Init() failed due to circular configuration"));
		BaseOSS = nullptr;
		return false;
	}

	EosOSS = IOnlineSubsystem::Get(EOS_SUBSYSTEM);
	if (EosOSS == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOSPlus::Init() failed to get the EOS OSS"));
		return false;
	}

	StatsInterfacePtr = MakeShareable(new FOnlineStatsEOSPlus(this));
	AchievementsInterfacePtr = MakeShareable(new FOnlineAchievementsEOSPlus(this));
	UserInterfacePtr = MakeShareable(new FOnlineUserEOSPlus(this));
	SessionInterfacePtr = MakeShareable(new FOnlineSessionEOSPlus(this));
	LeaderboardsInterfacePtr = MakeShareable(new FOnlineLeaderboardsEOSPlus(this));

	return true;
}

void FOnlineSubsystemEOSPlus::PreUnload()
{
	//EOSPlus will be shutdown after its component subsystems, so we need to delete the references to their interfaces beforehand to avoid errors

#define DESTRUCT_INTERFACE(Interface) \
	if (Interface.IsValid()) \
	{ \
		ensure(Interface.IsUnique()); \
		Interface = nullptr; \
	}

	DESTRUCT_INTERFACE(StatsInterfacePtr);
	DESTRUCT_INTERFACE(AchievementsInterfacePtr);
	DESTRUCT_INTERFACE(UserInterfacePtr);
	DESTRUCT_INTERFACE(SessionInterfacePtr);
	DESTRUCT_INTERFACE(LeaderboardsInterfacePtr);

#undef DESTRUCT_INTERFACE
}

bool FOnlineSubsystemEOSPlus::Shutdown()
{
	BaseOSS = nullptr;
	EosOSS = nullptr;

	return true;
}

IOnlineSessionPtr FOnlineSubsystemEOSPlus::GetSessionInterface() const
{
	return SessionInterfacePtr;
}

IOnlineFriendsPtr FOnlineSubsystemEOSPlus::GetFriendsInterface() const
{
	return UserInterfacePtr;
}

IOnlineGroupsPtr FOnlineSubsystemEOSPlus::GetGroupsInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetGroupsInterface() : nullptr;
}

IOnlinePartyPtr FOnlineSubsystemEOSPlus::GetPartyInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetPartyInterface() : nullptr;
}

IOnlineSharedCloudPtr FOnlineSubsystemEOSPlus::GetSharedCloudInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetSharedCloudInterface() : nullptr;
}

IOnlineUserCloudPtr FOnlineSubsystemEOSPlus::GetUserCloudInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetUserCloudInterface() : nullptr;
}

IOnlineEntitlementsPtr FOnlineSubsystemEOSPlus::GetEntitlementsInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetEntitlementsInterface() : nullptr;
}

IOnlineLeaderboardsPtr FOnlineSubsystemEOSPlus::GetLeaderboardsInterface() const
{
	return LeaderboardsInterfacePtr;
}

IOnlineVoicePtr FOnlineSubsystemEOSPlus::GetVoiceInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetVoiceInterface() : nullptr;
}

IOnlineExternalUIPtr FOnlineSubsystemEOSPlus::GetExternalUIInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetExternalUIInterface() : nullptr;
}

IOnlineTimePtr FOnlineSubsystemEOSPlus::GetTimeInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetTimeInterface() : nullptr;
}

IOnlineIdentityPtr FOnlineSubsystemEOSPlus::GetIdentityInterface() const
{
	return UserInterfacePtr;
}

IOnlineTitleFilePtr FOnlineSubsystemEOSPlus::GetTitleFileInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetTitleFileInterface() : nullptr;
}

IOnlineStoreV2Ptr FOnlineSubsystemEOSPlus::GetStoreV2Interface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetStoreV2Interface() : nullptr;
}

IOnlinePurchasePtr FOnlineSubsystemEOSPlus::GetPurchaseInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetPurchaseInterface() : nullptr;
}

IOnlineEventsPtr FOnlineSubsystemEOSPlus::GetEventsInterface() const
{
	return StatsInterfacePtr;
}

IOnlineAchievementsPtr FOnlineSubsystemEOSPlus::GetAchievementsInterface() const
{
	return AchievementsInterfacePtr;
}

IOnlineSharingPtr FOnlineSubsystemEOSPlus::GetSharingInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetSharingInterface() : nullptr;
}

IOnlineUserPtr FOnlineSubsystemEOSPlus::GetUserInterface() const
{
	return nullptr;
}

IOnlineMessagePtr FOnlineSubsystemEOSPlus::GetMessageInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetMessageInterface() : nullptr;
}

IOnlinePresencePtr FOnlineSubsystemEOSPlus::GetPresenceInterface() const
{
	return UserInterfacePtr;
}

IOnlineChatPtr FOnlineSubsystemEOSPlus::GetChatInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetChatInterface() : nullptr;
}

IOnlineStatsPtr FOnlineSubsystemEOSPlus::GetStatsInterface() const
{
	return StatsInterfacePtr;
}

IOnlineTurnBasedPtr FOnlineSubsystemEOSPlus::GetTurnBasedInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetTurnBasedInterface() : nullptr;
}

IOnlineTournamentPtr FOnlineSubsystemEOSPlus::GetTournamentInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetTournamentInterface() : nullptr;
}

bool FOnlineSubsystemEOSPlus::IsLocalPlayer(const FUniqueNetId& UniqueId) const
{
	if (!IsDedicated())
	{
		if (UserInterfacePtr.IsValid())
		{
			FUniqueNetIdEOSPlusPtr NetIdPlus = UserInterfacePtr->GetNetIdPlus(UniqueId.ToString());
			if (NetIdPlus.IsValid())
			{
				for (int32 LocalUserNum = 0; LocalUserNum < MAX_LOCAL_PLAYERS; LocalUserNum++)
				{
					FUniqueNetIdPtr LocalUniqueId = UserInterfacePtr->GetUniquePlayerId(LocalUserNum);
					if (LocalUniqueId.IsValid() && *NetIdPlus == *LocalUniqueId)
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}