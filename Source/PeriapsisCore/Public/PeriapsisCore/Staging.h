// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include "PeriapsisCore/Vessel.h"

#include <vector>

namespace Periapsis::Core
{
	// What one stage of a vessel delivers (spec 4.5 and 8.2).
	struct StageStats
	{
		int Stage = -1;         // the stage that starts it; -1 for engines already lit
		double StartMass = 0.0; // kg
		double EndMass = 0.0;   // kg, before the next stage drops anything
		double DeltaV = 0.0;    // m/s
		double BurnTime = 0.0;  // s at full throttle
		double StartThrust = 0.0; // N
		double StartTwr = 0.0;  // thrust-to-weight at the start, against the given gravity
		double EndTwr = 0.0;    // and at burnout
		int Engines = 0;        // burning at the start
	};

	// Flies the stage sequence at full throttle, from the vessel's current state, at a constant
	// ambient pressure, and reports each stage. A stage burns until every engine the next stage
	// drops has flamed out (so boosters drop when they are spent and the core keeps burning),
	// or, when the next stage drops no engines, until nothing burns. The burn is split where an
	// engine flames out and each piece follows the rocket equation exactly, with the effective
	// Isp of the engines burning in it: dv = F / mdot * ln(m0 / m1).
	//
	// Gravity only scales the thrust-to-weight ratios (zero leaves them at zero).
	PERIAPSISCORE_API std::vector<StageStats> AnalyzeStages(const Vessel& V, double PressurePa = 0.0, double Gravity = 0.0);

	PERIAPSISCORE_API double TotalDeltaV(const std::vector<StageStats>& Stages);
}
