#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "PeriapsisHUD.generated.h"

// Flight readout and controls help, drawn as plain text (M2: debug-grade UI).
UCLASS()
class PERIAPSISGAME_API APeriapsisHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	// Formats a duration as "1d 02h 03m 04s", dropping leading zero units.
	static FString FormatDuration(double Seconds);

private:
	void DrawLines(const TArray<FString>& Lines, float X, float Y, const FLinearColor& Color, float Scale);
};
