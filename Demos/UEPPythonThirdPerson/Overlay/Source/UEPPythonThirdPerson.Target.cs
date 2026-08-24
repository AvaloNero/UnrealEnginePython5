using UnrealBuildTool;
using System.Collections.Generic;

public class UEPPythonThirdPersonTarget : TargetRules
{
    public UEPPythonThirdPersonTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

        // The sample has no Unreal Insights transport requirement. Disabling
        // Trace for the Game target prevents UE's TCP control listener from
        // making every timestamped Development package a new Firewall app.
        // The shared UnrealEditor target is intentionally left unchanged.
        bEnableTrace = false;

        ExtraModuleNames.Add("UEPPythonThirdPerson");
    }
}
