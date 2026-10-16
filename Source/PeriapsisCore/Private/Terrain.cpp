#include "PeriapsisCore/Terrain.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Periapsis::Core
{
	namespace
	{
		// SplitMix64: a tiny generator whose output is fixed by its definition.
		uint64_t NextRandom(uint64_t& State)
		{
			uint64_t Z = (State += 0x9E3779B97F4A7C15ull);
			Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
			Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
			return Z ^ (Z >> 31);
		}

		double Fade(double T) { return T * T * T * (T * (T * 6.0 - 15.0) + 10.0); }
		double Lerp(double T, double A, double B) { return A + T * (B - A); }

		double Gradient(int Hash, double X, double Y, double Z)
		{
			const int H = Hash & 15;
			const double U = H < 8 ? X : Y;
			const double V = H < 4 ? Y : (H == 12 || H == 14 ? X : Z);
			return ((H & 1) ? -U : U) + ((H & 2) ? -V : V);
		}

		// Shifts each octave so they do not all vanish together on the integer lattice.
		Vec3 OctaveOffset(int Octave)
		{
			return {Octave * 17.137 + 0.314, Octave * 29.611 + 0.731, Octave * 3.733 + 0.113};
		}

		double OctaveSum(double Persistence, int Octaves)
		{
			double Sum = 0.0;
			double Weight = 1.0;
			for (int Octave = 0; Octave < Octaves; ++Octave)
			{
				Sum += Weight;
				Weight *= Persistence;
			}
			return Sum;
		}
	}

	GradientNoise::GradientNoise(uint64_t Seed)
	{
		for (int Index = 0; Index < 256; ++Index)
		{
			Permutation[static_cast<size_t>(Index)] = static_cast<uint8_t>(Index);
		}
		uint64_t State = Seed;
		for (int Index = 255; Index > 0; --Index)
		{
			const int Other = static_cast<int>(NextRandom(State) % static_cast<uint64_t>(Index + 1));
			std::swap(Permutation[static_cast<size_t>(Index)], Permutation[static_cast<size_t>(Other)]);
		}
		for (size_t Index = 0; Index < 256; ++Index)
		{
			Permutation[Index + 256] = Permutation[Index];
		}
	}

	double GradientNoise::Sample(const Vec3& P) const
	{
		const double FloorX = std::floor(P.X);
		const double FloorY = std::floor(P.Y);
		const double FloorZ = std::floor(P.Z);
		const int X = static_cast<int>(static_cast<int64_t>(FloorX) & 255);
		const int Y = static_cast<int>(static_cast<int64_t>(FloorY) & 255);
		const int Z = static_cast<int>(static_cast<int64_t>(FloorZ) & 255);
		const double Fx = P.X - FloorX;
		const double Fy = P.Y - FloorY;
		const double Fz = P.Z - FloorZ;
		const double U = Fade(Fx);
		const double V = Fade(Fy);
		const double W = Fade(Fz);

		auto Perm = [this](int I) { return static_cast<int>(Permutation[static_cast<size_t>(I)]); };
		const int A = Perm(X) + Y;
		const int AA = Perm(A) + Z;
		const int AB = Perm(A + 1) + Z;
		const int B = Perm(X + 1) + Y;
		const int BA = Perm(B) + Z;
		const int BB = Perm(B + 1) + Z;

		const double Value = Lerp(W,
			Lerp(V, Lerp(U, Gradient(Perm(AA), Fx, Fy, Fz), Gradient(Perm(BA), Fx - 1, Fy, Fz)),
				Lerp(U, Gradient(Perm(AB), Fx, Fy - 1, Fz), Gradient(Perm(BB), Fx - 1, Fy - 1, Fz))),
			Lerp(V, Lerp(U, Gradient(Perm(AA + 1), Fx, Fy, Fz - 1), Gradient(Perm(BA + 1), Fx - 1, Fy, Fz - 1)),
				Lerp(U, Gradient(Perm(AB + 1), Fx, Fy - 1, Fz - 1), Gradient(Perm(BB + 1), Fx - 1, Fy - 1, Fz - 1))));
		return std::clamp(Value, -1.0, 1.0);
	}

	TerrainShape::TerrainShape(uint64_t Seed, std::vector<TerrainLayer> InLayers, double InRadius)
		: Layers(std::move(InLayers))
		, Radius(InRadius)
	{
		uint64_t State = Seed;
		for (const TerrainLayer& Layer : Layers)
		{
			Noises.emplace_back(NextRandom(State));
			const double Sum = OctaveSum(Layer.Persistence, Layer.Octaves);
			if (Layer.Kind == TerrainLayer::EKind::Fbm)
			{
				Lowest -= Layer.Amplitude;
			}
			Highest += Layer.Amplitude;

			double Weight = 1.0;
			double Frequency = 1.0 / Layer.Wavelength;
			const double Shape = Layer.Kind == TerrainLayer::EKind::Ridged ? 2.0 : 1.0;
			for (int Octave = 0; Octave < Layer.Octaves; ++Octave)
			{
				Slope += Layer.Amplitude / Sum * Weight * Frequency * GradientNoise::kMaxGradient * Shape;
				Weight *= Layer.Persistence;
				Frequency *= Layer.Lacunarity;
			}
		}
	}

	double TerrainShape::Height(const Vec3& BodyFixedDirection) const
	{
		if (Layers.empty())
		{
			return 0.0;
		}
		const Vec3 OnSurface = Normalized(BodyFixedDirection) * Radius;
		double Total = 0.0;
		for (size_t Index = 0; Index < Layers.size(); ++Index)
		{
			const TerrainLayer& Layer = Layers[Index];
			const GradientNoise& Noise = Noises[Index];
			double Sum = 0.0;
			double Weight = 1.0;
			double Frequency = 1.0 / Layer.Wavelength;
			for (int Octave = 0; Octave < Layer.Octaves; ++Octave)
			{
				const double N = Noise.Sample(OnSurface * Frequency + OctaveOffset(Octave));
				if (Layer.Kind == TerrainLayer::EKind::Ridged)
				{
					const double Ridge = 1.0 - std::fabs(N);
					Sum += Weight * Ridge * Ridge;
				}
				else
				{
					Sum += Weight * N;
				}
				Weight *= Layer.Persistence;
				Frequency *= Layer.Lacunarity;
			}
			Total += Layer.Amplitude * Sum / OctaveSum(Layer.Persistence, Layer.Octaves);
		}
		return Total;
	}
}
