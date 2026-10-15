#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PeriapsisCore/Vec3.h"
#include "PeriapsisUniverseView.generated.h"

class FPeriapsisUniverse;
class UDirectionalLightComponent;
class UPostProcessComponent;
class UStaticMesh;
class UStaticMeshComponent;

// Draws the universe around a focus (the ship or a body), which is always placed at the
// world origin: every position is rendered relative to it, so precision never depends on
// how far from the root body the action is. Bodies are spheres; the ship, its predicted
// trajectory and the body orbits are debug lines (M2 prototype, the map view of the spec).
UCLASS()
class PERIAPSISGAME_API APeriapsisUniverseView : public AActor
{
	GENERATED_BODY()

public:
	APeriapsisUniverseView();

	virtual void Tick(float DeltaSeconds) override;

	// Focus index: -1 is the ship, otherwise a body index.
	void CycleFocus();
	int32 GetFocus() const { return Focus; }
	FString GetFocusName() const;
	// Radius of what is in focus, in Unreal units (0 for the ship): the camera stays outside.
	double GetFocusRadiusUnreal() const;

	static constexpr int32 ShipFocus = -1;

protected:
	virtual void BeginPlay() override;

private:
	FPeriapsisUniverse* GetUniverse() const;
	Periapsis::Core::Vec3 FocusPosition(const FPeriapsisUniverse& Universe) const;
	void EnsureBodyMeshes(const FPeriapsisUniverse& Universe);
	void DrawBodyOrbits(const FPeriapsisUniverse& Universe, const Periapsis::Core::Vec3& Origin) const;
	void DrawShip(FPeriapsisUniverse& Universe, const Periapsis::Core::Vec3& Origin) const;
	void DrawTrajectory(FPeriapsisUniverse& Universe, const Periapsis::Core::Vec3& Origin) const;
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

	int32 Focus = ShipFocus;
};
