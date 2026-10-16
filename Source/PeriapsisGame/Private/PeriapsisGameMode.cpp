#include "PeriapsisGameMode.h"

#include "Engine/World.h"
#include "PeriapsisBubble.h"
#include "PeriapsisTerrain.h"
#include "PeriapsisHUD.h"
#include "PeriapsisMapPawn.h"
#include "PeriapsisPlayerController.h"
#include "PeriapsisUniverseView.h"

APeriapsisGameMode::APeriapsisGameMode()
{
	DefaultPawnClass = APeriapsisMapPawn::StaticClass();
	PlayerControllerClass = APeriapsisPlayerController::StaticClass();
	HUDClass = APeriapsisHUD::StaticClass();
}

void APeriapsisGameMode::BeginPlay()
{
	Super::BeginPlay();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<APeriapsisBubble>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	GetWorld()->SpawnActor<APeriapsisTerrain>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	GetWorld()->SpawnActor<APeriapsisUniverseView>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
}

void APeriapsisGameMode::RestartPlayer(AController* NewPlayer)
{
	if (NewPlayer && !NewPlayer->IsPendingKillPending())
	{
		RestartPlayerAtTransform(NewPlayer, FTransform::Identity);
	}
}
