#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PeriapsisCore/LocalFrame.h"
#include "PeriapsisCore/Vec3.h"
#include "PeriapsisPhysicsScene.h"
#include "PeriapsisBubble.generated.h"

class FPeriapsisUniverse;
class UStaticMesh;
namespace Chaos
{
	class FRigidBodyHandle_Internal;
}
class UProceduralMeshComponent;
class UStaticMeshComponent;

// How the simulation maps to the Unreal world for what is on screen this frame. The world is
// the physics bubble's local frame (or, on rails, the root frame centred on the ship), at the
// moment Chaos is showing, shifted so it lines up with where Chaos draws the ship.
struct PERIAPSISGAME_API FPeriapsisViewTransform
{
	double Time = 0.0;
	Periapsis::Core::LocalFrame Frame;  // at Time; invalid on rails
	Periapsis::Core::Vec3 FrameBodyRoot; // where the frame's body is in the root frame, at Time
	Periapsis::Core::Vec3 Correction;    // local, added after the frame
	Periapsis::Core::Vec3 RailsOrigin;   // on rails: the root position at the world origin

	FVector ToWorld(const Periapsis::Core::Vec3& RootPosition) const;
	Periapsis::Core::Vec3 ToRoot(const FVector& World) const;
	FVector DirectionToWorld(const Periapsis::Core::Vec3& Inertial) const;
	// Rotation from a body's fixed frame (in Unreal axes, mirrored like everything else) to the world.
	FQuat BodyFixedToWorld(const Periapsis::Core::CelestialBody& Body) const;
};

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
	virtual void SetAcceleration(int32 Vessel, const Periapsis::Core::Vec3& Acceleration, bool bInContact) override;
	virtual void SetAttitude(int32 Vessel, const Periapsis::Core::Vec3& Forward) override;
	virtual void SetFrozen(int32 Vessel, bool bFrozen) override;
	virtual bool HasGround() const override;

	// The universe time of what is on screen: behind the simulation while Chaos interpolates.
	double GetRenderTime() const;
	// How to draw everything that is not a simulated mesh, this frame.
	FPeriapsisViewTransform GetViewTransform() const;
	// Places the meshes of the vessels on rails.
	void PlaceRailsVessels(const FPeriapsisViewTransform& View);
	UStaticMeshComponent* GetVesselMesh(int32 Vessel) const;
	bool IsSimulating(int32 Vessel) const;


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

	// Collision ground under the vessels near the surface (body-fixed frame only), hidden.
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> GroundPatch;
	void UpdateGroundPatch(FPeriapsisUniverse& Universe);
	bool bGroundReady = false;
	Periapsis::Core::Vec3 GroundCenter;  // local
	int32 GroundRebases = -1;            // rebases and frame changes it was built for
	int32 GroundFrameChanges = -1;
	FPeriapsisUniverse* GroundUniverse = nullptr;

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
