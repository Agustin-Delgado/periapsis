using UnrealBuildTool;

public class PeriapsisTarget : TargetRules
{
	public PeriapsisTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "PeriapsisCore", "PeriapsisSim", "PeriapsisGame" });
	}
}
