#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PeriapsisCore/Vec3.h"
#include "PeriapsisPhysicsScene.h"
#include "PeriapsisBubble.generated.h"

class FPeriapsisUniverse;
class UStaticMesh;
namespace Chaos
{
	class FRigidBodyHandle_Internal;
}
class UStaticMeshComponent;

// The physics bubble on Chaos (M3): one mesh per vessel, simulated by Chaos while the vessel
// is in the bubble and placed on its rails otherwise.
//
// Chaos runs at a fixed 120 Hz on its own clock. Before each step, AsyncPhysicsTickActor
// hands control to the universe (FPeriapsisUniverse::PhysicsStep), which reads and writes
// the bodies through IPeriapsisPhysicsScene on the physics-thread side. What is drawn is
// interpolated by Chaos a little behind the last step; GetRenderTime() says which universe
// time that is, so the rest of the scene is drawn at the same moment.
UCLASS()
class PERIAPSISGAME_API APeriapsisBubble : public AActor, public IPeriapsisPhysicsScene
{
	GENERATED_BODY()

public:
	APeriapsisBubble();

	virtual void Tick(float DeltaSeconds) override;
	virtual void AsyncPhysicsTickActor(float DeltaTime, float SimTime) override;

	// IPeriapsisPhysicsScene
	virtual void AddVessel(int32 Vessel, double MassKg) override;
	virtual void RemoveVessel(int32 Vessel) override;
	virtual bool IsReady(int32 Vessel) const override;
	virtual Periapsis::Core::StateVector GetState(int32 Vessel) const override;
	virtual void SetState(int32 Vessel, const Periapsis::Core::StateVector& Local) override;
	virtual void SetMass(int32 Vessel, double MassKg) override;
	virtual void SetAcceleration(int32 Vessel, const Periapsis::Core::Vec3& Acceleration) override;
	virtual void SetAttitude(int32 Vessel, const Periapsis::Core::Vec3& Forward) override;

	// The universe time of what is on screen: behind the simulation while Chaos interpolates.
	double GetRenderTime() const;
	// Where the world origin is, in the root body's frame, for what is on screen: everything
	// that is not a simulated mesh is drawn relative to it.
	Periapsis::Core::Vec3 GetRenderOrigin() const;
	// Places the meshes of the vessels on rails, relative to the render origin.
	void PlaceRailsVessels();
	UStaticMeshComponent* GetVesselMesh(int32 Vessel) const;
	bool IsSimulating(int32 Vessel) const;

	// Size of the placeholder meshes, in metres: a 2 x 6 m cylinder for the active vessel,
	// a 3 m cube for the others (parts come in M5).
	static constexpr double ShipDiameter = 2.0;
	static constexpr double ShipLength = 6.0;
	static constexpr double OtherSize = 3.0;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FPeriapsisUniverse* GetUniverse() const;
	void EnsureVesselMeshes(const FPeriapsisUniverse& Universe);
	Chaos::FRigidBodyHandle_Internal* GetHandle(int32 Vessel) const;
	double ResultsTime() const;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> VesselMeshes;

	// Changes requested from the physics step, applied on the next game-thread tick: bodies
	// are not created or destroyed while Chaos is stepping.
	TMap<int32, double> PendingAdds; // vessel, mass in kg
	TSet<int32> PendingRemovals;
	TMap<int32, double> PendingMasses;

	// The universe this scene is attached to.
	FPeriapsisUniverse* AttachedUniverse = nullptr;
	// Chaos's clock (start of the step being run) and the universe time it matched.
	double SimClock = 0.0;
	bool bSimClockStarted = false;
	double UniverseMinusSim = 0.0;
	// Universe time when the bubble last started: nothing earlier is on screen from it.
	double BubbleStartTime = 0.0;
	// Length of the step being prepared.
	double StepDt = 0.0;
};
