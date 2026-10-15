#include "PeriapsisCore/Flight.h"

#include "PeriapsisCore/Integrators.h"
#include "PeriapsisCore/Kepler.h"
#include "PeriapsisCore/Maneuver.h"
#include "PeriapsisCore/Rocket.h"

#include <algorithm>
#include <cmath>

namespace Periapsis::Core
{
	Vec3 AttitudeDirection(AttitudeMode Mode, const StateVector& State)
	{
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

			if (Norm(CurrentState.Position) <= SystemRef.Body(CurrentBody).Radius)
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
