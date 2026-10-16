#include "PeriapsisFlightSubsystem.h"

#include "GameFramework/WorldSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PeriapsisUniverse.h"

DEFINE_LOG_CATEGORY_STATIC(LogPeriapsisFlight, Log, All);

UPeriapsisFlightSubsystem::UPeriapsisFlightSubsystem() = default;
UPeriapsisFlightSubsystem::~UPeriapsisFlightSubsystem() = default;

bool UPeriapsisFlightSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UPeriapsisFlightSubsystem::LoadScenario(const FString& RelativePath)
{
	FPeriapsisScenario Scenario;
	TArray<FString> Errors;
	if (!PeriapsisData::LoadScenario(RelativePath, Scenario, Errors))
	{
		UE_LOG(LogPeriapsisFlight, Error, TEXT("Scenario '%s' not loaded:"), *RelativePath);
		for (const FString& Error : Errors)
		{
			UE_LOG(LogPeriapsisFlight, Error, TEXT("  %s"), *Error);
		}
		return false;
	}
	Universe = MakeUnique<FPeriapsisUniverse>(MoveTemp(Scenario));
	UE_LOG(LogPeriapsisFlight, Log, TEXT("Scenario '%s' loaded: %d bodies, ship '%s'"), *RelativePath,
		Universe->NumBodies(), *Universe->GetShipName());
	return true;
}

void UPeriapsisFlightSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!Universe)
	{
		// -Scenario=scenarios/landing_hop.json picks another one from the command line.
		FString Path = DefaultScenario;
		FParse::Value(FCommandLine::Get(), TEXT("Scenario="), Path);
		LoadScenario(Path);
	}
}

void UPeriapsisFlightSubsystem::Deinitialize()
{
	Universe.Reset();
	Super::Deinitialize();
}

void UPeriapsisFlightSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Universe && !GetWorld()->IsPaused())
	{
		// The world runs dilated while the physics bubble warps; the rails use real time.
		const AWorldSettings* Settings = GetWorld()->GetWorldSettings();
		const double Dilation = Settings ? Settings->GetEffectiveTimeDilation() : 1.0;
		Universe->Tick(DeltaTime / FMath::Max(Dilation, UE_SMALL_NUMBER));
		if (AWorldSettings* Writable = GetWorld()->GetWorldSettings())
		{
			Writable->SetTimeDilation(static_cast<float>(Universe->GetPhysicsTimeDilation()));
		}
	}
}

TStatId UPeriapsisFlightSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPeriapsisFlightSubsystem, STATGROUP_Tickables);
}
