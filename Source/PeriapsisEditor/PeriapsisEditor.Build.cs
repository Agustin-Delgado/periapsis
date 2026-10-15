using UnrealBuildTool;

// Editor tools: data validators and importers. Never shipped.
public class PeriapsisEditor : ModuleRules
{
	public PeriapsisEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UnrealEd", "PeriapsisCore", "PeriapsisSim", "PeriapsisGame" });
	}
}
