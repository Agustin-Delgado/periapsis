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

bool UPeriapsisFlightSubsystem::LaunchDesign(const Periapsis::Core::VesselDesign& Design)
{
	FPeriapsisScenario Scenario;
	TArray<FString> Errors;
	if (!PeriapsisData::LoadScenario(LaunchScenario, Scenario, Errors))
	{
		for (const FString& Error : Errors)
		{
			UE_LOG(LogPeriapsisFlight, Error, TEXT("  %s"), *Error);
		}
		return false;
	}
	if (Design.IsEmpty() || !Design.Validate(*Scenario.Catalog).empty())
	{
		UE_LOG(LogPeriapsisFlight, Error, TEXT("Design '%s' cannot be launched"), UTF8_TO_TCHAR(Design.Name.c_str()));
		return false;
	}
	PeriapsisData::SetShipDesign(Scenario, Design);
	Universe = MakeUnique<FPeriapsisUniverse>(MoveTemp(Scenario));
	bBuilding = false;
	UE_LOG(LogPeriapsisFlight, Log, TEXT("Launching '%s' (%d parts)"), *Universe->GetShipName(),
		static_cast<int32>(Universe->GetFlight().GetVessel().Parts().size()));
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
	// -Builder opens the builder first.
	bBuilding = FParse::Param(FCommandLine::Get(), TEXT("Builder"));
}

void UPeriapsisFlightSubsystem::Deinitialize()
{
	Universe.Reset();
	Super::Deinitialize();
}

void UPeriapsisFlightSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bBuilding)
	{
		if (AWorldSettings* Writable = GetWorld()->GetWorldSettings())
		{
			Writable->SetTimeDilation(1.0f);
		}
		return; // the universe waits while the builder is open
	}
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
