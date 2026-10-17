#include "PeriapsisBuilderView.h"

#include "Components/DirectionalLightComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PeriapsisBubble.h"
#include "PeriapsisDataLoader.h"
#include "PeriapsisFlightSubsystem.h"
#include "PeriapsisMapPawn.h"
#include "PeriapsisUniverse.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UnitBridge.h"

using namespace Periapsis::Core;

namespace
{
	const FLinearColor PreviewColor(0.2f, 0.9f, 0.3f);
	const FColor TargetColor(60, 230, 90);
	const FColor CenterOfMassColor(255, 220, 40);
}

APeriapsisBuilderView::APeriapsisBuilderView()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	DesignMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Design"));
	DesignMesh->SetupAttachment(RootComponent);
	DesignMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Preview"));
	PreviewMesh->SetupAttachment(RootComponent);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetCastShadow(false);

	Light = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Light"));
	Light->SetupAttachment(RootComponent);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetIntensity(6.0f);
	Light->SetRelativeRotation(FRotator(-35.0, 140.0, 0.0));
	Light->ForwardShadingPriority = 2; // over the sun (hidden with the universe) and the fill light
	FillLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(RootComponent);
	FillLight->SetMobility(EComponentMobility::Movable);
	FillLight->SetIntensity(2.5f);
	FillLight->SetCastShadows(false);
	FillLight->SetRelativeRotation(FRotator(-10.0, -40.0, 0.0));
	FillLight->ForwardShadingPriority = 0;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	Material = BasicMaterial.Object;
	SetActorHiddenInGame(true);
}

UPeriapsisFlightSubsystem* APeriapsisBuilderView::GetSubsystem() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UPeriapsisFlightSubsystem>() : nullptr;
}

void APeriapsisBuilderView::EnsureBuilder()
{
	if (Builder)
	{
		return;
	}
	const UPeriapsisFlightSubsystem* Subsystem = GetSubsystem();
	const FPeriapsisUniverse* Universe = Subsystem ? Subsystem->GetUniverse() : nullptr;
	if (!Universe || !Universe->GetScenario().Catalog)
	{
		return;
	}
	// Starts from the ship of the scenario when it is made of parts.
	const FPeriapsisScenario& Scenario = Universe->GetScenario();
	Builder = MakeUnique<FPeriapsisBuilder>(Scenario.Catalog, Scenario.ShipDesign);
	if (Scenario.ShipDesign.IsEmpty())
	{
		NewDesign();
	}
	Builder->SelectPartById(TEXT("tank_small_long"));
	bDirty = true;
}

void APeriapsisBuilderView::NewDesign()
{
	if (!Builder)
	{
		return;
	}
	VesselDesign Fresh;
	Fresh.Name = "Untitled";
	const int Pod = Builder->GetCatalog().FindPart("pod_kestrel");
	Fresh.SetRoot(Builder->GetCatalog(), Pod >= 0 ? Pod : 0);
	Builder->Load(Fresh);
	bDirty = true;
}

FString APeriapsisBuilderView::Save()
{
	if (!Builder)
	{
		return FString();
	}
	FString Name = UTF8_TO_TCHAR(Builder->GetDesign().Name.c_str());
	Name = FPaths::MakeValidFileName(Name.IsEmpty() ? FString(TEXT("Untitled")) : Name).Replace(TEXT(" "), TEXT("_"));
	const FString Path = FPaths::Combine(PeriapsisData::SavedDesignsDirectory(), Name + TEXT(".json"));
	const bool bSaved = FFileHelper::SaveStringToFile(PeriapsisData::SerializeDesign(Builder->GetDesign(), Builder->GetCatalog()), *Path);
	return bSaved ? Path : FString();
}

void APeriapsisBuilderView::LoadNext()
{
	if (!Builder)
	{
		return;
	}
	// The shipped designs, then the saved ones.
	TArray<FString> Paths;
	TArray<FString> Found;
	const FString Shipped = FPaths::Combine(PeriapsisData::DataDirectory(), TEXT("vessels"));
	IFileManager::Get().FindFiles(Found, *FPaths::Combine(Shipped, TEXT("*.json")), true, false);
	Found.Sort();
	for (const FString& File : Found)
	{
		Paths.Add(FPaths::Combine(Shipped, File));
	}
	Found.Reset();
	IFileManager::Get().FindFiles(Found, *FPaths::Combine(PeriapsisData::SavedDesignsDirectory(), TEXT("*.json")), true, false);
	Found.Sort();
	for (const FString& File : Found)
	{
		Paths.Add(FPaths::Combine(PeriapsisData::SavedDesignsDirectory(), File));
	}
	for (int32 Attempt = 0; Attempt < Paths.Num(); ++Attempt)
	{
		const FString& Path = Paths[(NextDesignToLoad + Attempt) % Paths.Num()];
		VesselDesign Loaded;
		TArray<FString> Errors;
		if (PeriapsisData::LoadDesign(Path, PeriapsisData::DataDirectory(), Builder->GetCatalog(), Loaded, Errors))
		{
			NextDesignToLoad = (NextDesignToLoad + Attempt + 1) % Paths.Num();
			Builder->Load(MoveTemp(Loaded));
			bDirty = true;
			return;
		}
	}
}

double APeriapsisBuilderView::GetRadiusUnreal() const
{
	if (!Builder || Builder->GetDesign().IsEmpty())
	{
		return FUnitBridge::ToUnreal(3.0);
	}
	return FUnitBridge::ToUnreal(Builder->MakeVessel().BoundingRadius());
}

void APeriapsisBuilderView::Rebuild()
{
	const Vessel Craft = Builder->MakeVessel();
	if (Craft.IsValid())
	{
		BuildVesselMesh(DesignMesh, Craft, Material, false);
	}
	else
	{
		DesignMesh->ClearAllMeshSections();
	}
	BuildPartsMesh(PreviewMesh, Builder->GetCatalog(), Builder->Preview(), Material, false, &PreviewColor);
	LastSelectedPart = Builder->GetSelectedPart();
	LastTarget = Builder->GetTargetIndex();
	LastSideAngle = Builder->GetSideAngle();
	LastSideHeight = Builder->GetSideHeight();
	LastSymmetry = Builder->GetSymmetry();
	bDirty = false;
}

void APeriapsisBuilderView::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UPeriapsisFlightSubsystem* Subsystem = GetSubsystem();
	const bool bBuilding = Subsystem && Subsystem->IsBuilding();
	SetActorHiddenInGame(!bBuilding);
	Light->SetVisibility(bBuilding);
	FillLight->SetVisibility(bBuilding);
	if (!bBuilding)
	{
		return;
	}
	EnsureBuilder();
	if (!Builder)
	{
		return;
	}
	// Whatever moved the target or changed the part redraws the preview.
	if (bDirty || LastSelectedPart != Builder->GetSelectedPart() || LastTarget != Builder->GetTargetIndex()
		|| LastSideAngle != Builder->GetSideAngle() || LastSideHeight != Builder->GetSideHeight() || LastSymmetry != Builder->GetSymmetry())
	{
		Rebuild();
	}

	const Vessel Craft = Builder->MakeVessel();
	FVector Center = FVector::ZeroVector;
	double Radius = 3.0; // m
	if (Craft.IsValid())
	{
		const FVector CenterOfMass = FUnitBridge::ToUnreal(Craft.CenterOfMass());
		DrawDebugSphere(GetWorld(), CenterOfMass, 25.0f, 12, CenterOfMassColor, false, -1.0f, SDPG_Foreground, 2.0f);
		DrawDebugString(GetWorld(), CenterOfMass + FVector(0.0, 0.0, 40.0), TEXT("CoM"), nullptr, CenterOfMassColor, 0.0f, true, 1.0f);
		// The camera looks at the middle of the design, not at its centre of mass (which sits low
		// in a fuelled rocket), and backs off to fit it.
		const Vec3 Axis{0.0, 0.0, 1.0};
		const Vec3 From = Craft.CenterOfMass();
		const double Top = Craft.Extent(Axis, From);
		const double Bottom = Craft.Extent(-Axis, From);
		Center = FUnitBridge::ToUnreal(From + Axis * (0.5 * (Top - Bottom)));
		double Side = 0.0;
		for (const Vec3& Direction : {Vec3{1.0, 0.0, 0.0}, Vec3{-1.0, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0}, Vec3{0.0, -1.0, 0.0}})
		{
			Side = FMath::Max(Side, Craft.Extent(Direction, From));
		}
		Radius = FMath::Max(0.5 * (Top + Bottom), Side);
	}
	const FPeriapsisAttachTarget Target = Builder->GetTarget();
	if (Target.Instance != INDEX_NONE)
	{
		const FVector At = FUnitBridge::ToUnreal(Target.Position);
		const FVector Out = FUnitBridge::DirectionToUnreal(Target.Direction);
		const float Length = FMath::Clamp(static_cast<float>(FUnitBridge::ToUnreal(Radius)) * 0.25f, 60.0f, 400.0f);
		DrawDebugDirectionalArrow(GetWorld(), At, At + Out * Length, Length * 0.3f, TargetColor, false, -1.0f, SDPG_Foreground, 3.0f);
		DrawDebugPoint(GetWorld(), At, 12.0f, TargetColor, false, -1.0f, SDPG_Foreground);
	}
	if (APeriapsisMapPawn* Camera = Cast<APeriapsisMapPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		Camera->SetActorLocation(Center);
		Camera->SetUp(FVector::UpVector);
		// Refit when the design changes size; between changes the zoom is the player's.
		if (!FMath::IsNearlyEqual(Radius, FramedRadius, 0.05 * FramedRadius))
		{
			FramedRadius = Radius;
			Camera->SetDistance(FUnitBridge::ToUnreal(Radius) * 3.6);
		}
	}
}
