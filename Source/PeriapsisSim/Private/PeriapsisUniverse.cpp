#include "PeriapsisUniverse.h"

using namespace Periapsis::Core;

namespace
{
	constexpr int32 RootBody = 0; // the loader guarantees the first body is the root
}

FPeriapsisUniverse::FPeriapsisUniverse(FPeriapsisScenario InScenario)
	: Scenario(MoveTemp(InScenario))
{
	Restart();
}

void FPeriapsisUniverse::Restart()
{
	ActiveFlight = MakeUnique<Periapsis::Core::Flight>(Scenario.SystemData.System, Scenario.Ship, Scenario.StartBody,
		Scenario.StartState, Scenario.StartTime);
	Attitude = AttitudeMode::Prograde;
	WarpLevel = 0;
	WarpLimitReason.Reset();
}

void FPeriapsisUniverse::SetThrottle(double Throttle)
{
	ActiveFlight->SetThrottle(Throttle);
}

void FPeriapsisUniverse::IncreaseWarp()
{
	WarpLevel = FMath::Min(WarpLevel + 1, NumWarpLevels - 1);
}

void FPeriapsisUniverse::DecreaseWarp()
{
	WarpLevel = FMath::Max(WarpLevel - 1, 0);
}

double FPeriapsisUniverse::TimeToNextEvent()
{
	const std::vector<TrajectoryPatch>& Patches = ActiveFlight->Trajectory();
	if (Patches.empty() || Patches.front().EndReason == PatchEnd::Horizon)
	{
		return TNumericLimits<double>::Max();
	}
	return Patches.front().EndTime - ActiveFlight->Time();
}

void FPeriapsisUniverse::ClampWarp(double RealDeltaSeconds)
{
	WarpLimitReason.Reset();
	if (ActiveFlight->IsThrusting() && WarpLevel > MaxPoweredWarpLevel)
	{
		WarpLevel = MaxPoweredWarpLevel;
		WarpLimitReason = TEXT("engine burning");
	}
	if (ActiveFlight->IsThrusting())
	{
		return; // the prediction is being recomputed every frame; no event to anticipate
	}
	const double ToEvent = TimeToNextEvent();
	while (WarpLevel > 0 && WarpLevels[WarpLevel] * RealDeltaSeconds * MinFramesToEvent > ToEvent)
	{
		--WarpLevel;
		WarpLimitReason = TEXT("approaching SOI change or surface");
	}
}

void FPeriapsisUniverse::Tick(double RealDeltaSeconds)
{
	if (ActiveFlight->Status() != FlightStatus::Flying || !(RealDeltaSeconds > 0.0))
	{
		WarpLevel = 0;
		return;
	}
	ClampWarp(RealDeltaSeconds);
	ActiveFlight->SetThrustDirection(AttitudeDirection(Attitude, ActiveFlight->State()));
	ActiveFlight->Advance(RealDeltaSeconds * GetWarp());
}

Vec3 FPeriapsisUniverse::BodyPosition(int32 BodyIndex) const
{
	return GetSystem().StateRelativeTo(BodyIndex, RootBody, ActiveFlight->Time()).Position;
}

Vec3 FPeriapsisUniverse::ShipPosition() const
{
	return ChangeFrame(GetSystem(), ActiveFlight->BodyIndex(), RootBody, ActiveFlight->State(), ActiveFlight->Time()).Position;
}
