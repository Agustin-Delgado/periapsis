// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include <vector>

namespace Periapsis::Core
{
	// Tsiolkovsky: dv = Isp * g0 * ln(m0 / m1).
	PERIAPSISCORE_API double RocketDeltaV(double IspSeconds, double InitialMass, double FinalMass);

	// Mass left after spending DeltaV.
	PERIAPSISCORE_API double MassAfterBurn(double IspSeconds, double InitialMass, double DeltaV);

	// mdot = F / (Isp * g0), kg/s.
	PERIAPSISCORE_API double MassFlowRate(double Thrust, double IspSeconds);

	// Isp at an ambient pressure, linear between vacuum and sea level and clamped at 0.
	PERIAPSISCORE_API double IspAtPressure(double IspVacuum, double IspSeaLevel, double PressurePa);

	// The engine keeps its mass flow: thrust scales with Isp as pressure changes.
	PERIAPSISCORE_API double ThrustAtPressure(double ThrustVacuum, double IspVacuum, double IspSeaLevel, double PressurePa);

	// Thrust-to-weight ratio against the local gravity (not g0).
	PERIAPSISCORE_API double ThrustToWeight(double Thrust, double Mass, double LocalGravity);

	// Time to spend DeltaV at constant thrust, accounting for the mass going down.
	PERIAPSISCORE_API double BurnDuration(double DeltaV, double Thrust, double IspSeconds, double InitialMass);

	// A stage as the rocket equation sees it. Masses exclude the stages above it.
	struct StageMass
	{
		double WetMass = 0.0;    // kg, full tanks
		double DryMass = 0.0;    // kg, after its propellant is spent
		double IspSeconds = 0.0; // effective Isp of the stage's engines
	};

	// Delta-v per stage, in firing order (index 0 burns first and carries everything above).
	// Resource flow between tanks is a Sim-layer concern; this is the analytic reference.
	PERIAPSISCORE_API std::vector<double> StageDeltaV(const std::vector<StageMass>& StagesInFiringOrder, double PayloadMass = 0.0);
}
