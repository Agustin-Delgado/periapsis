#include "PeriapsisBubble.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PeriapsisFlightSubsystem.h"
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

	FQuat NoseAlong(const Vec3& Forward)
	{
		// The cylinder's axis is its local Z.
		return FRotationMatrix::MakeFromZ(FUnitBridge::DirectionToUnreal(Forward)).ToQuat();
	}
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
		Mesh->SetStaticMesh(bShip ? CylinderMesh : CubeMesh);
		Mesh->SetWorldScale3D(bShip ? FVector(ShipDiameter, ShipDiameter, ShipLength) / BasicShapeSize : FVector(OtherSize / BasicShapeSize));
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

Vec3 APeriapsisBubble::GetRenderOrigin() const
{
	const FPeriapsisUniverse* Universe = GetUniverse();
	if (!Universe)
	{
		return {};
	}
	const Vec3 Active = Universe->VesselPositionAt(FPeriapsisUniverse::ActiveVessel, GetRenderTime());
	if (IsSimulating(FPeriapsisUniverse::ActiveVessel))
	{
		// Anchored to where Chaos shows the ship, however it interpolates (rebases included).
		return Active - FUnitBridge::ToSI(VesselMeshes[FPeriapsisUniverse::ActiveVessel]->GetComponentLocation());
	}
	return Active; // on rails the ship sits at the world origin
}

void APeriapsisBubble::PlaceRailsVessels()
{
	const FPeriapsisUniverse* Universe = GetUniverse();
	if (!Universe || VesselMeshes.Num() != Universe->NumVessels())
	{
		return;
	}
	const double Time = GetRenderTime();
	const Vec3 Origin = GetRenderOrigin();
	for (int32 Index = 0; Index < VesselMeshes.Num(); ++Index)
	{
		if (!IsSimulating(Index))
		{
			const FVector Location = FUnitBridge::ToUnreal(Universe->VesselPositionAt(Index, Time) - Origin);
			VesselMeshes[Index]->SetWorldLocationAndRotation(Location, NoseAlong(Universe->GetVessel(Index).Flight->ThrustDirection()),
				false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
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
	return Handle && Handle->ObjectState() == Chaos::EObjectStateType::Dynamic;
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

void APeriapsisBubble::SetAcceleration(int32 Vessel, const Vec3& Acceleration)
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
		Handle->SetX(Handle->X() - AccelerationUnreal * (0.5 * StepDt * StepDt));
		Handle->AddForce(AccelerationUnreal * Handle->M());
	}
}

void APeriapsisBubble::SetAttitude(int32 Vessel, const Vec3& Forward)
{
	if (Chaos::FRigidBodyHandle_Internal* Handle = GetHandle(Vessel))
	{
		// Perfect attitude hold until vessels have parts and reaction wheels (M5).
		Handle->SetR(NoseAlong(Forward));
		Handle->SetW(FVector::ZeroVector);
	}
}
