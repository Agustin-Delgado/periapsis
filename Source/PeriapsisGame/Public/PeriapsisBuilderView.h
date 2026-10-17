#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PeriapsisBuilder.h"
#include "PeriapsisBuilderView.generated.h"

class UDirectionalLightComponent;
class UProceduralMeshComponent;
class UPeriapsisFlightSubsystem;

// The builder screen (spec 8.2): the design drawn at the world origin with the part about to be
// attached in green, the target marked, and the centre of mass. The universe is hidden while it
// is open. Input comes from the player controller and the readout from the HUD; the editing
// itself is FPeriapsisBuilder.
UCLASS()
class PERIAPSISGAME_API APeriapsisBuilderView : public AActor
{
	GENERATED_BODY()

public:
	APeriapsisBuilderView();

	virtual void Tick(float DeltaSeconds) override;

	// Null until the builder is first opened.
	FPeriapsisBuilder* GetBuilder() const { return Builder.Get(); }
	// Call after any edit: redraws the design.
	void MarkChanged() { bDirty = true; }
	// Fits the camera to the design again on the next tick.
	void RefitCamera() { FramedRadius = 0.0; }

	// New design: a command pod on its own.
	void NewDesign();
	// Saves to Saved/Designs/<name>.json; returns the path, empty on failure.
	FString Save();
	// Loads the next design from Data/vessels and Saved/Designs, in turn.
	void LoadNext();
	// Size of the design around its centre of mass, in Unreal units (for the camera).
	double GetRadiusUnreal() const;

private:
	UPeriapsisFlightSubsystem* GetSubsystem() const;
	void EnsureBuilder();
	void Rebuild();

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> DesignMesh;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> PreviewMesh;

	UPROPERTY()
	TObjectPtr<UDirectionalLightComponent> Light;

	// From the other side, dimmer: the shadowed side of the rocket stays readable.
	UPROPERTY()
	TObjectPtr<UDirectionalLightComponent> FillLight;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Material;

	TUniquePtr<FPeriapsisBuilder> Builder;
	bool bDirty = true;
	int32 LastSelectedPart = INDEX_NONE;
	int32 LastTarget = INDEX_NONE;
	double LastSideAngle = 0.0;
	double LastSideHeight = 0.0;
	int32 LastSymmetry = 0;
	int32 NextDesignToLoad = 0;
	double FramedRadius = 0.0; // m: the size the camera distance was last fitted to
};
