using UnrealBuildTool;

public class PeriapsisEditorTarget : TargetRules
{
	public PeriapsisEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "PeriapsisCore", "PeriapsisSim", "PeriapsisGame", "PeriapsisEditor" });
	}
}
