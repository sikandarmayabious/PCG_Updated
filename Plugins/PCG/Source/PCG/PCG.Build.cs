// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class PCG : ModuleRules
	{
		public PCG(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"ComputeFramework",
					"Core",
					"CoreUObject",
					"DeveloperSettings",
					"Engine",
					"Foliage",
					"GeometryAlgorithms",
					"GeometryCore",
					"GeometryFramework",
					"Landscape",
					"PhysicsCore",
					"Projects",
					"RenderCore",
					"RHI",
				});
			SetupModulePhysicsSupport(Target);

			// UE 5.8 moved this editor-facing Landscape helper to the Landscape
			// module's Internal folder. Engine plugins inherit this include path,
			// while a project override must declare it explicitly.
			PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("Landscape"), "Internal"));

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"PCGCompute",
					"Renderer",
					"Voronoi",
					"Json"
				});

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"UnrealEd",
						"AssetRegistry",
						"Settings",
						"SourceControl",
						"InteractiveToolsFramework",
						"DataHierarchyEditor"
					});
			}

			//PublicDefinitions.Add("PCG_DATA_USAGE_LOGGING"); // Generates log to help debug lifetimes of transient resources.
			//PublicDefinitions.Add("PCG_GPU_KERNEL_PROFILING"); // Enables repeating kernel dispatches every frame to help profiling.
		}
	}
}
