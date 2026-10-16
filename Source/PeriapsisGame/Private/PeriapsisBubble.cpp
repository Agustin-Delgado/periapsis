#include "PeriapsisBubble.h"

#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PeriapsisCore/Surface.h"
#include "PeriapsisCore/TerrainMesh.h"
#include "PeriapsisFlightSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "PeriapsisUniverse.h"
#include "UObject/ConstructorHelpers.h"
#include "UnitBridge.h"

using namespace Periapsis::Core;

namespace
{
	// Facets around a round part: drawn, and in its collision hull.
	constexpr int32 PartSegments = 24;
	constexpr int32 HullSegments = 12;

	// The collision ground: a square grid this fine around the vessels, rebuilt when the active
	// vessel strays a quarter of the way to its edge, and only this close to the ground.
	constexpr int32 GroundResolution = 33;
	constexpr double GroundSpacing = 3.0;         // m
	constexpr double GroundNeededBelow = 300.0;   // m of clearance
}

FVector FPeriapsisViewTransform::ToWorld(const Vec3& RootPosition) const
{
	if (!Frame.IsValid())
	{
		return FUnitBridge::ToUnreal(RootPosition - RailsOrigin);
	}
	return FUnitBridge::ToUnreal(Frame.ToLocal({RootPosition - FrameBodyRoot, Vec3{}}).Position + Correction);
}

Vec3 FPeriapsisViewTransform::ToRoot(const FVector& World) const
{
	if (!Frame.IsValid())
	{
		return FUnitBridge::ToSI(World) + RailsOrigin;
	}
	return Frame.ToBody({FUnitBridge::ToSI(World) - Correction, Vec3{}}).Position + FrameBodyRoot;
}

FVector FPeriapsisViewTransform::DirectionToWorld(const Vec3& Inertial) const
{
	return FUnitBridge::DirectionToUnreal(Frame.IsValid() ? Frame.DirectionToLocal(Inertial) : Inertial);
}

FQuat FPeriapsisViewTransform::AttitudeToWorld(const Mat3& Attitude) const
{
	return FMatrix(DirectionToWorld(Attitude.X), -DirectionToWorld(Attitude.Y), DirectionToWorld(Attitude.Z), FVector::ZeroVector).ToQuat();
}

FLinearColor PartColor(const FString& Category)
{
	if (Category == TEXT("command"))
	{
		return FLinearColor(0.72f, 0.76f, 0.84f);
	}
	if (Category == TEXT("tank"))
	{
		return FLinearColor(0.9f, 0.9f, 0.88f);
	}
	if (Category == TEXT("engine"))
	{
		return FLinearColor(0.32f, 0.32f, 0.36f);
	}
	if (Category == TEXT("decoupler"))
	{
		return FLinearColor(0.95f, 0.62f, 0.12f);
	}
	if (Category == TEXT("structure"))
	{
		return FLinearColor(0.5f, 0.53f, 0.58f);
	}
	return FLinearColor(0.8f, 0.45f, 0.2f);
}

void BuildVesselMesh(UProceduralMeshComponent* Mesh, const Vessel& Craft, UMaterialInterface* Material, bool bCollision)
{
	std::vector<PartInstance> Placed;
	for (const VesselPart& Part : Craft.Parts())
	{
		Placed.push_back(Part.Instance);
	}
	BuildPartsMesh(Mesh, Craft.Catalog(), Placed, Material, bCollision);
}

void BuildPartsMesh(UProceduralMeshComponent* Mesh, const PartCatalog& Catalog, const std::vector<PartInstance>& Parts,
	UMaterialInterface* Material, bool bCollision, const FLinearColor* Tint)
{
	struct FSection
	{
		TArray<FVector> Vertices;
		TArray<FVector> Normals;
		TArray<int32> Triangles;
		FLinearColor Color;
	};
	TArray<FSection> Sections;
	TMap<FString, int32> SectionOfCategory;
	TArray<TArray<FVector>> Hulls;
	for (const PartInstance& Placed : Parts)
	{
		const PartDefinition& Part = Catalog.Parts[Placed.Part];
		const FString Category = UTF8_TO_TCHAR(Part.Category.c_str());
		int32 SectionIndex = INDEX_NONE;
		if (const int32* Found = SectionOfCategory.Find(Category))
		{
			SectionIndex = *Found;
		}
		else
		{
			SectionIndex = Sections.Num();
			SectionOfCategory.Add(Category, SectionIndex);
			Sections.AddDefaulted_GetRef().Color = Tint ? *Tint : PartColor(Category);
		}
		FSection& Section = Sections[SectionIndex];
		// Mirrored like the terrain, with the same triangle order: the outside shows in Unreal.
		const PartMesh Solid = BuildPartMesh(Part.Geometry, PartSegments);
		const int32 Base = Section.Vertices.Num();
		for (size_t Vertex = 0; Vertex < Solid.Positions.size(); ++Vertex)
		{
			Section.Vertices.Add(FUnitBridge::ToUnreal(Placed.Position + Placed.Rotation * Solid.Positions[Vertex]));
			Section.Normals.Add(FUnitBridge::DirectionToUnreal(Placed.Rotation * Solid.Normals[Vertex]));
		}
		for (const int Corner : Solid.Triangles)
		{
			Section.Triangles.Add(Base + Corner);
		}
		if (bCollision)
		{
			TArray<FVector>& Hull = Hulls.AddDefaulted_GetRef();
			for (const Vec3& Point : Part.Geometry.HullPoints(HullSegments))
			{
				Hull.Add(FUnitBridge::ToUnreal(Placed.Position + Placed.Rotation * Point));
			}
		}
	}

	Mesh->ClearAllMeshSections();
	for (int32 Index = 0; Index < Sections.Num(); ++Index)
	{
		const FSection& Section = Sections[Index];
		Mesh->CreateMeshSection(Index, Section.Vertices, Section.Triangles, Section.Normals, {}, {}, {}, false);
		if (Material)
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, Mesh);
			Instance->SetVectorParameterValue(TEXT("Color"), Section.Color);
			Mesh->SetMaterial(Index, Instance);
		}
	}
	if (bCollision)
	{
		Mesh->bUseComplexAsSimpleCollision = false;
		Mesh->SetCollisionConvexMeshes(Hulls);
	}
}

FQuat FPeriapsisViewTransform::BodyFixedToWorld(const CelestialBody& Body) const
{
	// Where the three Unreal axes of the body-fixed frame end up in the world.
	auto Axis = [&](const FVector& UnrealAxis)
	{
		return DirectionToWorld(FromBodyFixed(Body, FUnitBridge::ToSI(UnrealAxis), Time));
	};
	return FMatrix(Axis(FVector::XAxisVector), Axis(FVector::YAxisVector), Axis(FVector::ZAxisVector), FVector::ZeroVector).ToQuat();
}

APeriapsisBubble::APeriapsisBubble()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bAsyncPhysicsTickEnabled = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	Material = BasicMaterial.Object;

	GroundPatch = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GroundPatch"));
	GroundPatch->SetupAttachment(RootComponent);
	GroundPatch->SetUsingAbsoluteLocation(true);
	GroundPatch->SetUsingAbsoluteRotation(true);
	GroundPatch->SetHiddenInGame(true);
	GroundPatch->bUseAsyncCooking = false; // small grid: cooking it on the spot costs a few milliseconds
	GroundPatch->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
}

FPeriapsisUniverse* APeriapsisBubble::GetUniverse() const
{
	const UPeriapsisFlightSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UPeriapsisFlightSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetUniverse() : nullptr;
}

void APeriapsisBubble::BeginPlay()
{
	Super::BeginPlay();
	Tick(0.0f); // attach to the universe and create the meshes before the first physics step
}

void APeriapsisBubble::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AttachedUniverse && AttachedUniverse == GetUniverse())
	{
		AttachedUniverse->SetPhysicsScene(nullptr);
	}
	AttachedUniverse = nullptr;
	Super::EndPlay(EndPlayReason);
}

UProceduralMeshComponent* APeriapsisBubble::CreateVesselBody(int32 Index, const Vessel& Craft)
{
	UProceduralMeshComponent* Body = NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("Vessel_%d"), Index));
	Body->SetupAttachment(RootComponent);
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetUsingAbsoluteLocation(true);
	Body->SetUsingAbsoluteRotation(true);
	Body->bUseAsyncCooking = false; // the body is needed as soon as the vessel joins
	// Chaos gets no gravity, damping or sleep of its own: the universe gives every
	// acceleration, and a vessel coasting with the frame is nearly still but not resting.
	Body->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetEnableGravity(false);
	Body->SetLinearDamping(0.0f);
	Body->SetAngularDamping(0.0f);
	Body->BodyInstance.SleepFamily = ESleepFamily::Custom;
	Body->BodyInstance.CustomSleepThresholdMultiplier = 0.0f;
	Body->RegisterComponent();
	// Once registered: the collision shapes are cooked into the body only then.
	BuildVesselMesh(Body, Craft, Material, true);
	return Body;
}

void APeriapsisBubble::EnsureVesselBodies(const FPeriapsisUniverse& Universe)
{
	if (BuiltGeneration != Universe.GetGeneration() || BuiltUniverse != &Universe)
	{
		// A restart: every vessel is back as it was, start over.
		for (UProceduralMeshComponent* Body : VesselBodies)
		{
			if (Body)
			{
				Body->DestroyComponent();
			}
		}
		VesselBodies.Reset();
		CentersOfMass.Reset();
		PartCounts.Reset();
		PendingAdds.Reset();
		PendingRemovals.Reset();
		PendingMasses.Reset();
		BuiltGeneration = Universe.GetGeneration();
		BuiltUniverse = &Universe;
	}
	// Vessels are only ever added (pieces that come off), at the end.
	for (int32 Index = VesselBodies.Num(); Index < Universe.NumVessels(); ++Index)
	{
		const Vessel& Craft = Universe.GetVessel(Index).Flight->GetVessel();
		VesselBodies.Add(CreateVesselBody(Index, Craft));
		CentersOfMass.Add(Craft.CenterOfMass());
		PartCounts.Add(static_cast<int32>(Craft.Parts().size()));
	}
}

void APeriapsisBubble::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FPeriapsisUniverse* Universe = GetUniverse();
	const UPeriapsisFlightSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UPeriapsisFlightSubsystem>() : nullptr;
	const bool bBuilding = Subsystem && Subsystem->IsBuilding();
	SetActorHiddenInGame(bBuilding);
	if (bBuilding)
	{
		// The universe waits: let go of it (the flights keep their last states) and pick it up
		// again when the builder closes.
		if (AttachedUniverse && AttachedUniverse == Universe)
		{
			AttachedUniverse->SetPhysicsScene(nullptr);
		}
		AttachedUniverse = nullptr;
		bSimClockStarted = false;
		for (UProceduralMeshComponent* Body : VesselBodies)
		{
			Body->SetSimulatePhysics(false);
			Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		PendingAdds.Reset();
		PendingRemovals.Reset();
		PendingMasses.Reset();
		return;
	}
	if (Universe != AttachedUniverse)
	{
		// A new scenario was loaded: the old universe is gone, start over with the new one.
		AttachedUniverse = Universe;
		bSimClockStarted = false;
		if (Universe)
		{
			Universe->SetPhysicsScene(this);
		}
	}
	if (!Universe)
	{
		return;
	}
	EnsureVesselBodies(*Universe);
	UpdateGroundPatch(*Universe);

	for (const int32 Vessel : PendingRemovals)
	{
		UProceduralMeshComponent* Body = VesselBodies[Vessel];
		Body->SetSimulatePhysics(false);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	PendingRemovals.Reset();

	for (const TPair<int32, double>& Mass : PendingMasses)
	{
		UProceduralMeshComponent* Body = VesselBodies[Mass.Key];
		// Mass only matters to collisions: follow the fuel in 1 % steps, not every frame.
		if (FMath::Abs(Body->GetMass() - Mass.Value) > 0.01 * Mass.Value)
		{
			Body->SetMassOverrideInKg(NAME_None, static_cast<float>(Mass.Value), true);
		}
	}
	PendingMasses.Reset();

	for (const TPair<int32, Vessel>& Add : PendingAdds)
	{
		UProceduralMeshComponent* Body = VesselBodies[Add.Key];
		const int32 Parts = static_cast<int32>(Add.Value.Parts().size());
		Body->SetSimulatePhysics(false);
		if (Parts != PartCounts[Add.Key])
		{
			// Parts came off: a new shape. It starts again from the universe's state.
			BuildVesselMesh(Body, Add.Value, Material, true);
			PartCounts[Add.Key] = Parts;
		}
		CentersOfMass[Add.Key] = Add.Value.CenterOfMass();
		Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Body->SetMassOverrideInKg(NAME_None, static_cast<float>(Add.Value.Mass()), true);
		Body->SetSimulatePhysics(true);
		Body->SetEnableGravity(false);
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
	PendingAdds.Reset();
}

void APeriapsisBubble::AsyncPhysicsTickActor(float DeltaTime, float SimTime)
{
	Super::AsyncPhysicsTickActor(DeltaTime, SimTime);
	FPeriapsisUniverse* Universe = AttachedUniverse;
	if (!Universe || Universe != GetUniverse())
	{
		return;
	}
	if (!bSimClockStarted)
	{
		// Chaos passes its clock as a float; keep it in double from here on.
		SimClock = SimTime;
		bSimClockStarted = true;
	}
	const double StepStart = SimClock;
	SimClock += DeltaTime;
	StepDt = DeltaTime;

	const bool bWasActive = Universe->IsPhysicsActive();
	const double UniverseAtStart = Universe->GetTime();
	Universe->PhysicsStep(DeltaTime);
	if (Universe->IsPhysicsActive())
	{
		if (!bWasActive)
		{
			BubbleStartTime = UniverseAtStart;
		}
		UniverseMinusSim = UniverseAtStart - StepStart;
	}
}

double APeriapsisBubble::ResultsTime() const
{
	const FPhysScene* Scene = GetWorld() ? GetWorld()->GetPhysicsScene() : nullptr;
	return Scene && Scene->GetSolver() ? Scene->GetSolver()->GetPhysicsResultsTime_External() : 0.0;
}

double APeriapsisBubble::GetRenderTime() const
{
	const FPeriapsisUniverse* Universe = GetUniverse();
	if (!Universe)
	{
		return 0.0;
	}
	if (!Universe->IsPhysicsActive() || !bSimClockStarted)
	{
		return Universe->GetTime();
	}
	return FMath::Clamp(ResultsTime() + UniverseMinusSim, BubbleStartTime, Universe->GetTime());
}

FPeriapsisViewTransform APeriapsisBubble::GetViewTransform() const
{
	FPeriapsisViewTransform View;
	const FPeriapsisUniverse* Universe = GetUniverse();
	if (!Universe)
	{
		return View;
	}
	View.Time = GetRenderTime();
	const Vec3 Ship = Universe->VesselPositionAt(FPeriapsisUniverse::ActiveVessel, View.Time);
	if (!Universe->IsPhysicsActive() || !Universe->GetLocalFrame().IsValid())
	{
		View.RailsOrigin = Ship; // on rails the ship sits at the world origin
		return View;
	}
	View.Frame = Universe->GetLocalFrame();
	View.Frame.AdvanceTo(View.Time);
	View.FrameBodyRoot = Universe->BodyPositionAt(View.Frame.BodyIndex(), View.Time);
	if (IsSimulating(FPeriapsisUniverse::ActiveVessel))
	{
		// Anchored to where Chaos shows the ship, however it interpolates (rebases included).
		const Vec3 ShipLocal = View.Frame.ToLocal({Ship - View.FrameBodyRoot, Vec3{}}).Position;
		View.Correction = FUnitBridge::ToSI(GetVesselCenter(FPeriapsisUniverse::ActiveVessel)) - ShipLocal;
	}
	return View;
}

void APeriapsisBubble::PlaceRailsVessels(const FPeriapsisViewTransform& View)
{
	const FPeriapsisUniverse* Universe = GetUniverse();
	if (!Universe || VesselBodies.Num() != Universe->NumVessels())
	{
		return;
	}
	for (int32 Index = 0; Index < VesselBodies.Num(); ++Index)
	{
		if (!IsSimulating(Index))
		{
			// The centre of mass on its rails, the body turned to the vessel's attitude around it.
			const Flight& VesselFlight = *Universe->GetVessel(Index).Flight;
			const FVector Center = View.ToWorld(Universe->VesselPositionAt(Index, View.Time));
			const FQuat Rotation = View.AttitudeToWorld(VesselFlight.Attitude());
			VesselBodies[Index]->SetWorldLocationAndRotation(Center - Rotation.RotateVector(CenterOfMassUnreal(Index)), Rotation, false, nullptr,
				ETeleportType::TeleportPhysics);
		}
	}
}

void APeriapsisBubble::UpdateGroundPatch(FPeriapsisUniverse& Universe)
{
	const LocalFrame& Frame = Universe.GetLocalFrame();
	const bool bNeeded = Universe.IsPhysicsActive() && Frame.IsValid() && Frame.IsBodyFixed()
		&& Universe.VesselAltitudeAboveTerrain(FPeriapsisUniverse::ActiveVessel) < GroundNeededBelow;
	if (!bNeeded)
	{
		if (GroundPatch && bGroundReady)
		{
			GroundPatch->ClearAllMeshSections();
		}
		bGroundReady = false;
		return;
	}

	const Vec3 Active = Universe.VesselLocalState(FPeriapsisUniverse::ActiveVessel).Position;
	const double Half = 0.5 * GroundSpacing * (GroundResolution - 1);
	const bool bStale = !bGroundReady || GroundUniverse != &Universe || GroundRebases != Universe.GetRebaseCount()
		|| GroundFrameChanges != Universe.GetFrameChanges() || FMath::Abs(Active.X - GroundCenter.X) > 0.25 * Half
		|| FMath::Abs(Active.Y - GroundCenter.Y) > 0.25 * Half;
	if (!bStale)
	{
		return;
	}

	const CelestialBody& Body = Universe.GetSystem().Body(Frame.BodyIndex());
	const Periapsis::Core::GroundPatch Patch = BuildGroundPatch(Frame, Body, Active, GroundResolution, GroundSpacing);
	TArray<FVector> Vertices;
	Vertices.Reserve(static_cast<int32>(Patch.Positions.size()));
	for (const Vec3& P : Patch.Positions)
	{
		Vertices.Add(FUnitBridge::ToUnreal(P));
	}
	TArray<int32> Triangles(Patch.Triangles.data(), static_cast<int32>(Patch.Triangles.size()));
	GroundPatch->CreateMeshSection(0, Vertices, Triangles, {}, {}, {}, {}, true);

	GroundCenter = Active;
	GroundRebases = Universe.GetRebaseCount();
	GroundFrameChanges = Universe.GetFrameChanges();
	GroundUniverse = &Universe;
	bGroundReady = true;
}

UProceduralMeshComponent* APeriapsisBubble::GetVesselBody(int32 Vessel) const
{
	return VesselBodies.IsValidIndex(Vessel) ? VesselBodies[Vessel].Get() : nullptr;
}

FVector APeriapsisBubble::CenterOfMassUnreal(int32 Vessel) const
{
	return CentersOfMass.IsValidIndex(Vessel) ? FUnitBridge::ToUnreal(CentersOfMass[Vessel]) : FVector::ZeroVector;
}

FVector APeriapsisBubble::GetVesselCenter(int32 Vessel) const
{
	const UProceduralMeshComponent* Body = GetVesselBody(Vessel);
	return Body ? Body->GetComponentTransform().TransformPosition(CenterOfMassUnreal(Vessel)) : FVector::ZeroVector;
}

bool APeriapsisBubble::IsSimulating(int32 Vessel) const
{
	const UProceduralMeshComponent* Body = GetVesselBody(Vessel);
	return Body && Body->IsSimulatingPhysics();
}

Chaos::FRigidBodyHandle_Internal* APeriapsisBubble::GetHandle(int32 Vessel) const
{
	const UProceduralMeshComponent* Mesh = GetVesselBody(Vessel);
	const FBodyInstance* Body = Mesh ? Mesh->GetBodyInstance() : nullptr;
	Chaos::FSingleParticlePhysicsProxy* Proxy = Body ? Body->GetPhysicsActor() : nullptr;
	return Proxy ? Proxy->GetPhysicsThreadAPI() : nullptr;
}

// IPeriapsisPhysicsScene. Called from the physics step: bodies are only created and removed
// on the next game-thread tick, and state goes through the physics-thread handles.

void APeriapsisBubble::AddVessel(int32 Vessel, const Periapsis::Core::Vessel& Shape)
{
	PendingRemovals.Remove(Vessel);
	PendingAdds.Add(Vessel, Shape);
	PendingMasses.Remove(Vessel);
}

void APeriapsisBubble::RemoveVessel(int32 Vessel)
{
	PendingAdds.Remove(Vessel);
	PendingRemovals.Add(Vessel);
	PendingMasses.Remove(Vessel);
}

bool APeriapsisBubble::IsReady(int32 Vessel) const
{
	if (PendingAdds.Contains(Vessel) || PendingRemovals.Contains(Vessel))
	{
		return false;
	}
	const Chaos::FRigidBodyHandle_Internal* Handle = GetHandle(Vessel);
	return Handle && (Handle->ObjectState() == Chaos::EObjectStateType::Dynamic || Handle->ObjectState() == Chaos::EObjectStateType::Sleeping);
}

StateVector APeriapsisBubble::GetState(int32 Vessel) const
{
	const Chaos::FRigidBodyHandle_Internal* Handle = GetHandle(Vessel);
	if (!Handle)
	{
		return {};
	}
	// The body's position is the vessel frame's origin; the universe tracks the centre of mass.
	const FVector Center = FVector(Handle->X()) + FQuat(Handle->R()).RotateVector(CenterOfMassUnreal(Vessel));
	return {FUnitBridge::ToSI(Center), FUnitBridge::ToSI(FVector(Handle->V()))};
}

void APeriapsisBubble::SetState(int32 Vessel, const StateVector& Local)
{
	if (Chaos::FRigidBodyHandle_Internal* Handle = GetHandle(Vessel))
	{
		Handle->SetX(FUnitBridge::ToUnreal(Local.Position) - FQuat(Handle->R()).RotateVector(CenterOfMassUnreal(Vessel)));
		Handle->SetV(FUnitBridge::ToUnreal(Local.Velocity));
	}
}

void APeriapsisBubble::SetMass(int32 Vessel, double MassKg, const Vec3& CenterOfMass)
{
	PendingMasses.Add(Vessel, MassKg);
	if (CentersOfMass.IsValidIndex(Vessel) && !PendingAdds.Contains(Vessel))
	{
		// The tanks drain and the centre of mass moves through the structure, which stays put.
		CentersOfMass[Vessel] = CenterOfMass;
	}
}

void APeriapsisBubble::SetAcceleration(int32 Vessel, const Vec3& Acceleration, bool bInContact)
{
	if (Chaos::FRigidBodyHandle_Internal* Handle = GetHandle(Vessel))
	{
		if (Handle->ObjectState() == Chaos::EObjectStateType::Sleeping)
		{
			Handle->SetObjectState(Chaos::EObjectStateType::Dynamic);
		}
		// Chaos steps with semi-implicit Euler (v += a dt, then x += v dt), which overshoots the
		// position by a dt^2 / 2 every step under a constant acceleration: about 0.7 m over a
		// 30 s burn. Starting the step that much behind makes the step exact.
		const FVector AccelerationUnreal = FUnitBridge::ToUnreal(Acceleration);
		if (!bInContact)
		{
			// (In contact the solver moves the body; shifting it would show up as sliding.)
			Handle->SetX(Handle->X() - AccelerationUnreal * (0.5 * StepDt * StepDt));
		}
		Handle->AddForce(AccelerationUnreal * Handle->M());
		// Infinite inertia: contacts never turn a vessel, only SetAttitude does. Without it a
		// hull held upright fights the solver on slopes and creeps, or tips over and rolls.
		// (Mass changes from the game thread recompute the inertia, so it is cleared every step.)
		Handle->SetInvI(Chaos::TVec3<Chaos::FRealSingle>(0.0f));
		Handle->SetW(FVector::ZeroVector);
	}
}

void APeriapsisBubble::SetFrozen(int32 Vessel, bool bFrozen)
{
	if (Chaos::FRigidBodyHandle_Internal* Handle = GetHandle(Vessel))
	{
		if (bFrozen)
		{
			Handle->SetV(FVector::ZeroVector);
			Handle->SetW(FVector::ZeroVector);
		}
		Handle->SetObjectState(bFrozen ? Chaos::EObjectStateType::Sleeping : Chaos::EObjectStateType::Dynamic);
	}
}

bool APeriapsisBubble::HasGround() const
{
	// Only while the patch was built for this very frame and still lies under the ship.
	const FPeriapsisUniverse* Universe = AttachedUniverse;
	if (!bGroundReady || !Universe || GroundUniverse != Universe || GroundRebases != Universe->GetRebaseCount()
		|| GroundFrameChanges != Universe->GetFrameChanges())
	{
		return false;
	}
	const Vec3 Active = Universe->VesselLocalState(FPeriapsisUniverse::ActiveVessel).Position;
	const double Reach = 0.5 * GroundSpacing * (GroundResolution - 1) - 10.0;
	return FMath::Abs(Active.X - GroundCenter.X) < Reach && FMath::Abs(Active.Y - GroundCenter.Y) < Reach;
}

void APeriapsisBubble::SetAttitude(int32 Vessel, const Mat3& Attitude)
{
	if (Chaos::FRigidBodyHandle_Internal* Handle = GetHandle(Vessel))
	{
		// Perfect attitude hold (no reaction wheels or torques yet), turning around the centre of
		// mass so the vessel does not move as it turns.
		const FVector Offset = CenterOfMassUnreal(Vessel);
		const FVector Center = FVector(Handle->X()) + FQuat(Handle->R()).RotateVector(Offset);
		const FQuat Rotation = FUnitBridge::RotationToUnreal(Attitude);
		Handle->SetR(Rotation);
		Handle->SetX(Center - Rotation.RotateVector(Offset));
		Handle->SetW(FVector::ZeroVector);
	}
}
