#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OreDealCeremonyWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UCanvasPanelSlot;
class UTextBlock;

UCLASS()
class TEAMPROJECT_API UOreDealCeremonyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPrompt(const FText& Text, bool bTop);
	void HidePrompt();

protected:
	virtual void NativeOnInitialized() override;

private:
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
	UPROPERTY(Transient) TObjectPtr<UBorder> Frame;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PromptText;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> PromptSlot;
};
