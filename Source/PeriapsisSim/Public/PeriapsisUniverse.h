#pragma once

#include "CoreMinimal.h"
#include "PeriapsisDataLoader.h"
#include "PeriapsisCore/Flight.h"

// The running simulation: the celestial system, the active ship and the clock.
// Plain C++ so it can be tested without a world; UPeriapsisFlightSubsystem owns one.
class PERIAPSISSIM_API FPeriapsisUniverse
{
public:
	// Time warp levels, as multiples of real time.
	static constexpr double WarpLevels[] = {1.0, 2.0, 4.0, 10.0, 50.0, 100.0, 1000.0, 10000.0, 100000.0};
	static constexpr int32 NumWarpLevels = UE_ARRAY_COUNT(WarpLevels);
	// Highest warp while the engine burns (the integrator runs every sub-step).
	static constexpr int32 MaxPoweredWarpLevel = 2; // 4x
	// Warp is lowered so that an SOI change or an impact never comes in fewer frames than this.
	static constexpr double MinFramesToEvent = 8.0;

	explicit FPeriapsisUniverse(FPeriapsisScenario InScenario);
	// The flight keeps a reference to the system stored in this object: it must not move.
	FPeriapsisUniverse(const FPeriapsisUniverse&) = delete;
	FPeriapsisUniverse& operator=(const FPeriapsisUniverse&) = delete;
	FPeriapsisUniverse(FPeriapsisUniverse&&) = delete;
	FPeriapsisUniverse& operator=(FPeriapsisUniverse&&) = delete;

	// Advances by RealDeltaSeconds times the current warp, after clamping the warp.
	void Tick(double RealDeltaSeconds);
	void Restart();

	// Controls.
	void SetThrottle(double Throttle);
	void AddThrottle(double Delta) { SetThrottle(ActiveFlight->Throttle() + Delta); }
	void SetAttitude(Periapsis::Core::AttitudeMode Mode) { Attitude = Mode; }
	void IncreaseWarp();
	void DecreaseWarp();
	void ResetWarp() { WarpLevel = 0; }

	// State for the view and the HUD.
	Periapsis::Core::Flight& GetFlight() { return *ActiveFlight; }
	const Periapsis::Core::Flight& GetFlight() const { return *ActiveFlight; }
	const Periapsis::Core::CelestialSystem& GetSystem() const { return Scenario.SystemData.System; }
	const FPeriapsisBodyInfo& GetBodyInfo(int32 Index) const { return Scenario.SystemData.Bodies[Index]; }
	int32 NumBodies() const { return Scenario.SystemData.Bodies.Num(); }
	const FString& GetShipName() const { return Scenario.ShipName; }
	Periapsis::Core::AttitudeMode GetAttitude() const { return Attitude; }
	int32 GetWarpLevel() const { return WarpLevel; }
	double GetWarp() const { return WarpLevels[WarpLevel]; }
	double GetTime() const { return ActiveFlight->Time(); }
	// Why the warp was lowered in the last tick, empty if it was not.
	const FString& GetWarpLimitReason() const { return WarpLimitReason; }

	// Positions in the frame of the root body, the common frame of the view.
	Periapsis::Core::Vec3 BodyPosition(int32 BodyIndex) const;
	Periapsis::Core::Vec3 ShipPosition() const;
	// Time until the next SOI change or impact on the predicted trajectory (infinite if none).
	double TimeToNextEvent();

private:
	void ClampWarp(double RealDeltaSeconds);

	FPeriapsisScenario Scenario;
	TUniquePtr<Periapsis::Core::Flight> ActiveFlight;
	Periapsis::Core::AttitudeMode Attitude = Periapsis::Core::AttitudeMode::Prograde;
	int32 WarpLevel = 0;
	FString WarpLimitReason;
};
