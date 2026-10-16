#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PeriapsisMapPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;

// Orbit camera around the world origin, where the universe view puts the focus.
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

	static constexpr double MaxDistance = 1.0e11; // 1,000,000 km

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> Arm;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;
};
