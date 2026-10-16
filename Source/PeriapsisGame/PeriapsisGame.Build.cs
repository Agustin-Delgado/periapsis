using UnrealBuildTool;

// Actors, controllers, UI, cameras, terrain, VFX and campaign.
public class PeriapsisGame : ModuleRules
{
	public PeriapsisGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "PhysicsCore", "Chaos", "PeriapsisCore", "PeriapsisSim" });
	}
}
