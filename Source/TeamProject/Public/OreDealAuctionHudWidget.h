#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OreDealAuctionHudWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UTextBlock;
class UTexture2D;

/** Everything the auction HUD shows for the local player, rebuilt by UOreDealAuctionComponent each tick. */
struct FOreDealAuctionHudState
{
	FString Status;

	/** Centre-bottom hand picker (Z/X move, C select, Space confirm). */
	bool bPickerOpen = false;
	TArray<int32> PickerCards;
	int32 PickerCursor = 0;
	TArray<int32> PickerSelected;
	FString PickerHint;

	/** Bottom-right queue of taken cards that must be placed. Only the front card is shown. */
	TArray<int32> PendingCards;
	int32 WagonFocus = 0;
	int32 WagonCount = 2;
	bool bPendingInputEnabled = true;
};

/** Local-only HUD for the auction trade phase. Display only: never takes mouse or keyboard focus. */
UCLASS()
class TEAMPROJECT_API UOreDealAuctionHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	void ApplyState(const FOreDealAuctionHudState& State);
	void Flash(const FString& Message, float Seconds = 3.0f);

private:
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlashText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PickerHintText;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> PickerCardWidgets;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PickerMarks;
	UPROPERTY(Transient) TObjectPtr<UBorder> PendingCardWidget;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PendingText;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> CardTextures;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> CardBackTexture;

	TArray<int32> BuiltPickerCards;
	float FlashRemaining = 0.0f;

	void BuildRoot();
	void RebuildPicker(const TArray<int32>& Cards);
	UTexture2D* TextureFor(int32 Resource) const;
};
