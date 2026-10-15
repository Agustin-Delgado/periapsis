// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

namespace Periapsis::Core
{
	// Not named PI: Unreal defines PI as a macro.
	inline constexpr double kPi = 3.14159265358979323846;
	inline constexpr double kTwoPi = 2.0 * kPi;
	inline constexpr double kHalfPi = 0.5 * kPi;

	// Standard gravity, used by the rocket equation and Isp. Not the local gravity of any body.
	inline constexpr double kStandardGravity = 9.80665; // m/s^2

	// Sea-level pressure that Isp curves are referenced to.
	inline constexpr double kStandardAtmospherePa = 101325.0;

	inline constexpr double DegToRad(double Degrees) { return Degrees * (kPi / 180.0); }
	inline constexpr double RadToDeg(double Radians) { return Radians * (180.0 / kPi); }
}
