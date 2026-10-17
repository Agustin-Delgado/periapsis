#include "PeriapsisHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "PeriapsisCore/Constants.h"
#include "PeriapsisCore/Kepler.h"
#include "PeriapsisCore/OrbitalElements.h"
#include "PeriapsisCore/Rocket.h"
#include "PeriapsisCore/Staging.h"
#include "PeriapsisCore/Surface.h"
#include "PeriapsisBuilderView.h"
#include "PeriapsisPlayerController.h"
#include "PeriapsisUniverse.h"
#include "PeriapsisUniverseView.h"
#include "EngineUtils.h"
#include "PeriapsisTerrain.h"

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
		case AttitudeMode::SurfacePrograde: return TEXT("surface prograde");
		case AttitudeMode::SurfaceRetrograde: return TEXT("surface retrograde");
		case AttitudeMode::Up: return TEXT("up");
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
	const float Gap = 14.0f * Scale;

	// Tabs split a line into cells; the cells of a block line up in columns as wide as their
	// widest cell (the font is proportional, so padding with spaces does not align).
	TArray<TArray<FString>> Cells;
	TArray<float> Widths;
	for (const FString& Line : Lines)
	{
		TArray<FString>& Row = Cells.AddDefaulted_GetRef();
		Line.ParseIntoArray(Row, TEXT("	"), false);
		if (Row.Num() < 2)
		{
			continue; // a line without tabs takes the whole width
		}
		for (int32 Column = 0; Column < Row.Num(); ++Column)
		{
			float Width = 0.0f, Height = 0.0f;
			GetTextSize(Row[Column], Width, Height, Font, Scale);
			if (Widths.Num() <= Column)
			{
				Widths.Add(0.0f);
			}
			Widths[Column] = FMath::Max(Widths[Column], Width);
		}
	}

	FCanvasTextItem Item(FVector2D::ZeroVector, FText::GetEmpty(), Font, Color);
	Item.Scale = FVector2D(Scale, Scale);
	Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f), FVector2D(Scale, Scale)); // readable over a bright ground
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const TArray<FString>& Row = Cells[Index];
		if (Row.Num() < 2)
		{
			Item.Text = FText::FromString(Lines[Index]);
			Canvas->DrawItem(Item, X, Y);
		}
		else
		{
			float CellX = X;
			for (int32 Column = 0; Column < Row.Num(); ++Column)
			{
				Item.Text = FText::FromString(Row[Column]);
				Canvas->DrawItem(Item, CellX, Y);
				CellX += Widths[Column] + Gap;
			}
		}
		Y += LineHeight;
	}
}

void APeriapsisHUD::DrawBuilder(const APeriapsisPlayerController& Controller, float Scale, float Margin)
{
	const APeriapsisBuilderView* View = Controller.GetBuilderView();
	const FPeriapsisBuilder* Builder = View ? View->GetBuilder() : nullptr;
	if (!Builder)
	{
		DrawLines({TEXT("Builder: no part catalog loaded")}, Margin, Margin, FLinearColor::Red, Scale);
		return;
	}
	const PartCatalog& Catalog = Builder->GetCatalog();
	const VesselDesign& Design = Builder->GetDesign();

	// Left: the parts.
	TArray<FString> Left;
	Left.Add(FString::Printf(TEXT("BUILDER  -  %s"), UTF8_TO_TCHAR(Design.Name.c_str())));
	Left.Add(TEXT(""));
	Left.Add(TEXT("Parts (Up / Down)"));
	for (int32 Index = 0; Index < static_cast<int32>(Catalog.Parts.size()); ++Index)
	{
		const PartDefinition& Part = Catalog.Parts[Index];
		Left.Add(FString::Printf(TEXT("%s	%s	%.0f kg	%.0f"), Index == Builder->GetSelectedPart() ? TEXT(">") : TEXT(""),
			UTF8_TO_TCHAR(Part.Name.c_str()), Part.WetMass(), Part.Cost));
	}
	Left.Add(TEXT(""));
	const FPeriapsisAttachTarget Target = Builder->GetTarget();
	if (Design.IsEmpty())
	{
		Left.Add(TEXT("Target     none: Enter starts the design with this part"));
	}
	else if (Target.Instance != INDEX_NONE)
	{
		const PartInstance& On = Design.Parts()[Target.Instance];
		const FString Where = Target.Node.IsEmpty()
			? FString::Printf(TEXT("side at %.0f deg, %+.2f m (Q/E, W/S)"), Builder->GetSideAngle(), Builder->GetSideHeight())
			: FString::Printf(TEXT("node '%s'"), *Target.Node);
		Left.Add(FString::Printf(TEXT("Target     %s, %s  (%d of %d, Left / Right)"), UTF8_TO_TCHAR(Catalog.Parts[On.Part].Name.c_str()), *Where,
			Builder->GetTargetIndex() + 1, Builder->GetTargets().Num()));
		if (On.Stage >= 0)
		{
			Left.Add(FString::Printf(TEXT("           in stage %d  ([ / ])"), On.Stage));
		}
	}
	Left.Add(FString::Printf(TEXT("Symmetry   %d  (Y)"), Builder->GetSymmetry()));
	if (!Builder->GetMessage().IsEmpty())
	{
		Left.Add(Builder->GetMessage());
	}
	if (!Controller.GetLastSaved().IsEmpty())
	{
		Left.Add(FString::Printf(TEXT("Saved to %s"), *Controller.GetLastSaved()));
	}
	DrawLines(Left, Margin, Margin, FLinearColor::White, Scale);

	// Right: the vessel, stage by stage, in vacuum and at sea level on home (spec 8.2).
	const Vessel Craft = Builder->MakeVessel();
	TArray<FString> Right;
	const double Gravity = 9.81;
	if (Craft.IsValid())
	{
		const std::vector<StageStats> Vacuum = Builder->Stats(0.0, Gravity);
		const std::vector<StageStats> SeaLevel = Builder->Stats(kStandardAtmospherePa, Gravity);
		const int32 PartCount = static_cast<int32>(Craft.Parts().size());
		Right.Add(FString::Printf(TEXT("%d part%s   %.1f t   cost %.0f"), PartCount, PartCount == 1 ? TEXT("") : TEXT("s"), Craft.Mass() / 1000.0, Craft.Cost()));
		Right.Add(FString::Printf(TEXT("Delta-v    %.0f m/s vacuum, %.0f m/s sea level"), TotalDeltaV(Vacuum), TotalDeltaV(SeaLevel)));
		Right.Add(TEXT(""));
		Right.Add(TEXT("Stage	dv vac / SL	TWR vac / SL	burn	mass"));
		for (size_t Index = 0; Index < Vacuum.size(); ++Index)
		{
			const StageStats& V = Vacuum[Index];
			const StageStats& S = SeaLevel[Index];
			Right.Add(FString::Printf(TEXT("S%d	%.0f / %.0f m/s	%.2f / %.2f	%s	%.1f t"), V.Stage, V.DeltaV, S.DeltaV, V.StartTwr, S.StartTwr,
				*FormatDuration(V.BurnTime), V.StartMass / 1000.0));
		}
		Right.Add(TEXT(""));
		Right.Add(TEXT("TWR against home's 9.81 m/s2. Yellow: centre of mass."));
	}
	const TArray<FString> Problems = Builder->Problems();
	const float Column = Canvas->ClipX * 0.55f;
	DrawLines(Right, Column, Margin, FLinearColor::White, Scale);
	if (!Problems.IsEmpty())
	{
		TArray<FString> Lines;
		for (const FString& Problem : Problems)
		{
			Lines.Add(TEXT("! ") + Problem);
		}
		const float Below = Margin + GEngine->GetMediumFont()->GetMaxCharHeight() * Scale * 1.15f * (Right.Num() + 1);
		DrawLines(Lines, Column, Below, FLinearColor(1.0f, 0.4f, 0.3f), Scale);
	}

	if (Controller.IsHelpVisible())
	{
		const TArray<FString> Help = {
			TEXT("Enter  attach     Delete  remove     U  undo     T  auto-stage     [ ]  stage of the target"),
			TEXT("N  new     O  open next design     F5  save     L  launch from the pad     B / Esc  back to flight"),
			TEXT("Right drag  rotate     Wheel  zoom     F1  hide help"),
		};
		const float HelpHeight = GEngine->GetMediumFont()->GetMaxCharHeight() * Scale * 1.15f * Help.Num();
		DrawLines(Help, Margin, Canvas->ClipY - Margin - HelpHeight, FLinearColor(0.7f, 0.7f, 0.7f), Scale);
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
	if (Controller->IsBuilding())
	{
		DrawBuilder(*Controller, Scale, Margin);
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
	else if (ShipFlight.Status() == FlightStatus::Landed)
	{
		Lines.Add(TEXT("LANDED  -  throttle up to take off"));
	}
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("Orbiting   %s"), *Universe->GetBodyInfo(ShipFlight.BodyIndex()).DisplayName));
	Lines.Add(FString::Printf(TEXT("Altitude   %s"), *Km(Radius - Body.Radius)));
	Lines.Add(FString::Printf(TEXT("Speed      %.1f m/s"), Norm(State.Velocity)));
	{
		// Relative to the ground under the ship: what matters for landing.
		const Vec3 Up = Normalized(State.Position);
		const Vec3 OverGround = State.Velocity - GroundVelocity(Body, State.Position);
		const double Clearance = Universe->VesselAltitudeAboveTerrain(FPeriapsisUniverse::ActiveVessel)
			- Universe->GetVessel(FPeriapsisUniverse::ActiveVessel).GroundClearance();
		Lines.Add(FString::Printf(TEXT("Ground     %s below  (surface %.1f m/s, vertical %+.1f m/s)"),
			Clearance < 10000.0 ? *FString::Printf(TEXT("%.0f m"), Clearance) : *Km(Clearance), Norm(OverGround), Dot(OverGround, Up)));
	}
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
	const Vessel& Craft = ShipFlight.GetVessel();
	const int32 PartCount = static_cast<int32>(Craft.Parts().size());
	Lines.Add(FString::Printf(TEXT("Mass       %.0f kg, %d part%s  (propellant %.0f kg)"), Craft.Mass(), PartCount, PartCount == 1 ? TEXT("") : TEXT("s"),
		Craft.PropellantMass()));
	// Stages still to come, at full throttle in vacuum, against the gravity here (spec 4.5).
	const std::vector<StageStats> Stages = AnalyzeStages(Craft, 0.0, LocalGravity);
	Lines.Add(FString::Printf(TEXT("Delta-v    %.0f m/s   TWR %.2f now"), TotalDeltaV(Stages),
		ThrustToWeight(Craft.Engines(1.0).Thrust, Craft.Mass(), LocalGravity)));
	if (Craft.StageCount() > 0)
	{
		Lines.Add(Craft.NextStage() < Craft.StageCount()
			? FString::Printf(TEXT("Stages     next %d of %d  (Space)"), Craft.NextStage(), Craft.StageCount())
			: FString(TEXT("Stages     all fired")));
		for (const StageStats& Stage : Stages)
		{
			Lines.Add(FString::Printf(TEXT("  %s	%.0f m/s	TWR %.2f	%s"), Stage.Stage < 0 ? TEXT("now") : *FString::Printf(TEXT("S%d"), Stage.Stage),
				Stage.DeltaV, Stage.StartTwr, *FormatDuration(Stage.BurnTime)));
		}
	}
	Lines.Add(TEXT(""));
	if (Universe->IsPhysicsActive())
	{
		int32 InBubble = 0;
		for (int32 Index = 0; Index < Universe->NumVessels(); ++Index)
		{
			InBubble += Universe->GetVessel(Index).bInBubble ? 1 : 0;
		}
		const LocalFrame& Frame = Universe->GetLocalFrame();
		// The frame is already at the end of the step being simulated; the ship's state is from its start.
		const StateVector ShipNow = PropagateKepler(ShipFlight.State(), Body.Mu, Frame.Time() - ShipFlight.Time());
		const StateVector ShipLocal = Frame.ToLocal(ChangeFrame(Universe->GetSystem(), ShipFlight.BodyIndex(), Frame.BodyIndex(),
			ShipNow, Frame.Time()));
		Lines.Add(FString::Printf(TEXT("Physics    bubble, %d vessel%s, %s"), InBubble, InBubble == 1 ? TEXT("") : TEXT("s"),
			Universe->GetLocalFrame().IsBodyFixed()
				? *FString::Printf(TEXT("turning with %s"), *Universe->GetBodyInfo(Universe->GetLocalFrame().BodyIndex()).DisplayName)
				: TEXT("free-falling")));
		Lines.Add(FString::Printf(TEXT("Origin     %.0f m, %.1f m/s away  (%d rebases)"), Norm(ShipLocal.Position), Norm(ShipLocal.Velocity),
			Universe->GetRebaseCount()));
	}
	else
	{
		Lines.Add(TEXT("Physics    off: on rails"));
	}
	for (int32 Index = 0; Index < Universe->NumVessels(); ++Index)
	{
		if (Index != FPeriapsisUniverse::ActiveVessel)
		{
			const FPeriapsisVessel& Other = Universe->GetVessel(Index);
			Lines.Add(FString::Printf(TEXT("%-10s %.2f km%s"), *Other.Name, Universe->VesselDistance(Index, FPeriapsisUniverse::ActiveVessel) / 1000.0,
				Other.bInBubble ? TEXT("  in bubble") : TEXT("")));
		}
	}
	for (TActorIterator<APeriapsisTerrain> It(GetWorld()); It; ++It)
	{
		Lines.Add(FString::Printf(TEXT("Terrain    %d chunks, finest level %d, %d being built"), It->NumDrawnChunks(), It->DeepestDrawnLevel(),
			It->NumPendingChunks()));
	}
	if (const APeriapsisUniverseView* View = Controller->GetUniverseView())
	{
		Lines.Add(TEXT(""));
		Lines.Add(FString::Printf(TEXT("Camera on  %s"), *View->GetFocusName()));
	}
	DrawLines(Lines, Margin, Margin, FLinearColor::White, Scale);

	if (Controller->IsHelpVisible())
	{
		const TArray<FString> Help = {
			TEXT("Shift / Ctrl  throttle up / down     Z / X  full / cut     Space  stage"),
			TEXT("1-6  prograde, retrograde, normal, anti-normal, radial out, radial in"),
			TEXT("7 / 8  surface retrograde (for landing) / surface prograde    9  up (for launch)"),
			TEXT(". / ,  warp up / down     /  warp 1x"),
			TEXT("Right drag  rotate     Wheel  zoom     Tab  next focus"),
			TEXT("R  restart     B  builder     F1  hide help"),
		};
		const float HelpHeight = GEngine->GetMediumFont()->GetMaxCharHeight() * Scale * 1.15f * Help.Num();
		DrawLines(Help, Margin, Canvas->ClipY - Margin - HelpHeight, FLinearColor(0.7f, 0.7f, 0.7f), Scale);
	}
}
