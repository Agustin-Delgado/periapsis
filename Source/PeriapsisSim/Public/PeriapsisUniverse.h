#pragma once

#include "CoreMinimal.h"
#include "PeriapsisDataLoader.h"
#include "PeriapsisCore/Flight.h"
#include "PeriapsisCore/LocalFrame.h"

class IPeriapsisPhysicsScene;

// A vessel of the universe: its flight, and its place in the physics bubble.
struct PERIAPSISSIM_API FPeriapsisVessel
{
	FString Name;
	TUniquePtr<Periapsis::Core::Flight> Flight;
	bool bInBubble = false; // has a body in the physics scene
	bool bPlaced = false;   // and that body is simulating, with the vessel's state
	bool bFrozen = false;   // landed: its body rests in the scene without being simulated
	bool bHeldByGround = false; // standing on the analytic ground (the scene has no terrain)
	bool bTouchingGround = false; // within the ground tolerance on the last step
	double RestTime = 0.0;     // s spent still on the ground
	double LastSinkSpeed = 0.0; // m/s towards the ground on the previous step

	// From the centre of mass down to the lowest point of the vessel, straight down towards the
	// body as it is turned now.
	double GroundClearance() const;
	// Radius of a sphere around the centre of mass that holds the vessel.
	double Size() const { return Flight->GetVessel().BoundingRadius(); }
};

// The running simulation: the celestial system, the vessels and the clock.
// Plain C++ so it can be tested without a world; UPeriapsisFlightSubsystem owns one.
//
// Two regimes (spec 3.3). On rails, Tick() moves every vessel along its conics. In the
// physics bubble, which needs a physics scene (SetPhysicsScene) and a warp of at most
// MaxPhysicsWarpLevel, the physics engine moves the active vessel and the vessels near it,
// and the clock advances with its steps (PhysicsStep) instead of with Tick().
class PERIAPSISSIM_API FPeriapsisUniverse
{
public:
	// Time warp levels, as multiples of real time.
	static constexpr double WarpLevels[] = {1.0, 2.0, 4.0, 10.0, 50.0, 100.0, 1000.0, 10000.0, 100000.0};
	static constexpr int32 NumWarpLevels = UE_ARRAY_COUNT(WarpLevels);
	// Highest warp while the engine burns (the integrator runs every sub-step).
	static constexpr int32 MaxPoweredWarpLevel = 2; // 4x
	// Highest warp of the physics bubble; above it everything goes on rails.
	static constexpr int32 MaxPhysicsWarpLevel = 2; // 4x
	// Warp is lowered so that an SOI change or an impact never comes in fewer frames than this.
	static constexpr double MinFramesToEvent = 8.0;
	// Touching the ground faster than this destroys a vessel (no landing legs yet).
	static constexpr double CrashSpeed = 12.0; // m/s
	// A vessel this still on the ground for this long goes to sleep as landed (spec 5.10).
	static constexpr double RestSpeed = 0.05;   // m/s, relative to the ground
	static constexpr double RestDuration = 1.0; // s
	// Contact with the ground is judged with this slack: the collision mesh is not the
	// analytic terrain exactly.
	static constexpr double GroundTolerance = 0.5; // m
	// The bubble leaves the body-fixed frame this far above the altitude where it enters it.
	static constexpr double RotatingFrameExitFactor = 1.25;
	static constexpr int32 ActiveVessel = 0;

	explicit FPeriapsisUniverse(FPeriapsisScenario InScenario);
	~FPeriapsisUniverse();
	// The flights keep a reference to the system stored in this object: it must not move.
	FPeriapsisUniverse(const FPeriapsisUniverse&) = delete;
	FPeriapsisUniverse& operator=(const FPeriapsisUniverse&) = delete;
	FPeriapsisUniverse(FPeriapsisUniverse&&) = delete;
	FPeriapsisUniverse& operator=(FPeriapsisUniverse&&) = delete;

	// Once per frame. On rails, advances by RealDeltaSeconds times the warp, after clamping
	// the warp. In the bubble the physics steps advance the clock, and this only decides
	// whether the bubble should stay.
	void Tick(double RealDeltaSeconds);
	void Restart();
	// Goes up on every Restart(): whatever was built for the vessels before is stale.
	int32 GetGeneration() const { return Generation; }

	// The physics engine that runs the bubble; null (the default) keeps everything on rails.
	// The scene must outlive its use here, or be detached first.
	void SetPhysicsScene(IPeriapsisPhysicsScene* InScene);
	// Called by the physics engine right before it integrates a step of Dt seconds.
	void PhysicsStep(double Dt);
	bool IsPhysicsActive() const { return bPhysicsActive; }
	// The time scale the physics engine must run at: the warp while in the bubble.
	double GetPhysicsTimeDilation() const;
	const Periapsis::Core::LocalFrame& GetLocalFrame() const { return Frame; }
	// A vessel's state in the local frame now (on rails too: then it is only informative).
	Periapsis::Core::StateVector VesselLocalState(int32 Index) const;
	const Periapsis::Core::BubbleSettings& GetBubbleSettings() const { return Bubble; }
	int32 GetRebaseCount() const { return RebaseCount; }
	// How many times the bubble switched between free-falling and body-fixed (or of body).
	int32 GetFrameChanges() const { return FrameChanges; }

	// Controls.
	void SetThrottle(double Throttle);
	void AddThrottle(double Delta) { SetThrottle(GetFlight().Throttle() + Delta); }
	void SetAttitude(Periapsis::Core::AttitudeMode Mode) { Attitude = Mode; }
	// Fires the active vessel's next stage (spec 5.5). Pieces that come off become vessels.
	void Stage();
	void IncreaseWarp();
	void DecreaseWarp();
	void ResetWarp() { WarpLevel = 0; }

	// State for the view and the HUD.
	Periapsis::Core::Flight& GetFlight() { return *Vessels[ActiveVessel].Flight; }
	const Periapsis::Core::Flight& GetFlight() const { return *Vessels[ActiveVessel].Flight; }
	int32 NumVessels() const { return Vessels.Num(); }
	const FPeriapsisVessel& GetVessel(int32 Index) const { return Vessels[Index]; }
	const Periapsis::Core::CelestialSystem& GetSystem() const { return Scenario.SystemData.System; }
	const FPeriapsisBodyInfo& GetBodyInfo(int32 Index) const { return Scenario.SystemData.Bodies[Index]; }
	int32 NumBodies() const { return Scenario.SystemData.Bodies.Num(); }
	const FString& GetShipName() const { return Vessels[ActiveVessel].Name; }
	const FPeriapsisScenario& GetScenario() const { return Scenario; }
	Periapsis::Core::AttitudeMode GetAttitude() const { return Attitude; }
	int32 GetWarpLevel() const { return WarpLevel; }
	double GetWarp() const { return WarpLevels[WarpLevel]; }
	double GetTime() const { return CurrentTime; }
	// Why the warp was lowered in the last tick, empty if it was not.
	const FString& GetWarpLimitReason() const { return WarpLimitReason; }

	// Positions in the frame of the root body, the common frame of the view.
	Periapsis::Core::Vec3 BodyPosition(int32 BodyIndex) const { return BodyPositionAt(BodyIndex, CurrentTime); }
	Periapsis::Core::Vec3 BodyPositionAt(int32 BodyIndex, double Time) const;
	Periapsis::Core::Vec3 ShipPosition() const { return VesselPosition(ActiveVessel); }
	Periapsis::Core::Vec3 VesselPosition(int32 Index) const;
	// A vessel's position at a time close to its flight's, following its current conic. For
	// drawing at the moment the physics engine shows, slightly behind the simulation.
	Periapsis::Core::Vec3 VesselPositionAt(int32 Index, double Time) const;
	// Distance between two vessels, in metres.
	double VesselDistance(int32 A, int32 B) const;
	// Height of a vessel's centre above the ground under it, in metres.
	double VesselAltitudeAboveTerrain(int32 Index) const;
	// Time until the next SOI change or impact on the predicted trajectory (infinite if none).
	double TimeToNextEvent();

private:
	void ClampWarp(double RealDeltaSeconds);
	bool WantsPhysics() const;
	void EnterPhysics();
	void LeavePhysics();
	void ReadPhysicsStates();
	void UpdateMembership();
	void ApplyPhysicsForces(double Dt);
	void UpdateFrameKind();
	void HandleGround(double Dt);
	void TakeOffIfThrottled();
	Periapsis::Core::Vec3 AttitudeFor(const Periapsis::Core::Flight& Flight) const;
	void RemoveFromBubble(FPeriapsisVessel& Vessel, int32 Index);
	// Turns pieces that came off any vessel into vessels; a vessel in the bubble that lost parts
	// gets a new body.
	void CollectSeparations();
	void RebuildInScene(int32 Index);
	int32 DebrisCount = 0;
	// The scene is only touched from the physics step: what staging asks for in between waits.
	bool bInPhysicsStep = false;
	TArray<int32> PendingRebuilds;
	bool bPendingUnfreeze = false;
	void ApplyPendingSceneChanges();
	int32 Generation = 0;
	// A vessel's state in the local frame's body.
	Periapsis::Core::StateVector StateInFrameBody(const FPeriapsisVessel& Vessel) const;

	FPeriapsisScenario Scenario;
	TArray<FPeriapsisVessel> Vessels;
	double CurrentTime = 0.0;
	Periapsis::Core::AttitudeMode Attitude = Periapsis::Core::AttitudeMode::Prograde;
	int32 WarpLevel = 0;
	FString WarpLimitReason;

	IPeriapsisPhysicsScene* Scene = nullptr;
	bool bPhysicsActive = false;
	Periapsis::Core::LocalFrame Frame;
	Periapsis::Core::BubbleSettings Bubble;
	int32 RebaseCount = 0;
	int32 FrameChanges = 0;
};
