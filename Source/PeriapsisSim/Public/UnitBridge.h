#pragma once

#include "CoreMinimal.h"
#include "PeriapsisCore/Vec3.h"

// The single boundary between simulation units (SI, double, metres) and Unreal units
// (centimetres). Nothing else converts: if a value crosses into the engine, it goes
// through here.
struct FUnitBridge
{
	static constexpr double CentimetresPerMetre = 100.0;

	static constexpr double ToUnreal(double Metres) { return Metres * CentimetresPerMetre; }
	static constexpr double ToSI(double Centimetres) { return Centimetres / CentimetresPerMetre; }

	static FVector ToUnreal(const Periapsis::Core::Vec3& Metres)
	{
		return FVector(Metres.X, Metres.Y, Metres.Z) * CentimetresPerMetre;
	}

	static Periapsis::Core::Vec3 ToSI(const FVector& Centimetres)
	{
		const FVector Metres = Centimetres / CentimetresPerMetre;
		return {Metres.X, Metres.Y, Metres.Z};
	}
};
