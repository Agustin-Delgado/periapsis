// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include "PeriapsisCore/StateVector.h"

namespace Periapsis::Core
{
	// Stumpff functions C(z) and S(z) used by the universal-variable formulation.
	// Series expansion near zero so near-parabolic orbits do not lose precision.
	PERIAPSISCORE_API double StumpffC(double Z);
	PERIAPSISCORE_API double StumpffS(double Z);

	// Kepler's equation M = E - e sin E, for 0 <= e < 1. Any M is accepted; the
	// returned E is in the same revolution as M (E - M is in [-e, e]).
	PERIAPSISCORE_API double SolveKeplerElliptic(double MeanAnomaly, double Eccentricity);

	// Hyperbolic Kepler equation M = e sinh H - H, for e > 1.
	PERIAPSISCORE_API double SolveKeplerHyperbolic(double MeanAnomaly, double Eccentricity);

	// Anomaly conversions. Angles in radians; true anomaly in (-pi, pi].
	PERIAPSISCORE_API double TrueToEccentricAnomaly(double TrueAnomaly, double Eccentricity);
	PERIAPSISCORE_API double EccentricToTrueAnomaly(double EccentricAnomaly, double Eccentricity);
	PERIAPSISCORE_API double TrueToHyperbolicAnomaly(double TrueAnomaly, double Eccentricity);
	PERIAPSISCORE_API double HyperbolicToTrueAnomaly(double HyperbolicAnomaly, double Eccentricity);
	PERIAPSISCORE_API double EccentricToMeanAnomaly(double EccentricAnomaly, double Eccentricity);
	PERIAPSISCORE_API double HyperbolicToMeanAnomaly(double HyperbolicAnomaly, double Eccentricity);

	// Mean anomaly for e != 1 (elliptic M or hyperbolic M). Parabolic orbits use
	// Barker's equation instead, see TrueToMeanAnomaly.
	PERIAPSISCORE_API double TrueToMeanAnomaly(double TrueAnomaly, double Eccentricity);
	PERIAPSISCORE_API double MeanToTrueAnomaly(double MeanAnomaly, double Eccentricity);

	// Two-body propagation with universal variables: one code path for elliptic,
	// parabolic and hyperbolic orbits. DeltaTime may be negative. Elliptic orbits
	// are reduced modulo the period first, so long propagations keep precision.
	PERIAPSISCORE_API StateVector PropagateKepler(const StateVector& Initial, double Mu, double DeltaTime);

	// Wrap an angle into (-pi, pi].
	PERIAPSISCORE_API double WrapAngle(double Angle);
	// Wrap an angle into [0, 2 pi).
	PERIAPSISCORE_API double WrapAngleTwoPi(double Angle);
}
