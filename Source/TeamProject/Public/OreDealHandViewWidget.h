#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OreDealHandViewWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UTextBlock;
class UTexture2D;

/**
 * Local-only hand viewer shown at the bottom-left of the owning player's screen.
 * Q toggles it, Z/X move the focused card. The focused card sits at a fixed anchor and is drawn
 * largest; later cards fan out to the right behind it and earlier cards to the left behind it.
 */
UCLASS()
class TEAMPROJECT_API UOreDealHandViewWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	void SetOpen(bool bInOpen);
	bool IsOpen() const { return bOpen; }
	void ToggleOpen() { SetOpen(!bOpen); }
	/** Moves the focused card by Delta (X = +1, Z = -1). */
	void StepFocus(int32 Delta);

private:
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HintText;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> CardWidgets;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CardNumbers;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> CardTextures;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> CardBackTexture;

	TArray<int32> HandCards;
	TArray<float> DisplayOffsets;
	int32 FocusIndex = 0;
	bool bOpen = false;
	float PollElapsed = 0.0f;

	void BuildRoot();
	void LoadTextures();
	void PollHand(bool bForce);
	void RebuildCards();
	void UpdateLayout(float DeltaTime, bool bSnap);
	void UpdateHint();
};
