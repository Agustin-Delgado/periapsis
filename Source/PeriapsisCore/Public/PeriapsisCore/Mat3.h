// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Vec3.h"

#include <cmath>

namespace Periapsis::Core
{
	// A 3x3 matrix stored by columns. As a rotation, its columns are where the X, Y and Z axes
	// of the rotated frame end up. Also used for inertia tensors (symmetric).
	struct Mat3
	{
		Vec3 X{1.0, 0.0, 0.0};
		Vec3 Y{0.0, 1.0, 0.0};
		Vec3 Z{0.0, 0.0, 1.0};

		constexpr Mat3() = default;
		constexpr Mat3(const Vec3& InX, const Vec3& InY, const Vec3& InZ) : X(InX), Y(InY), Z(InZ) {}

		static constexpr Mat3 Identity() { return {}; }
		static constexpr Mat3 Zero() { return {Vec3{}, Vec3{}, Vec3{}}; }

		constexpr Vec3 operator*(const Vec3& V) const { return X * V.X + Y * V.Y + Z * V.Z; }
		constexpr Mat3 operator*(const Mat3& O) const { return {*this * O.X, *this * O.Y, *this * O.Z}; }
		constexpr Mat3 operator+(const Mat3& O) const { return {X + O.X, Y + O.Y, Z + O.Z}; }
		constexpr Mat3 operator*(double S) const { return {X * S, Y * S, Z * S}; }

		constexpr Mat3 Transposed() const { return {{X.X, Y.X, Z.X}, {X.Y, Y.Y, Z.Y}, {X.Z, Y.Z, Z.Z}}; }
		constexpr double Determinant() const { return Dot(X, Cross(Y, Z)); }

		// Rotation by Angle (radians, counter-clockwise) around a unit Axis (Rodrigues).
		static Mat3 Rotation(const Vec3& Axis, double Angle)
		{
			const double C = std::cos(Angle);
			const double S = std::sin(Angle);
			auto Rotate = [&](const Vec3& V) { return V * C + Cross(Axis, V) * S + Axis * (Dot(Axis, V) * (1.0 - C)); };
			return {Rotate({1.0, 0.0, 0.0}), Rotate({0.0, 1.0, 0.0}), Rotate({0.0, 0.0, 1.0})};
		}

		// The rotation whose columns are an orthonormal right-handed basis built from two
		// directions: Z exactly along Forward, X as close as possible to Hint.
		static Mat3 FromZX(const Vec3& Forward, const Vec3& Hint)
		{
			const Vec3 Z3 = Normalized(Forward);
			const Vec3 Y3 = Normalized(Cross(Z3, Hint));
			return {Cross(Y3, Z3), Y3, Z3};
		}
	};

	// Outer product A B^T, for the parallel-axis theorem.
	constexpr Mat3 Outer(const Vec3& A, const Vec3& B) { return {A * B.X, A * B.Y, A * B.Z}; }

	// The full attitude of a vessel pointing along Forward. Only the forward direction is
	// commanded, so the roll is fixed by convention: X is as close as possible to the frame's
	// +Z (its pole), or to +X when Forward is nearly along the pole. Everything that places
	// parts in space (separations, drawing, collision) uses this one convention.
	inline Mat3 AttitudeFromForward(const Vec3& Forward)
	{
		const Vec3 Z3 = Normalized(Forward);
		const Vec3 Pole{0.0, 0.0, 1.0};
		const Vec3 Hint = NormSquared(Cross(Z3, Pole)) > 1e-6 ? Pole : Vec3{1.0, 0.0, 0.0};
		return Mat3::FromZX(Z3, Hint);
	}
}
