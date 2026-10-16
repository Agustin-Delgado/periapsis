#include "PeriapsisCore/Staging.h"

#include <cmath>

namespace Periapsis::Core
{
	namespace
	{
		// Parts that the next stage's decouplers will take away.
		std::vector<bool> DroppedByNextStage(const Vessel& V)
		{
			const std::vector<VesselPart>& Parts = V.Parts();
			std::vector<bool> Dropped(Parts.size(), false);
			for (size_t Index = 1; Index < Parts.size(); ++Index)
			{
				const PartInstance& Instance = Parts[Index].Instance;
				const bool bFires = Instance.Stage == V.NextStage() && V.Definition(static_cast<int>(Index)).Decoupler.has_value();
				Dropped[Index] = bFires || Dropped[Instance.Parent];
			}
			return Dropped;
		}

		StageStats Burn(Vessel& V, int Stage, double PressurePa, double Gravity)
		{
			StageStats Stats;
			Stats.Stage = Stage;
			Stats.StartMass = V.Mass();
			const Propulsion AtStart = V.Engines(1.0, PressurePa);
			Stats.StartThrust = AtStart.Thrust;
			Stats.Engines = AtStart.BurningEngines;
			if (Gravity > 0.0)
			{
				Stats.StartTwr = AtStart.Thrust / (Stats.StartMass * Gravity);
			}

			const bool bNextStage = V.NextStage() < V.StageCount();
			const std::vector<bool> Dropped = bNextStage ? DroppedByNextStage(V) : std::vector<bool>(V.Parts().size(), false);
			bool bDropsEngines = false;
			for (size_t Index = 0; Index < Dropped.size(); ++Index)
			{
				bDropsEngines |= Dropped[Index] && V.Definition(static_cast<int>(Index)).Engine.has_value();
			}

			double LastThrust = AtStart.Thrust;
			// Each pass empties a propellant somewhere, so this ends; the cap is only a guard.
			for (int Pass = 0; Pass < 10000; ++Pass)
			{
				const Propulsion Now = V.Engines(1.0, PressurePa);
				if (Now.BurningEngines == 0 || !(Now.MassFlow > 0.0))
				{
					break;
				}
				if (bDropsEngines)
				{
					bool bDroppedBurning = false;
					for (size_t Index = 0; Index < Dropped.size(); ++Index)
					{
						bDroppedBurning |= Dropped[Index] && V.IsEngineBurning(static_cast<int>(Index));
					}
					if (!bDroppedBurning)
					{
						break;
					}
				}
				const double Duration = V.TimeToFlameout(1.0);
				if (!std::isfinite(Duration) || !(Duration > 0.0))
				{
					break;
				}
				LastThrust = Now.Thrust;
				const double Before = V.Mass();
				const double After = Before - Now.MassFlow * Duration;
				Stats.DeltaV += Now.Thrust / Now.MassFlow * std::log(Before / After);
				Stats.BurnTime += Duration;
				V.Consume(Duration, 1.0);
			}
			Stats.EndMass = V.Mass();
			if (Gravity > 0.0)
			{
				// Just before burnout, with the engines of the last stretch.
				Stats.EndTwr = LastThrust / (Stats.EndMass * Gravity);
			}
			return Stats;
		}
	}

	std::vector<StageStats> AnalyzeStages(const Vessel& V, double PressurePa, double Gravity)
	{
		std::vector<StageStats> Result;
		if (!V.IsValid())
		{
			return Result;
		}
		Vessel Work = V;
		bool bLit = false;
		for (int Part = 0; Part < static_cast<int>(Work.Parts().size()); ++Part)
		{
			bLit |= Work.Parts()[Part].bEngineActive;
		}
		if (bLit)
		{
			Result.push_back(Burn(Work, Work.NextStage() - 1, PressurePa, Gravity));
		}
		while (Work.NextStage() < Work.StageCount())
		{
			const int Stage = Work.NextStage();
			Work.ActivateNextStage(); // the pieces that come off are left behind
			Result.push_back(Burn(Work, Stage, PressurePa, Gravity));
		}
		return Result;
	}

	double TotalDeltaV(const std::vector<StageStats>& Stages)
	{
		double Total = 0.0;
		for (const StageStats& Stage : Stages)
		{
			Total += Stage.DeltaV;
		}
		return Total;
	}
}
