#include "PeriapsisDataLoader.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PeriapsisCore/Constants.h"
#include "PeriapsisCore/OrbitalElements.h"
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
}

FString PeriapsisData::DataDirectory()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"));
}

bool PeriapsisData::ParseSystem(const FString& Json, FPeriapsisSystemData& Out, TArray<FString>& OutErrors)
{
	const int32 ErrorsBefore = OutErrors.Num();
	const TSharedPtr<FJsonObject> Root = ParseObject(Json, TEXT("system"), OutErrors);
	if (!Root)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* BodyValues = nullptr;
	if (!Root->TryGetArrayField(TEXT("bodies"), BodyValues) || BodyValues->IsEmpty())
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
	if (!ParseSystem(SystemJson, Result.SystemData, OutErrors))
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

	const TSharedPtr<FJsonObject>* OrbitObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("start_orbit"), OrbitObject))
	{
		OutErrors.Add(TEXT("scenario: missing 'start_orbit'"));
		return false;
	}
	const FJsonObject& O = **OrbitObject;
	FString BodyId;
	double Altitude = 0.0, Inclination = 0.0, TrueAnomaly = 0.0;
	O.TryGetStringField(TEXT("body"), BodyId);
	ReadNumber(O, TEXT("altitude_m"), TEXT("start_orbit"), Altitude, OutErrors);
	O.TryGetNumberField(TEXT("inclination_deg"), Inclination);
	O.TryGetNumberField(TEXT("true_anomaly_deg"), TrueAnomaly);

	Result.StartBody = Result.SystemData.Bodies.IndexOfByPredicate([&](const FPeriapsisBodyInfo& B) { return B.Id == BodyId; });
	if (Result.StartBody == INDEX_NONE)
	{
		OutErrors.Add(FString::Printf(TEXT("start_orbit: unknown body '%s'"), *BodyId));
		Result.StartBody = kNoBody;
	}
	else
	{
		const CelestialBody& Body = Result.SystemData.System.Body(Result.StartBody);
		const double Radius = Body.Radius + Altitude;
		RequirePositive(Altitude, TEXT("altitude_m"), TEXT("start_orbit"), OutErrors);
		if (Radius >= Body.SphereOfInfluence)
		{
			OutErrors.Add(TEXT("start_orbit: altitude is outside the body's sphere of influence"));
		}
		OrbitalElements Circular;
		Circular.SemiLatusRectum = Radius;
		Circular.Inclination = DegToRad(Inclination);
		Circular.TrueAnomaly = DegToRad(TrueAnomaly);
		Circular.Epoch = Result.StartTime;
		Result.StartState = ElementsToState(Circular, Body.Mu);
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
