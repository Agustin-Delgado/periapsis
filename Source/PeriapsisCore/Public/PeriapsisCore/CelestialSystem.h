// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include "PeriapsisCore/OrbitalElements.h"

#include <string>
#include <string_view>
#include <vector>

namespace Periapsis::Core
{
	inline constexpr int kNoBody = -1;

	struct CelestialBody
	{
		std::string Id;
		int ParentIndex = kNoBody;
		double Mu = 0.0;                // m^3/s^2
		double Radius = 0.0;            // m
		double SphereOfInfluence = 0.0; // m; <= 0 means "compute with Laplace's formula"
		double RotationPeriod = 0.0;    // s, sidereal
		OrbitalElements Orbit;          // relative to the parent; ignored for the root
	};

	// Planets and moons on fixed Keplerian orbits (the system's ephemeris).
	// Bodies are stored in insertion order; a parent must be added before its children.
	class CelestialSystem
	{
	public:
		// Returns the new body's index, or kNoBody if the parent is invalid or a root
		// already exists. Fills in the sphere of influence when it is not given.
		PERIAPSISCORE_API int AddBody(CelestialBody NewBody);

		int BodyCount() const { return static_cast<int>(Bodies.size()); }
		const CelestialBody& Body(int Index) const { return Bodies[static_cast<size_t>(Index)]; }
		const std::vector<int>& Children(int Index) const { return ChildIndices[static_cast<size_t>(Index)]; }
		PERIAPSISCORE_API int FindBody(std::string_view Id) const;

		// State of a body relative to its parent at a universal time. Zero for the root.
		PERIAPSISCORE_API StateVector StateRelativeToParent(int Index, double UniversalTime) const;

		// State of Body relative to FrameBody, summing only up to their common ancestor
		// so nearby bodies do not pay the precision cost of the root frame.
		PERIAPSISCORE_API StateVector StateRelativeTo(int BodyIndex, int FrameBodyIndex, double UniversalTime) const;

	private:
		std::vector<CelestialBody> Bodies;
		std::vector<StateVector> EpochStates;
		std::vector<std::vector<int>> ChildIndices;
	};
}
