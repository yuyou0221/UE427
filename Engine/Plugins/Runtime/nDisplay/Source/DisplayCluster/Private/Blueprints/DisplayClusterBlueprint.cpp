// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/DisplayClusterBlueprint.h"
#include "Blueprints/DisplayClusterBlueprintGeneratedClass.h"
#include "Containers/Set.h"
#include "DisplayClusterRootActor.h"
#include "EngineAnalytics.h"
#include "IDisplayClusterConfiguration.h"
#include "Misc/DisplayClusterLog.h"


UDisplayClusterBlueprint::UDisplayClusterBlueprint()
	: ConfigData(nullptr), AssetVersion(0)
{
	BlueprintType = BPTYPE_Normal;
}

#if WITH_EDITOR

UClass* UDisplayClusterBlueprint::GetBlueprintClass() const
{
	return UDisplayClusterBlueprintGeneratedClass::StaticClass();
}

void UDisplayClusterBlueprint::GetReparentingRules(TSet<const UClass*>& AllowedChildrenOfClasses,
	TSet<const UClass*>& DisallowedChildrenOfClasses) const
{
	AllowedChildrenOfClasses.Add(ADisplayClusterRootActor::StaticClass());
}
#endif

void UDisplayClusterBlueprint::UpdateConfigExportProperty()
{
	bool bConfigExported = false;

	if (UDisplayClusterConfigurationData* Config = GetOrLoadConfig())
	{
		Config->Meta.ExportAssetPath = GetPathName();

		FString PrettyConfig;

		bConfigExported = IDisplayClusterConfiguration::Get().ConfigAsString(Config, PrettyConfig);

		if (bConfigExported)
		{
			// We cache a somewhat minified version of the config so that the context view of the asset registry data is less bloated.

			ConfigExport.Empty(PrettyConfig.Len());

			for (auto CharIt = PrettyConfig.CreateConstIterator(); CharIt; ++CharIt)
			{
				const TCHAR Char = *CharIt;

				// Remove tabs, carriage returns and newlines.
				if ((Char == TCHAR('\t')) || (Char == TCHAR('\r')) || (Char == TCHAR('\n')))
				{
					continue;
				}

				ConfigExport.AppendChar(Char);
			}
		}
	}

	if (!bConfigExported)
	{
		ConfigExport = TEXT("");
	}
}

namespace DisplayClusterBlueprint
{
	void SendAnalytics(const FString& EventName, const UDisplayClusterConfigurationData* const ConfigData)
	{
		if (!FEngineAnalytics::IsAvailable())
		{
			return;
		}

		// Gather attributes related to this config
		TArray<FAnalyticsEventAttribute> EventAttributes;

		if (ConfigData)
		{
			if (ConfigData->Cluster)
			{
				// Number of Nodes
				EventAttributes.Add(FAnalyticsEventAttribute(TEXT("NumNodes"), ConfigData->Cluster->Nodes.Num()));

				// Number of Viewports
				TSet<FString> UniquelyNamedViewports;

				for (auto NodesIt = ConfigData->Cluster->Nodes.CreateConstIterator(); NodesIt; ++NodesIt)
				{
					for (auto ViewportsIt = ConfigData->Cluster->Nodes.CreateConstIterator(); ViewportsIt; ++ViewportsIt)
					{
						UniquelyNamedViewports.Add(ViewportsIt->Key);
					}
				}

				// Number of uniquely named viewports
				EventAttributes.Add(FAnalyticsEventAttribute(TEXT("NumUniquelyNamedViewports"), UniquelyNamedViewports.Num()));
			}
		}

		FEngineAnalytics::GetProvider().RecordEvent(EventName, EventAttributes);
	}
}

void UDisplayClusterBlueprint::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

	UpdateConfigExportProperty();
	DisplayClusterBlueprint::SendAnalytics(TEXT("Usage.nDisplay.ConfigSaved"), ConfigData);
}

UDisplayClusterBlueprintGeneratedClass* UDisplayClusterBlueprint::GetGeneratedClass() const
{
	return Cast<UDisplayClusterBlueprintGeneratedClass>(*GeneratedClass);
}

UDisplayClusterConfigurationData* UDisplayClusterBlueprint::GetOrLoadConfig()
{
	if (GeneratedClass)
	{
		if (ADisplayClusterRootActor* CDO = Cast<ADisplayClusterRootActor>(GeneratedClass->ClassDefaultObject))
		{
			ConfigData = CDO->GetConfigData();
		}
	}
	
	return ConfigData;
}

void UDisplayClusterBlueprint::SetConfigData(UDisplayClusterConfigurationData* InConfigData, bool bForceRecreate)
{
#if WITH_EDITOR
	Modify();
#endif

	if (GeneratedClass)
	{
		if (ADisplayClusterRootActor* CDO = Cast<ADisplayClusterRootActor>(GeneratedClass->ClassDefaultObject))
		{
			CDO->UpdateConfigDataInstance(InConfigData, bForceRecreate);
			GetOrLoadConfig();
		}
	}
	
#if WITH_EDITORONLY_DATA
	if(InConfigData)
	{
		InConfigData->SaveConfig();
	}
#endif
}

const FString& UDisplayClusterBlueprint::GetConfigPath() const
{
	static FString EmptyString;
#if WITH_EDITORONLY_DATA
	return ConfigData ? ConfigData->PathToConfig : EmptyString;
#else
	return EmptyString;
#endif
}

void UDisplayClusterBlueprint::SetConfigPath(const FString& InPath)
{
#if WITH_EDITORONLY_DATA
	if(UDisplayClusterConfigurationData* LoadedConfigData = GetOrLoadConfig())
	{
		LoadedConfigData->PathToConfig = InPath;
		LoadedConfigData->SaveConfig();
	}
#endif
}
