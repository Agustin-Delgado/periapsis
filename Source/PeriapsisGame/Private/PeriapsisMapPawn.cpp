#include "PeriapsisMapPawn.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnitBridge.h"

APeriapsisMapPawn::APeriapsisMapPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Arm"));
	Arm->SetupAttachment(RootComponent);
	Arm->bDoCollisionTest = false;
	Arm->bEnableCameraLag = false;
	Arm->bUsePawnControlRotation = false;
	Arm->SetUsingAbsoluteRotation(true);
	ApplyRotation();
	Arm->TargetArmLength = 5.0e8; // 5,000 km: the home planet and the parking orbit
	// -CameraDistance=<metres> starts closer, e.g. on the ground (screenshots, landing tests).
	double Metres = 0.0;
	if (FParse::Value(FCommandLine::Get(), TEXT("CameraDistance="), Metres) && Metres > 0.0)
	{
		Arm->TargetArmLength = static_cast<float>(FUnitBridge::ToUnreal(Metres));
	}

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Arm, USpringArmComponent::SocketName);
	Camera->SetFieldOfView(60.0f);
}

void APeriapsisMapPawn::Orbit(double DeltaYawDegrees, double DeltaPitchDegrees)
{
	Yaw = FRotator::NormalizeAxis(Yaw + DeltaYawDegrees);
	Pitch = FMath::Clamp(Pitch + DeltaPitchDegrees, -89.0, 89.0);
	ApplyRotation();
}

void APeriapsisMapPawn::SetYaw(double YawDegrees)
{
	Yaw = FRotator::NormalizeAxis(YawDegrees);
	ApplyRotation();
}

void APeriapsisMapPawn::SetUp(const FVector& Up)
{
	const FVector Unit = Up.GetSafeNormal();
	if (!Unit.IsZero())
	{
		// The smallest turn from the up of now, so the view does not spin about the new up.
		TargetUpFrame = FQuat::FindBetweenNormals(TargetUpFrame.GetUpVector(), Unit) * TargetUpFrame;
	}
}

void APeriapsisMapPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!UpFrame.Equals(TargetUpFrame, 1e-6))
	{
		const float RealDelta = GetWorld() ? GetWorld()->DeltaRealTimeSeconds : DeltaSeconds;
		UpFrame = FQuat::Slerp(UpFrame, TargetUpFrame, FMath::Clamp(RealDelta * 4.0f, 0.0f, 1.0f)).GetNormalized();
		ApplyRotation();
	}
}

void APeriapsisMapPawn::ApplyRotation()
{
	Arm->SetRelativeRotation(UpFrame * FQuat(FRotator(Pitch, Yaw, 0.0)));
}

void APeriapsisMapPawn::Zoom(double Factor, double MinDistance)
{
	Arm->TargetArmLength = static_cast<float>(FMath::Clamp(Arm->TargetArmLength * Factor, FMath::Max(MinDistance, 1000.0), MaxDistance));
}

void APeriapsisMapPawn::SetDistance(double Distance)
{
	Arm->TargetArmLength = static_cast<float>(FMath::Clamp(Distance, 1000.0, MaxDistance));
}

double APeriapsisMapPawn::GetDistance() const
{
	return Arm->TargetArmLength;
}
