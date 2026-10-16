#include "PeriapsisDataLoader.h"

#include "Dom/JsonObject.h"
#include "Algo/Find.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PeriapsisCore/Constants.h"
#include "PeriapsisCore/OrbitalElements.h"
#include "PeriapsisCore/Surface.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	using namespace Periapsis::Core;

	TSharedPtr<FJsonObject> ParseObject(const FString& Json, const FString& What, TArray<FString>& OutErrors)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutErrors.Add(FString::Printf(TEXT("%s: invalid JSON (%s)"), *What, *Reader->GetErrorMessage()));
			return nullptr;
		}
		return Root;
	}

	// Reads a required number; adds an error naming the field when it is missing.
	bool ReadNumber(const FJsonObject& Object, const TCHAR* Field, const FString& Where, double& Out, TArray<FString>& OutErrors)
	{
		if (!Object.TryGetNumberField(Field, Out))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: missing number '%s'"), *Where, Field));
			return false;
		}
		return true;
	}

	void RequirePositive(double Value, const TCHAR* Field, const FString& Where, TArray<FString>& OutErrors)
	{
		if (!(Value > 0.0))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: '%s' must be positive (got %g)"), *Where, Field, Value));
		}
	}

	// "terrain": {"seed": N, "profile": "terrain/x.json"}; the profile lists the noise layers.
	void ParseTerrain(const FJsonObject& T, const FString& DataRoot, const FString& Where, CelestialBody& Body, TArray<FString>& OutErrors)
	{
		double Seed = 0.0;
		T.TryGetNumberField(TEXT("seed"), Seed);
		FString ProfilePath;
		if (!T.TryGetStringField(TEXT("profile"), ProfilePath))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: terrain needs a 'profile'"), *Where));
			return;
		}
		FString ProfileJson;
		if (!FFileHelper::LoadFileToString(ProfileJson, *FPaths::Combine(DataRoot, ProfilePath)))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: cannot read terrain profile '%s'"), *Where, *ProfilePath));
			return;
		}
		const FString ProfileWhere = FString::Printf(TEXT("terrain profile '%s'"), *ProfilePath);
		const TSharedPtr<FJsonObject> Profile = ParseObject(ProfileJson, ProfileWhere, OutErrors);
		const TArray<TSharedPtr<FJsonValue>>* LayerValues = nullptr;
		if (!Profile || !Profile->TryGetArrayField(TEXT("layers"), LayerValues))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: missing 'layers'"), *ProfileWhere));
			return;
		}

		std::vector<TerrainLayer> Layers;
		for (int32 Index = 0; Index < LayerValues->Num(); ++Index)
		{
			const FString LayerWhere = FString::Printf(TEXT("%s, layers[%d]"), *ProfileWhere, Index);
			const TSharedPtr<FJsonObject>* LayerObject = nullptr;
			if (!(*LayerValues)[Index]->TryGetObject(LayerObject))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: not an object"), *LayerWhere));
				continue;
			}
			const FJsonObject& L = **LayerObject;
			TerrainLayer Layer;
			FString Type;
			L.TryGetStringField(TEXT("type"), Type);
			if (Type == TEXT("fbm"))
			{
				Layer.Kind = TerrainLayer::EKind::Fbm;
			}
			else if (Type == TEXT("ridged"))
			{
				Layer.Kind = TerrainLayer::EKind::Ridged;
			}
			else
			{
				OutErrors.Add(FString::Printf(TEXT("%s: 'type' must be 'fbm' or 'ridged' (got '%s')"), *LayerWhere, *Type));
			}
			double Octaves = 1.0;
			ReadNumber(L, TEXT("amplitude_m"), LayerWhere, Layer.Amplitude, OutErrors);
			ReadNumber(L, TEXT("wavelength_m"), LayerWhere, Layer.Wavelength, OutErrors);
			ReadNumber(L, TEXT("octaves"), LayerWhere, Octaves, OutErrors);
			L.TryGetNumberField(TEXT("persistence"), Layer.Persistence);
			L.TryGetNumberField(TEXT("lacunarity"), Layer.Lacunarity);
			Layer.Octaves = static_cast<int>(Octaves);
			RequirePositive(Layer.Amplitude, TEXT("amplitude_m"), LayerWhere, OutErrors);
			RequirePositive(Layer.Wavelength, TEXT("wavelength_m"), LayerWhere, OutErrors);
			if (Layer.Octaves < 1 || Layer.Octaves > 24 || Octaves != Layer.Octaves)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: 'octaves' must be a whole number from 1 to 24"), *LayerWhere));
			}
			if (!(Layer.Persistence > 0.0 && Layer.Persistence <= 1.0))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: 'persistence' must be in (0, 1]"), *LayerWhere));
			}
			if (!(Layer.Lacunarity > 1.0))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: 'lacunarity' must be above 1"), *LayerWhere));
			}
			Layers.push_back(Layer);
		}
		Body.Terrain = TerrainShape(static_cast<uint64_t>(Seed), std::move(Layers), Body.Radius);
		if (Body.Terrain.MaxHeight() > 0.05 * Body.Radius || -Body.Terrain.MinHeight() > 0.05 * Body.Radius)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: terrain relief must stay within 5%% of the radius"), *Where));
		}
	}

	// Above the ground: body, latitude, longitude, height of the ship's base over the terrain and
	// vertical speed, moving with the ground otherwise. For landing tests and short hops.
	void ParseStartSurface(const FJsonObject& O, const FPeriapsisSystemData& SystemData, double StartTime, double Clearance, int32& OutBody,
		StateVector& OutState, TArray<FString>& OutErrors)
	{
		const FString Where = TEXT("start_surface");
		FString BodyId;
		double Latitude = 0.0, Longitude = 0.0, Height = 0.0, VerticalSpeed = 0.0;
		O.TryGetStringField(TEXT("body"), BodyId);
		ReadNumber(O, TEXT("latitude_deg"), Where, Latitude, OutErrors);
		ReadNumber(O, TEXT("longitude_deg"), Where, Longitude, OutErrors);
		ReadNumber(O, TEXT("height_m"), Where, Height, OutErrors);
		O.TryGetNumberField(TEXT("vertical_speed_m_s"), VerticalSpeed);
		OutBody = SystemData.Bodies.IndexOfByPredicate([&](const FPeriapsisBodyInfo& B) { return B.Id == BodyId; });
		if (OutBody == INDEX_NONE)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: unknown body '%s'"), *Where, *BodyId));
			OutBody = kNoBody;
			return;
		}
		if (FMath::Abs(Latitude) > 90.0 || Height < 0.0)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: latitude must be within +-90 and height not negative"), *Where));
			return;
		}
		const CelestialBody& Body = SystemData.System.Body(OutBody);
		const double Lat = DegToRad(Latitude);
		const double Lon = DegToRad(Longitude);
		const Vec3 Up{FMath::Cos(Lat) * FMath::Cos(Lon), FMath::Cos(Lat) * FMath::Sin(Lon), FMath::Sin(Lat)};
		const double Radius = Body.Radius + Body.Terrain.Height(Up) + Clearance + Height;
		const Vec3 Position = FromBodyFixed(Body, Up * Radius, StartTime);
		OutState = {Position, GroundVelocity(Body, Position) + Normalized(Position) * VerticalSpeed};
	}

	// A circular orbit: body, altitude, inclination and true anomaly at the scenario start.
	void ParseStartOrbit(const FJsonObject& O, const FString& Where, const FPeriapsisSystemData& SystemData, double StartTime,
		int32& OutBody, StateVector& OutState, TArray<FString>& OutErrors)
	{
		FString BodyId;
		double Altitude = 0.0, Inclination = 0.0, TrueAnomaly = 0.0;
		O.TryGetStringField(TEXT("body"), BodyId);
		ReadNumber(O, TEXT("altitude_m"), Where, Altitude, OutErrors);
		O.TryGetNumberField(TEXT("inclination_deg"), Inclination);
		O.TryGetNumberField(TEXT("true_anomaly_deg"), TrueAnomaly);

		OutBody = SystemData.Bodies.IndexOfByPredicate([&](const FPeriapsisBodyInfo& B) { return B.Id == BodyId; });
		if (OutBody == INDEX_NONE)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: unknown body '%s'"), *Where, *BodyId));
			OutBody = kNoBody;
			return;
		}
		const CelestialBody& Body = SystemData.System.Body(OutBody);
		const double Radius = Body.Radius + Altitude;
		RequirePositive(Altitude, TEXT("altitude_m"), Where, OutErrors);
		if (Altitude <= Body.Terrain.MaxHeight())
		{
			OutErrors.Add(FString::Printf(TEXT("%s: altitude is below the highest terrain (%.0f m)"), *Where, Body.Terrain.MaxHeight()));
		}
		if (Radius >= Body.SphereOfInfluence)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: altitude is outside the body's sphere of influence"), *Where));
		}
		OrbitalElements Circular;
		Circular.SemiLatusRectum = Radius;
		Circular.Inclination = DegToRad(Inclination);
		Circular.TrueAnomaly = DegToRad(TrueAnomaly);
		Circular.Epoch = StartTime;
		OutState = ElementsToState(Circular, Body.Mu);
	}
}

FString PeriapsisData::DataDirectory()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"));
}

bool PeriapsisData::ParseSystem(const FString& Json, FPeriapsisSystemData& Out, TArray<FString>& OutErrors, const FString& DataRoot)
{
	const FString Root = DataRoot.IsEmpty() ? DataDirectory() : DataRoot;
	const int32 ErrorsBefore = OutErrors.Num();
	const TSharedPtr<FJsonObject> System = ParseObject(Json, TEXT("system"), OutErrors);
	if (!System)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* BodyValues = nullptr;
	if (!System->TryGetArrayField(TEXT("bodies"), BodyValues) || BodyValues->IsEmpty())
	{
		OutErrors.Add(TEXT("system: 'bodies' must be a non-empty array"));
		return false;
	}

	FPeriapsisSystemData Result;
	TMap<FString, int32> IndexById;

	for (int32 Position = 0; Position < BodyValues->Num(); ++Position)
	{
		const TSharedPtr<FJsonObject>* BodyObject = nullptr;
		if (!(*BodyValues)[Position]->TryGetObject(BodyObject))
		{
			OutErrors.Add(FString::Printf(TEXT("bodies[%d]: not an object"), Position));
			continue;
		}
		const FJsonObject& B = **BodyObject;

		FPeriapsisBodyInfo Info;
		if (!B.TryGetStringField(TEXT("id"), Info.Id) || Info.Id.IsEmpty())
		{
			OutErrors.Add(FString::Printf(TEXT("bodies[%d]: missing 'id'"), Position));
			continue;
		}
		const FString Where = FString::Printf(TEXT("body '%s'"), *Info.Id);
		if (IndexById.Contains(Info.Id))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: duplicate id"), *Where));
			continue;
		}
		if (!B.TryGetStringField(TEXT("name"), Info.DisplayName))
		{
			Info.DisplayName = Info.Id;
		}
		const TArray<TSharedPtr<FJsonValue>>* Color = nullptr;
		if (B.TryGetArrayField(TEXT("display_color"), Color))
		{
			if (Color->Num() == 3)
			{
				Info.Color = FLinearColor((*Color)[0]->AsNumber(), (*Color)[1]->AsNumber(), (*Color)[2]->AsNumber());
			}
			else
			{
				OutErrors.Add(FString::Printf(TEXT("%s: 'display_color' needs 3 numbers"), *Where));
			}
		}

		CelestialBody Body;
		Body.Id = TCHAR_TO_UTF8(*Info.Id);
		double SurfaceGravity = 0.0;
		const bool bHasNumbers = ReadNumber(B, TEXT("radius_m"), Where, Body.Radius, OutErrors)
			& ReadNumber(B, TEXT("surface_gravity_m_s2"), Where, SurfaceGravity, OutErrors);
		if (!bHasNumbers)
		{
			continue;
		}
		RequirePositive(Body.Radius, TEXT("radius_m"), Where, OutErrors);
		RequirePositive(SurfaceGravity, TEXT("surface_gravity_m_s2"), Where, OutErrors);
		Body.Mu = SurfaceGravity * Body.Radius * Body.Radius;
		B.TryGetNumberField(TEXT("rotation_period_s"), Body.RotationPeriod);
		double RotationAtEpoch = 0.0;
		B.TryGetNumberField(TEXT("rotation_at_epoch_deg"), RotationAtEpoch);
		Body.RotationAtEpoch = DegToRad(RotationAtEpoch);

		const TSharedPtr<FJsonObject>* TerrainObject = nullptr;
		if (B.TryGetObjectField(TEXT("terrain"), TerrainObject))
		{
			ParseTerrain(**TerrainObject, Root, Where, Body, OutErrors);
		}
		Info.RotatingFrameAltitude = Body.Terrain.MaxHeight() + 15000.0;
		B.TryGetNumberField(TEXT("rotating_frame_altitude_m"), Info.RotatingFrameAltitude);
		if (Info.RotatingFrameAltitude <= Body.Terrain.MaxHeight())
		{
			OutErrors.Add(FString::Printf(TEXT("%s: 'rotating_frame_altitude_m' must be above the highest terrain (%.0f m)"), *Where,
				Body.Terrain.MaxHeight()));
		}

		FString ParentId;
		const bool bHasParent = B.TryGetStringField(TEXT("parent"), ParentId);
		if (!bHasParent && Position > 0)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: only the first body can be the root; set 'parent'"), *Where));
			continue;
		}
		if (bHasParent)
		{
			const int32* ParentIndex = IndexById.Find(ParentId);
			if (!ParentIndex)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: parent '%s' is not defined before it"), *Where, *ParentId));
				continue;
			}
			Body.ParentIndex = *ParentIndex;

			const TSharedPtr<FJsonObject>* OrbitObject = nullptr;
			if (!B.TryGetObjectField(TEXT("orbit"), OrbitObject))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: missing 'orbit'"), *Where));
				continue;
			}
			const FJsonObject& O = **OrbitObject;
			double A = 0.0, E = 0.0, I = 0.0, Lan = 0.0, ArgP = 0.0, M = 0.0, Epoch = 0.0;
			bool bOrbitOk = ReadNumber(O, TEXT("semi_major_axis_m"), Where, A, OutErrors);
			bOrbitOk &= ReadNumber(O, TEXT("eccentricity"), Where, E, OutErrors);
			O.TryGetNumberField(TEXT("inclination_deg"), I);
			O.TryGetNumberField(TEXT("longitude_of_ascending_node_deg"), Lan);
			O.TryGetNumberField(TEXT("argument_of_periapsis_deg"), ArgP);
			O.TryGetNumberField(TEXT("mean_anomaly_at_epoch_deg"), M);
			O.TryGetNumberField(TEXT("epoch_s"), Epoch);
			if (!bOrbitOk)
			{
				continue;
			}
			if (!(E >= 0.0 && E < 1.0))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: body orbits must be elliptic, 0 <= eccentricity < 1 (got %g)"), *Where, E));
				continue;
			}
			const CelestialBody& Parent = Result.System.Body(Body.ParentIndex);
			if (A * (1.0 - E) <= Parent.Radius + Body.Radius)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: periapsis %.0f m intersects '%s'"), *Where, A * (1.0 - E), *ParentId));
				continue;
			}
			Body.Orbit = ElementsFromClassical(A, E, DegToRad(I), DegToRad(Lan), DegToRad(ArgP), DegToRad(M), Epoch);
		}

		if (OutErrors.Num() > ErrorsBefore)
		{
			continue;
		}
		const int Index = Result.System.AddBody(Body);
		if (Index == kNoBody)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: rejected by the celestial system"), *Where));
			continue;
		}
		if (bHasParent && Result.System.Body(Index).SphereOfInfluence <= Body.Radius)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: sphere of influence (%.0f m) is smaller than the body"), *Where,
				Result.System.Body(Index).SphereOfInfluence));
		}
		IndexById.Add(Info.Id, Index);
		Result.Bodies.Add(Info);
	}

	if (OutErrors.Num() > ErrorsBefore)
	{
		return false;
	}
	Out = MoveTemp(Result);
	return true;
}

bool PeriapsisData::ParseScenario(const FString& Json, const FString& DataRoot, FPeriapsisScenario& Out, TArray<FString>& OutErrors)
{
	const int32 ErrorsBefore = OutErrors.Num();
	const TSharedPtr<FJsonObject> Root = ParseObject(Json, TEXT("scenario"), OutErrors);
	if (!Root)
	{
		return false;
	}

	FPeriapsisScenario Result;
	FString SystemPath;
	if (!Root->TryGetStringField(TEXT("system"), SystemPath))
	{
		OutErrors.Add(TEXT("scenario: missing 'system'"));
		return false;
	}
	FString SystemJson;
	if (!FFileHelper::LoadFileToString(SystemJson, *FPaths::Combine(DataRoot, SystemPath)))
	{
		OutErrors.Add(FString::Printf(TEXT("scenario: cannot read system file '%s'"), *SystemPath));
		return false;
	}
	if (!ParseSystem(SystemJson, Result.SystemData, OutErrors, DataRoot))
	{
		return false;
	}
	Root->TryGetNumberField(TEXT("start_time_s"), Result.StartTime);

	const TSharedPtr<FJsonObject>* ShipObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("ship"), ShipObject))
	{
		OutErrors.Add(TEXT("scenario: missing 'ship'"));
		return false;
	}
	const FJsonObject& S = **ShipObject;
	const FString ShipWhere = TEXT("ship");
	S.TryGetStringField(TEXT("name"), Result.ShipName);
	ReadNumber(S, TEXT("dry_mass_kg"), ShipWhere, Result.Ship.DryMass, OutErrors);
	ReadNumber(S, TEXT("fuel_mass_kg"), ShipWhere, Result.Ship.FuelMass, OutErrors);
	ReadNumber(S, TEXT("thrust_n"), ShipWhere, Result.Ship.Thrust, OutErrors);
	ReadNumber(S, TEXT("isp_s"), ShipWhere, Result.Ship.IspSeconds, OutErrors);
	RequirePositive(Result.Ship.DryMass, TEXT("dry_mass_kg"), ShipWhere, OutErrors);
	RequirePositive(Result.Ship.Thrust, TEXT("thrust_n"), ShipWhere, OutErrors);
	RequirePositive(Result.Ship.IspSeconds, TEXT("isp_s"), ShipWhere, OutErrors);
	if (Result.Ship.FuelMass < 0.0)
	{
		OutErrors.Add(TEXT("ship: 'fuel_mass_kg' cannot be negative"));
	}
	FString Attitude;
	if (S.TryGetStringField(TEXT("attitude"), Attitude))
	{
		static const TPair<const TCHAR*, AttitudeMode> Modes[] = {
			{TEXT("prograde"), AttitudeMode::Prograde},
			{TEXT("retrograde"), AttitudeMode::Retrograde},
			{TEXT("normal"), AttitudeMode::Normal},
			{TEXT("anti_normal"), AttitudeMode::AntiNormal},
			{TEXT("radial_out"), AttitudeMode::RadialOut},
			{TEXT("radial_in"), AttitudeMode::RadialIn},
			{TEXT("surface_prograde"), AttitudeMode::SurfacePrograde},
			{TEXT("surface_retrograde"), AttitudeMode::SurfaceRetrograde},
		};
		const TPair<const TCHAR*, AttitudeMode>* Found = Algo::FindByPredicate(Modes, [&](const auto& Mode) { return Attitude == Mode.Key; });
		if (Found)
		{
			Result.StartAttitude = Found->Value;
		}
		else
		{
			OutErrors.Add(FString::Printf(TEXT("ship: unknown 'attitude' '%s'"), *Attitude));
		}
	}
	S.TryGetNumberField(TEXT("length_m"), Result.ShipLength);
	S.TryGetNumberField(TEXT("diameter_m"), Result.ShipDiameter);
	RequirePositive(Result.ShipLength, TEXT("length_m"), ShipWhere, OutErrors);
	RequirePositive(Result.ShipDiameter, TEXT("diameter_m"), ShipWhere, OutErrors);

	const TSharedPtr<FJsonObject>* OrbitObject = nullptr;
	const TSharedPtr<FJsonObject>* SurfaceObject = nullptr;
	const bool bOrbit = Root->TryGetObjectField(TEXT("start_orbit"), OrbitObject);
	const bool bSurface = Root->TryGetObjectField(TEXT("start_surface"), SurfaceObject);
	if (bOrbit == bSurface)
	{
		OutErrors.Add(TEXT("scenario: needs exactly one of 'start_orbit' and 'start_surface'"));
		return false;
	}
	if (bOrbit)
	{
		ParseStartOrbit(**OrbitObject, TEXT("start_orbit"), Result.SystemData, Result.StartTime, Result.StartBody, Result.StartState, OutErrors);
	}
	else
	{
		ParseStartSurface(**SurfaceObject, Result.SystemData, Result.StartTime, 0.5 * Result.ShipLength, Result.StartBody, Result.StartState,
			OutErrors);
	}

	const TArray<TSharedPtr<FJsonValue>>* Others = nullptr;
	if (Root->TryGetArrayField(TEXT("other_vessels"), Others))
	{
		for (int32 Index = 0; Index < Others->Num(); ++Index)
		{
			const FString Where = FString::Printf(TEXT("other_vessels[%d]"), Index);
			const TSharedPtr<FJsonObject>* VesselObject = nullptr;
			if (!(*Others)[Index]->TryGetObject(VesselObject))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: must be an object"), *Where));
				continue;
			}
			const FJsonObject& V = **VesselObject;
			FPeriapsisPassiveVessel Vessel;
			if (!V.TryGetStringField(TEXT("name"), Vessel.Name) || Vessel.Name.IsEmpty())
			{
				OutErrors.Add(FString::Printf(TEXT("%s: missing 'name'"), *Where));
			}
			ReadNumber(V, TEXT("mass_kg"), Where, Vessel.Mass, OutErrors);
			RequirePositive(Vessel.Mass, TEXT("mass_kg"), Where, OutErrors);
			V.TryGetNumberField(TEXT("size_m"), Vessel.Size);
			RequirePositive(Vessel.Size, TEXT("size_m"), Where, OutErrors);
			const TSharedPtr<FJsonObject>* VesselOrbit = nullptr;
			if (!V.TryGetObjectField(TEXT("start_orbit"), VesselOrbit))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: missing 'start_orbit'"), *Where));
				continue;
			}
			ParseStartOrbit(**VesselOrbit, Where + TEXT(".start_orbit"), Result.SystemData, Result.StartTime, Vessel.StartBody,
				Vessel.StartState, OutErrors);
			Result.OtherVessels.Add(MoveTemp(Vessel));
		}
	}

	if (OutErrors.Num() > ErrorsBefore)
	{
		return false;
	}
	Out = MoveTemp(Result);
	return true;
}

bool PeriapsisData::LoadScenario(const FString& RelativePath, FPeriapsisScenario& Out, TArray<FString>& OutErrors)
{
	const FString Root = DataDirectory();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *FPaths::Combine(Root, RelativePath)))
	{
		OutErrors.Add(FString::Printf(TEXT("cannot read scenario '%s' under %s"), *RelativePath, *Root));
		return false;
	}
	return ParseScenario(Json, Root, Out, OutErrors);
}
