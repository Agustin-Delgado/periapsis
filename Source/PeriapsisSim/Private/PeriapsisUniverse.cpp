#include "PeriapsisUniverse.h"

#include "PeriapsisCore/Kepler.h"
#include "PeriapsisCore/Surface.h"
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
	Ship.Length = Scenario.ShipLength;
	Ship.Width = Scenario.ShipDiameter;
	Ship.Flight = MakeUnique<Flight>(System, Scenario.Ship, Scenario.StartBody, Scenario.StartState, Scenario.StartTime);
	for (const FPeriapsisPassiveVessel& Passive : Scenario.OtherVessels)
	{
		ShipConfig NoEngine;
		NoEngine.DryMass = Passive.Mass;
		FPeriapsisVessel& Vessel = Vessels.AddDefaulted_GetRef();
		Vessel.Name = Passive.Name;
		Vessel.Length = Passive.Size;
		Vessel.Width = Passive.Size;
		Vessel.Flight = MakeUnique<Flight>(System, NoEngine, Passive.StartBody, Passive.StartState, Scenario.StartTime);
	}

	CurrentTime = Scenario.StartTime;
	Attitude = Scenario.StartAttitude;
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

Vec3 FPeriapsisUniverse::AttitudeFor(const Flight& F) const
{
	return AttitudeDirection(Attitude, F.State(), &GetSystem().Body(F.BodyIndex()));
}

void FPeriapsisUniverse::TakeOffIfThrottled()
{
	Flight& Active = GetFlight();
	if (Active.Status() == FlightStatus::Landed && Active.Throttle() > 0.0 && Active.Fuel() > 0.0)
	{
		Active.TakeOff();
		FPeriapsisVessel& Vessel = Vessels[ActiveVessel];
		Vessel.RestTime = 0.0;
		if (Vessel.bFrozen)
		{
			Scene->SetFrozen(ActiveVessel, false);
			Vessel.bFrozen = false;
		}
	}
}

void FPeriapsisUniverse::Tick(double RealDeltaSeconds)
{
	if (GetFlight().Status() == FlightStatus::Crashed || !(RealDeltaSeconds > 0.0))
	{
		WarpLevel = 0;
		return;
	}
	if (!bPhysicsActive)
	{
		TakeOffIfThrottled();
	}
	ClampWarp(RealDeltaSeconds);
	if (bPhysicsActive || WantsPhysics())
	{
		return; // the physics steps move the clock (and leave the bubble if the warp went up)
	}

	CurrentTime += RealDeltaSeconds * GetWarp();
	Flight& Active = GetFlight();
	Active.SetThrustDirection(AttitudeFor(Active));
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
	return Scene != nullptr && WarpLevel <= MaxPhysicsWarpLevel && GetFlight().Status() != FlightStatus::Crashed;
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

	// The frame follows the active vessel across spheres of influence, and turns with the
	// body near the ground.
	UpdateFrameKind();

	// Floating origin: keep the active vessel near the origin, slow in the frame.
	if (Vessels[ActiveVessel].bPlaced)
	{
		const StateVector ActiveLocal = Frame.ToLocal(StateInFrameBody(Vessels[ActiveVessel]));
		if (Frame.NeedsRebase(ActiveLocal, Bubble))
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
	TakeOffIfThrottled();
	HandleGround(Dt);
	if (GetFlight().Status() == FlightStatus::Crashed)
	{
		LeavePhysics();
		return;
	}
	ApplyPhysicsForces(Dt);

	CurrentTime += Dt;
	Frame.AdvanceTo(CurrentTime);
	// What the engine does not move this step coasts on rails (the active vessel too, burning,
	// while its body is still joining the simulation), and landed vessels turn with the ground.
	for (FPeriapsisVessel& Vessel : Vessels)
	{
		if (!Vessel.bPlaced || Vessel.bFrozen)
		{
			Vessel.Flight->Advance(CurrentTime - Vessel.Flight->Time());
		}
	}
}

void FPeriapsisUniverse::UpdateFrameKind()
{
	const Flight& Active = GetFlight();
	const CelestialBody& Body = GetSystem().Body(Active.BodyIndex());
	const double Altitude = Norm(Active.State().Position) - Body.Radius;
	const double Threshold = GetBodyInfo(Active.BodyIndex()).RotatingFrameAltitude;
	const bool bBodyFixed = Frame.IsBodyFixed() ? Altitude < Threshold * RotatingFrameExitFactor : Altitude < Threshold;
	if (bBodyFixed == Frame.IsBodyFixed() && Frame.BodyIndex() == Active.BodyIndex())
	{
		return;
	}
	// A new frame on the active vessel; every simulated body is re-expressed in it, exactly.
	Frame = bBodyFixed ? LocalFrame::MakeBodyFixed(GetSystem(), Active.BodyIndex(), Active.State().Position, CurrentTime)
					   : LocalFrame(GetSystem(), Active.BodyIndex(), Active.State(), CurrentTime);
	++FrameChanges;
	for (int32 Index = 0; Index < Vessels.Num(); ++Index)
	{
		if (Vessels[Index].bPlaced)
		{
			Scene->SetState(Index, Frame.ToLocal(StateInFrameBody(Vessels[Index])));
		}
	}
}

void FPeriapsisUniverse::HandleGround(double Dt)
{
	if (!Frame.IsBodyFixed())
	{
		for (FPeriapsisVessel& Vessel : Vessels)
		{
			Vessel.bHeldByGround = false;
			Vessel.bTouchingGround = false;
			Vessel.RestTime = 0.0;
		}
		return;
	}
	const CelestialBody& Body = GetSystem().Body(Frame.BodyIndex());
	for (int32 Index = 0; Index < Vessels.Num(); ++Index)
	{
		FPeriapsisVessel& Vessel = Vessels[Index];
		Vessel.bHeldByGround = false;
		Vessel.bTouchingGround = false;
		Flight& F = *Vessel.Flight;
		if (!Vessel.bPlaced || Vessel.bFrozen || F.Status() != FlightStatus::Flying || F.BodyIndex() != Frame.BodyIndex())
		{
			continue;
		}

		// In the body-fixed frame the velocity is the velocity relative to the ground.
		StateVector Local = Frame.ToLocal(F.State());
		const Vec3 Up = Frame.DirectionToLocal(Normalized(F.State().Position));
		const double Clearance = AltitudeAboveTerrain(Body, F.State().Position, F.Time()) - Vessel.GroundClearance();
		const double Sink = -Dot(Local.Velocity, Up);
		const bool bTouching = Clearance < GroundTolerance;
		Vessel.bTouchingGround = bTouching;
		const double ImpactSpeed = FMath::Max(Sink, Vessel.LastSinkSpeed);
		Vessel.LastSinkSpeed = Sink;

		if (bTouching && ImpactSpeed > CrashSpeed)
		{
			F.Crash();
			RemoveFromBubble(Vessel, Index);
			continue;
		}

		// The analytic ground: where the engine has no terrain (or let a vessel sink through it),
		// the vessel is put back on the surface and held there.
		const bool bBelow = Clearance < -GroundTolerance;
		if (bBelow || (bTouching && !Scene->HasGround()))
		{
			if (Clearance < 0.0)
			{
				Local.Position = Local.Position + Up * (-Clearance);
			}
			const double Rising = FMath::Max(Dot(Local.Velocity, Up), 0.0);
			Local.Velocity = Scene->HasGround() ? Local.Velocity + Up * FMath::Max(Sink, 0.0) : Up * Rising;
			Scene->SetState(Index, Local);
			F.SetPhysicsState(Frame.BodyIndex(), Frame.ToBody(Local), F.Time());
			Vessel.bHeldByGround = !Scene->HasGround();
		}

		// Still on the ground long enough: landed, and asleep.
		const bool bStill = bTouching && Norm(Local.Velocity) < RestSpeed && !F.IsThrusting();
		Vessel.RestTime = bStill ? Vessel.RestTime + Dt : 0.0;
		if (Vessel.RestTime >= RestDuration)
		{
			F.Land();
			Scene->SetState(Index, Frame.ToLocal(F.State()));
			Scene->SetFrozen(Index, true);
			Vessel.bFrozen = true;
			Vessel.bHeldByGround = false;
			Vessel.RestTime = 0.0;
		}
	}
}

void FPeriapsisUniverse::EnterPhysics()
{
	FPeriapsisVessel& Active = Vessels[ActiveVessel];
	Frame = LocalFrame(GetSystem(), Active.Flight->BodyIndex(), Active.Flight->State(), CurrentTime);
	bPhysicsActive = true;
	UpdateFrameKind();
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
	Vessel.bFrozen = false;
	Vessel.bHeldByGround = false;
	Vessel.RestTime = 0.0;
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
			// Joined the simulation: it takes over from the rails, at the same state. A landed
			// vessel joins asleep.
			Scene->SetState(Index, Frame.ToLocal(StateInFrameBody(Vessel)));
			Vessel.bPlaced = true;
			Vessel.bFrozen = Vessel.Flight->Status() == FlightStatus::Landed;
			if (Vessel.bFrozen)
			{
				Scene->SetFrozen(Index, true);
			}
			continue;
		}
		if (Vessel.bFrozen)
		{
			continue; // landed: its flight follows the ground
		}
		Vessel.Flight->SetPhysicsState(Frame.BodyIndex(), Frame.ToBody(Scene->GetState(Index)), CurrentTime);
	}
}

void FPeriapsisUniverse::UpdateMembership()
{
	const bool bActiveFlying = GetFlight().Status() != FlightStatus::Crashed;
	for (int32 Index = 0; Index < Vessels.Num(); ++Index)
	{
		if (Index == ActiveVessel)
		{
			continue;
		}
		FPeriapsisVessel& Vessel = Vessels[Index];
		const bool bBelongs = bActiveFlying && Vessel.Flight->Status() != FlightStatus::Crashed
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
	Active.SetThrustDirection(AttitudeFor(Active));
	for (int32 Index = 0; Index < Vessels.Num(); ++Index)
	{
		FPeriapsisVessel& Vessel = Vessels[Index];
		if (!Vessel.bPlaced || Vessel.bFrozen)
		{
			continue;
		}
		const StateVector Local = Frame.ToLocal(StateInFrameBody(Vessel));
		Vec3 Acceleration = Frame.StepGravity(Local, Dt);
		if (Vessel.bHeldByGround)
		{
			// The analytic ground pushes back and holds (no sliding): only what lifts the vessel acts.
			const Vec3 Up = Frame.DirectionToLocal(Normalized(Vessel.Flight->State().Position));
			Acceleration = Up * FMath::Max(Dot(Acceleration, Up), 0.0);
		}
		if (Index == ActiveVessel)
		{
			const double DeltaV = Vessel.Flight->BurnFor(Dt);
			Acceleration = Acceleration + Frame.DirectionToLocal(Vessel.Flight->ThrustDirection()) * (DeltaV / Dt);
			Scene->SetMass(Index, Vessel.Flight->Mass());
		}
		Scene->SetAcceleration(Index, Acceleration, Vessel.bTouchingGround);
		// Attitude is held in flight; standing on the ground with the engine off, the vessel
		// settles on its own (holding it upright on a slope makes it slide).
		if (!Vessel.bTouchingGround || Vessel.Flight->IsThrusting())
		{
			Scene->SetAttitude(Index, Frame.DirectionToLocal(Vessel.Flight->ThrustDirection()));
		}
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
	const CelestialBody& Body = GetSystem().Body(F.BodyIndex());
	if (F.Status() == FlightStatus::Landed)
	{
		const Vec3 Position = FromBodyFixed(Body, F.LandedPosition(), Time);
		return ChangeFrame(GetSystem(), F.BodyIndex(), RootBody, {Position, GroundVelocity(Body, Position)}, Time).Position;
	}
	if (F.Status() != FlightStatus::Flying)
	{
		return VesselPosition(Index);
	}
	const StateVector State = PropagateKepler(F.State(), GetSystem().Body(F.BodyIndex()).Mu, Time - F.Time());
	return ChangeFrame(GetSystem(), F.BodyIndex(), RootBody, State, Time).Position;
}

StateVector FPeriapsisUniverse::VesselLocalState(int32 Index) const
{
	if (!Frame.IsValid())
	{
		return {};
	}
	const Flight& F = *Vessels[Index].Flight;
	StateVector Now = F.State();
	if (F.Status() == FlightStatus::Flying)
	{
		Now = PropagateKepler(F.State(), GetSystem().Body(F.BodyIndex()).Mu, Frame.Time() - F.Time());
	}
	else if (F.Status() == FlightStatus::Landed)
	{
		const CelestialBody& Body = GetSystem().Body(F.BodyIndex());
		Now.Position = FromBodyFixed(Body, F.LandedPosition(), Frame.Time());
		Now.Velocity = GroundVelocity(Body, Now.Position);
	}
	return Frame.ToLocal(ChangeFrame(GetSystem(), F.BodyIndex(), Frame.BodyIndex(), Now, Frame.Time()));
}

double FPeriapsisUniverse::VesselAltitudeAboveTerrain(int32 Index) const
{
	const Flight& F = *Vessels[Index].Flight;
	return AltitudeAboveTerrain(GetSystem().Body(F.BodyIndex()), F.State().Position, F.Time());
}

double FPeriapsisUniverse::VesselDistance(int32 A, int32 B) const
{
	const Flight& FA = *Vessels[A].Flight;
	const Flight& FB = *Vessels[B].Flight;
	const StateVector BInA = ChangeFrame(GetSystem(), FB.BodyIndex(), FA.BodyIndex(), FB.State(), FB.Time());
	return Norm(BInA.Position - FA.State().Position);
}
