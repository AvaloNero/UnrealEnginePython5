// Copyright 1998-2018 20Tab S.r.l All Rights Reserved.

using UnrealBuildTool;
using System;

public class PythonAutomation : ModuleRules
{
    public PythonAutomation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("UEP_ENABLE_UNITY_BUILD"));

        PrivateIncludePaths.AddRange(
            new string[] {
                "PythonConsole/Private",
				// ... add other private include paths required here ...
			}
        );

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject", // @todo Mac: for some reason it's needed to link in debug on Mac
				"Engine",
				"Python3",
                "UnrealEd",
                "UnrealEnginePython"
            }
        );

    }
}
