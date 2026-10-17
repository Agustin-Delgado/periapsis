#pragma once

#include "CoreMinimal.h"
#include "PeriapsisCore/CelestialSystem.h"
#include "PeriapsisCore/Flight.h"
#include "PeriapsisCore/Vessel.h"

#include <memory>

// What the view needs to draw a body; not simulation data.
struct FPeriapsisBodyInfo
{
	FString Id;
	FString DisplayName;
	FLinearColor Color = FLinearColor::White;
	// Below this altitude the physics bubble turns with the body (ADR 0004).
	double RotatingFrameAltitude = 0.0; // m
	// The ground's colour against height (stops in increasing height, blended between them),
	// and the colour of slopes steeper than SteepSlopeDeg. Empty: the display colour.
	TArray<TPair<double, FLinearColor>> GroundColors;
	FLinearColor SteepColor = FLinearColor::White;
	double SteepSlopeDeg = 90.0;

	// The ground's colour at a height (m above the radius) and slope (deg from level).
	PERIAPSISSIM_API FLinearColor GroundColor(double Height, double SlopeDeg) const;
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

// Where a scenario that starts on the ground puts the ship.
struct FPeriapsisSurfaceStart
{
	double Latitude = 0.0;      // deg
	double Longitude = 0.0;     // deg
	double Height = 0.0;        // m of the ship's base over the ground
	double VerticalSpeed = 0.0; // m/s, up
};

struct FPeriapsisScenario
{
	FPeriapsisSystemData SystemData;
	// Every part in Data/parts (the builder needs it even when the ship is not made of them).
	std::shared_ptr<const Periapsis::Core::PartCatalog> Catalog;
	FString ShipName;
	// The ship: a design made of parts, or the one-part hull of M2 to M4.
	Periapsis::Core::Vessel Ship;
	Periapsis::Core::VesselDesign ShipDesign; // empty for a one-part hull
	int32 StartBody = Periapsis::Core::kNoBody;
	Periapsis::Core::StateVector StartState; // relative to StartBody
	double StartTime = 0.0;
	Periapsis::Core::AttitudeMode StartAttitude = Periapsis::Core::AttitudeMode::Prograde;
	TOptional<FPeriapsisSurfaceStart> SurfaceStart; // set when it starts on the ground
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

	// Parts (M5): DataRoot/parts/resources.json and one file per part next to it.
	PERIAPSISSIM_API bool LoadPartCatalog(const FString& DataRoot, Periapsis::Core::PartCatalog& Out, TArray<FString>& OutErrors);
	PERIAPSISSIM_API bool ParsePart(const FString& Json, const Periapsis::Core::PartCatalog& Resources, Periapsis::Core::PartDefinition& Out,
		TArray<FString>& OutErrors);
	// A vessel design: parts by catalog id, attached by node or by side, with symmetry and stages.
	PERIAPSISSIM_API bool ParseDesign(const FString& Json, const Periapsis::Core::PartCatalog& Catalog, Periapsis::Core::VesselDesign& Out,
		TArray<FString>& OutErrors);
	// The same format back (every part listed, symmetry groups kept), for designs made in the builder.
	PERIAPSISSIM_API FString SerializeDesign(const Periapsis::Core::VesselDesign& Design, const Periapsis::Core::PartCatalog& Catalog);
	// A design file, relative to DataRoot or absolute.
	PERIAPSISSIM_API bool LoadDesign(const FString& Path, const FString& DataRoot, const Periapsis::Core::PartCatalog& Catalog,
		Periapsis::Core::VesselDesign& Out, TArray<FString>& OutErrors);

	// Puts the ship on its surface start spot (for a scenario that has one), standing upright.
	PERIAPSISSIM_API void PlaceOnSurface(FPeriapsisScenario& Scenario);
	// Replaces the ship with a design built from the scenario's catalog (a launch from the builder).
	PERIAPSISSIM_API void SetShipDesign(FPeriapsisScenario& Scenario, const Periapsis::Core::VesselDesign& Design);

	// Where the builder saves designs: <Project>/Saved/Designs (not part of the repository).
	PERIAPSISSIM_API FString SavedDesignsDirectory();
}
