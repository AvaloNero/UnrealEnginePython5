using UnrealBuildTool;

public class UEPPythonThirdPerson : ModuleRules
{
    public UEPPythonThirdPerson(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "Engine"
            }
        );
        PrivateDependencyModuleNames.Add("UnrealEnginePython");
    }
}
