// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class PythonEditor : ModuleRules
    {
        public PythonEditor(ReadOnlyTargetRules Target) : base(Target)
        {

            PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
            bUseUnity = !string.IsNullOrEmpty(System.Environment.GetEnvironmentVariable("UEP_ENABLE_UNITY_BUILD"));

            PrivateIncludePaths.AddRange(
                new string[] {
                    "PythonEditor/Private",
                }
                );

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
					"Engine",
                    "SlateCore",
                    "Slate",
                    "AssetTools",
                    "UnrealEd",
                    "EditorStyle",
                    "PropertyEditor",
                    "Kismet",  // for FWorkflowCentricApplication
					"InputCore",
                    "DirectoryWatcher",
                    "LevelEditor",
                    "Projects",
					"Python3",
                    "UnrealEnginePython"
                }
                );
        }
    }
}
