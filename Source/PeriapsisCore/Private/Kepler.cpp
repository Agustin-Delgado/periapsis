#include "PeriapsisCore/Kepler.h"

#include "PeriapsisCore/Constants.h"

#include <cmath>
#include <limits>

namespace Periapsis::Core
{
	namespace
	{
		constexpr double kEpsilon = std::numeric_limits<double>::epsilon();

		// Safeguarded Newton for monotonically increasing functions: Newton steps,
		// falling back to bisection whenever a step leaves the bracket or shrinks too
		// slowly. Always converges as long as F(Lo) < 0 <= F(Hi); NaN evaluations are
		// treated as "above".
		template <typename FunctionAndDerivative>
		double SolveMonotonic(FunctionAndDerivative&& Eval, double Lo, double Hi, double Guess)
		{
			double X = (Guess > Lo && Guess < Hi) ? Guess : 0.5 * (Lo + Hi);
			// Far down a hyperbola the function grows exponentially and Newton crawls
			// toward the root by a constant amount per step: bisect unless each step is
			// at most half the one before last (as in Numerical Recipes' rtsafe).
			double StepBeforeLast = Hi - Lo;
			double LastStep = Hi - Lo;
			for (int Iteration = 0; Iteration < 200; ++Iteration)
			{
				double Derivative = 0.0;
				const double Value = Eval(X, Derivative);
				if (Value == 0.0)
				{
					return X;
				}
				if (Value < 0.0)
				{
					Lo = X;
				}
				else
				{
					Hi = X;
				}

				double Next = X - Value / Derivative;
				if (!std::isfinite(Next) || Next <= Lo || Next >= Hi || std::fabs(Next - X) > 0.5 * std::fabs(StepBeforeLast))
				{
					Next = 0.5 * (Lo + Hi);
				}
				StepBeforeLast = LastStep;
				LastStep = Next - X;

				const double Tolerance = 2.0 * kEpsilon * std::fmax(1.0, std::fabs(Next));
				if (std::fabs(Next - X) <= Tolerance || (Hi - Lo) <= Tolerance)
				{
					return Next;
				}
				X = Next;
			}
			return X;
		}
	}

	double WrapAngle(double Angle)
	{
		double Wrapped = std::fmod(Angle + kPi, kTwoPi);
		if (Wrapped <= 0.0)
		{
			Wrapped += kTwoPi;
		}
		return Wrapped - kPi;
	}

	double WrapAngleTwoPi(double Angle)
	{
		double Wrapped = std::fmod(Angle, kTwoPi);
		if (Wrapped < 0.0)
		{
			Wrapped += kTwoPi;
		}
		return Wrapped >= kTwoPi ? 0.0 : Wrapped;
	}

	double StumpffC(double Z)
	{
		if (Z > 1.0)
		{
			const double SqrtZ = std::sqrt(Z);
			return (1.0 - std::cos(SqrtZ)) / Z;
		}
		if (Z < -1.0)
		{
			const double SqrtMinusZ = std::sqrt(-Z);
			return (std::cosh(SqrtMinusZ) - 1.0) / (-Z);
		}
		// C(z) = sum (-z)^k / (2k+2)!; the closed forms cancel catastrophically near 0.
		double Term = 0.5;
		double Sum = Term;
		for (int K = 1; K < 30; ++K)
		{
			Term *= -Z / ((2.0 * K + 1.0) * (2.0 * K + 2.0));
			Sum += Term;
			if (std::fabs(Term) < kEpsilon * std::fabs(Sum))
			{
				break;
			}
		}
		return Sum;
	}

	double StumpffS(double Z)
	{
		if (Z > 1.0)
		{
			const double SqrtZ = std::sqrt(Z);
			return (SqrtZ - std::sin(SqrtZ)) / (SqrtZ * SqrtZ * SqrtZ);
		}
		if (Z < -1.0)
		{
			const double SqrtMinusZ = std::sqrt(-Z);
			return (std::sinh(SqrtMinusZ) - SqrtMinusZ) / (SqrtMinusZ * SqrtMinusZ * SqrtMinusZ);
		}
		// S(z) = sum (-z)^k / (2k+3)!
		double Term = 1.0 / 6.0;
		double Sum = Term;
		for (int K = 1; K < 30; ++K)
		{
			Term *= -Z / ((2.0 * K + 2.0) * (2.0 * K + 3.0));
			Sum += Term;
			if (std::fabs(Term) < kEpsilon * std::fabs(Sum))
			{
				break;
			}
		}
		return Sum;
	}

	double SolveKeplerElliptic(double MeanAnomaly, double Eccentricity)
	{
		// Solve on the wrapped anomaly, then restore the revolution count, so the
		// bracket [-pi, pi] holds: f(E) = E - e sin E - M is monotonic there.
		const double Wrapped = WrapAngle(MeanAnomaly);
		const double Revolutions = MeanAnomaly - Wrapped;
		const double Guess = Wrapped + Eccentricity * std::sin(Wrapped);
		const double E = SolveMonotonic(
			[&](double X, double& Derivative)
			{
				Derivative = 1.0 - Eccentricity * std::cos(X);
				return X - Eccentricity * std::sin(X) - Wrapped;
			},
			-kPi - 1e-12, kPi + 1e-12, Guess);
		return E + Revolutions;
	}

	double SolveKeplerHyperbolic(double MeanAnomaly, double Eccentricity)
	{
		if (MeanAnomaly == 0.0)
		{
			return 0.0;
		}
		// Odd function: solve for |M| and restore the sign.
		const double AbsM = std::fabs(MeanAnomaly);
		// e sinh H - H = M implies asinh(M/e) <= H <= asinh(M/(e-1)).
		const double Lo = std::asinh(AbsM / Eccentricity);
		const double Hi = std::asinh(AbsM / (Eccentricity - 1.0));
		const double H = SolveMonotonic(
			[&](double X, double& Derivative)
			{
				Derivative = Eccentricity * std::cosh(X) - 1.0;
				return Eccentricity * std::sinh(X) - X - AbsM;
			},
			Lo * (1.0 - 1e-12), Hi * (1.0 + 1e-12) + 1e-300, Lo);
		return MeanAnomaly < 0.0 ? -H : H;
	}

	double TrueToEccentricAnomaly(double TrueAnomaly, double Eccentricity)
	{
		const double Beta = std::sqrt(1.0 - Eccentricity * Eccentricity);
		return std::atan2(Beta * std::sin(TrueAnomaly), Eccentricity + std::cos(TrueAnomaly));
	}

	double EccentricToTrueAnomaly(double EccentricAnomaly, double Eccentricity)
	{
		const double Beta = std::sqrt(1.0 - Eccentricity * Eccentricity);
		return std::atan2(Beta * std::sin(EccentricAnomaly), std::cos(EccentricAnomaly) - Eccentricity);
	}

	double TrueToHyperbolicAnomaly(double TrueAnomaly, double Eccentricity)
	{
		const double SinhH = std::sqrt(Eccentricity * Eccentricity - 1.0) * std::sin(TrueAnomaly)
			/ (1.0 + Eccentricity * std::cos(TrueAnomaly));
		return std::asinh(SinhH);
	}

	double HyperbolicToTrueAnomaly(double HyperbolicAnomaly, double Eccentricity)
	{
		const double Beta = std::sqrt(Eccentricity * Eccentricity - 1.0);
		return std::atan2(Beta * std::sinh(HyperbolicAnomaly), Eccentricity - std::cosh(HyperbolicAnomaly));
	}

	double EccentricToMeanAnomaly(double EccentricAnomaly, double Eccentricity)
	{
		return EccentricAnomaly - Eccentricity * std::sin(EccentricAnomaly);
	}

	double HyperbolicToMeanAnomaly(double HyperbolicAnomaly, double Eccentricity)
	{
		return Eccentricity * std::sinh(HyperbolicAnomaly) - HyperbolicAnomaly;
	}

	double TrueToMeanAnomaly(double TrueAnomaly, double Eccentricity)
	{
		if (Eccentricity < 1.0)
		{
			return EccentricToMeanAnomaly(TrueToEccentricAnomaly(TrueAnomaly, Eccentricity), Eccentricity);
		}
		if (Eccentricity > 1.0)
		{
			return HyperbolicToMeanAnomaly(TrueToHyperbolicAnomaly(TrueAnomaly, Eccentricity), Eccentricity);
		}
		// Barker: M = D + D^3 / 3 with D = tan(nu / 2).
		const double D = std::tan(0.5 * TrueAnomaly);
		return D + D * D * D / 3.0;
	}

	double MeanToTrueAnomaly(double MeanAnomaly, double Eccentricity)
	{
		if (Eccentricity < 1.0)
		{
			return EccentricToTrueAnomaly(SolveKeplerElliptic(MeanAnomaly, Eccentricity), Eccentricity);
		}
		if (Eccentricity > 1.0)
		{
			return HyperbolicToTrueAnomaly(SolveKeplerHyperbolic(MeanAnomaly, Eccentricity), Eccentricity);
		}
		// Cubic D^3/3 + D - M = 0 has a single real root (Cardano). Odd in M: solving
		// for |M| avoids the cancellation 1.5 M + sqrt(...) has for large negative M.
		const double AbsM = std::fabs(MeanAnomaly);
		const double W = std::cbrt(1.5 * AbsM + std::sqrt(1.0 + 2.25 * AbsM * AbsM));
		double D = W - 1.0 / W;
		// W - 1/W cancels for small M; two Newton steps restore full precision.
		for (int Iteration = 0; Iteration < 2; ++Iteration)
		{
			D -= (D * D * D / 3.0 + D - AbsM) / (D * D + 1.0);
		}
		return std::copysign(2.0 * std::atan(D), MeanAnomaly);
	}

	StateVector PropagateKepler(const StateVector& Initial, double Mu, double DeltaTime)
	{
		if (DeltaTime == 0.0)
		{
			return Initial;
		}

		const double SqrtMu = std::sqrt(Mu);
		const double R0 = Norm(Initial.Position);
		const double RadialTerm = Dot(Initial.Position, Initial.Velocity) / SqrtMu; // r0 * vr0 / sqrt(mu)
		const double Alpha = 2.0 / R0 - NormSquared(Initial.Velocity) / Mu;          // 1 / a

		// Bound orbits repeat: drop whole periods so chi stays small and precise.
		double Dt = DeltaTime;
		if (Alpha > 0.0)
		{
			const double Period = kTwoPi / (SqrtMu * Alpha * std::sqrt(Alpha));
			Dt = std::fmod(DeltaTime, Period);
			if (Dt == 0.0)
			{
				return Initial;
			}
		}

		const double OneMinusAlphaR0 = 1.0 - Alpha * R0;
		const double Target = SqrtMu * Dt;

		// F(chi) = sqrt(mu) * t(chi) - sqrt(mu) * dt. F'(chi) = r(chi) > 0, so F is
		// monotonic and a bracket plus safeguarded Newton always converges.
		auto Eval = [&](double Chi, double& Derivative)
		{
			const double Chi2 = Chi * Chi;
			const double Z = Alpha * Chi2;
			const double C = StumpffC(Z);
			const double S = StumpffS(Z);
			Derivative = RadialTerm * Chi * (1.0 - Z * S) + OneMinusAlphaR0 * Chi2 * C + R0;
			return RadialTerm * Chi2 * C + OneMinusAlphaR0 * Chi2 * Chi * S + R0 * Chi - Target;
		};

		// Initial guess: mean motion for ellipses, local speed otherwise.
		double Guess = Alpha > 0.0 ? SqrtMu * Dt * Alpha : SqrtMu * Dt / R0;

		// Bracket the root by expanding away from zero.
		double Lo = 0.0;
		double Hi = 0.0;
		double Dummy = 0.0;
		double Step = std::fabs(Guess) > 0.0 ? std::fabs(Guess) : 1.0;
		if (Dt > 0.0)
		{
			Hi = Step;
			for (int Expansions = 0; Expansions < 1100 && Eval(Hi, Dummy) < 0.0; ++Expansions)
			{
				Lo = Hi;
				Hi *= 2.0;
			}
		}
		else
		{
			Lo = -Step;
			// Capped: far down a hyperbola the evaluation overflows to NaN.
			for (int Expansions = 0; Expansions < 1100 && !(Eval(Lo, Dummy) < 0.0); ++Expansions)
			{
				Hi = Lo;
				Lo *= 2.0;
			}
		}

		const double Chi = SolveMonotonic(Eval, Lo, Hi, Guess);

		const double Chi2 = Chi * Chi;
		const double Z = Alpha * Chi2;
		const double C = StumpffC(Z);
		const double S = StumpffS(Z);

		const double F = 1.0 - Chi2 / R0 * C;
		const double G = Dt - Chi2 * Chi * S / SqrtMu;
		const Vec3 Position = Initial.Position * F + Initial.Velocity * G;
		const double R = Norm(Position);
		const double FDot = SqrtMu / (R * R0) * Chi * (Z * S - 1.0);
		const double GDot = 1.0 - Chi2 / R * C;
		const Vec3 Velocity = Initial.Position * FDot + Initial.Velocity * GDot;

		return {Position, Velocity};
	}
}
