#include "PeriapsisMapPawn.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

APeriapsisMapPawn::APeriapsisMapPawn()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Arm"));
	Arm->SetupAttachment(RootComponent);
	Arm->bDoCollisionTest = false;
	Arm->bEnableCameraLag = false;
	Arm->bUsePawnControlRotation = false;
	Arm->SetUsingAbsoluteRotation(true);
	Arm->SetRelativeRotation(FRotator(-35.0, 0.0, 0.0));
	Arm->TargetArmLength = 5.0e8; // 5,000 km: the home planet and the parking orbit

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Arm, USpringArmComponent::SocketName);
	Camera->SetFieldOfView(60.0f);
}

void APeriapsisMapPawn::Orbit(double DeltaYawDegrees, double DeltaPitchDegrees)
{
	FRotator Rotation = Arm->GetRelativeRotation();
	Rotation.Yaw += DeltaYawDegrees;
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + DeltaPitchDegrees, -89.0, 89.0);
	Arm->SetRelativeRotation(Rotation);
}

void APeriapsisMapPawn::Zoom(double Factor, double MinDistance)
{
	Arm->TargetArmLength = static_cast<float>(FMath::Clamp(Arm->TargetArmLength * Factor, FMath::Max(MinDistance, 1000.0), MaxDistance));
}

double APeriapsisMapPawn::GetDistance() const
{
	return Arm->TargetArmLength;
}
