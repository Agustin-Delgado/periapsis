#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PeriapsisMapPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;

// Orbit camera around the focus, which the universe view (or the builder) moves the pawn to.
// Its pitch and yaw are taken about an "up" that follows the local vertical near the ground, so
// a rocket on the pad stands upright on the screen wherever on the planet it is.
UCLASS()
class PERIAPSISGAME_API APeriapsisMapPawn : public APawn
{
	GENERATED_BODY()

public:
	APeriapsisMapPawn();

	void Orbit(double DeltaYawDegrees, double DeltaPitchDegrees);
	// Multiplies the camera distance, staying outside MinDistance (Unreal units).
	void Zoom(double Factor, double MinDistance);
	double GetDistance() const;
	void SetDistance(double Distance);
	// The up of the orbit (world space, unit). The camera turns to it over a fraction of a second.
	void SetUp(const FVector& Up);
	void SetYaw(double YawDegrees);

	virtual void Tick(float DeltaSeconds) override;

	static constexpr double MaxDistance = 1.0e11; // 1,000,000 km

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> Arm;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	void ApplyRotation();

	double Pitch = -35.0;
	double Yaw = 0.0;
	FQuat UpFrame = FQuat::Identity;     // turns world Z to the current up
	FQuat TargetUpFrame = FQuat::Identity;
};
