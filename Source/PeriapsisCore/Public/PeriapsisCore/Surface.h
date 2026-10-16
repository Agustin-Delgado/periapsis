// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/CelestialSystem.h"
#include "PeriapsisCore/Constants.h"

#include <cmath>

namespace Periapsis::Core
{
	// A body's surface turns with it around +Z. The body-fixed frame shares the body's centre
	// and turns with it: terrain, landed vessels and the physics frame near the ground live in it.

	inline double RotationAngle(const CelestialBody& Body, double UniversalTime)
	{
		if (!(Body.RotationPeriod > 0.0))
		{
			return Body.RotationAtEpoch;
		}
		// Reduced first, so the angle keeps its precision after years of game time.
		return Body.RotationAtEpoch + kTwoPi * std::fmod(UniversalTime, Body.RotationPeriod) / Body.RotationPeriod;
	}

	// Angular velocity around +Z, rad/s.
	inline double AngularSpeed(const CelestialBody& Body)
	{
		return Body.RotationPeriod > 0.0 ? kTwoPi / Body.RotationPeriod : 0.0;
	}

	inline Vec3 RotateAboutZ(const Vec3& V, double Angle)
	{
		const double C = std::cos(Angle);
		const double S = std::sin(Angle);
		return {C * V.X - S * V.Y, S * V.X + C * V.Y, V.Z};
	}

	// Vectors between the body's inertial frame and its body-fixed frame (same origin).
	inline Vec3 ToBodyFixed(const CelestialBody& Body, const Vec3& Inertial, double UniversalTime)
	{
		return RotateAboutZ(Inertial, -RotationAngle(Body, UniversalTime));
	}
	inline Vec3 FromBodyFixed(const CelestialBody& Body, const Vec3& BodyFixed, double UniversalTime)
	{
		return RotateAboutZ(BodyFixed, RotationAngle(Body, UniversalTime));
	}

	// Inertial velocity of the ground (or of anything fixed to the body) at a position.
	inline Vec3 GroundVelocity(const CelestialBody& Body, const Vec3& Position)
	{
		return Cross(Vec3{0.0, 0.0, AngularSpeed(Body)}, Position);
	}

	// Distance from the centre to the ground under an inertial position.
	inline double SurfaceRadius(const CelestialBody& Body, const Vec3& Position, double UniversalTime)
	{
		return Body.Radius + Body.Terrain.Height(ToBodyFixed(Body, Position, UniversalTime));
	}

	// Height above the ground under an inertial position (negative below it).
	inline double AltitudeAboveTerrain(const CelestialBody& Body, const Vec3& Position, double UniversalTime)
	{
		return Norm(Position) - SurfaceRadius(Body, Position, UniversalTime);
	}

	// No ground is higher than this.
	inline double MaxSurfaceRadius(const CelestialBody& Body)
	{
		return Body.Radius + Body.Terrain.MaxHeight();
	}
}
