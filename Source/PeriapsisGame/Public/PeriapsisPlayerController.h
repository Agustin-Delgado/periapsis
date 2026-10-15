#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PeriapsisPlayerController.generated.h"

class APeriapsisUniverseView;
class FPeriapsisUniverse;

// M2 controls, read by polling keys (no input assets needed):
//   Shift / Ctrl     throttle up / down        Z / X    full / cut throttle
//   1..6             prograde, retrograde, normal, anti-normal, radial out, radial in
//   . / ,  /         warp up / down / back to 1x
//   right mouse drag rotate the camera        wheel    zoom
//   Tab              next focus (ship, bodies) R        restart          F1   help
UCLASS()
class PERIAPSISGAME_API APeriapsisPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	APeriapsisPlayerController();

	virtual void PlayerTick(float DeltaTime) override;

	bool IsHelpVisible() const { return bShowHelp; }
	APeriapsisUniverseView* GetUniverseView() const;
	FPeriapsisUniverse* GetUniverse() const;

protected:
	virtual void BeginPlay() override;

private:
	bool bShowHelp = true;
};
