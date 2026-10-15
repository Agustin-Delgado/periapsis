#include "PeriapsisHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "PeriapsisCore/Constants.h"
#include "PeriapsisCore/OrbitalElements.h"
#include "PeriapsisCore/Rocket.h"
#include "PeriapsisPlayerController.h"
#include "PeriapsisUniverse.h"
#include "PeriapsisUniverseView.h"

using namespace Periapsis::Core;

namespace
{
	const TCHAR* AttitudeName(AttitudeMode Mode)
	{
		switch (Mode)
		{
		case AttitudeMode::Prograde: return TEXT("prograde");
		case AttitudeMode::Retrograde: return TEXT("retrograde");
		case AttitudeMode::Normal: return TEXT("normal");
		case AttitudeMode::AntiNormal: return TEXT("anti-normal");
		case AttitudeMode::RadialOut: return TEXT("radial out");
		case AttitudeMode::RadialIn: return TEXT("radial in");
		}
		return TEXT("?");
	}

	FString Km(double Metres)
	{
		return FMath::IsFinite(Metres) ? FString::Printf(TEXT("%.1f km"), Metres / 1000.0) : FString(TEXT("-"));
	}
}

FString APeriapsisHUD::FormatDuration(double Seconds)
{
	if (!FMath::IsFinite(Seconds))
	{
		return TEXT("-");
	}
	const int64 Total = FMath::FloorToInt64(FMath::Max(Seconds, 0.0));
	const int64 Days = Total / 86400;
	const int64 Hours = (Total / 3600) % 24;
	const int64 Minutes = (Total / 60) % 60;
	const int64 Secs = Total % 60;
	if (Days > 0)
	{
		return FString::Printf(TEXT("%lldd %02lldh %02lldm %02llds"), Days, Hours, Minutes, Secs);
	}
	if (Hours > 0)
	{
		return FString::Printf(TEXT("%lldh %02lldm %02llds"), Hours, Minutes, Secs);
	}
	if (Minutes > 0)
	{
		return FString::Printf(TEXT("%lldm %02llds"), Minutes, Secs);
	}
	return FString::Printf(TEXT("%.1fs"), FMath::Max(Seconds, 0.0));
}

void APeriapsisHUD::DrawLines(const TArray<FString>& Lines, float X, float Y, const FLinearColor& Color, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	const float LineHeight = Font->GetMaxCharHeight() * Scale * 1.15f;
	for (const FString& Line : Lines)
	{
		DrawText(Line, Color, X, Y, Font, Scale);
		Y += LineHeight;
	}
}

void APeriapsisHUD::DrawHUD()
{
	Super::DrawHUD();

	const APeriapsisPlayerController* Controller = Cast<APeriapsisPlayerController>(GetOwningPlayerController());
	FPeriapsisUniverse* Universe = Controller ? Controller->GetUniverse() : nullptr;
	const float Scale = FMath::Max(1.0f, Canvas->ClipY / 900.0f);
	const float Margin = 16.0f * Scale;

	if (!Universe)
	{
		DrawLines({TEXT("No scenario loaded: see the log (LogPeriapsisFlight).")}, Margin, Margin, FLinearColor::Red, Scale);
		return;
	}

	Flight& ShipFlight = Universe->GetFlight();
	const CelestialBody& Body = Universe->GetSystem().Body(ShipFlight.BodyIndex());
	const StateVector& State = ShipFlight.State();
	const OrbitalElements Elements = StateToElements(State, Body.Mu, ShipFlight.Time());
	const double Radius = Norm(State.Position);
	const double LocalGravity = Body.Mu / (Radius * Radius);

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("%s  -  T+ %s"), *Universe->GetShipName(), *FormatDuration(ShipFlight.Time())));
	FString Warp = FString::Printf(TEXT("Warp %gx"), Universe->GetWarp());
	if (!Universe->GetWarpLimitReason().IsEmpty())
	{
		Warp += FString::Printf(TEXT("  (limited: %s)"), *Universe->GetWarpLimitReason());
	}
	Lines.Add(Warp);
	if (ShipFlight.Status() == FlightStatus::Crashed)
	{
		Lines.Add(TEXT("CRASHED  -  press R to restart"));
	}
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("Orbiting   %s"), *Universe->GetBodyInfo(ShipFlight.BodyIndex()).DisplayName));
	Lines.Add(FString::Printf(TEXT("Altitude   %s"), *Km(Radius - Body.Radius)));
	Lines.Add(FString::Printf(TEXT("Speed      %.1f m/s"), Norm(State.Velocity)));
	Lines.Add(FString::Printf(TEXT("Apoapsis   %s"), *Km(ApoapsisRadius(Elements) - Body.Radius)));
	Lines.Add(FString::Printf(TEXT("Periapsis  %s"), *Km(PeriapsisRadius(Elements) - Body.Radius)));
	Lines.Add(FString::Printf(TEXT("Ecc        %.4f   Inc %.2f deg"), Elements.Eccentricity, RadToDeg(Elements.Inclination)));
	Lines.Add(FString::Printf(TEXT("Period     %s"), *FormatDuration(OrbitalPeriod(Elements, Body.Mu))));

	const std::vector<TrajectoryPatch>& Patches = ShipFlight.Trajectory();
	if (!Patches.empty() && Patches.front().EndReason != PatchEnd::Horizon)
	{
		const TrajectoryPatch& Next = Patches.front();
		const FString What = Next.EndReason == PatchEnd::Impact
			? FString(TEXT("Impact"))
			: FString::Printf(TEXT("SOI -> %s"), *Universe->GetBodyInfo(Next.NextBodyIndex).DisplayName);
		Lines.Add(FString::Printf(TEXT("Next       %s in %s"), *What, *FormatDuration(Next.EndTime - ShipFlight.Time())));
	}
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("Throttle   %3.0f%%%s"), ShipFlight.Throttle() * 100.0, ShipFlight.IsThrusting() ? TEXT("  BURNING") : TEXT("")));
	Lines.Add(FString::Printf(TEXT("Attitude   %s"), AttitudeName(Universe->GetAttitude())));
	Lines.Add(FString::Printf(TEXT("Fuel       %.0f kg"), ShipFlight.Fuel()));
	Lines.Add(FString::Printf(TEXT("Delta-v    %.0f m/s"), ShipFlight.RemainingDeltaV()));
	Lines.Add(FString::Printf(TEXT("TWR        %.2f"), ThrustToWeight(ShipFlight.Ship().Thrust, ShipFlight.Mass(), LocalGravity)));
	if (const APeriapsisUniverseView* View = Controller->GetUniverseView())
	{
		Lines.Add(TEXT(""));
		Lines.Add(FString::Printf(TEXT("Camera on  %s"), *View->GetFocusName()));
	}
	DrawLines(Lines, Margin, Margin, FLinearColor::White, Scale);

	if (Controller->IsHelpVisible())
	{
		const TArray<FString> Help = {
			TEXT("Shift / Ctrl  throttle up / down     Z / X  full / cut"),
			TEXT("1-6  prograde, retrograde, normal, anti-normal, radial out, radial in"),
			TEXT(". / ,  warp up / down     /  warp 1x"),
			TEXT("Right drag  rotate     Wheel  zoom     Tab  next focus"),
			TEXT("R  restart     F1  hide help"),
		};
		const float HelpHeight = GEngine->GetMediumFont()->GetMaxCharHeight() * Scale * 1.15f * Help.Num();
		DrawLines(Help, Margin, Canvas->ClipY - Margin - HelpHeight, FLinearColor(0.7f, 0.7f, 0.7f), Scale);
	}
}
