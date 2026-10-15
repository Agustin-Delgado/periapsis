#include "PeriapsisCore/Maneuver.h"

#include "PeriapsisCore/Constants.h"
#include "PeriapsisCore/Rocket.h"

#include <cmath>

namespace Periapsis::Core
{
	ManeuverBasis ComputeManeuverBasis(const StateVector& State)
	{
		ManeuverBasis Basis;
		Basis.Prograde = Normalized(State.Velocity);
		Basis.Normal = Normalized(Cross(State.Position, State.Velocity));
		Basis.RadialOut = Cross(Basis.Prograde, Basis.Normal);
		return Basis;
	}

	Vec3 ManeuverDeltaV(const StateVector& StateAtNode, const ManeuverNode& Node)
	{
		const ManeuverBasis Basis = ComputeManeuverBasis(StateAtNode);
		return Basis.Prograde * Node.Prograde + Basis.Normal * Node.Normal + Basis.RadialOut * Node.Radial;
	}

	StateVector ApplyManeuver(const StateVector& StateAtNode, const ManeuverNode& Node)
	{
		return {StateAtNode.Position, StateAtNode.Velocity + ManeuverDeltaV(StateAtNode, Node)};
	}

	double BurnLeadTime(double DeltaV, double Thrust, double IspSeconds, double InitialMass)
	{
		return BurnDuration(0.5 * DeltaV, Thrust, IspSeconds, InitialMass);
	}

	HohmannTransfer ComputeHohmann(double Mu, double FromRadius, double ToRadius)
	{
		const double SumRadii = FromRadius + ToRadius;
		const double TransferA = 0.5 * SumRadii;

		HohmannTransfer Transfer;
		Transfer.DeltaV1 = std::sqrt(Mu / FromRadius) * (std::sqrt(2.0 * ToRadius / SumRadii) - 1.0);
		Transfer.DeltaV2 = std::sqrt(Mu / ToRadius) * (1.0 - std::sqrt(2.0 * FromRadius / SumRadii));
		Transfer.TimeOfFlight = kPi * std::sqrt(TransferA * TransferA * TransferA / Mu);
		return Transfer;
	}
}
