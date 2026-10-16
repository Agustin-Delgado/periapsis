#include "PeriapsisPlayerController.h"

#include "EngineUtils.h"
#include "PeriapsisFlightSubsystem.h"
#include "PeriapsisMapPawn.h"
#include "PeriapsisUniverse.h"
#include "PeriapsisUniverseView.h"

using Periapsis::Core::AttitudeMode;

namespace
{
	constexpr double ThrottleRatePerSecond = 0.5;
	constexpr double OrbitDegreesPerMouseUnit = 1.0;
	constexpr double ZoomStep = 1.25;
}

APeriapsisPlayerController::APeriapsisPlayerController()
{
	bShowMouseCursor = true;
}

void APeriapsisPlayerController::BeginPlay()
{
	Super::BeginPlay();
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);
}

APeriapsisUniverseView* APeriapsisPlayerController::GetUniverseView() const
{
	for (TActorIterator<APeriapsisUniverseView> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

FPeriapsisUniverse* APeriapsisPlayerController::GetUniverse() const
{
	const UPeriapsisFlightSubsystem* Subsystem = GetWorld()->GetSubsystem<UPeriapsisFlightSubsystem>();
	return Subsystem ? Subsystem->GetUniverse() : nullptr;
}

void APeriapsisPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	APeriapsisMapPawn* MapPawn = Cast<APeriapsisMapPawn>(GetPawn());
	APeriapsisUniverseView* View = GetUniverseView();
	FPeriapsisUniverse* Universe = GetUniverse();

	if (MapPawn)
	{
		if (IsInputKeyDown(EKeys::RightMouseButton))
		{
			float DeltaX = 0.0f, DeltaY = 0.0f;
			GetInputMouseDelta(DeltaX, DeltaY);
			MapPawn->Orbit(DeltaX * OrbitDegreesPerMouseUnit, DeltaY * OrbitDegreesPerMouseUnit);
		}
		const double MinDistance = View ? View->GetFocusRadiusUnreal() * 1.3 : 0.0;
		if (WasInputKeyJustPressed(EKeys::MouseScrollUp))
		{
			MapPawn->Zoom(1.0 / ZoomStep, MinDistance);
		}
		if (WasInputKeyJustPressed(EKeys::MouseScrollDown))
		{
			MapPawn->Zoom(ZoomStep, MinDistance);
		}
	}

	if (View && WasInputKeyJustPressed(EKeys::Tab))
	{
		View->CycleFocus();
		if (MapPawn)
		{
			MapPawn->Zoom(1.0, View->GetFocusRadiusUnreal() * 1.3); // step out of a body if needed
		}
	}
	if (WasInputKeyJustPressed(EKeys::F1))
	{
		bShowHelp = !bShowHelp;
	}

	if (!Universe)
	{
		return;
	}

	// Throttle, in real time: the world is dilated while the physics bubble warps.
	const double RealDelta = GetWorld()->DeltaRealTimeSeconds;
	if (IsInputKeyDown(EKeys::LeftShift))
	{
		Universe->AddThrottle(ThrottleRatePerSecond * RealDelta);
	}
	if (IsInputKeyDown(EKeys::LeftControl))
	{
		Universe->AddThrottle(-ThrottleRatePerSecond * RealDelta);
	}
	if (WasInputKeyJustPressed(EKeys::Z))
	{
		Universe->SetThrottle(1.0);
	}
	if (WasInputKeyJustPressed(EKeys::X))
	{
		Universe->SetThrottle(0.0);
	}

	// Attitude.
	const TPair<FKey, AttitudeMode> AttitudeKeys[] = {
		{EKeys::One, AttitudeMode::Prograde},
		{EKeys::Two, AttitudeMode::Retrograde},
		{EKeys::Three, AttitudeMode::Normal},
		{EKeys::Four, AttitudeMode::AntiNormal},
		{EKeys::Five, AttitudeMode::RadialOut},
		{EKeys::Six, AttitudeMode::RadialIn},
	};
	for (const TPair<FKey, AttitudeMode>& Binding : AttitudeKeys)
	{
		if (WasInputKeyJustPressed(Binding.Key))
		{
			Universe->SetAttitude(Binding.Value);
		}
	}

	// Time warp.
	if (WasInputKeyJustPressed(EKeys::Period))
	{
		Universe->IncreaseWarp();
	}
	if (WasInputKeyJustPressed(EKeys::Comma))
	{
		Universe->DecreaseWarp();
	}
	if (WasInputKeyJustPressed(EKeys::Slash))
	{
		Universe->ResetWarp();
	}
	if (WasInputKeyJustPressed(EKeys::R))
	{
		Universe->Restart();
	}
}
