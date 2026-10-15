#include "PeriapsisCore/OrbitalElements.h"

#include "PeriapsisCore/Constants.h"
#include "PeriapsisCore/Kepler.h"

#include <cmath>
#include <limits>

namespace Periapsis::Core
{
	namespace
	{
		constexpr double kInfinity = std::numeric_limits<double>::infinity();

		// Rotation perifocal -> inertial: R3(-lan) * R1(-i) * R3(-argp), applied to (x, y, 0).
		Vec3 PerifocalToInertial(double PX, double PY, const OrbitalElements& El)
		{
			const double CosO = std::cos(El.LongitudeOfAscendingNode);
			const double SinO = std::sin(El.LongitudeOfAscendingNode);
			const double CosI = std::cos(El.Inclination);
			const double SinI = std::sin(El.Inclination);
			const double CosW = std::cos(El.ArgumentOfPeriapsis);
			const double SinW = std::sin(El.ArgumentOfPeriapsis);

			const double Xx = CosO * CosW - SinO * SinW * CosI;
			const double Xy = SinO * CosW + CosO * SinW * CosI;
			const double Xz = SinW * SinI;
			const double Yx = -CosO * SinW - SinO * CosW * CosI;
			const double Yy = -SinO * SinW + CosO * CosW * CosI;
			const double Yz = CosW * SinI;

			return {Xx * PX + Yx * PY, Xy * PX + Yy * PY, Xz * PX + Yz * PY};
		}
	}

	OrbitalElements StateToElements(const StateVector& State, double Mu, double Epoch)
	{
		const Vec3& R = State.Position;
		const Vec3& V = State.Velocity;
		const double RMag = Norm(R);

		const Vec3 H = Cross(R, V);
		const double HMag = Norm(H);
		const Vec3 HUnit = H / HMag;

		// Node vector k x h. Its length relative to h is sin(i).
		const Vec3 Node{-H.Y, H.X, 0.0};
		const double NodeMag = Norm(Node);

		const Vec3 EVec = (R * (NormSquared(V) - Mu / RMag) - V * Dot(R, V)) / Mu;
		const double E = Norm(EVec);

		OrbitalElements El;
		El.Epoch = Epoch;
		El.SemiLatusRectum = HMag * HMag / Mu;
		// atan2 keeps precision near i = 0 and i = pi, where acos(hz/h) does not.
		El.Inclination = std::atan2(NodeMag, H.Z);

		const bool bEquatorial = NodeMag <= kEquatorialTolerance * HMag;
		const bool bCircular = E <= kCircularTolerance;

		const Vec3 NodeUnit = bEquatorial ? Vec3{1.0, 0.0, 0.0} : Node / NodeMag;
		El.LongitudeOfAscendingNode = bEquatorial ? 0.0 : WrapAngleTwoPi(std::atan2(Node.Y, Node.X));

		const Vec3 PeriapsisUnit = bCircular ? NodeUnit : EVec / E;
		El.Eccentricity = bCircular ? 0.0 : E;
		El.ArgumentOfPeriapsis = bCircular ? 0.0 : WrapAngleTwoPi(SignedAngle(NodeUnit, PeriapsisUnit, HUnit));
		El.TrueAnomaly = WrapAngle(SignedAngle(PeriapsisUnit, R / RMag, HUnit));
		return El;
	}

	StateVector ElementsToState(const OrbitalElements& El, double Mu)
	{
		const double P = El.SemiLatusRectum;
		const double E = El.Eccentricity;
		const double CosNu = std::cos(El.TrueAnomaly);
		const double SinNu = std::sin(El.TrueAnomaly);

		const double R = P / (1.0 + E * CosNu);
		const double VScale = std::sqrt(Mu / P);

		StateVector State;
		State.Position = PerifocalToInertial(R * CosNu, R * SinNu, El);
		State.Velocity = PerifocalToInertial(-VScale * SinNu, VScale * (E + CosNu), El);
		return State;
	}

	OrbitalElements ElementsFromClassical(double SemiMajorAxisM, double Eccentricity, double Inclination,
		double LongitudeOfAscendingNode, double ArgumentOfPeriapsis, double MeanAnomalyAtEpochRad, double Epoch)
	{
		OrbitalElements El;
		El.SemiLatusRectum = SemiMajorAxisM * (1.0 - Eccentricity * Eccentricity);
		El.Eccentricity = Eccentricity;
		El.Inclination = Inclination;
		El.LongitudeOfAscendingNode = WrapAngleTwoPi(LongitudeOfAscendingNode);
		El.ArgumentOfPeriapsis = WrapAngleTwoPi(ArgumentOfPeriapsis);
		El.TrueAnomaly = WrapAngle(MeanToTrueAnomaly(MeanAnomalyAtEpochRad, Eccentricity));
		El.Epoch = Epoch;
		return El;
	}

	StateVector ElementsToStateAt(const OrbitalElements& El, double Mu, double UniversalTime)
	{
		return PropagateKepler(ElementsToState(El, Mu), Mu, UniversalTime - El.Epoch);
	}

	double SemiMajorAxis(const OrbitalElements& El)
	{
		const double OneMinusE2 = 1.0 - El.Eccentricity * El.Eccentricity;
		return OneMinusE2 == 0.0 ? kInfinity : El.SemiLatusRectum / OneMinusE2;
	}

	double PeriapsisRadius(const OrbitalElements& El)
	{
		return El.SemiLatusRectum / (1.0 + El.Eccentricity);
	}

	double ApoapsisRadius(const OrbitalElements& El)
	{
		return El.Eccentricity < 1.0 ? El.SemiLatusRectum / (1.0 - El.Eccentricity) : kInfinity;
	}

	double OrbitalPeriod(const OrbitalElements& El, double Mu)
	{
		if (El.Eccentricity >= 1.0)
		{
			return kInfinity;
		}
		const double A = SemiMajorAxis(El);
		return kTwoPi * std::sqrt(A * A * A / Mu);
	}

	double MeanMotion(const OrbitalElements& El, double Mu)
	{
		if (El.Eccentricity == 1.0)
		{
			// Barker's equation uses 2 sqrt(mu / p^3) as its "mean motion".
			const double P = El.SemiLatusRectum;
			return 2.0 * std::sqrt(Mu / (P * P * P));
		}
		const double A = std::fabs(SemiMajorAxis(El));
		return std::sqrt(Mu / (A * A * A));
	}

	double MeanAnomalyAtEpoch(const OrbitalElements& El)
	{
		return TrueToMeanAnomaly(El.TrueAnomaly, El.Eccentricity);
	}

	double LaplaceSphereOfInfluence(double SemiMajorAxisM, double Mu, double ParentMu)
	{
		return SemiMajorAxisM * std::pow(Mu / ParentMu, 0.4);
	}
}
