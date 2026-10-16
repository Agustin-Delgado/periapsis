#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PeriapsisPlayerController.generated.h"

class APeriapsisBuilderView;
class APeriapsisUniverseView;
class FPeriapsisUniverse;

// M2 controls, read by polling keys (no input assets needed):
//   Shift / Ctrl     throttle up / down        Z / X    full / cut throttle
//   1..6             prograde, retrograde, normal, anti-normal, radial out, radial in
//   . / ,  /         warp up / down / back to 1x
//   right mouse drag rotate the camera        wheel    zoom
//   Tab              next focus (ship, bodies) R        restart          F1   help
//   Space            fire the next stage       B        open the builder (see HandleBuilderInput)
UCLASS()
class PERIAPSISGAME_API APeriapsisPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	APeriapsisPlayerController();

	virtual void PlayerTick(float DeltaTime) override;

	bool IsHelpVisible() const { return bShowHelp; }
	APeriapsisUniverseView* GetUniverseView() const;
	APeriapsisBuilderView* GetBuilderView() const;
	bool IsBuilding() const;
	FPeriapsisUniverse* GetUniverse() const;

protected:
	virtual void BeginPlay() override;

private:
	// Builder keys: Up/Down part, Left/Right target, Q/E turn and W/S slide along a side,
	// Y symmetry, Enter attach, Delete remove, [ / ] stage, T auto-stage, U undo, N new,
	// F5 save, O open the next design, L launch, B or Escape back to flight.
	void HandleBuilderInput();
	void OpenBuilder();
	FString LastSaved;
	// The camera distance of the flight, put back when the builder closes without a launch.
	bool bWasBuilding = false;
	double FlightDistance = 0.0;
	const FPeriapsisUniverse* UniverseBeforeBuilding = nullptr;

public:
	const FString& GetLastSaved() const { return LastSaved; }

private:

	bool bShowHelp = true;
};
