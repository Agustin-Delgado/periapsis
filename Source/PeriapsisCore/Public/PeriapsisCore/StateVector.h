// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Vec3.h"

namespace Periapsis::Core
{
	// Source of truth for anything on a conic: position and velocity relative to the
	// dominant body, in its inertial frame. Elements are derived from it on demand.
	struct StateVector
	{
		Vec3 Position; // m
		Vec3 Velocity; // m/s
	};

	// Component-wise, for translating states between frames.
	inline StateVector operator+(const StateVector& A, const StateVector& B) { return {A.Position + B.Position, A.Velocity + B.Velocity}; }
	inline StateVector operator-(const StateVector& A, const StateVector& B) { return {A.Position - B.Position, A.Velocity - B.Velocity}; }

	// Specific orbital energy (J/kg). Negative = bound.
	inline double SpecificEnergy(const StateVector& S, double Mu)
	{
		return 0.5 * NormSquared(S.Velocity) - Mu / Norm(S.Position);
	}

	// Specific angular momentum vector (m^2/s).
	inline Vec3 AngularMomentum(const StateVector& S) { return Cross(S.Position, S.Velocity); }

	// Point-mass gravitational acceleration at a position relative to the body.
	inline Vec3 GravityAcceleration(const Vec3& Position, double Mu)
	{
		const double R = Norm(Position);
		return Position * (-Mu / (R * R * R));
	}
}
