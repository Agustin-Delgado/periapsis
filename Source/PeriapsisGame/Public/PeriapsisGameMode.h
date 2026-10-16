#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PeriapsisGameMode.generated.h"

// Orbital prototype (M2) with its physics bubble (M3). Works on an empty map: everything it
// needs is spawned from code.
UCLASS()
class PERIAPSISGAME_API APeriapsisGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APeriapsisGameMode();

	// No player starts needed: the universe view moves the camera onto its focus.
	virtual void RestartPlayer(AController* NewPlayer) override;

protected:
	virtual void BeginPlay() override;
};
