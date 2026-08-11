// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

using System;
using UnrealBuildTool;

public class UnrealEnginePython : ModuleRules
{
	public UnrealEnginePython(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("UEP_ENABLE_UNITY_BUILD"));
		// Legacy UEP branches compared only ENGINE_MINOR_VERSION. UE5 resets that
		// value to 8, which would select UE4.8 code paths. Keep those guards on a
		// monotonic porting feature level until each branch is removed.
		PublicDefinitions.Add("UEP_LEGACY_ENGINE_MINOR_VERSION=58");
		// UE 5.8 dynamic Python types use the FField/FProperty reflection model.
		PublicDefinitions.Add("UEP_WITH_DYNAMIC_CLASS_GENERATION=1");

		// UE 5.8 ships and stages CPython 3.11 through this external module.
		// Keeping Python selection in UnrealBuildTool avoids mixing CRTs or loading a
		// different system Python at runtime.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Networking",
				"Python3",
				"Sockets",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AIModule",
				"AppFramework",
				"ApplicationCore",
				"CoreUObject",
				"Engine",
				"EnhancedInput",
				"Foliage",
				"HTTP",
				"InputCore",
				"Landscape",
				"LevelSequence",
				"MovieScene",
				"MovieSceneCapture",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"UMG",
				"Voice",
			}
		);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AIGraph",
					"AssetTools",
					"BlueprintGraph",
					"CinematicCamera",
					"CollectionManager",
					"ContentBrowser",
					"DesktopWidgets",
					"EditorStyle",
					"EditorWidgets",
					"FBX",
					"GraphEditor",
					"LandscapeEditor",
					"LevelEditor",
					"LevelSequenceEditor",
					"MaterialEditor",
					"MovieSceneTools",
					"MovieSceneTracks",
					"Persona",
					"Projects",
					"PropertyEditor",
					"RawMesh",
					"Sequencer",
					"SequencerWidgets",
					"UMGEditor",
					"UnrealEd",
				}
			);
		}
	}
}
