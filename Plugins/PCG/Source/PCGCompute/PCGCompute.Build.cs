// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class PCGCompute : ModuleRules
	{
		public PCGCompute(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Projects",
					"RHI",
					"RenderCore",
					"Renderer",
				});

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Engine",
				});

			PrivateIncludePaths.AddRange(
				new string[] {
					Path.Combine(GetModuleDirectory("Renderer"), "Private"),
					// UE 5.8 renderer implementation headers referenced by the PCG
					// compute ray tracing path now live here.
					Path.Combine(GetModuleDirectory("Renderer"), "Internal"),
				});
		}
	}
}
