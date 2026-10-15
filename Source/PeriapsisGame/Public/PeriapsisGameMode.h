#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PeriapsisGameMode.generated.h"

// Orbital prototype (M2). Works on an empty map: everything it needs is spawned from code.
UCLASS()
class PERIAPSISGAME_API APeriapsisGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APeriapsisGameMode();

	// No player starts needed: the map camera always sits at the origin.
	virtual void RestartPlayer(AController* NewPlayer) override;

protected:
	virtual void BeginPlay() override;
};
