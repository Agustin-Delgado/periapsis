// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include "PeriapsisCore/Vec3.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Periapsis::Core
{
	// Improved gradient noise (Perlin 2002) over a permutation built from a seed. Same output
	// on every platform and compiler: the shuffle and the hash are written out here, not taken
	// from the standard library.
	class GradientNoise
	{
	public:
		PERIAPSISCORE_API explicit GradientNoise(uint64_t Seed = 0);

		// In [-1, 1] (clamped: the raw value can overshoot slightly).
		PERIAPSISCORE_API double Sample(const Vec3& P) const;

		// Upper bound of the gradient's length, per unit of input.
		static constexpr double kMaxGradient = 3.0;

	private:
		std::array<uint8_t, 512> Permutation{};
	};

	// One layer of the terrain, a sum of octaves of gradient noise.
	struct TerrainLayer
	{
		enum class EKind
		{
			Fbm,    // rolling ground, from -Amplitude to +Amplitude
			Ridged, // mountain ridges, from 0 to +Amplitude
		};
		EKind Kind = EKind::Fbm;
		double Amplitude = 0.0;  // m
		double Wavelength = 0.0; // m, of the first octave
		int Octaves = 1;
		double Persistence = 0.5; // amplitude ratio between octaves
		double Lacunarity = 2.0;  // frequency ratio between octaves
	};

	// The relief of a body: a height above its radius for every direction, in the body-fixed
	// frame (spec 6.3). A flat shape (no layers) is a perfect sphere.
	class TerrainShape
	{
	public:
		TerrainShape() = default;
		PERIAPSISCORE_API TerrainShape(uint64_t Seed, std::vector<TerrainLayer> InLayers, double InRadius);

		bool IsFlat() const { return Layers.empty(); }
		const std::vector<TerrainLayer>& GetLayers() const { return Layers; }

		// Height above the body's radius (m) towards a body-fixed direction (need not be unit).
		PERIAPSISCORE_API double Height(const Vec3& BodyFixedDirection) const;

		// Strict bounds of Height().
		double MinHeight() const { return Lowest; }
		double MaxHeight() const { return Highest; }
		// Bound of the slope: |Height(a) - Height(b)| <= MaxSlope() * distance on the surface.
		double MaxSlope() const { return Slope; }

	private:
		std::vector<TerrainLayer> Layers;
		std::vector<GradientNoise> Noises; // one per layer
		double Radius = 0.0;
		double Lowest = 0.0;
		double Highest = 0.0;
		double Slope = 0.0;
	};
}
