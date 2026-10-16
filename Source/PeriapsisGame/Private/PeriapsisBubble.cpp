#include "PeriapsisBubble.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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
	// The engine's basic shapes are 1 m across.
	constexpr double BasicShapeSize = 1.0;
	const FLinearColor ShipColor(0.85f, 0.85f, 0.9f);
	const FLinearColor OtherColor(0.8f, 0.45f, 0.2f);

	FQuat NoseAlong(const FVector& Forward)
	{
		// The cylinder's axis is its local Z.
		return FRotationMatrix::MakeFromZ(Forward).ToQuat();
	}

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

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CylinderMesh = Cylinder.Object;
	CubeMesh = Cube.Object;
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

void APeriapsisBubble::EnsureVesselMeshes(const FPeriapsisUniverse& Universe)
{
	if (VesselMeshes.Num() == Universe.NumVessels())
	{
		return;
	}
	for (UStaticMeshComponent* Mesh : VesselMeshes)
	{
		if (Mesh)
		{
			Mesh->DestroyComponent();
		}
	}
	VesselMeshes.Reset();
	PendingAdds.Reset();
	PendingRemovals.Reset();
	PendingMasses.Reset();

	for (int32 Index = 0; Index < Universe.NumVessels(); ++Index)
	{
		const bool bShip = Index == FPeriapsisUniverse::ActiveVessel;
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Vessel_%d"), Index));
		Mesh->SetupAttachment(RootComponent);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetUsingAbsoluteLocation(true);
		Mesh->SetUsingAbsoluteRotation(true);
		// Placeholder hulls from the scenario: a cylinder for the ship, cubes for the others
		// (parts come in M5).
		const FPeriapsisVessel& Vessel = Universe.GetVessel(Index);
		Mesh->SetStaticMesh(bShip ? CylinderMesh : CubeMesh);
		Mesh->SetWorldScale3D(FVector(Vessel.Width, Vessel.Width, Vessel.Length) / BasicShapeSize);
		if (Material)
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, Mesh);
			Instance->SetVectorParameterValue(TEXT("Color"), bShip ? ShipColor : OtherColor);
			Mesh->SetMaterial(0, Instance);
		}
		// Chaos gets no gravity, damping or sleep of its own: the universe gives every
		// acceleration, and a vessel coasting with the frame is nearly still but not resting.
		Mesh->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetEnableGravity(false);
		Mesh->SetLinearDamping(0.0f);
		Mesh->SetAngularDamping(0.0f);
		Mesh->BodyInstance.SleepFamily = ESleepFamily::Custom;
		Mesh->BodyInstance.CustomSleepThresholdMultiplier = 0.0f;
		Mesh->RegisterComponent();
		VesselMeshes.Add(Mesh);
	}
}

void APeriapsisBubble::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FPeriapsisUniverse* Universe = GetUniverse();
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
	EnsureVesselMeshes(*Universe);
	UpdateGroundPatch(*Universe);

	for (const int32 Vessel : PendingRemovals)
	{
		UStaticMeshComponent* Mesh = VesselMeshes[Vessel];
		Mesh->SetSimulatePhysics(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	PendingRemovals.Reset();

	for (const TPair<int32, double>& Mass : PendingMasses)
	{
		UStaticMeshComponent* Mesh = VesselMeshes[Mass.Key];
		// Mass only matters to collisions: follow the fuel in 1 % steps, not every frame.
		if (FMath::Abs(Mesh->GetMass() - Mass.Value) > 0.01 * Mass.Value)
		{
			Mesh->SetMassOverrideInKg(NAME_None, static_cast<float>(Mass.Value), true);
		}
	}
	PendingMasses.Reset();

	for (const TPair<int32, double>& Add : PendingAdds)
	{
		UStaticMeshComponent* Mesh = VesselMeshes[Add.Key];
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->SetMassOverrideInKg(NAME_None, static_cast<float>(Add.Value), true);
		Mesh->SetSimulatePhysics(true);
		Mesh->SetEnableGravity(false);
		Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
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
		View.Correction = FUnitBridge::ToSI(VesselMeshes[FPeriapsisUniverse::ActiveVessel]->GetComponentLocation()) - ShipLocal;
	}
	return View;
}

void APeriapsisBubble::PlaceRailsVessels(const FPeriapsisViewTransform& View)
{
	const FPeriapsisUniverse* Universe = GetUniverse();
	if (!Universe || VesselMeshes.Num() != Universe->NumVessels())
	{
		return;
	}
	for (int32 Index = 0; Index < VesselMeshes.Num(); ++Index)
	{
		if (!IsSimulating(Index))
		{
			const FVector Location = View.ToWorld(Universe->VesselPositionAt(Index, View.Time));
			const FVector Nose = View.DirectionToWorld(Universe->GetVessel(Index).Flight->ThrustDirection());
			VesselMeshes[Index]->SetWorldLocationAndRotation(Location, NoseAlong(Nose), false, nullptr, ETeleportType::TeleportPhysics);
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

UStaticMeshComponent* APeriapsisBubble::GetVesselMesh(int32 Vessel) const
{
	return VesselMeshes.IsValidIndex(Vessel) ? VesselMeshes[Vessel].Get() : nullptr;
}

bool APeriapsisBubble::IsSimulating(int32 Vessel) const
{
	const UStaticMeshComponent* Mesh = GetVesselMesh(Vessel);
	return Mesh && Mesh->IsSimulatingPhysics();
}

Chaos::FRigidBodyHandle_Internal* APeriapsisBubble::GetHandle(int32 Vessel) const
{
	const UStaticMeshComponent* Mesh = GetVesselMesh(Vessel);
	const FBodyInstance* Body = Mesh ? Mesh->GetBodyInstance() : nullptr;
	Chaos::FSingleParticlePhysicsProxy* Proxy = Body ? Body->GetPhysicsActor() : nullptr;
	return Proxy ? Proxy->GetPhysicsThreadAPI() : nullptr;
}

// IPeriapsisPhysicsScene. Called from the physics step: bodies are only created and removed
// on the next game-thread tick, and state goes through the physics-thread handles.

void APeriapsisBubble::AddVessel(int32 Vessel, double MassKg)
{
	PendingRemovals.Remove(Vessel);
	PendingAdds.Add(Vessel, MassKg);
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
	return {FUnitBridge::ToSI(FVector(Handle->X())), FUnitBridge::ToSI(FVector(Handle->V()))};
}

void APeriapsisBubble::SetState(int32 Vessel, const StateVector& Local)
{
	if (Chaos::FRigidBodyHandle_Internal* Handle = GetHandle(Vessel))
	{
		Handle->SetX(FUnitBridge::ToUnreal(Local.Position));
		Handle->SetV(FUnitBridge::ToUnreal(Local.Velocity));
	}
}

void APeriapsisBubble::SetMass(int32 Vessel, double MassKg)
{
	PendingMasses.Add(Vessel, MassKg);
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

void APeriapsisBubble::SetAttitude(int32 Vessel, const Vec3& Forward)
{
	if (Chaos::FRigidBodyHandle_Internal* Handle = GetHandle(Vessel))
	{
		// Perfect attitude hold until vessels have parts and reaction wheels (M5).
		Handle->SetR(NoseAlong(FUnitBridge::DirectionToUnreal(Forward)));
		Handle->SetW(FVector::ZeroVector);
	}
}
