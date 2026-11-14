#include "OreDealAuctionHudWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

namespace
{
	// Same order as BP_CardBoard.FloatMats and the hand viewer.
	const TCHAR* AuctionCardTexturePaths[] = {
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Stone.T_Card_Stone"),
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Coal.T_Card_Coal"),
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Gold.T_Card_Gold"),
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Iron.T_Card_Iron"),
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Diamond.T_Card_Diamond"),
	};
	const TCHAR* AuctionCardBackPath = TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Back.T_Card_Back");

	constexpr float PickerCardWidth = 110.0f;
	constexpr float PickerCardHeight = 154.0f;
	constexpr float PickerStep = 118.0f;
	constexpr float PickerMaxWidth = 1100.0f;
	constexpr float PickerBottom = 40.0f;
	constexpr float PickerLift = 30.0f;
	constexpr float PendingCardWidth = 140.0f;
	constexpr float PendingCardHeight = 196.0f;
	constexpr float ScreenMargin = 40.0f;

	UTextBlock* MakeLabel(UWidgetTree* Tree, int32 Size, const FLinearColor& Color)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = Size;
		Label->SetFont(Font);
		Label->SetColorAndOpacity(FSlateColor(Color));
		Label->SetShadowOffset(FVector2D(1.5f, 1.5f));
		Label->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		return Label;
	}

	const FLinearColor TextColor(0.98f, 0.95f, 0.86f, 1.0f);
	const FLinearColor GoldColor(1.0f, 0.78f, 0.25f, 1.0f);
	const FLinearColor WarnColor(1.0f, 0.55f, 0.35f, 1.0f);
}

void UOreDealAuctionHudWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	for (const TCHAR* Path : AuctionCardTexturePaths) CardTextures.Add(LoadObject<UTexture2D>(nullptr, Path));
	CardBackTexture = LoadObject<UTexture2D>(nullptr, AuctionCardBackPath);
	if (!RootCanvas) BuildRoot();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

UTexture2D* UOreDealAuctionHudWidget::TextureFor(int32 Resource) const
{
	UTexture2D* Texture = CardTextures.IsValidIndex(Resource) ? CardTextures[Resource].Get() : nullptr;
	return Texture ? Texture : CardBackTexture.Get();
}

void UOreDealAuctionHudWidget::BuildRoot()
{
	if (!WidgetTree) return;
	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("OreDealAuctionRoot"));
	WidgetTree->RootWidget = RootCanvas;

	StatusText = MakeLabel(WidgetTree, 20, TextColor);
	StatusText->SetJustification(ETextJustify::Center);
	if (UCanvasPanelSlot* StatusSlot = RootCanvas->AddChildToCanvas(StatusText))
	{
		StatusSlot->SetAnchors(FAnchors(0.5f, 0.0f));
		StatusSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		StatusSlot->SetAutoSize(true);
		StatusSlot->SetPosition(FVector2D(0.0f, 150.0f));
		StatusSlot->SetZOrder(10);
	}

	FlashText = MakeLabel(WidgetTree, 26, WarnColor);
	FlashText->SetJustification(ETextJustify::Center);
	FlashText->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* FlashSlot = RootCanvas->AddChildToCanvas(FlashText))
	{
		FlashSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		FlashSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		FlashSlot->SetAutoSize(true);
		FlashSlot->SetPosition(FVector2D(0.0f, -140.0f));
		FlashSlot->SetZOrder(20);
	}

	PickerHintText = MakeLabel(WidgetTree, 17, GoldColor);
	PickerHintText->SetJustification(ETextJustify::Center);
	PickerHintText->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* HintSlot = RootCanvas->AddChildToCanvas(PickerHintText))
	{
		HintSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		HintSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		HintSlot->SetAutoSize(true);
		HintSlot->SetPosition(FVector2D(0.0f, -(PickerBottom + PickerCardHeight * 1.15f + PickerLift + 14.0f)));
		HintSlot->SetZOrder(10);
	}

	PendingCardWidget = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	PendingCardWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* CardSlot = RootCanvas->AddChildToCanvas(PendingCardWidget))
	{
		CardSlot->SetAnchors(FAnchors(1.0f, 1.0f));
		CardSlot->SetAlignment(FVector2D(1.0f, 1.0f));
		CardSlot->SetSize(FVector2D(PendingCardWidth, PendingCardHeight));
		CardSlot->SetPosition(FVector2D(-ScreenMargin, -ScreenMargin));
		CardSlot->SetZOrder(10);
	}

	PendingText = MakeLabel(WidgetTree, 16, TextColor);
	PendingText->SetJustification(ETextJustify::Right);
	PendingText->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* TextSlot = RootCanvas->AddChildToCanvas(PendingText))
	{
		TextSlot->SetAnchors(FAnchors(1.0f, 1.0f));
		TextSlot->SetAlignment(FVector2D(1.0f, 1.0f));
		TextSlot->SetAutoSize(true);
		TextSlot->SetPosition(FVector2D(-ScreenMargin, -(ScreenMargin + PendingCardHeight + 12.0f)));
		TextSlot->SetZOrder(10);
	}
}

void UOreDealAuctionHudWidget::RebuildPicker(const TArray<int32>& Cards)
{
	for (UBorder* Card : PickerCardWidgets)
		if (Card) Card->RemoveFromParent();
	PickerCardWidgets.Reset();
	PickerMarks.Reset();
	BuiltPickerCards = Cards;
	if (!RootCanvas || !WidgetTree) return;

	for (int32 Index = 0; Index < Cards.Num(); ++Index)
	{
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		if (UTexture2D* Texture = TextureFor(Cards[Index])) Card->SetBrushFromTexture(Texture);
		Card->SetPadding(FMargin(8.0f, 6.0f));
		Card->SetHorizontalAlignment(HAlign_Left);
		Card->SetVerticalAlignment(VAlign_Top);
		Card->SetRenderTransformPivot(FVector2D(0.5f, 1.0f));
		UTextBlock* Mark = MakeLabel(WidgetTree, 15, TextColor);
		Card->SetContent(Mark);
		if (UCanvasPanelSlot* CardSlot = RootCanvas->AddChildToCanvas(Card))
		{
			CardSlot->SetAnchors(FAnchors(0.5f, 1.0f));
			CardSlot->SetAlignment(FVector2D(0.5f, 1.0f));
			CardSlot->SetSize(FVector2D(PickerCardWidth, PickerCardHeight));
		}
		PickerCardWidgets.Add(Card);
		PickerMarks.Add(Mark);
	}
}

void UOreDealAuctionHudWidget::ApplyState(const FOreDealAuctionHudState& State)
{
	if (!RootCanvas) return;

	if (StatusText)
	{
		StatusText->SetText(FText::FromString(State.Status));
		StatusText->SetVisibility(State.Status.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	// Hand picker.
	const TArray<int32> EmptyCards;
	const TArray<int32>& Cards = State.bPickerOpen ? State.PickerCards : EmptyCards;
	if (Cards != BuiltPickerCards || PickerCardWidgets.Num() != Cards.Num()) RebuildPicker(Cards);
	const int32 Count = PickerCardWidgets.Num();
	const float Step = Count > 0 ? FMath::Min(PickerStep, PickerMaxWidth / Count) : PickerStep;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UBorder* Card = PickerCardWidgets[Index];
		if (!Card) continue;
		const bool bCursor = Index == State.PickerCursor;
		const bool bSelected = State.PickerSelected.Contains(Index);
		if (UCanvasPanelSlot* CardSlot = Cast<UCanvasPanelSlot>(Card->Slot))
		{
			const float X = (Index - (Count - 1) * 0.5f) * Step;
			const float Lift = bSelected ? PickerLift : 0.0f;
			CardSlot->SetPosition(FVector2D(X, -(PickerBottom + Lift)));
			CardSlot->SetZOrder(bCursor ? 200 : 100 + Index);
		}
		const float Scale = bCursor ? 1.15f : 1.0f;
		Card->SetRenderScale(FVector2D(Scale, Scale));
		const float Shade = bCursor ? 1.0f : (bSelected ? 0.92f : 0.68f);
		Card->SetBrushColor(FLinearColor(Shade, Shade, Shade, 1.0f));
		if (PickerMarks.IsValidIndex(Index) && PickerMarks[Index])
		{
			PickerMarks[Index]->SetText(FText::FromString(bSelected ? FString::Printf(TEXT("%02d  ✔ 선택"), Index + 1) : FString::Printf(TEXT("%02d"), Index + 1)));
			PickerMarks[Index]->SetColorAndOpacity(FSlateColor(bSelected ? GoldColor : TextColor));
		}
		Card->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (PickerHintText)
	{
		PickerHintText->SetText(FText::FromString(State.PickerHint));
		PickerHintText->SetVisibility(State.bPickerOpen && !State.PickerHint.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	// Taken cards queue: only the front card is visible; no browsing.
	const bool bPending = !State.PendingCards.IsEmpty();
	if (PendingCardWidget)
	{
		PendingCardWidget->SetVisibility(bPending ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bPending)
			if (UTexture2D* Texture = TextureFor(State.PendingCards[0])) PendingCardWidget->SetBrushFromTexture(Texture);
	}
	if (PendingText)
	{
		PendingText->SetVisibility(bPending ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bPending)
		{
			FString WagonLine;
			for (int32 Wagon = 0; Wagon < FMath::Max(State.WagonCount, 1); ++Wagon)
			{
				if (!WagonLine.IsEmpty()) WagonLine += TEXT("  ");
				WagonLine += Wagon == State.WagonFocus ? FString::Printf(TEXT("[▶ 마차 %d]"), Wagon + 1) : FString::Printf(TEXT("마차 %d"), Wagon + 1);
			}
			const FString Controls = State.bPendingInputEnabled
				? TEXT("A / D : 마차 선택   ·   Space : 배치")
				: TEXT("카드 선택을 마치면 배치할 수 있습니다");
			PendingText->SetText(FText::FromString(FString::Printf(TEXT("가져온 카드  %d장 남음\n%s\n%s"), State.PendingCards.Num(), *WagonLine, *Controls)));
		}
	}
}

void UOreDealAuctionHudWidget::Flash(const FString& Message, float Seconds)
{
	if (!FlashText) return;
	FlashText->SetText(FText::FromString(Message));
	FlashText->SetVisibility(ESlateVisibility::HitTestInvisible);
	FlashRemaining = Seconds;
}

void UOreDealAuctionHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (FlashRemaining > 0.0f)
	{
		FlashRemaining -= InDeltaTime;
		if (FlashRemaining <= 0.0f && FlashText) FlashText->SetVisibility(ESlateVisibility::Collapsed);
	}
}
