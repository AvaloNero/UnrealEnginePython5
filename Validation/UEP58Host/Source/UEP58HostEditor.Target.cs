using UnrealBuildTool;
using System.Collections.Generic;

public class UEP58HostEditorTarget : TargetRules
{
    public UEP58HostEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("UEP58Host");
    }
}
