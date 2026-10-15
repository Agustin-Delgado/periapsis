// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include <cmath>

namespace Periapsis::Core
{
	// Double precision vector in SI units. Deliberately not FVector: the Core never
	// touches engine types, the conversion lives at the engine boundary.
	struct Vec3
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;

		constexpr Vec3() = default;
		constexpr Vec3(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}

		constexpr Vec3 operator+(const Vec3& O) const { return {X + O.X, Y + O.Y, Z + O.Z}; }
		constexpr Vec3 operator-(const Vec3& O) const { return {X - O.X, Y - O.Y, Z - O.Z}; }
		constexpr Vec3 operator-() const { return {-X, -Y, -Z}; }
		constexpr Vec3 operator*(double S) const { return {X * S, Y * S, Z * S}; }
		constexpr Vec3 operator/(double S) const { return {X / S, Y / S, Z / S}; }
		constexpr Vec3& operator+=(const Vec3& O) { X += O.X; Y += O.Y; Z += O.Z; return *this; }
		constexpr Vec3& operator-=(const Vec3& O) { X -= O.X; Y -= O.Y; Z -= O.Z; return *this; }
		constexpr Vec3& operator*=(double S) { X *= S; Y *= S; Z *= S; return *this; }
	};

	constexpr Vec3 operator*(double S, const Vec3& V) { return V * S; }

	constexpr double Dot(const Vec3& A, const Vec3& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }

	constexpr Vec3 Cross(const Vec3& A, const Vec3& B)
	{
		return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
	}

	inline double Norm(const Vec3& V) { return std::sqrt(Dot(V, V)); }
	constexpr double NormSquared(const Vec3& V) { return Dot(V, V); }

	// Returns the zero vector for a zero input instead of NaNs.
	inline Vec3 Normalized(const Vec3& V)
	{
		const double Length = Norm(V);
		return Length > 0.0 ? V / Length : Vec3{};
	}

	// Signed angle from A to B measured counter-clockwise around Axis. A and B are
	// expected to be perpendicular to Axis (or close to it).
	inline double SignedAngle(const Vec3& A, const Vec3& B, const Vec3& Axis)
	{
		return std::atan2(Dot(Cross(A, B), Normalized(Axis)), Dot(A, B));
	}
}
