using UnrealBuildTool;

// Orbital mechanics in plain C++. Only PeriapsisCoreModule.cpp touches Unreal (module
// registration); everything else must also build in Tools/CoreTests without the engine.
public class PeriapsisCore : ModuleRules
{
	public PeriapsisCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
