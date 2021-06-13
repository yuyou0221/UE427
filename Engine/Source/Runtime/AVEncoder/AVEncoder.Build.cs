// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System;

public class AVEncoder : ModuleRules
{
	public AVEncoder(ReadOnlyTargetRules Target) : base(Target)
	{
		// Without these two compilation fails on VS2017 with D8049: command line is too long to fit in debug record.
		bLegacyPublicIncludePaths = false;
		DefaultBuildSettings = BuildSettingsVersion.V2;

		// PCHUsage = PCHUsageMode.NoPCHs;

		// PrecompileForTargets = PrecompileTargetsType.None;

		PublicIncludePaths.AddRange(new string[] {
			// ... add public include paths required here ...
		});

		PrivateIncludePaths.AddRange(new string[] {
			// ... add other private include paths required here ...
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Engine"
		});

		PublicDependencyModuleNames.AddRange(new string[] {
			"RenderCore",
			"Core",
			"RHI"
			// ... add other public dependencies that you statically link with here ...
		});

		DynamicallyLoadedModuleNames.AddRange(new string[] {
			// ... add any modules that your module loads dynamically here ...
		});

		string EngineSourceDirectory = Path.GetFullPath(Target.RelativeEnginePath);

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows) || Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"nvEncode",
				"Amf"
			});

			AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
			PrivateIncludePathModuleNames.Add("VulkanRHI");

			PrivateIncludePaths.Add(Path.Combine(EngineSourceDirectory, "Source/Runtime/VulkanRHI/Private"));
			AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
		}

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			PublicDependencyModuleNames.Add("D3D12RHI");

			if (Target.Platform != UnrealTargetPlatform.XboxOne)
			{
				// d3d to be able to use NVENC
				PublicSystemLibraries.AddRange(new string[] {
					"dxgi.lib",
					"d3d11.lib",
					"d3d12.lib",
					"mfplat.lib",
					"mfuuid.lib"
				});
				
				PrivateIncludePaths.Add(Path.Combine(EngineSourceDirectory, "Source/Runtime/VulkanRHI/Private/Windows"));
			}
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PrivateDependencyModuleNames.Add("CUDA");
			PrivateIncludePaths.Add(Path.Combine(EngineSourceDirectory, "Source/Runtime/VulkanRHI/Private/Linux"));
		}

		// TEMPORARY: set this to zero for all platforms until CUDA TPS review clears
		PublicDefinitions.Add("WITH_CUDA=0");
	}
}
