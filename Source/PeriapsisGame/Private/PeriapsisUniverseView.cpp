#include "PeriapsisUniverseView.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "PeriapsisBubble.h"
#include "PeriapsisCore/Kepler.h"
#include "PeriapsisCore/OrbitalElements.h"
#include "PeriapsisFlightSubsystem.h"
#include "PeriapsisUniverse.h"
#include "UObject/ConstructorHelpers.h"
#include "UnitBridge.h"

using namespace Periapsis::Core;

namespace
{
	// The engine sphere has a radius of 50 cm.
	constexpr double SphereMeshRadius = 50.0;
	constexpr int32 OrbitSegments = 256;
	// Open trajectories around the root body never end; draw this much of them.
	constexpr double OpenTrajectorySpan = 2.0e6;
	// Debug strings live in the HUD's list: a duration of -1 keeps them forever (they pile
	// up and freeze in place), 0 draws them for exactly one frame.
	constexpr float LabelDuration = 0.0f;

	const FColor PatchColors[] = {FColor(40, 200, 255), FColor(255, 150, 40), FColor(255, 230, 60), FColor(200, 120, 255)};
	const FColor OtherVesselColor(200, 110, 60);
	const FColor BodyOrbitColor(110, 110, 120);
	const FColor SoiColor(60, 60, 75);

	FString FormatAltitude(double Metres)
	{
		return FMath::Abs(Metres) >= 10000.0 ? FString::Printf(TEXT("%.1f km"), Metres / 1000.0) : FString::Printf(TEXT("%.0f m"), Metres);
	}
}

APeriapsisUniverseView::APeriapsisUniverseView()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	SphereMesh = Sphere.Object;
	BodyMaterial = Material.Object;

	SunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	SunLight->SetupAttachment(RootComponent);
	SunLight->SetMobility(EComponentMobility::Movable);
	SunLight->SetIntensity(6.0f);
	SunLight->SetRelativeRotation(FRotator(-25.0, 135.0, 0.0));

	// Space has no auto-exposure reference: a fixed exposure keeps the planets readable.
	Exposure = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Exposure"));
	Exposure->SetupAttachment(RootComponent);
	Exposure->bUnbound = true;
	Exposure->Settings.bOverride_AutoExposureMethod = true;
	Exposure->Settings.AutoExposureMethod = AEM_Manual;
	Exposure->Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Exposure->Settings.AutoExposureApplyPhysicalCameraExposure = false;
	Exposure->Settings.bOverride_AutoExposureBias = true;
	Exposure->Settings.AutoExposureBias = 0.0f;
}

void APeriapsisUniverseView::BeginPlay()
{
	Super::BeginPlay();
	if (const FPeriapsisUniverse* Universe = GetUniverse())
	{
		EnsureBodyMeshes(*Universe);
	}
}

FPeriapsisUniverse* APeriapsisUniverseView::GetUniverse() const
{
	const UPeriapsisFlightSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UPeriapsisFlightSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetUniverse() : nullptr;
}

APeriapsisBubble* APeriapsisUniverseView::GetBubble() const
{
	if (!Bubble.IsValid() && GetWorld())
	{
		for (TActorIterator<APeriapsisBubble> It(GetWorld()); It; ++It)
		{
			Bubble = *It;
			break;
		}
	}
	return Bubble.Get();
}

void APeriapsisUniverseView::EnsureBodyMeshes(const FPeriapsisUniverse& Universe)
{
	if (BodyMeshes.Num() == Universe.NumBodies() || !SphereMesh)
	{
		return;
	}
	for (UStaticMeshComponent* Mesh : BodyMeshes)
	{
		if (Mesh)
		{
			Mesh->DestroyComponent();
		}
	}
	BodyMeshes.Reset();

	for (int32 Index = 0; Index < Universe.NumBodies(); ++Index)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Body_%s"), *Universe.GetBodyInfo(Index).Id));
		Mesh->SetupAttachment(RootComponent);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(SphereMesh);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		const double RadiusUnreal = FUnitBridge::ToUnreal(Universe.GetSystem().Body(Index).Radius);
		Mesh->SetWorldScale3D(FVector(RadiusUnreal / SphereMeshRadius));
		if (BodyMaterial)
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(BodyMaterial, Mesh);
			Instance->SetVectorParameterValue(TEXT("Color"), Universe.GetBodyInfo(Index).Color);
			Mesh->SetMaterial(0, Instance);
		}
		Mesh->RegisterComponent();
		BodyMeshes.Add(Mesh);
	}
}

void APeriapsisUniverseView::CycleFocus()
{
	const FPeriapsisUniverse* Universe = GetUniverse();
	const int32 Count = Universe ? Universe->NumVessels() + Universe->NumBodies() : 1;
	Focus = (Focus + 1) % Count;
}

FString APeriapsisUniverseView::GetFocusName() const
{
	const FPeriapsisUniverse* Universe = GetUniverse();
	if (!Universe)
	{
		return TEXT("-");
	}
	return Focus < Universe->NumVessels() ? Universe->GetVessel(Focus).Name : Universe->GetBodyInfo(Focus - Universe->NumVessels()).DisplayName;
}

double APeriapsisUniverseView::GetFocusRadiusUnreal() const
{
	const FPeriapsisUniverse* Universe = GetUniverse();
	if (!Universe || Focus < Universe->NumVessels())
	{
		return FUnitBridge::ToUnreal(0.5 * APeriapsisBubble::ShipLength);
	}
	return FUnitBridge::ToUnreal(Universe->GetSystem().Body(Focus - Universe->NumVessels()).Radius);
}

FVector APeriapsisUniverseView::ToView(const Vec3& RootPosition, const Vec3& Origin)
{
	return FUnitBridge::ToUnreal(RootPosition - Origin);
}

FVector APeriapsisUniverseView::VesselLocation(const FPeriapsisUniverse& Universe, int32 Vessel, const Vec3& Origin, double Time) const
{
	// Simulated vessels are where Chaos shows them; the bubble places the others.
	const APeriapsisBubble* Scene = GetBubble();
	const UStaticMeshComponent* Mesh = Scene ? Scene->GetVesselMesh(Vessel) : nullptr;
	return Mesh ? Mesh->GetComponentLocation() : ToView(Universe.VesselPositionAt(Vessel, Time), Origin);
}

FVector APeriapsisUniverseView::FocusLocation(const FPeriapsisUniverse& Universe, const Vec3& Origin, double Time) const
{
	if (Focus < Universe.NumVessels())
	{
		return VesselLocation(Universe, Focus, Origin, Time);
	}
	return ToView(Universe.BodyPositionAt(Focus - Universe.NumVessels(), Time), Origin);
}

double APeriapsisUniverseView::ScreenScale(const FVector& At) const
{
	// A length that looks about the same on screen at any zoom: a fraction of the camera distance.
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	return Camera ? FVector::Dist(Camera->GetCameraLocation(), At) : 1.0e5;
}

void APeriapsisUniverseView::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FPeriapsisUniverse* Universe = GetUniverse();
	if (!Universe)
	{
		return;
	}
	EnsureBodyMeshes(*Universe);
	if (Focus >= Universe->NumVessels() + Universe->NumBodies())
	{
		Focus = 0;
	}

	// Everything is drawn at the moment the physics bubble shows, around its origin.
	APeriapsisBubble* Scene = GetBubble();
	if (Scene)
	{
		Scene->PlaceRailsVessels();
	}
	const double Time = Scene ? Scene->GetRenderTime() : Universe->GetTime();
	const Vec3 Origin = Scene ? Scene->GetRenderOrigin() : Universe->ShipPosition();

	for (int32 Index = 0; Index < BodyMeshes.Num(); ++Index)
	{
		const FVector Position = ToView(Universe->BodyPositionAt(Index, Time), Origin);
		BodyMeshes[Index]->SetWorldLocation(Position);
		const double Radius = FUnitBridge::ToUnreal(Universe->GetSystem().Body(Index).Radius);
		DrawDebugString(GetWorld(), Position + FVector(0.0, 0.0, Radius * 1.15), Universe->GetBodyInfo(Index).DisplayName,
			nullptr, FColor::White, LabelDuration, true, 1.2f);
	}

	DrawBodyOrbits(*Universe, Origin, Time);
	for (int32 Vessel = 0; Vessel < Universe->NumVessels(); ++Vessel)
	{
		DrawTrajectory(*Universe, *Universe->GetVessel(Vessel).Flight, Origin, Time, Vessel == FPeriapsisUniverse::ActiveVessel);
	}
	DrawVessels(*Universe, Origin, Time);

	if (APawn* Camera = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Camera->SetActorLocation(FocusLocation(*Universe, Origin, Time));
	}
}

void APeriapsisUniverseView::DrawBodyOrbits(const FPeriapsisUniverse& Universe, const Vec3& Origin, double Now) const
{
	const CelestialSystem& System = Universe.GetSystem();
	for (int32 Index = 0; Index < System.BodyCount(); ++Index)
	{
		const CelestialBody& Body = System.Body(Index);
		if (Body.ParentIndex == kNoBody)
		{
			continue;
		}
		const double ParentMu = System.Body(Body.ParentIndex).Mu;
		const Vec3 ParentNow = Universe.BodyPositionAt(Body.ParentIndex, Now);
		const double Period = OrbitalPeriod(Body.Orbit, ParentMu);
		FVector Previous = ToView(ElementsToStateAt(Body.Orbit, ParentMu, Now).Position + ParentNow, Origin);
		for (int32 Segment = 1; Segment <= OrbitSegments; ++Segment)
		{
			const double Time = Now + Period * Segment / OrbitSegments;
			const FVector Point = ToView(ElementsToStateAt(Body.Orbit, ParentMu, Time).Position + ParentNow, Origin);
			DrawDebugLine(GetWorld(), Previous, Point, BodyOrbitColor);
			Previous = Point;
		}
		// Sphere of influence, drawn in the body's orbital plane.
		const FVector Center = ToView(Universe.BodyPositionAt(Index, Now), Origin);
		const StateVector BodyState = System.StateRelativeToParent(Index, Now);
		const FVector Normal = FUnitBridge::DirectionToUnreal(AngularMomentum(BodyState));
		const FVector AxisY = FUnitBridge::DirectionToUnreal(BodyState.Position);
		DrawDebugCircle(GetWorld(), Center, FUnitBridge::ToUnreal(Body.SphereOfInfluence), 96, SoiColor, false, -1.0f, 0, 0.0f,
			AxisY, FVector::CrossProduct(Normal, AxisY), false);
	}
}

void APeriapsisUniverseView::DrawTrajectory(const FPeriapsisUniverse& Universe, Flight& VesselFlight, const Vec3& Origin, double Now,
	bool bActive) const
{
	const CelestialSystem& System = Universe.GetSystem();
	if (VesselFlight.Status() != FlightStatus::Flying)
	{
		return;
	}
	const std::vector<TrajectoryPatch>& Patches = VesselFlight.Trajectory();
	// Other vessels: their current orbit only, dimmed.
	const size_t PatchCount = bActive ? Patches.size() : FMath::Min<size_t>(Patches.size(), 1);

	for (size_t PatchIndex = 0; PatchIndex < PatchCount; ++PatchIndex)
	{
		const TrajectoryPatch& Patch = Patches[PatchIndex];
		const CelestialBody& Body = System.Body(Patch.BodyIndex);
		const FColor Color = bActive ? PatchColors[FMath::Min<size_t>(PatchIndex, UE_ARRAY_COUNT(PatchColors) - 1)] : OtherVesselColor;
		// Each patch is drawn around where its body is now, as map views usually do.
		const Vec3 BodyNow = Universe.BodyPositionAt(Patch.BodyIndex, Now);
		const bool bClosed = Patch.Elements.Eccentricity < 1.0;

		const double Start = FMath::Max(Patch.StartTime, Now);
		double End = Patch.EndTime;
		End = FMath::Min(End, Start + (bClosed ? OrbitalPeriod(Patch.Elements, Body.Mu) : OpenTrajectorySpan));
		if (!(End > Start))
		{
			continue;
		}

		// Closed patches are sampled evenly in time. Open ones are sampled evenly in true
		// anomaly: in time, the bend around periapsis falls between two samples.
		const OrbitalElements StartElements = StateToElements(PropagateKepler(Patch.StartState, Body.Mu, Start - Patch.StartTime), Body.Mu);
		const OrbitalElements EndElements = StateToElements(PropagateKepler(Patch.StartState, Body.Mu, End - Patch.StartTime), Body.Mu);
		auto PointAt = [&](double Fraction)
		{
			if (bClosed)
			{
				const double Time = Start + (End - Start) * Fraction;
				return ToView(PropagateKepler(Patch.StartState, Body.Mu, Time - Patch.StartTime).Position + BodyNow, Origin);
			}
			OrbitalElements At = StartElements;
			At.TrueAnomaly = FMath::Lerp(StartElements.TrueAnomaly, EndElements.TrueAnomaly, Fraction);
			return ToView(ElementsToState(At, Body.Mu).Position + BodyNow, Origin);
		};
		FVector Previous = PointAt(0.0);
		for (int32 Segment = 1; Segment <= OrbitSegments; ++Segment)
		{
			const FVector Point = PointAt(static_cast<double>(Segment) / OrbitSegments);
			DrawDebugLine(GetWorld(), Previous, Point, Color, false, -1.0f, 0, 0.0f);
			Previous = Point;
		}

		// Apsis markers, when the orbit is not round enough to make them meaningless.
		if (bActive && Patch.Elements.Eccentricity > 1e-3)
		{
			OrbitalElements AtApsis = Patch.Elements;
			AtApsis.TrueAnomaly = 0.0;
			const FVector Pe = ToView(ElementsToState(AtApsis, Body.Mu).Position + BodyNow, Origin);
			DrawDebugPoint(GetWorld(), Pe, 8.0f, Color);
			DrawDebugString(GetWorld(), Pe, FString::Printf(TEXT("Pe %s"), *FormatAltitude(PeriapsisRadius(Patch.Elements) - Body.Radius)),
				nullptr, Color, LabelDuration, true, 1.0f);
			if (bClosed)
			{
				AtApsis.TrueAnomaly = PI;
				const FVector Ap = ToView(ElementsToState(AtApsis, Body.Mu).Position + BodyNow, Origin);
				DrawDebugPoint(GetWorld(), Ap, 8.0f, Color);
				DrawDebugString(GetWorld(), Ap, FString::Printf(TEXT("Ap %s"), *FormatAltitude(ApoapsisRadius(Patch.Elements) - Body.Radius)),
					nullptr, Color, LabelDuration, true, 1.0f);
			}
		}

		if (bActive && Patch.EndReason != PatchEnd::Horizon)
		{
			const FVector EndPoint = ToView(Patch.EndState.Position + BodyNow, Origin);
			FString Label = TEXT("Impact");
			if (Patch.EndReason != PatchEnd::Impact)
			{
				Label = FString::Printf(TEXT("SOI -> %s"), *Universe.GetBodyInfo(Patch.NextBodyIndex).DisplayName);
			}
			DrawDebugPoint(GetWorld(), EndPoint, 10.0f, Color);
			DrawDebugString(GetWorld(), EndPoint, Label, nullptr, Color, LabelDuration, true, 1.0f);
		}
	}
}

void APeriapsisUniverseView::DrawVessels(FPeriapsisUniverse& Universe, const Vec3& Origin, double Time) const
{
	for (int32 Vessel = 0; Vessel < Universe.NumVessels(); ++Vessel)
	{
		const Flight& VesselFlight = *Universe.GetVessel(Vessel).Flight;
		const FVector At = VesselLocation(Universe, Vessel, Origin, Time);
		const bool bCrashed = VesselFlight.Status() == FlightStatus::Crashed;
		if (Vessel != FPeriapsisUniverse::ActiveVessel)
		{
			DrawDebugPoint(GetWorld(), At, 9.0f, bCrashed ? FColor::Red : OtherVesselColor);
			const double Distance = Universe.VesselDistance(Vessel, FPeriapsisUniverse::ActiveVessel);
			DrawDebugString(GetWorld(), At, FString::Printf(TEXT("%s  %s"), *Universe.GetVessel(Vessel).Name, *FormatAltitude(Distance)),
				nullptr, OtherVesselColor, LabelDuration, true, 1.0f);
			continue;
		}

		DrawDebugPoint(GetWorld(), At, 12.0f, bCrashed ? FColor::Red : FColor::White);
		// Engine direction, and velocity relative to the current body.
		const double Length = 0.06 * ScreenScale(At);
		const FVector Thrust = FUnitBridge::DirectionToUnreal(VesselFlight.ThrustDirection());
		const FVector Velocity = FUnitBridge::DirectionToUnreal(VesselFlight.State().Velocity);
		DrawDebugLine(GetWorld(), At, At + Velocity * Length, FColor::Green, false, -1.0f, 1, 0.0f);
		DrawDebugDirectionalArrow(GetWorld(), At, At + Thrust * Length,
			Length * 0.3, VesselFlight.IsThrusting() ? FColor(255, 90, 40) : FColor(160, 160, 160), false, -1.0f, 1, 0.0f);
	}
}
