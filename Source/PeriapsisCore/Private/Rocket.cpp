#include "PeriapsisCore/Rocket.h"

#include "PeriapsisCore/Constants.h"

#include <cmath>

namespace Periapsis::Core
{
	double RocketDeltaV(double IspSeconds, double InitialMass, double FinalMass)
	{
		return IspSeconds * kStandardGravity * std::log(InitialMass / FinalMass);
	}

	double MassAfterBurn(double IspSeconds, double InitialMass, double DeltaV)
	{
		return InitialMass * std::exp(-DeltaV / (IspSeconds * kStandardGravity));
	}

	double MassFlowRate(double Thrust, double IspSeconds)
	{
		return Thrust / (IspSeconds * kStandardGravity);
	}

	double IspAtPressure(double IspVacuum, double IspSeaLevel, double PressurePa)
	{
		const double Atmospheres = PressurePa / kStandardAtmospherePa;
		const double Isp = IspVacuum + (IspSeaLevel - IspVacuum) * Atmospheres;
		return Isp > 0.0 ? Isp : 0.0;
	}

	double ThrustAtPressure(double ThrustVacuum, double IspVacuum, double IspSeaLevel, double PressurePa)
	{
		return ThrustVacuum * IspAtPressure(IspVacuum, IspSeaLevel, PressurePa) / IspVacuum;
	}

	double ThrustToWeight(double Thrust, double Mass, double LocalGravity)
	{
		return Thrust / (Mass * LocalGravity);
	}

	double BurnDuration(double DeltaV, double Thrust, double IspSeconds, double InitialMass)
	{
		const double FinalMass = MassAfterBurn(IspSeconds, InitialMass, DeltaV);
		return (InitialMass - FinalMass) / MassFlowRate(Thrust, IspSeconds);
	}

	std::vector<double> StageDeltaV(const std::vector<StageMass>& StagesInFiringOrder, double PayloadMass)
	{
		std::vector<double> Result(StagesInFiringOrder.size(), 0.0);
		double MassAbove = PayloadMass;
		// Walk from the last stage to fire down to the first, accumulating what each one carries.
		for (size_t Index = StagesInFiringOrder.size(); Index-- > 0;)
		{
			const StageMass& Stage = StagesInFiringOrder[Index];
			Result[Index] = RocketDeltaV(Stage.IspSeconds, Stage.WetMass + MassAbove, Stage.DryMass + MassAbove);
			MassAbove += Stage.WetMass;
		}
		return Result;
	}
}
