#include "OreDealCeremonyWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"

void UOreDealCeremonyWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree) return;
	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CeremonyRoot"));
	WidgetTree->RootWidget = RootCanvas;
	RootCanvas->SetVisibility(ESlateVisibility::HitTestInvisible);
	Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CeremonyFrame"));
	Frame->SetBrushColor(FLinearColor(0.16f, 0.075f, 0.035f, 0.8f));
	Frame->SetPadding(FMargin(34.0f, 16.0f));
	Frame->SetHorizontalAlignment(HAlign_Center);
	Frame->SetVerticalAlignment(VAlign_Center);
	Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
	PromptText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CeremonyPrompt"));
	FSlateFontInfo Font = PromptText->GetFont();
	Font.Size = 48;
	Font.TypefaceFontName = TEXT("Bold");
	PromptText->SetFont(Font);
	PromptText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.91f, 0.68f, 1.0f)));
	PromptText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	PromptText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.95f));
	PromptText->SetJustification(ETextJustify::Center);
	Frame->SetContent(PromptText);
	PromptSlot = RootCanvas->AddChildToCanvas(Frame);
	PromptSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	PromptSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	PromptSlot->SetAutoSize(true);
	PromptSlot->SetZOrder(1000);
	SetIsFocusable(false);
	HidePrompt();
}

void UOreDealCeremonyWidget::SetPrompt(const FText& Text, bool bTop)
{
	if (!PromptText || !Frame || !PromptSlot) return;
	PromptText->SetText(Text);
	PromptSlot->SetAnchors(bTop ? FAnchors(0.5f, 0.0f) : FAnchors(0.5f, 0.5f));
	PromptSlot->SetAlignment(FVector2D(0.5f, bTop ? 0.0f : 0.5f));
	PromptSlot->SetPosition(FVector2D(0.0f, bTop ? 44.0f : 0.0f));
	Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UOreDealCeremonyWidget::HidePrompt()
{
	if (Frame) Frame->SetVisibility(ESlateVisibility::Collapsed);
	SetVisibility(ESlateVisibility::Collapsed);
}
