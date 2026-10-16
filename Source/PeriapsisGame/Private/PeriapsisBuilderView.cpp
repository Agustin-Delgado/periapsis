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
	FillLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(RootComponent);
	FillLight->SetMobility(EComponentMobility::Movable);
	FillLight->SetIntensity(2.5f);
	FillLight->SetCastShadows(false);
	FillLight->SetRelativeRotation(FRotator(-10.0, -40.0, 0.0));

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
	const FVector Center = Craft.IsValid() ? FUnitBridge::ToUnreal(Craft.CenterOfMass()) : FVector::ZeroVector;
	if (Craft.IsValid())
	{
		DrawDebugSphere(GetWorld(), Center, 25.0f, 12, CenterOfMassColor, false, -1.0f, 1, 2.0f);
		DrawDebugString(GetWorld(), Center, TEXT("CoM"), nullptr, CenterOfMassColor, 0.0f, true, 1.0f);
	}
	const FPeriapsisAttachTarget Target = Builder->GetTarget();
	if (Target.Instance != INDEX_NONE)
	{
		const FVector At = FUnitBridge::ToUnreal(Target.Position);
		const FVector Out = FUnitBridge::DirectionToUnreal(Target.Direction);
		DrawDebugDirectionalArrow(GetWorld(), At, At + Out * 120.0, 40.0f, TargetColor, false, -1.0f, 2, 3.0f);
		DrawDebugPoint(GetWorld(), At, 12.0f, TargetColor, false, -1.0f, 2);
	}
	if (APawn* Camera = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Camera->SetActorLocation(Center);
	}
}
