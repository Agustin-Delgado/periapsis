using UnrealBuildTool;

// Universe state, vessels, parts and resources. Depends on the Core, never on gameplay.
public class PeriapsisSim : ModuleRules
{
	public PeriapsisSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json", "PeriapsisCore" });
	}
}
