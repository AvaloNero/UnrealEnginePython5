using UnrealBuildTool;

public class UEP58Host : ModuleRules
{
    public UEP58Host(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "Python3",
                "UnrealEnginePython"
            }
        );
    }
}
