#include "PeriapsisPlayerController.h"

#include "EngineUtils.h"
#include "PeriapsisBuilderView.h"
#include "PeriapsisFlightSubsystem.h"
#include "PeriapsisMapPawn.h"
#include "PeriapsisUniverse.h"
#include "PeriapsisUniverseView.h"
#include "UnitBridge.h"

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

APeriapsisBuilderView* APeriapsisPlayerController::GetBuilderView() const
{
	for (TActorIterator<APeriapsisBuilderView> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool APeriapsisPlayerController::IsBuilding() const
{
	const UPeriapsisFlightSubsystem* Subsystem = GetWorld()->GetSubsystem<UPeriapsisFlightSubsystem>();
	return Subsystem && Subsystem->IsBuilding();
}

void APeriapsisPlayerController::OpenBuilder()
{
	if (UPeriapsisFlightSubsystem* Subsystem = GetWorld()->GetSubsystem<UPeriapsisFlightSubsystem>())
	{
		Subsystem->EnterBuilder();
	}
}

void APeriapsisPlayerController::HandleBuilderInput()
{
	APeriapsisBuilderView* View = GetBuilderView();
	FPeriapsisBuilder* Builder = View ? View->GetBuilder() : nullptr;
	UPeriapsisFlightSubsystem* Subsystem = GetWorld()->GetSubsystem<UPeriapsisFlightSubsystem>();
	if (!Builder || !Subsystem)
	{
		return;
	}
	auto Pressed = [this](const FKey& Key) { return WasInputKeyJustPressed(Key); };
	bool bChanged = true;
	if (Pressed(EKeys::Up))
	{
		Builder->SelectPart(-1);
	}
	else if (Pressed(EKeys::Down))
	{
		Builder->SelectPart(1);
	}
	else if (Pressed(EKeys::Left))
	{
		Builder->CycleTarget(-1);
	}
	else if (Pressed(EKeys::Right))
	{
		Builder->CycleTarget(1);
	}
	else if (Pressed(EKeys::Q))
	{
		Builder->TurnSide(-FPeriapsisBuilder::SideTurnDegrees);
	}
	else if (Pressed(EKeys::E))
	{
		Builder->TurnSide(FPeriapsisBuilder::SideTurnDegrees);
	}
	else if (Pressed(EKeys::W))
	{
		Builder->MoveSide(FPeriapsisBuilder::SideStepMetres);
	}
	else if (Pressed(EKeys::S))
	{
		Builder->MoveSide(-FPeriapsisBuilder::SideStepMetres);
	}
	else if (Pressed(EKeys::Y))
	{
		Builder->CycleSymmetry();
	}
	else if (Pressed(EKeys::Enter))
	{
		Builder->Attach();
	}
	else if (Pressed(EKeys::Delete) || Pressed(EKeys::BackSpace))
	{
		Builder->RemoveTarget();
	}
	else if (Pressed(EKeys::LeftBracket))
	{
		Builder->ShiftTargetStage(-1);
	}
	else if (Pressed(EKeys::RightBracket))
	{
		Builder->ShiftTargetStage(1);
	}
	else if (Pressed(EKeys::T))
	{
		Builder->AutoStage();
	}
	else if (Pressed(EKeys::U))
	{
		Builder->Undo();
	}
	else if (Pressed(EKeys::N))
	{
		View->NewDesign();
	}
	else if (Pressed(EKeys::O))
	{
		View->LoadNext();
	}
	else if (Pressed(EKeys::F5))
	{
		LastSaved = View->Save();
	}
	else if (Pressed(EKeys::L))
	{
		Subsystem->LaunchDesign(Builder->GetDesign());
	}
	else if (Pressed(EKeys::B) || Pressed(EKeys::Escape))
	{
		Subsystem->LeaveBuilder();
	}
	else
	{
		bChanged = false;
	}
	if (bChanged)
	{
		View->MarkChanged();
	}
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

	// The camera frames the rocket in the builder, and the launch after it.
	const bool bBuilding = IsBuilding();
	if (MapPawn && bBuilding != bWasBuilding)
	{
		if (bBuilding)
		{
			FlightDistance = MapPawn->GetDistance();
			UniverseBeforeBuilding = Universe;
			// The side at 0 deg (the builder's first) faces the camera; the view fits the design.
			MapPawn->SetYaw(180.0);
			if (APeriapsisBuilderView* BuilderView = GetBuilderView())
			{
				BuilderView->RefitCamera();
			}
		}
		else if (Universe != UniverseBeforeBuilding && Universe && Universe->NumVessels() > 0)
		{
			MapPawn->SetDistance(FUnitBridge::ToUnreal(Universe->GetVessel(FPeriapsisUniverse::ActiveVessel).Size()) * 3.6); // a new launch
		}
		else
		{
			MapPawn->SetDistance(FlightDistance);
		}
	}
	bWasBuilding = bBuilding;

	if (MapPawn)
	{
		if (IsInputKeyDown(EKeys::RightMouseButton))
		{
			float DeltaX = 0.0f, DeltaY = 0.0f;
			GetInputMouseDelta(DeltaX, DeltaY);
			MapPawn->Orbit(DeltaX * OrbitDegreesPerMouseUnit, DeltaY * OrbitDegreesPerMouseUnit);
		}
		APeriapsisBuilderView* BuilderView = GetBuilderView();
		const double MinDistance = IsBuilding() ? (BuilderView ? BuilderView->GetRadiusUnreal() * 1.2 : 0.0)
			: (View ? View->GetFocusRadiusUnreal() * 1.3 : 0.0);
		if (WasInputKeyJustPressed(EKeys::MouseScrollUp))
		{
			MapPawn->Zoom(1.0 / ZoomStep, MinDistance);
		}
		if (WasInputKeyJustPressed(EKeys::MouseScrollDown))
		{
			MapPawn->Zoom(ZoomStep, MinDistance);
		}
	}

	if (WasInputKeyJustPressed(EKeys::F1))
	{
		bShowHelp = !bShowHelp;
	}
	if (IsBuilding())
	{
		HandleBuilderInput();
		return;
	}
	if (WasInputKeyJustPressed(EKeys::B))
	{
		OpenBuilder();
		return;
	}

	if (View && WasInputKeyJustPressed(EKeys::Tab))
	{
		View->CycleFocus();
		if (MapPawn)
		{
			MapPawn->Zoom(1.0, View->GetFocusRadiusUnreal() * 1.3); // step out of a body if needed
		}
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
		{EKeys::Seven, AttitudeMode::SurfaceRetrograde},
		{EKeys::Eight, AttitudeMode::SurfacePrograde},
		{EKeys::Nine, AttitudeMode::Up},
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
	if (WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		Universe->Stage();
	}
}
