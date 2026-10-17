#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PeriapsisCore/LocalFrame.h"
#include "PeriapsisCore/Parts.h"
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
class UMaterialInstanceDynamic;

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
	// A vessel's attitude (vessel frame to the inertial axes of the root frame) in the world.
	FQuat AttitudeToWorld(const Periapsis::Core::Mat3& Attitude) const;
};

// The colour of a piece of a part's look, by the part's category for its body.
PERIAPSISGAME_API FLinearColor PartColor(const FString& Category, Periapsis::Core::PartPaint Paint);

// Fills a procedural mesh with a vessel's parts: one section per colour, and a
// convex collision shape per part, in the vessel frame converted to Unreal units.
PERIAPSISGAME_API void BuildVesselMesh(UProceduralMeshComponent* Mesh, const Periapsis::Core::Vessel& Vessel, UMaterialInterface* Material,
	bool bCollision);
// The same for loose parts placed in a vessel frame, all in one colour when Tint is given.
PERIAPSISGAME_API void BuildPartsMesh(UProceduralMeshComponent* Mesh, const Periapsis::Core::PartCatalog& Catalog,
	const std::vector<Periapsis::Core::PartInstance>& Parts, UMaterialInterface* Material, bool bCollision,
	const FLinearColor* Tint = nullptr);

// The physics bubble on Chaos (M3): one body per vessel, simulated by Chaos while the vessel
// is in the bubble and placed on its rails otherwise. A body is the vessel's parts drawn and
// collided as one compound rigid body (spec 5.2, M5); its origin is the vessel frame's origin
// and the universe's states are those of its centre of mass, so the two are converted here.
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
	virtual void AddVessel(int32 Vessel, const Periapsis::Core::Vessel& Shape) override;
	virtual void RemoveVessel(int32 Vessel) override;
	virtual bool IsReady(int32 Vessel) const override;
	virtual Periapsis::Core::StateVector GetState(int32 Vessel) const override;
	virtual void SetState(int32 Vessel, const Periapsis::Core::StateVector& Local) override;
	virtual void SetMass(int32 Vessel, double MassKg, const Periapsis::Core::Vec3& CenterOfMass) override;
	virtual void SetAcceleration(int32 Vessel, const Periapsis::Core::Vec3& Acceleration, bool bInContact) override;
	virtual void SetAttitude(int32 Vessel, const Periapsis::Core::Mat3& Attitude) override;
	virtual void SetFrozen(int32 Vessel, bool bFrozen) override;
	virtual bool HasGround() const override;

	// The universe time of what is on screen: behind the simulation while Chaos interpolates.
	double GetRenderTime() const;
	// How to draw everything that is not a simulated mesh, this frame.
	FPeriapsisViewTransform GetViewTransform() const;
	// Places the meshes of the vessels on rails.
	void PlaceRailsVessels(const FPeriapsisViewTransform& View);
	UProceduralMeshComponent* GetVesselBody(int32 Vessel) const;
	// Where a vessel's centre of mass is drawn now.
	FVector GetVesselCenter(int32 Vessel) const;
	bool IsSimulating(int32 Vessel) const;


protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FPeriapsisUniverse* GetUniverse() const;
	void EnsureVesselBodies(const FPeriapsisUniverse& Universe);
	UProceduralMeshComponent* CreateVesselBody(int32 Index, const Periapsis::Core::Vessel& Vessel);
	// The centre of mass of a vessel's body, in its component's space (Unreal units).
	FVector CenterOfMassUnreal(int32 Vessel) const;
	Chaos::FRigidBodyHandle_Internal* GetHandle(int32 Vessel) const;
	double ResultsTime() const;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY()
	TArray<TObjectPtr<UProceduralMeshComponent>> VesselBodies;
	// Per vessel: centre of mass in the vessel frame (SI).
	TArray<Periapsis::Core::Vec3> CentersOfMass;
	TArray<int32> PartCounts; // of the shape each body was built with
	// The universe (and its restart) the bodies were built for.
	int32 BuiltGeneration = -1;
	const FPeriapsisUniverse* BuiltUniverse = nullptr;

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
	TMap<int32, Periapsis::Core::Vessel> PendingAdds; // the shape to build
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
