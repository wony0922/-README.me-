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


	// Mona 12px pixel font (SIL OFL, /Game/MaterialTraders/UI/Fonts/F_Mona12KR). Sizes snap to multiples of 9 pt (= 12 px grid).
	FSlateFontInfo AuctionPixelFont(const FSlateFontInfo& Base, int32 Size)
	{
		static TWeakObjectPtr<UObject> Cached;
		if (!Cached.IsValid()) Cached = LoadObject<UObject>(nullptr, TEXT("/Game/MaterialTraders/UI/Fonts/F_Mona12KR.F_Mona12KR"));
		if (!Cached.IsValid())
		{
			FSlateFontInfo Fallback = Base;
			Fallback.Size = Size;
			return Fallback;
		}
		const int32 Snapped = FMath::Max(9, FMath::RoundToInt(Size / 9.0f) * 9);
		return FSlateFontInfo(Cached.Get(), Snapped, Snapped >= 27 ? FName(TEXT("Bold")) : FName(TEXT("Regular")));
	}

	UTextBlock* MakeLabel(UWidgetTree* Tree, int32 Size, const FLinearColor& Color)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetFont(AuctionPixelFont(Label->GetFont(), Size));
		Label->SetColorAndOpacity(FSlateColor(Color));
		Label->SetShadowOffset(FVector2D(1.5f, 1.5f));
		Label->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		return Label;
	}

	const FLinearColor TextColor(0.98f, 0.95f, 0.86f, 1.0f);
	const FLinearColor GoldColor(1.0f, 0.78f, 0.25f, 1.0f);

	// Dialogue UI pack (cream theme, 720p export). Text inside cream frames uses dark ink.
	const FLinearColor InkColor = FLinearColor(FColor(0x61, 0x3D, 0x3B));
	const FLinearColor InkWarn = FLinearColor(FColor(0xA8, 0x32, 0x28));

	FSlateBrush MakeAuctionDuiBrush(const TCHAR* Name, float NativeW, float NativeH, float L, float T, float R, float B, float PixelScale = 2.0f)
	{
		FSlateBrush Brush;
		if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/MaterialTraders/UI/Dialogue/T_DUI_%s.T_DUI_%s"), Name, Name)))
			Brush.SetResourceObject(Texture);
		Brush.ImageSize = FVector2D(NativeW * PixelScale, NativeH * PixelScale);
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.Margin = FMargin(L / NativeW, T / NativeH, R / NativeW, B / NativeH);
		return Brush;
	}

	/** Puts Label inside a cream frame and adds the frame to the canvas; the frame follows the label's visibility (see SyncFrame). */
	UCanvasPanelSlot* AddFramed(UWidgetTree* Tree, UCanvasPanel* Canvas, UTextBlock* Label, const FSlateBrush& Brush, const FMargin& Padding)
	{
		UBorder* Frame = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Frame->SetBrush(Brush);
		Frame->SetPadding(Padding);
		Frame->SetHorizontalAlignment(HAlign_Center);
		Frame->SetVerticalAlignment(VAlign_Center);
		Frame->SetContent(Label);
		Frame->SetVisibility(Label->GetVisibility());
		Label->SetShadowColorAndOpacity(FLinearColor::Transparent);
		return Canvas->AddChildToCanvas(Frame);
	}

	void SyncFrame(UTextBlock* Label)
	{
		if (!Label) return;
		if (UBorder* Frame = Cast<UBorder>(Label->GetParent()))
		{
			const ESlateVisibility Wanted = Label->GetVisibility() == ESlateVisibility::Collapsed ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible;
			if (Frame->GetVisibility() != Wanted) Frame->SetVisibility(Wanted);
		}
	}

	FSlateBrush NotificationBrush() { return MakeAuctionDuiBrush(TEXT("Notification"), 224, 32, 12, 8, 16, 8); }
	FSlateBrush TooltipBrush() { return MakeAuctionDuiBrush(TEXT("Tooltip"), 160, 32, 12, 8, 16, 8); }
	FSlateBrush ModalBrush() { return MakeAuctionDuiBrush(TEXT("Modal"), 320, 144, 12, 8, 16, 8); }
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

	StatusText = MakeLabel(WidgetTree, 19, InkColor);
	StatusText->SetJustification(ETextJustify::Center);
	if (UCanvasPanelSlot* StatusSlot = AddFramed(WidgetTree, RootCanvas, StatusText, NotificationBrush(), FMargin(26.0f, 8.0f, 32.0f, 10.0f)))
	{
		StatusSlot->SetAnchors(FAnchors(0.5f, 0.0f));
		StatusSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		StatusSlot->SetAutoSize(true);
		StatusSlot->SetPosition(FVector2D(0.0f, 230.0f));
		StatusSlot->SetZOrder(10);
	}

	FlashText = MakeLabel(WidgetTree, 24, InkWarn);
	FlashText->SetJustification(ETextJustify::Center);
	FlashText->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* FlashSlot = AddFramed(WidgetTree, RootCanvas, FlashText, ModalBrush(), FMargin(36.0f, 20.0f, 40.0f, 22.0f)))
	{
		FlashSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		FlashSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		FlashSlot->SetAutoSize(true);
		FlashSlot->SetPosition(FVector2D(0.0f, -140.0f));
		FlashSlot->SetZOrder(20);
	}

	PickerHintText = MakeLabel(WidgetTree, 16, InkColor);
	PickerHintText->SetJustification(ETextJustify::Center);
	PickerHintText->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* HintSlot = AddFramed(WidgetTree, RootCanvas, PickerHintText, TooltipBrush(), FMargin(24.0f, 7.0f, 30.0f, 9.0f)))
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

	PendingText = MakeLabel(WidgetTree, 15, InkColor);
	PendingText->SetJustification(ETextJustify::Right);
	PendingText->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* TextSlot = AddFramed(WidgetTree, RootCanvas, PendingText, NotificationBrush(), FMargin(24.0f, 10.0f, 30.0f, 12.0f)))
	{
		TextSlot->SetAnchors(FAnchors(1.0f, 0.0f));
		TextSlot->SetAlignment(FVector2D(1.0f, 0.0f));
		TextSlot->SetAutoSize(true);
		TextSlot->SetPosition(FVector2D(-ScreenMargin, 230.0f));
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
			static const TCHAR* ResourceNames[] = { TEXT("Rock"), TEXT("Coal"), TEXT("Copper"), TEXT("Iron"), TEXT("Silver"), TEXT("Gold"), TEXT("Diamond"), TEXT("Adamantium") };
			const int32 Resource = State.PickerCards.IsValidIndex(Index) ? State.PickerCards[Index] : INDEX_NONE;
			const FString Name = Resource >= 0 && Resource < UE_ARRAY_COUNT(ResourceNames) ? FString(ResourceNames[Resource]) : FString(TEXT("?"));
			PickerMarks[Index]->SetText(FText::FromString(bSelected ? FString::Printf(TEXT("%s\n%02d ✔"), *Name, Index + 1) : FString::Printf(TEXT("%s\n%02d"), *Name, Index + 1)));
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
				: TEXT("거래가 모두 끝나면 배치할 수 있습니다");
			static const TCHAR* ResourceNames[] = { TEXT("Rock"), TEXT("Coal"), TEXT("Copper"), TEXT("Iron"), TEXT("Silver"), TEXT("Gold"), TEXT("Diamond"), TEXT("Adamantium") };
			const int32 Resource = State.PendingCards[0];
			const FString CardName = Resource >= 0 && Resource < UE_ARRAY_COUNT(ResourceNames) ? FString(ResourceNames[Resource]) : FString(TEXT("?"));
			PendingText->SetText(FText::FromString(FString::Printf(TEXT("배치할 카드: %s  ·  %d장 남음\n%s\n%s"), *CardName, State.PendingCards.Num(), *WagonLine, *Controls)));
		}
	}
	SyncFrame(StatusText);
	SyncFrame(PickerHintText);
	SyncFrame(PendingText);
}

void UOreDealAuctionHudWidget::Flash(const FString& Message, float Seconds)
{
	if (!FlashText) return;
	FlashText->SetText(FText::FromString(Message));
	FlashText->SetVisibility(ESlateVisibility::HitTestInvisible);
	SyncFrame(FlashText);
	FlashRemaining = Seconds;
}

void UOreDealAuctionHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (FlashRemaining > 0.0f)
	{
		FlashRemaining -= InDeltaTime;
		if (FlashRemaining <= 0.0f && FlashText)
		{
			FlashText->SetVisibility(ESlateVisibility::Collapsed);
			SyncFrame(FlashText);
		}
	}
}
