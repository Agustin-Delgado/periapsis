// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include "PeriapsisCore/CelestialSystem.h"
#include "PeriapsisCore/PatchedConics.h"

#include <vector>

namespace Periapsis::Core
{
	// A point ship with a single engine (M2). Parts and stages replace this in M5.
	struct ShipConfig
	{
		double DryMass = 0.0;    // kg
		double FuelMass = 0.0;   // kg, at the start
		double Thrust = 0.0;     // N, vacuum
		double IspSeconds = 0.0; // s, vacuum
	};

	enum class FlightStatus
	{
		Flying,
		Landed, // at rest on the ground, turning with the body
		Crashed,
	};

	// Where the engine points, relative to the orbit around the current body.
	enum class AttitudeMode
	{
		Prograde,
		Retrograde,
		Normal,
		AntiNormal,
		RadialOut,
		RadialIn,
		SurfacePrograde,   // along the velocity relative to the ground
		SurfaceRetrograde, // against it: the landing attitude
	};

	// The surface modes need the body (its rotation); without it they use the inertial velocity.
	// When the velocity is too small to give a direction, the modes point up (radial out).
	PERIAPSISCORE_API Vec3 AttitudeDirection(AttitudeMode Mode, const StateVector& State, const CelestialBody* Body = nullptr);

	// Re-expresses a state relative to an adjacent body in the hierarchy (parent or child).
	PERIAPSISCORE_API StateVector ChangeFrame(const CelestialSystem& System, int FromBody, int ToBody,
		const StateVector& State, double UniversalTime);

	// The flight of one ship through the system.
	//
	// Coasting is on rails: the state is evaluated on the predicted conic patches, which is
	// exact at any time warp (no energy drift, no skipped encounters). Thrusting leaves the
	// rails and integrates gravity plus thrust with RK4 in fixed sub-steps; afterwards the
	// trajectory is predicted again from the new state.
	class Flight
	{
	public:
		// Integration step while thrusting, in seconds of simulation time.
		static constexpr double kPoweredStep = 1.0 / 120.0;
		// The physics bubble keeps vessels on the ground; one this deep below it fell through.
		static constexpr double kFellThroughDepth = 100.0; // m
		// A physics state this close to the predicted trajectory keeps the prediction.
		static constexpr double kPredictionMatchPosition = 0.01; // m
		static constexpr double kPredictionMatchVelocity = 1e-4; // m/s

		PERIAPSISCORE_API Flight(const CelestialSystem& InSystem, const ShipConfig& InConfig, int InBodyIndex,
			const StateVector& InState, double InTime, const PredictionSettings& InPrediction = {});

		PERIAPSISCORE_API void SetThrottle(double InThrottle);
		double Throttle() const { return ThrottleValue; }

		// Engine direction in the inertial frame of the current body. Normalized internally.
		PERIAPSISCORE_API void SetThrustDirection(const Vec3& Direction);
		const Vec3& ThrustDirection() const { return Direction; }

		PERIAPSISCORE_API void Advance(double DeltaTime);

		// Physics bubble (M3): the physics engine moves the ship instead of Advance(). The
		// state is relative to InBodyIndex at InTime; impacts and sphere-of-influence changes
		// are checked as after a thrust step. The predicted trajectory is kept while the state
		// stays on it, so coasting in the bubble does not re-predict every step.
		PERIAPSISCORE_API void SetPhysicsState(int InBodyIndex, const StateVector& InState, double InTime);

		// Burns the engine at the current throttle for DeltaTime, for the physics bubble to
		// apply, and returns the delta-v delivered along ThrustDirection() (m/s, rocket
		// equation). Zero when the engine is off or the tanks are empty.
		PERIAPSISCORE_API double BurnFor(double DeltaTime);

		// Puts the ship to rest on the ground where it is: from then on it turns with the body
		// and costs nothing to simulate (spec 5.10). TakeOff() makes it fly again from there.
		PERIAPSISCORE_API void Land();
		PERIAPSISCORE_API void TakeOff();
		// Destroyed by an impact the physics bubble detected.
		PERIAPSISCORE_API void Crash();
		// Where the landed ship sits, in the body-fixed frame.
		const Vec3& LandedPosition() const { return LandedAt; }

		double Time() const { return CurrentTime; }
		int BodyIndex() const { return CurrentBody; }
		const StateVector& State() const { return CurrentState; } // relative to BodyIndex()
		double Fuel() const { return FuelLeft; }
		double Mass() const { return Config.DryMass + FuelLeft; }
		FlightStatus Status() const { return CurrentStatus; }
		const ShipConfig& Ship() const { return Config; }
		const CelestialSystem& System() const { return SystemRef; }

		bool IsThrusting() const { return CurrentStatus == FlightStatus::Flying && ThrottleValue > 0.0 && FuelLeft > 0.0; }
		PERIAPSISCORE_API double RemainingDeltaV() const;

		// Predicted conics from the current time on. Recomputed only when the ship has
		// thrusted or left the patches computed before.
		PERIAPSISCORE_API const std::vector<TrajectoryPatch>& Trajectory();

	private:
		void Coast(double TargetTime);
		void Thrust(double TargetTime);
		void Predict();
		void FollowGround();
		bool CheckSoiTransition();
		bool MatchesPrediction() const;

		const CelestialSystem& SystemRef;
		ShipConfig Config;
		PredictionSettings Prediction;

		int CurrentBody;
		StateVector CurrentState;
		double CurrentTime;
		double FuelLeft;
		double ThrottleValue = 0.0;
		Vec3 Direction{1.0, 0.0, 0.0};
		FlightStatus CurrentStatus = FlightStatus::Flying;
		Vec3 LandedAt;

		std::vector<TrajectoryPatch> Patches;
		size_t PatchIndex = 0;
		bool bPatchesValid = false;
	};
}
