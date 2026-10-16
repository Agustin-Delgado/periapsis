#pragma once

#include "CoreMinimal.h"
#include "PeriapsisCore/Vec3.h"

// The single boundary between simulation units (SI, double, metres) and Unreal units
// (centimetres). Nothing else converts: if a value crosses into the engine, it goes
// through here.
//
// The simulation is right-handed (Z = X x Y) and Unreal is left-handed, so Y is mirrored
// on the way in and out. Without the mirror, an orbit that turns counter-clockwise seen
// from +Z (the usual prograde sense, angular momentum along +Z) would turn clockwise on
// screen. Vectors that are positions, velocities, accelerations, forces or directions
// convert with these functions. Axial vectors (angular velocity, torque, normals built
// with a cross product on the Unreal side) pick up an extra sign when mirrored: compute
// them on one side only, or convert their polar factors first.
struct FUnitBridge
{
	static constexpr double CentimetresPerMetre = 100.0;

	static constexpr double ToUnreal(double Metres) { return Metres * CentimetresPerMetre; }
	static constexpr double ToSI(double Centimetres) { return Centimetres / CentimetresPerMetre; }

	static FVector ToUnreal(const Periapsis::Core::Vec3& Metres)
	{
		return FVector(Metres.X, -Metres.Y, Metres.Z) * CentimetresPerMetre;
	}

	static Periapsis::Core::Vec3 ToSI(const FVector& Centimetres)
	{
		const FVector Metres = Centimetres / CentimetresPerMetre;
		return {Metres.X, -Metres.Y, Metres.Z};
	}

	// A unit direction: mirrored like any vector, with no change of scale.
	static FVector DirectionToUnreal(const Periapsis::Core::Vec3& Direction)
	{
		return FVector(Direction.X, -Direction.Y, Direction.Z).GetSafeNormal();
	}
};
