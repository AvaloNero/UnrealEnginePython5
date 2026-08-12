using UnrealBuildTool;

public class UEPLyraBridge : ModuleRules
{
    public UEPLyraBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "GameFeatures",
                "GameplayAbilities",
                "LyraGame"
            }
        );
    }
}
