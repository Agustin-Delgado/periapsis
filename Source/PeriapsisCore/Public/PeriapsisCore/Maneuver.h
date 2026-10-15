// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include "PeriapsisCore/StateVector.h"

namespace Periapsis::Core
{
	// A planned impulsive burn, expressed in the local orbital frame at UniversalTime.
	struct ManeuverNode
	{
		double UniversalTime = 0.0; // s
		double Prograde = 0.0;      // m/s along the velocity
		double Normal = 0.0;        // m/s along the angular momentum (r x v)
		double Radial = 0.0;        // m/s outward, perpendicular to velocity in the orbital plane
	};

	struct ManeuverBasis
	{
		Vec3 Prograde;
		Vec3 Normal;
		Vec3 RadialOut;
	};

	PERIAPSISCORE_API ManeuverBasis ComputeManeuverBasis(const StateVector& State);

	// Delta-v of the node in the inertial frame of the state.
	PERIAPSISCORE_API Vec3 ManeuverDeltaV(const StateVector& StateAtNode, const ManeuverNode& Node);

	// State right after the impulsive burn.
	PERIAPSISCORE_API StateVector ApplyManeuver(const StateVector& StateAtNode, const ManeuverNode& Node);

	inline double ManeuverMagnitude(const ManeuverNode& Node)
	{
		return Norm(Vec3{Node.Prograde, Node.Normal, Node.Radial});
	}

	// How long before the node to start burning so the burn is centred on it in
	// delta-v terms (half before, half after). Larger than half the burn time,
	// because the ship is heavier at the start.
	PERIAPSISCORE_API double BurnLeadTime(double DeltaV, double Thrust, double IspSeconds, double InitialMass);

	struct HohmannTransfer
	{
		double DeltaV1 = 0.0;      // m/s at departure (signed: negative = retrograde)
		double DeltaV2 = 0.0;      // m/s at arrival
		double TimeOfFlight = 0.0; // s
	};

	// Coplanar circular-to-circular transfer.
	PERIAPSISCORE_API HohmannTransfer ComputeHohmann(double Mu, double FromRadius, double ToRadius);
}
