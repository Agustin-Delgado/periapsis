#include "PeriapsisCore/Flight.h"

#include "PeriapsisCore/Integrators.h"
#include "PeriapsisCore/Kepler.h"
#include "PeriapsisCore/Maneuver.h"
#include "PeriapsisCore/Rocket.h"
#include "PeriapsisCore/Surface.h"

#include <algorithm>
#include <cmath>

namespace Periapsis::Core
{
	Vec3 AttitudeDirection(AttitudeMode Mode, const StateVector& State, const CelestialBody* Body)
	{
		constexpr double MinSpeedForDirection = 0.5; // m/s
		const Vec3 Up = Normalized(State.Position);
		if (Mode == AttitudeMode::SurfacePrograde || Mode == AttitudeMode::SurfaceRetrograde)
		{
			const Vec3 Relative = Body ? State.Velocity - GroundVelocity(*Body, State.Position) : State.Velocity;
			if (Norm(Relative) < MinSpeedForDirection)
			{
				return Up;
			}
			return Mode == AttitudeMode::SurfacePrograde ? Normalized(Relative) : -Normalized(Relative);
		}
		if (Norm(State.Velocity) < MinSpeedForDirection)
		{
			return Up;
		}
		const ManeuverBasis Basis = ComputeManeuverBasis(State);
		switch (Mode)
		{
		case AttitudeMode::Prograde:
			return Basis.Prograde;
		case AttitudeMode::Retrograde:
			return -Basis.Prograde;
		case AttitudeMode::Normal:
			return Basis.Normal;
		case AttitudeMode::AntiNormal:
			return -Basis.Normal;
		case AttitudeMode::RadialOut:
			return Basis.RadialOut;
		case AttitudeMode::RadialIn:
			return -Basis.RadialOut;
		case AttitudeMode::SurfacePrograde:
		case AttitudeMode::SurfaceRetrograde:
			break;
		}
		return Basis.Prograde;
	}

	StateVector ChangeFrame(const CelestialSystem& System, int FromBody, int ToBody, const StateVector& State,
		double UniversalTime)
	{
		if (FromBody == ToBody)
		{
			return State;
		}
		const StateVector FromRelativeToTo = System.StateRelativeTo(FromBody, ToBody, UniversalTime);
		return {State.Position + FromRelativeToTo.Position, State.Velocity + FromRelativeToTo.Velocity};
	}

	Flight::Flight(const CelestialSystem& InSystem, const ShipConfig& InConfig, int InBodyIndex,
		const StateVector& InState, double InTime, const PredictionSettings& InPrediction)
		: SystemRef(InSystem)
		, Config(InConfig)
		, Prediction(InPrediction)
		, CurrentBody(InBodyIndex)
		, CurrentState(InState)
		, CurrentTime(InTime)
		, FuelLeft(InConfig.FuelMass)
	{
	}

	void Flight::SetThrottle(double InThrottle)
	{
		ThrottleValue = std::clamp(InThrottle, 0.0, 1.0);
	}

	void Flight::SetThrustDirection(const Vec3& InDirection)
	{
		const Vec3 Unit = Normalized(InDirection);
		if (NormSquared(Unit) > 0.0)
		{
			Direction = Unit;
		}
	}

	double Flight::RemainingDeltaV() const
	{
		return RocketDeltaV(Config.IspSeconds, Mass(), Config.DryMass);
	}

	void Flight::Advance(double DeltaTime)
	{
		if (CurrentStatus == FlightStatus::Landed && DeltaTime > 0.0)
		{
			CurrentTime += DeltaTime;
			FollowGround();
			return;
		}
		if (CurrentStatus != FlightStatus::Flying || !(DeltaTime > 0.0))
		{
			return;
		}
		const double TargetTime = CurrentTime + DeltaTime;
		if (IsThrusting())
		{
			Thrust(TargetTime);
		}
		if (CurrentStatus == FlightStatus::Flying && CurrentTime < TargetTime)
		{
			Coast(TargetTime);
		}
	}

	void Flight::SetPhysicsState(int InBodyIndex, const StateVector& InState, double InTime)
	{
		if (CurrentStatus != FlightStatus::Flying)
		{
			return;
		}
		CurrentBody = InBodyIndex;
		CurrentState = InState;
		CurrentTime = InTime;

		if (AltitudeAboveTerrain(SystemRef.Body(CurrentBody), CurrentState.Position, CurrentTime) < -kFellThroughDepth)
		{
			CurrentStatus = FlightStatus::Crashed;
			ThrottleValue = 0.0;
			Patches.clear();
			bPatchesValid = false;
			return;
		}
		CheckSoiTransition();
		if (!MatchesPrediction())
		{
			Patches.clear();
			bPatchesValid = false;
		}
	}

	bool Flight::MatchesPrediction() const
	{
		if (!bPatchesValid || Patches.empty())
		{
			return false;
		}
		const TrajectoryPatch& Patch = Patches.front();
		if (Patch.BodyIndex != CurrentBody || CurrentTime < Patch.StartTime || CurrentTime > Patch.EndTime)
		{
			return false;
		}
		const StateVector Expected = PropagateKepler(Patch.StartState, SystemRef.Body(CurrentBody).Mu, CurrentTime - Patch.StartTime);
		return Norm(CurrentState.Position - Expected.Position) < kPredictionMatchPosition
			&& Norm(CurrentState.Velocity - Expected.Velocity) < kPredictionMatchVelocity;
	}

	double Flight::BurnFor(double DeltaTime)
	{
		if (!IsThrusting() || !(DeltaTime > 0.0))
		{
			return 0.0;
		}
		const double Flow = MassFlowRate(Config.Thrust, Config.IspSeconds) * ThrottleValue;
		const double StartMass = Mass();
		FuelLeft = std::max(0.0, FuelLeft - Flow * DeltaTime);
		Patches.clear();
		bPatchesValid = false;
		return RocketDeltaV(Config.IspSeconds, StartMass, Mass());
	}

	void Flight::Land()
	{
		if (CurrentStatus != FlightStatus::Flying)
		{
			return;
		}
		CurrentStatus = FlightStatus::Landed;
		ThrottleValue = 0.0;
		LandedAt = ToBodyFixed(SystemRef.Body(CurrentBody), CurrentState.Position, CurrentTime);
		Patches.clear();
		bPatchesValid = false;
		FollowGround();
	}

	void Flight::TakeOff()
	{
		if (CurrentStatus == FlightStatus::Landed)
		{
			CurrentStatus = FlightStatus::Flying;
		}
	}

	void Flight::Crash()
	{
		CurrentStatus = FlightStatus::Crashed;
		ThrottleValue = 0.0;
		Patches.clear();
		bPatchesValid = false;
	}

	void Flight::FollowGround()
	{
		const CelestialBody& Body = SystemRef.Body(CurrentBody);
		CurrentState.Position = FromBodyFixed(Body, LandedAt, CurrentTime);
		CurrentState.Velocity = GroundVelocity(Body, CurrentState.Position);
	}

	const std::vector<TrajectoryPatch>& Flight::Trajectory()
	{
		if (!bPatchesValid && CurrentStatus == FlightStatus::Flying)
		{
			Predict();
		}
		return Patches;
	}

	void Flight::Predict()
	{
		Patches = PredictTrajectory(SystemRef, CurrentBody, CurrentState, CurrentTime, Prediction);
		bPatchesValid = !Patches.empty();
	}

	void Flight::Coast(double TargetTime)
	{
		// Each pass either reaches the target or consumes one patch; the cap only guards
		// against a degenerate prediction that makes no progress.
		for (int Pass = 0; Pass < 1000 && CurrentTime < TargetTime; ++Pass)
		{
			if (!bPatchesValid)
			{
				Predict();
				if (!bPatchesValid)
				{
					return;
				}
			}

			const TrajectoryPatch Patch = Patches.front();
			const double Mu = SystemRef.Body(Patch.BodyIndex).Mu;
			if (TargetTime < Patch.EndTime || (Patch.EndReason == PatchEnd::Horizon && Patch.EndTime <= CurrentTime))
			{
				CurrentBody = Patch.BodyIndex;
				CurrentState = PropagateKepler(Patch.StartState, Mu, TargetTime - Patch.StartTime);
				CurrentTime = TargetTime;
				if (Patch.EndTime <= CurrentTime)
				{
					bPatchesValid = false;
				}
				return;
			}

			// The target is past this patch: jump to its end and handle what happens there.
			CurrentBody = Patch.BodyIndex;
			CurrentState = Patch.EndState;
			CurrentTime = Patch.EndTime;
			Patches.erase(Patches.begin());

			switch (Patch.EndReason)
			{
			case PatchEnd::Impact:
				CurrentStatus = FlightStatus::Crashed;
				ThrottleValue = 0.0;
				Patches.clear();
				bPatchesValid = false;
				return;
			case PatchEnd::Horizon:
				bPatchesValid = false;
				break;
			case PatchEnd::SoiExit:
			case PatchEnd::SoiEntry:
				CurrentState = ChangeFrame(SystemRef, Patch.BodyIndex, Patch.NextBodyIndex, CurrentState, CurrentTime);
				CurrentBody = Patch.NextBodyIndex;
				// The next patch starts from this very state; without one, predict again.
				bPatchesValid = !Patches.empty();
				break;
			}
		}
	}

	void Flight::Thrust(double TargetTime)
	{
		bPatchesValid = false;
		Patches.clear();

		while (CurrentTime < TargetTime && IsThrusting())
		{
			const double Force = Config.Thrust * ThrottleValue;
			const double Flow = MassFlowRate(Config.Thrust, Config.IspSeconds) * ThrottleValue;
			double Dt = std::min(kPoweredStep, TargetTime - CurrentTime);
			const bool bBurnsOut = Flow * Dt >= FuelLeft;
			if (bBurnsOut)
			{
				Dt = FuelLeft / Flow;
			}

			const double StartMass = Mass();
			const double Mu = SystemRef.Body(CurrentBody).Mu;
			const Vec3 Push = Direction * Force;
			auto Acceleration = [&](double SinceStepStart, const StateVector& S)
			{
				return GravityAcceleration(S.Position, Mu) + Push / (StartMass - Flow * SinceStepStart);
			};
			CurrentState = StepRungeKutta4(CurrentState, 0.0, Dt, Acceleration);
			CurrentTime += Dt;
			FuelLeft = bBurnsOut ? 0.0 : FuelLeft - Flow * Dt;

			if (AltitudeAboveTerrain(SystemRef.Body(CurrentBody), CurrentState.Position, CurrentTime) <= 0.0)
			{
				CurrentStatus = FlightStatus::Crashed;
				ThrottleValue = 0.0;
				return;
			}
			CheckSoiTransition();
		}
	}

	bool Flight::CheckSoiTransition()
	{
		const CelestialBody& Body = SystemRef.Body(CurrentBody);
		if (Body.ParentIndex != kNoBody && Norm(CurrentState.Position) > Body.SphereOfInfluence)
		{
			CurrentState = ChangeFrame(SystemRef, CurrentBody, Body.ParentIndex, CurrentState, CurrentTime);
			CurrentBody = Body.ParentIndex;
			return true;
		}
		for (const int Child : SystemRef.Children(CurrentBody))
		{
			const Vec3 ChildPosition = SystemRef.StateRelativeToParent(Child, CurrentTime).Position;
			if (Norm(CurrentState.Position - ChildPosition) < SystemRef.Body(Child).SphereOfInfluence)
			{
				CurrentState = ChangeFrame(SystemRef, CurrentBody, Child, CurrentState, CurrentTime);
				CurrentBody = Child;
				return true;
			}
		}
		return false;
	}
}
