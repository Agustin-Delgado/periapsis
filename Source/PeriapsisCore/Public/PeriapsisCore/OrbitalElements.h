// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include "PeriapsisCore/StateVector.h"

namespace Periapsis::Core
{
	// Classical elements, derived from a state vector for display and for data files.
	// The semi-latus rectum is stored instead of the semi-major axis because it stays
	// finite for parabolic orbits (a is infinite there).
	//
	// Degenerate orbits follow fixed conventions so the round trip is exact:
	//  - equatorial (i = 0 or pi): the node is placed on +X, so LongitudeOfAscendingNode = 0;
	//  - circular (e = 0): periapsis is placed on the node, so ArgumentOfPeriapsis = 0 and
	//    TrueAnomaly becomes the argument of latitude (or the true longitude if also equatorial).
	struct OrbitalElements
	{
		double SemiLatusRectum = 0.0;         // p, m
		double Eccentricity = 0.0;            // e
		double Inclination = 0.0;             // i, rad, [0, pi]
		double LongitudeOfAscendingNode = 0.0; // rad, [0, 2 pi)
		double ArgumentOfPeriapsis = 0.0;     // rad, [0, 2 pi)
		double TrueAnomaly = 0.0;             // rad, (-pi, pi], at Epoch
		double Epoch = 0.0;                   // s, universal time
	};

	// Eccentricity / inclination below which an orbit is treated as circular / equatorial.
	inline constexpr double kCircularTolerance = 1e-11;
	inline constexpr double kEquatorialTolerance = 1e-11;

	PERIAPSISCORE_API OrbitalElements StateToElements(const StateVector& State, double Mu, double Epoch = 0.0);
	PERIAPSISCORE_API StateVector ElementsToState(const OrbitalElements& Elements, double Mu);

	// Elements as they are written in data files: semi-major axis and mean anomaly at epoch.
	// For hyperbolic orbits a is negative. Not usable for parabolic orbits (use p directly).
	PERIAPSISCORE_API OrbitalElements ElementsFromClassical(double SemiMajorAxis, double Eccentricity, double Inclination,
		double LongitudeOfAscendingNode, double ArgumentOfPeriapsis, double MeanAnomalyAtEpoch, double Epoch);

	// Evaluate the elements at any universal time (on-rails propagation).
	PERIAPSISCORE_API StateVector ElementsToStateAt(const OrbitalElements& Elements, double Mu, double UniversalTime);

	// Derived quantities. Infinite where they do not exist (apoapsis/period of open orbits).
	PERIAPSISCORE_API double SemiMajorAxis(const OrbitalElements& Elements);
	PERIAPSISCORE_API double PeriapsisRadius(const OrbitalElements& Elements);
	PERIAPSISCORE_API double ApoapsisRadius(const OrbitalElements& Elements);
	PERIAPSISCORE_API double OrbitalPeriod(const OrbitalElements& Elements, double Mu);
	PERIAPSISCORE_API double MeanMotion(const OrbitalElements& Elements, double Mu);
	PERIAPSISCORE_API double MeanAnomalyAtEpoch(const OrbitalElements& Elements);

	// Radius of the sphere of influence by Laplace's formula: a * (mu / muParent)^(2/5).
	PERIAPSISCORE_API double LaplaceSphereOfInfluence(double SemiMajorAxis, double Mu, double ParentMu);
}
