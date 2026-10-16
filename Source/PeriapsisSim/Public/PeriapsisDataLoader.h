#pragma once

#include "CoreMinimal.h"
#include "PeriapsisCore/CelestialSystem.h"
#include "PeriapsisCore/Flight.h"

// What the view needs to draw a body; not simulation data.
struct FPeriapsisBodyInfo
{
	FString Id;
	FString DisplayName;
	FLinearColor Color = FLinearColor::White;
	// Below this altitude the physics bubble turns with the body (ADR 0004).
	double RotatingFrameAltitude = 0.0; // m
};

struct FPeriapsisSystemData
{
	Periapsis::Core::CelestialSystem System;
	TArray<FPeriapsisBodyInfo> Bodies; // same indices as System
};

// A vessel with no engine sharing the scenario with the ship (debris, a station).
struct FPeriapsisPassiveVessel
{
	FString Name;
	double Mass = 0.0; // kg
	double Size = 3.0; // m, edge of its (cubic) hull
	int32 StartBody = Periapsis::Core::kNoBody;
	Periapsis::Core::StateVector StartState; // relative to StartBody
};

struct FPeriapsisScenario
{
	FPeriapsisSystemData SystemData;
	FString ShipName;
	Periapsis::Core::ShipConfig Ship;
	double ShipLength = 6.0;   // m, of its (cylindrical) hull, along the engine's thrust
	double ShipDiameter = 2.0; // m
	int32 StartBody = Periapsis::Core::kNoBody;
	Periapsis::Core::StateVector StartState; // relative to StartBody
	double StartTime = 0.0;
	Periapsis::Core::AttitudeMode StartAttitude = Periapsis::Core::AttitudeMode::Prograde;
	TArray<FPeriapsisPassiveVessel> OtherVessels;
};

// Reads and validates the JSON files under Data/. Every function returns false and fills
// OutErrors (one readable line per problem) when the data is not usable; nothing is
// half-loaded.
namespace PeriapsisData
{
	// Root of the data files: <Project>/Data.
	PERIAPSISSIM_API FString DataDirectory();

	// Terrain profiles are read from DataRoot (DataDirectory() when empty).
	PERIAPSISSIM_API bool ParseSystem(const FString& Json, FPeriapsisSystemData& Out, TArray<FString>& OutErrors,
		const FString& DataRoot = FString());
	PERIAPSISSIM_API bool ParseScenario(const FString& Json, const FString& DataRoot, FPeriapsisScenario& Out, TArray<FString>& OutErrors);

	// Path relative to DataDirectory(), e.g. "scenarios/orbital_prototype.json".
	PERIAPSISSIM_API bool LoadScenario(const FString& RelativePath, FPeriapsisScenario& Out, TArray<FString>& OutErrors);
}
