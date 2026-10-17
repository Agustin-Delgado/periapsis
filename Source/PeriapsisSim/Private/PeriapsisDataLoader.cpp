#include "PeriapsisDataLoader.h"

#include "Dom/JsonObject.h"
#include "Algo/Find.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PeriapsisCore/Constants.h"
#include "PeriapsisCore/OrbitalElements.h"
#include "PeriapsisCore/Surface.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

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
	bool ReadColor(const FJsonObject& O, const TCHAR* Field, const FString& Where, FLinearColor& Out, TArray<FString>& OutErrors)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!O.TryGetArrayField(Field, Values) || Values->Num() != 3)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: '%s' needs 3 numbers"), *Where, Field));
			return false;
		}
		Out = FLinearColor((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		return true;
	}

	// "ground_colors": {"by_height": [{"height_m": h, "color": [r, g, b]}, ...],
	//                   "steep": {"slope_deg": s, "color": [r, g, b]}}
	void ParseGroundColors(const FJsonObject& Profile, const FString& Where, FPeriapsisBodyInfo& Info, TArray<FString>& OutErrors)
	{
		const TSharedPtr<FJsonObject>* Colors = nullptr;
		if (!Profile.TryGetObjectField(TEXT("ground_colors"), Colors))
		{
			return;
		}
		const FString ColorsWhere = Where + TEXT(", ground_colors");
		const TArray<TSharedPtr<FJsonValue>>* Stops = nullptr;
		if ((*Colors)->TryGetArrayField(TEXT("by_height"), Stops))
		{
			for (int32 Index = 0; Index < Stops->Num(); ++Index)
			{
				const FString StopWhere = FString::Printf(TEXT("%s.by_height[%d]"), *ColorsWhere, Index);
				const TSharedPtr<FJsonObject>* Stop = nullptr;
				double Height = 0.0;
				FLinearColor Color;
				if (!(*Stops)[Index]->TryGetObject(Stop) || !ReadNumber(**Stop, TEXT("height_m"), StopWhere, Height, OutErrors)
					|| !ReadColor(**Stop, TEXT("color"), StopWhere, Color, OutErrors))
				{
					continue;
				}
				if (!Info.GroundColors.IsEmpty() && Height <= Info.GroundColors.Last().Key)
				{
					OutErrors.Add(FString::Printf(TEXT("%s: heights must increase"), *StopWhere));
				}
				Info.GroundColors.Emplace(Height, Color);
			}
		}
		const TSharedPtr<FJsonObject>* Steep = nullptr;
		if ((*Colors)->TryGetObjectField(TEXT("steep"), Steep))
		{
			const FString SteepWhere = ColorsWhere + TEXT(".steep");
			ReadNumber(**Steep, TEXT("slope_deg"), SteepWhere, Info.SteepSlopeDeg, OutErrors);
			ReadColor(**Steep, TEXT("color"), SteepWhere, Info.SteepColor, OutErrors);
			if (!(Info.SteepSlopeDeg > 0.0 && Info.SteepSlopeDeg < 90.0))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: 'slope_deg' must be between 0 and 90"), *SteepWhere));
			}
		}
	}

	void ParseTerrain(const FJsonObject& T, const FString& DataRoot, const FString& Where, CelestialBody& Body, FPeriapsisBodyInfo& Info,
		TArray<FString>& OutErrors)
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
		ParseGroundColors(*Profile, ProfileWhere, Info, OutErrors);

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
	// vertical speed, moving with the ground otherwise. For launches, landing tests and hops.
	void ParseStartSurface(const FJsonObject& O, const FPeriapsisSystemData& SystemData, int32& OutBody, FPeriapsisSurfaceStart& OutStart,
		TArray<FString>& OutErrors)
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
		OutStart = {Latitude, Longitude, Height, VerticalSpeed};
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
	bool ReadVector(const FJsonObject& Object, const TCHAR* Field, const FString& Where, Vec3& Out, TArray<FString>& OutErrors)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.TryGetArrayField(Field, Values) || Values->Num() != 3)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: '%s' needs 3 numbers"), *Where, Field));
			return false;
		}
		Out = {(*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber()};
		return true;
	}

	int ResourceIndex(const PartCatalog& Catalog, const FString& Id, const FString& Where, TArray<FString>& OutErrors)
	{
		const int Index = Catalog.FindResource(TCHAR_TO_UTF8(*Id));
		if (Index < 0)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: unknown resource '%s'"), *Where, *Id));
		}
		return Index;
	}

	// The side of a part as a design file names it.
	constexpr const TCHAR* SurfaceNode = TEXT("surface");
}

FLinearColor FPeriapsisBodyInfo::GroundColor(double Height, double SlopeDeg) const
{
	FLinearColor Ground = Color; // the display colour when the profile names none
	if (!GroundColors.IsEmpty() && Height <= GroundColors[0].Key)
	{
		Ground = GroundColors[0].Value;
	}
	else if (!GroundColors.IsEmpty())
	{
		Ground = GroundColors.Last().Value;
		for (int32 Index = 1; Index < GroundColors.Num(); ++Index)
		{
			if (Height < GroundColors[Index].Key)
			{
				const TPair<double, FLinearColor>& Low = GroundColors[Index - 1];
				const TPair<double, FLinearColor>& High = GroundColors[Index];
				Ground = FMath::Lerp(Low.Value, High.Value, static_cast<float>((Height - Low.Key) / (High.Key - Low.Key)));
				break;
			}
		}
	}
	// Rock shows through over a few degrees around the steep slope.
	const float Steep = FMath::SmoothStep(static_cast<float>(SteepSlopeDeg - 4.0), static_cast<float>(SteepSlopeDeg + 4.0), static_cast<float>(SlopeDeg));
	return FMath::Lerp(Ground, SteepColor, Steep);
}

bool PeriapsisData::ParsePart(const FString& Json, const PartCatalog& Resources, PartDefinition& Out, TArray<FString>& OutErrors)
{
	const int32 ErrorsBefore = OutErrors.Num();
	const TSharedPtr<FJsonObject> Root = ParseObject(Json, TEXT("part"), OutErrors);
	if (!Root)
	{
		return false;
	}
	PartDefinition Part;
	FString Id, Name, Category;
	if (!Root->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty())
	{
		OutErrors.Add(TEXT("part: missing 'id'"));
		return false;
	}
	const FString Where = FString::Printf(TEXT("part '%s'"), *Id);
	Root->TryGetStringField(TEXT("name"), Name);
	Root->TryGetStringField(TEXT("category"), Category);
	Part.Id = TCHAR_TO_UTF8(*Id);
	Part.Name = TCHAR_TO_UTF8(*(Name.IsEmpty() ? Id : Name));
	Part.Category = TCHAR_TO_UTF8(*Category);
	ReadNumber(*Root, TEXT("dry_mass_kg"), Where, Part.DryMass, OutErrors);
	Root->TryGetNumberField(TEXT("cost"), Part.Cost);
	Root->TryGetBoolField(TEXT("surface_attach"), Part.bSurfaceAttach);

	const TSharedPtr<FJsonObject>* Geometry = nullptr;
	if (Root->TryGetObjectField(TEXT("geometry"), Geometry))
	{
		const FJsonObject& G = **Geometry;
		FString Shape;
		G.TryGetStringField(TEXT("shape"), Shape);
		ReadNumber(G, TEXT("length_m"), Where, Part.Geometry.Length, OutErrors);
		if (Shape == TEXT("frustum"))
		{
			Part.Geometry.Shape = PartGeometry::EShape::Frustum;
			ReadNumber(G, TEXT("bottom_radius_m"), Where, Part.Geometry.BottomRadius, OutErrors);
			ReadNumber(G, TEXT("top_radius_m"), Where, Part.Geometry.TopRadius, OutErrors);
		}
		else if (Shape == TEXT("box"))
		{
			Part.Geometry.Shape = PartGeometry::EShape::Box;
			ReadNumber(G, TEXT("width_m"), Where, Part.Geometry.Width, OutErrors);
		}
		else
		{
			OutErrors.Add(FString::Printf(TEXT("%s: geometry 'shape' must be 'frustum' or 'box' (got '%s')"), *Where, *Shape));
		}
	}
	else
	{
		OutErrors.Add(FString::Printf(TEXT("%s: missing 'geometry'"), *Where));
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (Root->TryGetArrayField(TEXT("attach_nodes"), Nodes))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Nodes)
		{
			const TSharedPtr<FJsonObject>* NodeObject = nullptr;
			if (!Value->TryGetObject(NodeObject))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: attach nodes must be objects"), *Where));
				continue;
			}
			AttachNode Node;
			FString NodeId;
			(*NodeObject)->TryGetStringField(TEXT("id"), NodeId);
			Node.Id = TCHAR_TO_UTF8(*NodeId);
			ReadVector(**NodeObject, TEXT("pos"), Where, Node.Position, OutErrors);
			ReadVector(**NodeObject, TEXT("dir"), Where, Node.Direction, OutErrors);
			(*NodeObject)->TryGetNumberField(TEXT("size"), Node.Size);
			Part.Nodes.push_back(Node);
		}
	}

	const TSharedPtr<FJsonObject>* Joint = nullptr;
	if (Root->TryGetObjectField(TEXT("joint_strength"), Joint))
	{
		ReadNumber(**Joint, TEXT("force_n"), Where, Part.JointForce, OutErrors);
		ReadNumber(**Joint, TEXT("torque_nm"), Where, Part.JointTorque, OutErrors);
	}
	else
	{
		OutErrors.Add(FString::Printf(TEXT("%s: missing 'joint_strength'"), *Where));
	}

	const TArray<TSharedPtr<FJsonValue>>* ResourceValues = nullptr;
	if (Root->TryGetArrayField(TEXT("resources"), ResourceValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *ResourceValues)
		{
			const TSharedPtr<FJsonObject>* ResourceObject = nullptr;
			FString ResourceId;
			double Mass = 0.0;
			if (!Value->TryGetObject(ResourceObject) || !(*ResourceObject)->TryGetStringField(TEXT("resource"), ResourceId)
				|| !(*ResourceObject)->TryGetNumberField(TEXT("mass_kg"), Mass))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: resources need 'resource' and 'mass_kg'"), *Where));
				continue;
			}
			Part.Resources.push_back({ResourceIndex(Resources, ResourceId, Where, OutErrors), Mass});
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Modules = nullptr;
	if (Root->TryGetArrayField(TEXT("modules"), Modules))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Modules)
		{
			const TSharedPtr<FJsonObject>* ModuleObject = nullptr;
			FString Type;
			if (!Value->TryGetObject(ModuleObject) || !(*ModuleObject)->TryGetStringField(TEXT("type"), Type))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: modules need a 'type'"), *Where));
				continue;
			}
			const FJsonObject& M = **ModuleObject;
			if (Type == TEXT("Engine"))
			{
				EngineModule Engine;
				ReadNumber(M, TEXT("thrust_vac_n"), Where, Engine.ThrustVacuum, OutErrors);
				ReadNumber(M, TEXT("isp_vac_s"), Where, Engine.IspVacuum, OutErrors);
				ReadNumber(M, TEXT("isp_sl_s"), Where, Engine.IspSeaLevel, OutErrors);
				M.TryGetNumberField(TEXT("gimbal_deg"), Engine.GimbalDegrees);
				const TSharedPtr<FJsonObject>* Propellants = nullptr;
				if (M.TryGetObjectField(TEXT("propellants"), Propellants))
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Propellants)->Values)
					{
						Engine.Propellants.push_back({ResourceIndex(Resources, Entry.Key, Where, OutErrors), Entry.Value->AsNumber()});
					}
				}
				else
				{
					OutErrors.Add(FString::Printf(TEXT("%s: the engine needs 'propellants' (resource: share of the mass flow)"), *Where));
				}
				Part.Engine = Engine;
			}
			else if (Type == TEXT("Decoupler"))
			{
				DecouplerModule Decoupler;
				M.TryGetNumberField(TEXT("ejection_impulse_ns"), Decoupler.EjectionImpulse);
				M.TryGetBoolField(TEXT("crossfeed"), Decoupler.bCrossfeed);
				Part.Decoupler = Decoupler;
			}
			else if (Type == TEXT("Command"))
			{
				Part.bCommand = true;
			}
			else
			{
				OutErrors.Add(FString::Printf(TEXT("%s: unknown module type '%s'"), *Where, *Type));
			}
		}
	}

	if (OutErrors.Num() > ErrorsBefore)
	{
		return false;
	}
	Out = MoveTemp(Part);
	return true;
}

bool PeriapsisData::LoadPartCatalog(const FString& DataRoot, PartCatalog& Out, TArray<FString>& OutErrors)
{
	const int32 ErrorsBefore = OutErrors.Num();
	const FString PartsDirectory = FPaths::Combine(DataRoot, TEXT("parts"));
	PartCatalog Result;

	FString ResourcesJson;
	if (!FFileHelper::LoadFileToString(ResourcesJson, *FPaths::Combine(PartsDirectory, TEXT("resources.json"))))
	{
		OutErrors.Add(TEXT("parts: cannot read parts/resources.json"));
		return false;
	}
	const TSharedPtr<FJsonObject> ResourcesRoot = ParseObject(ResourcesJson, TEXT("parts/resources.json"), OutErrors);
	const TArray<TSharedPtr<FJsonValue>>* ResourceValues = nullptr;
	if (!ResourcesRoot || !ResourcesRoot->TryGetArrayField(TEXT("resources"), ResourceValues))
	{
		OutErrors.Add(TEXT("parts/resources.json: missing 'resources'"));
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *ResourceValues)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FString Id, Name;
		if (!Value->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("id"), Id))
		{
			OutErrors.Add(TEXT("parts/resources.json: every resource needs an 'id'"));
			continue;
		}
		(*Object)->TryGetStringField(TEXT("name"), Name);
		Result.ResourceTypes.push_back({TCHAR_TO_UTF8(*Id), TCHAR_TO_UTF8(*(Name.IsEmpty() ? Id : Name))});
	}

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(PartsDirectory, TEXT("*.json")), true, false);
	Files.Sort(); // the same catalog order on every machine
	for (const FString& File : Files)
	{
		if (File == TEXT("resources.json"))
		{
			continue;
		}
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *FPaths::Combine(PartsDirectory, File)))
		{
			OutErrors.Add(FString::Printf(TEXT("parts: cannot read %s"), *File));
			continue;
		}
		PartDefinition Part;
		TArray<FString> PartErrors;
		if (ParsePart(Json, Result, Part, PartErrors))
		{
			if (FString(UTF8_TO_TCHAR(Part.Id.c_str())) + TEXT(".json") != File)
			{
				OutErrors.Add(FString::Printf(TEXT("parts/%s: the file must be named after the part's id"), *File));
			}
			Result.Parts.push_back(MoveTemp(Part));
		}
		for (const FString& Error : PartErrors)
		{
			OutErrors.Add(FString::Printf(TEXT("parts/%s: %s"), *File, *Error));
		}
	}
	for (const std::string& Error : Result.Validate())
	{
		OutErrors.Add(FString::Printf(TEXT("parts: %s"), UTF8_TO_TCHAR(Error.c_str())));
	}
	if (OutErrors.Num() > ErrorsBefore)
	{
		return false;
	}
	Out = MoveTemp(Result);
	return true;
}

bool PeriapsisData::ParseDesign(const FString& Json, const PartCatalog& Catalog, VesselDesign& Out, TArray<FString>& OutErrors)
{
	const int32 ErrorsBefore = OutErrors.Num();
	const TSharedPtr<FJsonObject> Root = ParseObject(Json, TEXT("design"), OutErrors);
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Root || !Root->TryGetArrayField(TEXT("parts"), Entries) || Entries->IsEmpty())
	{
		OutErrors.Add(TEXT("design: 'parts' must be a non-empty array"));
		return false;
	}
	VesselDesign Result;
	FString DesignName;
	Root->TryGetStringField(TEXT("name"), DesignName);
	Result.Name = TCHAR_TO_UTF8(*DesignName);

	// Each entry may stand for several instances (symmetry): its id maps to all of them.
	TMap<FString, TArray<int32>> InstancesById;
	TMap<int32, TArray<int32>> SavedGroups; // symmetry groups of a saved design: their instances
	for (int32 Index = 0; Index < Entries->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* EntryObject = nullptr;
		if (!(*Entries)[Index]->TryGetObject(EntryObject))
		{
			OutErrors.Add(FString::Printf(TEXT("design parts[%d]: not an object"), Index));
			return false;
		}
		const FJsonObject& E = **EntryObject;
		FString Id, PartId;
		E.TryGetStringField(TEXT("id"), Id);
		E.TryGetStringField(TEXT("part"), PartId);
		const FString Where = FString::Printf(TEXT("design part '%s'"), *Id);
		if (Id.IsEmpty() || InstancesById.Contains(Id))
		{
			OutErrors.Add(FString::Printf(TEXT("design parts[%d]: missing or duplicate 'id'"), Index));
			return false;
		}
		const int Part = Catalog.FindPart(TCHAR_TO_UTF8(*PartId));
		if (Part < 0)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: unknown part '%s'"), *Where, *PartId));
			return false;
		}

		TArray<int32> Added;
		FString ParentId;
		if (!E.TryGetStringField(TEXT("parent"), ParentId))
		{
			if (Index != 0)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: only the first part has no parent"), *Where));
				return false;
			}
			Result.SetRoot(Catalog, Part);
			Added.Add(0);
		}
		else
		{
			const TArray<int32>* Parents = InstancesById.Find(ParentId);
			if (!Parents)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: parent '%s' must be listed before it"), *Where, *ParentId));
				return false;
			}
			Attachment How;
			FString Node;
			E.TryGetStringField(TEXT("node"), Node);
			How.Node = Node == SurfaceNode ? std::string() : std::string(TCHAR_TO_UTF8(*Node));
			const TSharedPtr<FJsonObject>* Surface = nullptr;
			FString ParentNode;
			if (E.TryGetObjectField(TEXT("surface"), Surface))
			{
				double Angle = 0.0;
				(*Surface)->TryGetNumberField(TEXT("angle_deg"), Angle);
				(*Surface)->TryGetNumberField(TEXT("height_m"), How.SurfaceHeight);
				How.SurfaceAngle = DegToRad(Angle);
			}
			else if (E.TryGetStringField(TEXT("parent_node"), ParentNode))
			{
				How.ParentNode = TCHAR_TO_UTF8(*ParentNode);
			}
			else
			{
				OutErrors.Add(FString::Printf(TEXT("%s: needs 'parent_node' or 'surface'"), *Where));
				return false;
			}
			if (Node.IsEmpty())
			{
				OutErrors.Add(FString::Printf(TEXT("%s: needs 'node' (one of its nodes, or 'surface')"), *Where));
				return false;
			}
			double Roll = 0.0;
			E.TryGetNumberField(TEXT("roll_deg"), Roll);
			How.Roll = DegToRad(Roll);
			double Symmetry = 1.0;
			E.TryGetNumberField(TEXT("symmetry"), Symmetry);
			if (Symmetry < 1.0 || Symmetry > 16.0 || Symmetry != FMath::RoundToDouble(Symmetry))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: 'symmetry' must be a whole number from 1 to 16"), *Where));
				return false;
			}
			const std::vector<int> NewInstances = Result.Attach(Catalog, (*Parents)[0], How, Part, static_cast<int>(Symmetry));
			if (NewInstances.empty())
			{
				OutErrors.Add(FString::Printf(TEXT("%s: cannot attach it to '%s' that way (unknown or taken node)"), *Where, *ParentId));
				return false;
			}
			Added.Append(NewInstances.data(), static_cast<int32>(NewInstances.size()));
		}

		double Stage = -1.0, Priority = 0.0, Group = -1.0;
		E.TryGetNumberField(TEXT("stage"), Stage);
		E.TryGetNumberField(TEXT("flow_priority"), Priority);
		for (const int32 Instance : Added)
		{
			Result.SetStage(Instance, static_cast<int>(Stage));
			Result.SetFlowPriority(Instance, static_cast<int>(Priority));
		}
		if (E.TryGetNumberField(TEXT("symmetry_group"), Group) && Added.Num() == 1)
		{
			// A saved design lists symmetry copies one by one. They go back in one group at the
			// end: a group now would make the next copy's children land on every member.
			SavedGroups.FindOrAdd(static_cast<int32>(Group)).Add(Added[0]);
		}
		InstancesById.Add(Id, MoveTemp(Added));
	}

	for (const TPair<int32, TArray<int32>>& Group : SavedGroups)
	{
		for (int32 Member = 1; Member < Group.Value.Num(); ++Member)
		{
			Result.JoinSymmetryGroup(Group.Value[Member], Group.Value[0]);
		}
	}
	for (const std::string& Error : Result.Validate(Catalog))
	{
		OutErrors.Add(FString::Printf(TEXT("design: %s"), UTF8_TO_TCHAR(Error.c_str())));
	}
	if (OutErrors.Num() > ErrorsBefore)
	{
		return false;
	}
	Out = MoveTemp(Result);
	return true;
}

FString PeriapsisData::SerializeDesign(const VesselDesign& Design, const PartCatalog& Catalog)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("name"), UTF8_TO_TCHAR(Design.Name.c_str()));
	TArray<TSharedPtr<FJsonValue>> Entries;
	for (int32 Index = 0; Index < static_cast<int32>(Design.Parts().size()); ++Index)
	{
		const PartInstance& Instance = Design.Parts()[Index];
		const TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("id"), FString::Printf(TEXT("p%d"), Index));
		E->SetStringField(TEXT("part"), UTF8_TO_TCHAR(Catalog.Parts[Instance.Part].Id.c_str()));
		if (Instance.Parent >= 0)
		{
			const Attachment& How = Instance.Attach;
			E->SetStringField(TEXT("parent"), FString::Printf(TEXT("p%d"), Instance.Parent));
			if (How.ParentNode.empty())
			{
				const TSharedRef<FJsonObject> Surface = MakeShared<FJsonObject>();
				Surface->SetNumberField(TEXT("angle_deg"), RadToDeg(How.SurfaceAngle));
				Surface->SetNumberField(TEXT("height_m"), How.SurfaceHeight);
				E->SetObjectField(TEXT("surface"), Surface);
			}
			else
			{
				E->SetStringField(TEXT("parent_node"), UTF8_TO_TCHAR(How.ParentNode.c_str()));
			}
			E->SetStringField(TEXT("node"), How.Node.empty() ? FString(SurfaceNode) : FString(UTF8_TO_TCHAR(How.Node.c_str())));
			if (How.Roll != 0.0)
			{
				E->SetNumberField(TEXT("roll_deg"), RadToDeg(How.Roll));
			}
		}
		if (Instance.Stage >= 0)
		{
			E->SetNumberField(TEXT("stage"), Instance.Stage);
		}
		if (Instance.FlowPriority != 0)
		{
			E->SetNumberField(TEXT("flow_priority"), Instance.FlowPriority);
		}
		if (Instance.SymmetryGroup >= 0)
		{
			E->SetNumberField(TEXT("symmetry_group"), Instance.SymmetryGroup);
		}
		Entries.Add(MakeShared<FJsonValueObject>(E));
	}
	Root->SetArrayField(TEXT("parts"), Entries);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	return Json;
}

bool PeriapsisData::LoadDesign(const FString& Path, const FString& DataRoot, const PartCatalog& Catalog, VesselDesign& Out,
	TArray<FString>& OutErrors)
{
	const FString Full = FPaths::IsRelative(Path) ? FPaths::Combine(DataRoot, Path) : Path;
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Full))
	{
		OutErrors.Add(FString::Printf(TEXT("cannot read design '%s'"), *Path));
		return false;
	}
	TArray<FString> DesignErrors;
	const bool bOk = ParseDesign(Json, Catalog, Out, DesignErrors);
	for (const FString& Error : DesignErrors)
	{
		OutErrors.Add(FString::Printf(TEXT("%s: %s"), *Path, *Error));
	}
	return bOk;
}

void PeriapsisData::PlaceOnSurface(FPeriapsisScenario& Scenario)
{
	if (!Scenario.SurfaceStart.IsSet())
	{
		return;
	}
	const FPeriapsisSurfaceStart& Spot = Scenario.SurfaceStart.GetValue();
	const CelestialBody& Body = Scenario.SystemData.System.Body(Scenario.StartBody);
	// Standing upright: its base is this far below its centre of mass.
	const double Clearance = Scenario.Ship.Extent({0.0, 0.0, -1.0}, Scenario.Ship.CenterOfMass());
	const double Lat = DegToRad(Spot.Latitude);
	const double Lon = DegToRad(Spot.Longitude);
	const Vec3 Up{FMath::Cos(Lat) * FMath::Cos(Lon), FMath::Cos(Lat) * FMath::Sin(Lon), FMath::Sin(Lat)};
	const double Radius = Body.Radius + Body.Terrain.Height(Up) + Clearance + Spot.Height;
	const Vec3 Position = FromBodyFixed(Body, Up * Radius, Scenario.StartTime);
	Scenario.StartState = {Position, GroundVelocity(Body, Position) + Normalized(Position) * Spot.VerticalSpeed};
}

void PeriapsisData::SetShipDesign(FPeriapsisScenario& Scenario, const VesselDesign& Design)
{
	Scenario.ShipDesign = Design;
	Scenario.ShipName = UTF8_TO_TCHAR(Design.Name.c_str());
	Scenario.Ship = Vessel(Scenario.Catalog, Design);
	PlaceOnSurface(Scenario);
}

FString PeriapsisData::SavedDesignsDirectory()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Designs"));
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
			ParseTerrain(**TerrainObject, Root, Where, Body, Info, OutErrors);
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

	{
		auto Catalog = std::make_shared<PartCatalog>();
		if (!LoadPartCatalog(DataRoot, *Catalog, OutErrors))
		{
			return false;
		}
		Result.Catalog = Catalog;
	}

	const TSharedPtr<FJsonObject>* ShipObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("ship"), ShipObject))
	{
		OutErrors.Add(TEXT("scenario: missing 'ship'"));
		return false;
	}
	const FJsonObject& S = **ShipObject;
	const FString ShipWhere = TEXT("ship");
	S.TryGetStringField(TEXT("name"), Result.ShipName);
	FString DesignPath;
	if (S.TryGetStringField(TEXT("design"), DesignPath))
	{
		// A vessel made of parts.
		if (!LoadDesign(DesignPath, DataRoot, *Result.Catalog, Result.ShipDesign, OutErrors))
		{
			return false;
		}
		if (Result.ShipName.IsEmpty())
		{
			Result.ShipName = UTF8_TO_TCHAR(Result.ShipDesign.Name.c_str());
		}
		Result.Ship = Vessel(Result.Catalog, Result.ShipDesign);
	}
	else
	{
		// The one-part hull of M2 to M4.
		ShipConfig Config;
		double Length = 6.0, Diameter = 2.0;
		ReadNumber(S, TEXT("dry_mass_kg"), ShipWhere, Config.DryMass, OutErrors);
		ReadNumber(S, TEXT("fuel_mass_kg"), ShipWhere, Config.FuelMass, OutErrors);
		ReadNumber(S, TEXT("thrust_n"), ShipWhere, Config.Thrust, OutErrors);
		ReadNumber(S, TEXT("isp_s"), ShipWhere, Config.IspSeconds, OutErrors);
		RequirePositive(Config.DryMass, TEXT("dry_mass_kg"), ShipWhere, OutErrors);
		RequirePositive(Config.Thrust, TEXT("thrust_n"), ShipWhere, OutErrors);
		RequirePositive(Config.IspSeconds, TEXT("isp_s"), ShipWhere, OutErrors);
		if (Config.FuelMass < 0.0)
		{
			OutErrors.Add(TEXT("ship: 'fuel_mass_kg' cannot be negative"));
		}
		S.TryGetNumberField(TEXT("length_m"), Length);
		S.TryGetNumberField(TEXT("diameter_m"), Diameter);
		RequirePositive(Length, TEXT("length_m"), ShipWhere, OutErrors);
		RequirePositive(Diameter, TEXT("diameter_m"), ShipWhere, OutErrors);
		if (OutErrors.Num() > ErrorsBefore)
		{
			return false;
		}
		Result.Ship = Vessel::FromShipConfig(Config, Length, Diameter);
	}
	Result.Ship.SetName(TCHAR_TO_UTF8(*Result.ShipName));
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
			{TEXT("up"), AttitudeMode::Up},
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
		FPeriapsisSurfaceStart Surface;
		ParseStartSurface(**SurfaceObject, Result.SystemData, Result.StartBody, Surface, OutErrors);
		if (Result.StartBody != kNoBody)
		{
			Result.SurfaceStart = Surface;
			PlaceOnSurface(Result);
		}
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
