#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
// Included, not forward-declared: UHT-generated code destroys the subsystem and needs the full type.
#include "PeriapsisUniverse.h"
#include "PeriapsisFlightSubsystem.generated.h"

// Owns the running universe in game worlds (game and PIE, not editor previews) and ticks it.
UCLASS()
class PERIAPSISSIM_API UPeriapsisFlightSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UPeriapsisFlightSubsystem();
	virtual ~UPeriapsisFlightSubsystem() override;

	// Loads a scenario (path under Data/). Logs every data error and keeps the previous
	// universe if loading fails.
	bool LoadScenario(const FString& RelativePath);

	FPeriapsisUniverse* GetUniverse() const { return Universe.Get(); }

	// USubsystem / FTickableGameObject
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	static constexpr const TCHAR* DefaultScenario = TEXT("scenarios/orbital_prototype.json");

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	TUniquePtr<FPeriapsisUniverse> Universe;
};
