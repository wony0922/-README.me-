#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OreDealInventoryWidget.generated.h"

class UCanvasPanel;
class UImage;
class UTextBlock;
class UTexture2D;

/** Small always-visible special-currency/item card pile and the post-draw wood-card reveal. */
UCLASS()
class TEAMPROJECT_API UOreDealInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	void QueueWoodCardReveal(int32 Amount);

private:
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
	UPROPERTY(Transient) TObjectPtr<UImage> PileCards[3];
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PileLabel;
	UPROPERTY(Transient) TObjectPtr<UImage> RevealCard;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RevealLabel;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> CardBack;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> WoodCard;
	float RefreshElapsed = 0.0f;
	float RevealDelay = 0.0f;
	float RevealRemaining = 0.0f;
	int32 PendingRevealAmount = 0;
	void BuildRoot();
	void RefreshPile();
	void BeginWoodReveal();
};
