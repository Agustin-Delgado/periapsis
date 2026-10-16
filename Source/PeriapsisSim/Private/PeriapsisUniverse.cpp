#include "PeriapsisUniverse.h"

#include "PeriapsisCore/Kepler.h"
#include "PeriapsisPhysicsScene.h"

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

FPeriapsisUniverse::~FPeriapsisUniverse() = default;

void FPeriapsisUniverse::Restart()
{
	if (Scene)
	{
		for (int32 Index = 0; Index < Vessels.Num(); ++Index)
		{
			if (Vessels[Index].bInBubble)
			{
				Scene->RemoveVessel(Index);
			}
		}
	}
	bPhysicsActive = false;
	Frame = LocalFrame();
	RebaseCount = 0;

	const CelestialSystem& System = Scenario.SystemData.System;
	Vessels.Reset();
	FPeriapsisVessel& Ship = Vessels.AddDefaulted_GetRef();
	Ship.Name = Scenario.ShipName;
	Ship.Flight = MakeUnique<Flight>(System, Scenario.Ship, Scenario.StartBody, Scenario.StartState, Scenario.StartTime);
	for (const FPeriapsisPassiveVessel& Passive : Scenario.OtherVessels)
	{
		ShipConfig NoEngine;
		NoEngine.DryMass = Passive.Mass;
		FPeriapsisVessel& Vessel = Vessels.AddDefaulted_GetRef();
		Vessel.Name = Passive.Name;
		Vessel.Flight = MakeUnique<Flight>(System, NoEngine, Passive.StartBody, Passive.StartState, Scenario.StartTime);
	}

	CurrentTime = Scenario.StartTime;
	Attitude = AttitudeMode::Prograde;
	WarpLevel = 0;
	WarpLimitReason.Reset();
}

void FPeriapsisUniverse::SetThrottle(double Throttle)
{
	GetFlight().SetThrottle(Throttle);
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
	const std::vector<TrajectoryPatch>& Patches = GetFlight().Trajectory();
	if (Patches.empty() || Patches.front().EndReason == PatchEnd::Horizon)
	{
		return TNumericLimits<double>::Max();
	}
	return Patches.front().EndTime - GetFlight().Time();
}

void FPeriapsisUniverse::ClampWarp(double RealDeltaSeconds)
{
	WarpLimitReason.Reset();
	Flight& Active = GetFlight();
	if (Active.IsThrusting() && WarpLevel > MaxPoweredWarpLevel)
	{
		WarpLevel = MaxPoweredWarpLevel;
		WarpLimitReason = TEXT("engine burning");
	}
	if (Active.IsThrusting())
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
	if (GetFlight().Status() != FlightStatus::Flying || !(RealDeltaSeconds > 0.0))
	{
		WarpLevel = 0;
		return;
	}
	ClampWarp(RealDeltaSeconds);
	if (bPhysicsActive || WantsPhysics())
	{
		return; // the physics steps move the clock (and leave the bubble if the warp went up)
	}

	CurrentTime += RealDeltaSeconds * GetWarp();
	Flight& Active = GetFlight();
	Active.SetThrustDirection(AttitudeDirection(Attitude, Active.State()));
	for (FPeriapsisVessel& Vessel : Vessels)
	{
		Vessel.Flight->Advance(CurrentTime - Vessel.Flight->Time());
	}
}

void FPeriapsisUniverse::SetPhysicsScene(IPeriapsisPhysicsScene* InScene)
{
	if (InScene == Scene)
	{
		return;
	}
	if (bPhysicsActive)
	{
		// The flights keep the states read on the last step.
		LeavePhysics();
	}
	Scene = InScene;
}

bool FPeriapsisUniverse::WantsPhysics() const
{
	return Scene != nullptr && WarpLevel <= MaxPhysicsWarpLevel && GetFlight().Status() == FlightStatus::Flying;
}

double FPeriapsisUniverse::GetPhysicsTimeDilation() const
{
	if (bPhysicsActive || WantsPhysics())
	{
		return FMath::Min(GetWarp(), WarpLevels[MaxPhysicsWarpLevel]);
	}
	return 1.0;
}

void FPeriapsisUniverse::PhysicsStep(double Dt)
{
	if (!Scene || !(Dt > 0.0))
	{
		return;
	}
	if (!bPhysicsActive)
	{
		if (!WantsPhysics())
		{
			return;
		}
		EnterPhysics();
	}

	// Every vessel is now at CurrentTime: the bubble's from the engine, the rest on rails.
	ReadPhysicsStates();
	if (!WantsPhysics())
	{
		LeavePhysics(); // warp raised above the bubble's limit, or the active vessel crashed
		return;
	}

	// The frame follows the active vessel across spheres of influence.
	Frame.ChangeBody(GetFlight().BodyIndex());

	// Floating origin: keep the active vessel near the origin, slow in the frame.
	if (Vessels[ActiveVessel].bPlaced)
	{
		const StateVector ActiveLocal = Frame.ToLocal(StateInFrameBody(Vessels[ActiveVessel]));
		if (NeedsRebase(ActiveLocal, Bubble))
		{
			Frame.Rebase(ActiveLocal);
			++RebaseCount;
			for (int32 Index = 0; Index < Vessels.Num(); ++Index)
			{
				if (Vessels[Index].bPlaced)
				{
					Scene->SetState(Index, Frame.ToLocal(StateInFrameBody(Vessels[Index])));
				}
			}
		}
	}

	UpdateMembership();
	ApplyPhysicsForces(Dt);

	CurrentTime += Dt;
	Frame.AdvanceTo(CurrentTime);
	// What the engine does not move this step coasts on rails (the active vessel too, burning,
	// while its body is still joining the simulation).
	for (FPeriapsisVessel& Vessel : Vessels)
	{
		if (!Vessel.bPlaced)
		{
			Vessel.Flight->Advance(CurrentTime - Vessel.Flight->Time());
		}
	}
}

void FPeriapsisUniverse::EnterPhysics()
{
	FPeriapsisVessel& Active = Vessels[ActiveVessel];
	Frame = LocalFrame(GetSystem(), Active.Flight->BodyIndex(), Active.Flight->State(), CurrentTime);
	bPhysicsActive = true;
	Active.bInBubble = true;
	Active.bPlaced = false;
	Scene->AddVessel(ActiveVessel, Active.Flight->Mass());
}

void FPeriapsisUniverse::LeavePhysics()
{
	for (int32 Index = 0; Index < Vessels.Num(); ++Index)
	{
		if (Vessels[Index].bInBubble)
		{
			RemoveFromBubble(Vessels[Index], Index);
		}
	}
	bPhysicsActive = false;
	Frame = LocalFrame();
}

void FPeriapsisUniverse::RemoveFromBubble(FPeriapsisVessel& Vessel, int32 Index)
{
	Scene->RemoveVessel(Index);
	Vessel.bInBubble = false;
	Vessel.bPlaced = false;
}

StateVector FPeriapsisUniverse::StateInFrameBody(const FPeriapsisVessel& Vessel) const
{
	return ChangeFrame(GetSystem(), Vessel.Flight->BodyIndex(), Frame.BodyIndex(), Vessel.Flight->State(), Vessel.Flight->Time());
}

void FPeriapsisUniverse::ReadPhysicsStates()
{
	for (int32 Index = 0; Index < Vessels.Num(); ++Index)
	{
		FPeriapsisVessel& Vessel = Vessels[Index];
		if (!Vessel.bInBubble || !Scene->IsReady(Index))
		{
			continue;
		}
		if (!Vessel.bPlaced)
		{
			// Joined the simulation: it takes over from the rails, at the same state.
			Scene->SetState(Index, Frame.ToLocal(StateInFrameBody(Vessel)));
			Vessel.bPlaced = true;
			continue;
		}
		Vessel.Flight->SetPhysicsState(Frame.BodyIndex(), Frame.ToBody(Scene->GetState(Index)), CurrentTime);
	}
}

void FPeriapsisUniverse::UpdateMembership()
{
	const bool bActiveFlying = GetFlight().Status() == FlightStatus::Flying;
	for (int32 Index = 0; Index < Vessels.Num(); ++Index)
	{
		if (Index == ActiveVessel)
		{
			continue;
		}
		FPeriapsisVessel& Vessel = Vessels[Index];
		const bool bBelongs = bActiveFlying && Vessel.Flight->Status() == FlightStatus::Flying
			&& BelongsInBubble(Vessel.bInBubble, VesselDistance(Index, ActiveVessel), Bubble);
		if (bBelongs && !Vessel.bInBubble)
		{
			Scene->AddVessel(Index, Vessel.Flight->Mass());
			Vessel.bInBubble = true;
			Vessel.bPlaced = false;
		}
		else if (!bBelongs && Vessel.bInBubble)
		{
			RemoveFromBubble(Vessel, Index);
		}
	}
}

void FPeriapsisUniverse::ApplyPhysicsForces(double Dt)
{
	Flight& Active = GetFlight();
	Active.SetThrustDirection(AttitudeDirection(Attitude, Active.State()));
	for (int32 Index = 0; Index < Vessels.Num(); ++Index)
	{
		FPeriapsisVessel& Vessel = Vessels[Index];
		if (!Vessel.bPlaced)
		{
			continue;
		}
		Vec3 Acceleration = Frame.StepGravity(Frame.ToLocal(StateInFrameBody(Vessel)), Dt);
		if (Index == ActiveVessel)
		{
			const double DeltaV = Vessel.Flight->BurnFor(Dt);
			Acceleration = Acceleration + Vessel.Flight->ThrustDirection() * (DeltaV / Dt);
			Scene->SetMass(Index, Vessel.Flight->Mass());
		}
		Scene->SetAcceleration(Index, Acceleration);
		Scene->SetAttitude(Index, Vessel.Flight->ThrustDirection());
	}
}

Vec3 FPeriapsisUniverse::BodyPositionAt(int32 BodyIndex, double Time) const
{
	return GetSystem().StateRelativeTo(BodyIndex, RootBody, Time).Position;
}

Vec3 FPeriapsisUniverse::VesselPosition(int32 Index) const
{
	const Flight& F = *Vessels[Index].Flight;
	return ChangeFrame(GetSystem(), F.BodyIndex(), RootBody, F.State(), F.Time()).Position;
}

Vec3 FPeriapsisUniverse::VesselPositionAt(int32 Index, double Time) const
{
	const Flight& F = *Vessels[Index].Flight;
	if (F.Status() != FlightStatus::Flying)
	{
		return VesselPosition(Index);
	}
	const StateVector State = PropagateKepler(F.State(), GetSystem().Body(F.BodyIndex()).Mu, Time - F.Time());
	return ChangeFrame(GetSystem(), F.BodyIndex(), RootBody, State, Time).Position;
}

double FPeriapsisUniverse::VesselDistance(int32 A, int32 B) const
{
	const Flight& FA = *Vessels[A].Flight;
	const Flight& FB = *Vessels[B].Flight;
	const StateVector BInA = ChangeFrame(GetSystem(), FB.BodyIndex(), FA.BodyIndex(), FB.State(), FB.Time());
	return Norm(BInA.Position - FA.State().Position);
}
