#include "OreDealInventoryWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "OreDealTablePlayerController.h"

namespace
{
	UTextBlock* MakeInventoryText(UWidgetTree* Tree, int32 Size)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.94f, 0.78f)));
		Text->SetShadowOffset(FVector2D(1.5f, 1.5f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		return Text;
	}

	void SetAnchor(UWidget* Widget, const FAnchors& Anchors, const FVector2D& Alignment,
		const FVector2D& Position, const FVector2D& Size, int32 Z)
	{
		if (UCanvasPanelSlot* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr)
		{
			Slot->SetAnchors(Anchors);
			Slot->SetAlignment(Alignment);
			Slot->SetPosition(Position);
			Slot->SetSize(Size);
			Slot->SetZOrder(Z);
		}
	}
}

void UOreDealInventoryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	CardBack = LoadObject<UTexture2D>(nullptr, TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Back.T_Card_Back"));
	WoodCard = LoadObject<UTexture2D>(nullptr, TEXT("/Game/MaterialTraders/Cards/wood.wood"));
	if (!RootCanvas) BuildRoot();
	SetVisibility(ESlateVisibility::Visible);
	RefreshPile();
}

void UOreDealInventoryWidget::BuildRoot()
{
	if (!WidgetTree) return;
	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("OreDealInventoryRoot"));
	WidgetTree->RootWidget = RootCanvas;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		PileCards[Index] = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		if (CardBack) PileCards[Index]->SetBrushFromTexture(CardBack);
		PileCards[Index]->SetVisibility(ESlateVisibility::Collapsed);
		RootCanvas->AddChild(PileCards[Index]);
	}
	PileLabel = MakeInventoryText(WidgetTree, 14);
	RootCanvas->AddChild(PileLabel);

	RevealCard = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	if (WoodCard) RevealCard->SetBrushFromTexture(WoodCard);
	RevealCard->SetVisibility(ESlateVisibility::Collapsed);
	RootCanvas->AddChild(RevealCard);
	RevealLabel = MakeInventoryText(WidgetTree, 25);
	RevealLabel->SetJustification(ETextJustify::Center);
	RevealLabel->SetVisibility(ESlateVisibility::Collapsed);
	RootCanvas->AddChild(RevealLabel);
	SetAnchor(RevealCard, FAnchors(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D(0.0f, -5.0f), FVector2D(160.0f, 224.0f), 20);
	SetAnchor(RevealLabel, FAnchors(0.5f, 0.5f), FVector2D(0.5f, 0.0f), FVector2D(0.0f, 120.0f), FVector2D(360.0f, 48.0f), 21);
}

void UOreDealInventoryWidget::RefreshPile()
{
	const AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer());
	if (!PC || !RootCanvas) return;
	TArray<int32> Counts;
	PC->GetStoreInventoryCounts(Counts);
	int32 Total = FMath::Max(0, PC->GetStorePlayerWood());
	int32 ItemTotal = 0;
	for (const int32 Count : Counts) ItemTotal += FMath::Max(0, Count);
	Total += ItemTotal;
	const int32 VisibleCount = FMath::Min(3, Total);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (PileCards[Index])
		{
			SetAnchor(PileCards[Index], FAnchors(0.0f, 1.0f), FVector2D(0.0f, 1.0f), FVector2D(30.0f + Index * 16.0f, -35.0f - Index * 5.0f), FVector2D(64.0f, 90.0f), 1 + Index);
			PileCards[Index]->SetVisibility(Index < VisibleCount ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
	if (PileLabel)
	{
		PileLabel->SetText(FText::FromString(FString::Printf(TEXT("나무 %d  ·  아이템 %d\n인벤토리 열기"), PC->GetStorePlayerWood(), ItemTotal)));
		SetAnchor(PileLabel, FAnchors(0.0f, 1.0f), FVector2D(0.0f, 1.0f), FVector2D(28.0f, -135.0f), FVector2D(220.0f, 42.0f), 6);
		PileLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UOreDealInventoryWidget::QueueWoodCardReveal(int32 Amount)
{
	if (Amount <= 0) return;
	PendingRevealAmount += Amount;
	// The hand-card draw animation completes before the special-wood card is presented.
	RevealDelay = FMath::Max(RevealDelay, 1.1f);
}

void UOreDealInventoryWidget::BeginWoodReveal()
{
	if (!RevealCard || !RevealLabel || PendingRevealAmount <= 0) return;
	RevealLabel->SetText(FText::FromString(FString::Printf(TEXT("나무 카드  +%d"), PendingRevealAmount)));
	PendingRevealAmount = 0;
	RevealRemaining = 1.8f;
	RevealCard->SetRenderScale(FVector2D(0.68f, 0.68f));
	RevealCard->SetRenderOpacity(0.0f);
	RevealLabel->SetRenderOpacity(0.0f);
	RevealCard->SetVisibility(ESlateVisibility::HitTestInvisible);
	RevealLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UOreDealInventoryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshElapsed += InDeltaTime;
	if (RefreshElapsed >= 0.35f) { RefreshElapsed = 0.0f; RefreshPile(); }
	if (RevealDelay > 0.0f)
	{
		RevealDelay = FMath::Max(0.0f, RevealDelay - InDeltaTime);
		if (RevealDelay <= 0.0f) BeginWoodReveal();
	}
	if (RevealRemaining > 0.0f)
	{
		RevealRemaining = FMath::Max(0.0f, RevealRemaining - InDeltaTime);
		const float Alpha = FMath::Clamp(FMath::Min((1.8f - RevealRemaining) / 0.18f, RevealRemaining / 0.25f), 0.0f, 1.0f);
		const float Scale = FMath::Lerp(0.95f, 0.68f, FMath::Clamp((1.8f - RevealRemaining) / 1.8f, 0.0f, 1.0f));
		RevealCard->SetRenderScale(FVector2D(Scale, Scale));
		RevealCard->SetRenderOpacity(Alpha);
		RevealLabel->SetRenderOpacity(Alpha);
		if (RevealRemaining <= 0.0f)
		{
			RevealCard->SetVisibility(ESlateVisibility::Collapsed);
			RevealLabel->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

FReply UOreDealInventoryWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	const FVector2D Local = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const FVector2D Size = InGeometry.GetLocalSize();
	if (Local.X > 270.0f || Local.Y < Size.Y - 190.0f) return FReply::Unhandled();
	if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer())) PC->OpenStoreInventory();
	return FReply::Handled();
}
