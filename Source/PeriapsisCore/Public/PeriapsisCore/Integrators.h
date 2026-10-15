// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/StateVector.h"

namespace Periapsis::Core
{
	// Numerical integrators for when a ship leaves the rails (thrust, drag).
	// Acceleration is a callable: Vec3 (double Time, const StateVector& State).

	// Classic fourth-order Runge-Kutta. Handles velocity-dependent forces (drag).
	template <typename AccelerationFn>
	StateVector StepRungeKutta4(const StateVector& S, double Time, double Dt, AccelerationFn&& Acceleration)
	{
		const double HalfDt = 0.5 * Dt;

		const Vec3 K1V = Acceleration(Time, S);
		const Vec3 K1R = S.Velocity;

		const StateVector S2{S.Position + K1R * HalfDt, S.Velocity + K1V * HalfDt};
		const Vec3 K2V = Acceleration(Time + HalfDt, S2);
		const Vec3 K2R = S2.Velocity;

		const StateVector S3{S.Position + K2R * HalfDt, S.Velocity + K2V * HalfDt};
		const Vec3 K3V = Acceleration(Time + HalfDt, S3);
		const Vec3 K3R = S3.Velocity;

		const StateVector S4{S.Position + K3R * Dt, S.Velocity + K3V * Dt};
		const Vec3 K4V = Acceleration(Time + Dt, S4);
		const Vec3 K4R = S4.Velocity;

		const double Sixth = Dt / 6.0;
		return {
			S.Position + (K1R + K2R * 2.0 + K3R * 2.0 + K4R) * Sixth,
			S.Velocity + (K1V + K2V * 2.0 + K3V * 2.0 + K4V) * Sixth,
		};
	}

	// Velocity Verlet: symplectic, second order, for position-only forces (gravity).
	// The acceleration is still called with a full state for a uniform signature; its
	// velocity is the half-step velocity and should be ignored by the callee.
	template <typename AccelerationFn>
	StateVector StepVelocityVerlet(const StateVector& S, double Time, double Dt, AccelerationFn&& Acceleration)
	{
		const Vec3 A0 = Acceleration(Time, S);
		const Vec3 HalfVelocity = S.Velocity + A0 * (0.5 * Dt);
		const Vec3 Position = S.Position + HalfVelocity * Dt;
		const Vec3 A1 = Acceleration(Time + Dt, StateVector{Position, HalfVelocity});
		return {Position, HalfVelocity + A1 * (0.5 * Dt)};
	}
}
