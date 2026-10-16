#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PeriapsisCore/Vec3.h"
#include "PeriapsisUniverseView.generated.h"

class APeriapsisBubble;
class FPeriapsisUniverse;
class UDirectionalLightComponent;
class UPostProcessComponent;
class UStaticMesh;
class UStaticMeshComponent;

namespace Periapsis::Core
{
	class Flight;
}

// Draws the universe around the physics bubble. The world origin is the bubble's origin,
// where Chaos simulates the vessels near the ship (APeriapsisBubble); bodies, orbits and the
// vessels on rails are drawn relative to it, at the moment Chaos is showing. Precision never
// depends on how far from the root body the action is. Bodies are spheres; trajectories and
// markers are debug lines (M2 prototype, the map view of the spec). The camera pawn is moved
// onto the focus (a vessel or a body) every frame.
UCLASS()
class PERIAPSISGAME_API APeriapsisUniverseView : public AActor
{
	GENERATED_BODY()

public:
	APeriapsisUniverseView();

	virtual void Tick(float DeltaSeconds) override;

	// Focus order: the vessels (the ship first), then the bodies.
	void CycleFocus();
	FString GetFocusName() const;
	// Radius of what is in focus, in Unreal units: the camera stays outside it.
	double GetFocusRadiusUnreal() const;
	bool IsFocusOnShip() const { return Focus == 0; }

protected:
	virtual void BeginPlay() override;

private:
	FPeriapsisUniverse* GetUniverse() const;
	APeriapsisBubble* GetBubble() const;
	void EnsureBodyMeshes(const FPeriapsisUniverse& Universe);
	void DrawBodyOrbits(const FPeriapsisUniverse& Universe, const Periapsis::Core::Vec3& Origin, double Time) const;
	void DrawVessels(FPeriapsisUniverse& Universe, const Periapsis::Core::Vec3& Origin, double Time) const;
	void DrawTrajectory(const FPeriapsisUniverse& Universe, Periapsis::Core::Flight& Flight, const Periapsis::Core::Vec3& Origin,
		double Time, bool bActive) const;
	FVector VesselLocation(const FPeriapsisUniverse& Universe, int32 Vessel, const Periapsis::Core::Vec3& Origin, double Time) const;
	FVector FocusLocation(const FPeriapsisUniverse& Universe, const Periapsis::Core::Vec3& Origin, double Time) const;
	// Converts a position in the root frame to the view, relative to Origin.
	static FVector ToView(const Periapsis::Core::Vec3& RootPosition, const Periapsis::Core::Vec3& Origin);
	double ScreenScale(const FVector& At) const;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BodyMaterial;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> BodyMeshes;

	UPROPERTY()
	TObjectPtr<UDirectionalLightComponent> SunLight;

	UPROPERTY()
	TObjectPtr<UPostProcessComponent> Exposure;

	mutable TWeakObjectPtr<APeriapsisBubble> Bubble;
	int32 Focus = 0;
};
