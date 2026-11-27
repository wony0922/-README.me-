#include "OreDealHandViewWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "OreDealTablePlayerController.h"

namespace
{
	// BP_TradeManager order: Rock, Coal, Copper, Iron, Silver, Gold, Diamond, Adamantium.
	const TCHAR* HandViewCardTexturePaths[] = {
		TEXT("/Game/MaterialTraders/Cards/rock.rock"),
		TEXT("/Game/MaterialTraders/Cards/coal.coal"),
		TEXT("/Game/MaterialTraders/Cards/copper.copper"),
		TEXT("/Game/MaterialTraders/Cards/iron.iron"),
		TEXT("/Game/MaterialTraders/Cards/silver.silver"),
		TEXT("/Game/MaterialTraders/Cards/gold.gold"),
		TEXT("/Game/MaterialTraders/Cards/diamond.diamond"),
		TEXT("/Game/MaterialTraders/Cards/adamantium.adamantium"),
	};
	const TCHAR* CardBackTexturePath = TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Back.T_Card_Back");

	constexpr float HandViewCardWidth = 200.0f;
	constexpr float HandViewCardHeight = 280.0f;
	constexpr float CardStep = 82.0f;             // Keep the hand compact while making each face readable.
	constexpr float HandViewScreenMargin = 30.0f;
	constexpr int32 VisibleEachSide = 3;
	constexpr float FocusScale = 1.12f;
	constexpr float NeighbourScale = 0.86f;
	constexpr float ScaleFalloff = 0.05f;
	constexpr float MinScale = 0.72f;
	constexpr float HandViewMoveSpeed = 14.0f;
	// Leaves room on the left for three earlier cards at their smallest visible scale.
	constexpr float FocusCenterX = HandViewScreenMargin + VisibleEachSide * CardStep + (NeighbourScale - ScaleFalloff * (VisibleEachSide - 1)) * HandViewCardWidth * 0.5f;

	float ScaleForOffset(float Offset)
	{
		const float Distance = FMath::Abs(Offset);
		if (Distance <= 1.0f) return FMath::Lerp(FocusScale, NeighbourScale, Distance);
		return FMath::Max(NeighbourScale - ScaleFalloff * (Distance - 1.0f), MinScale);
	}

	float OpacityForOffset(float Offset)
	{
		const float Distance = FMath::Abs(Offset);
		return FMath::Clamp(1.0f - (Distance - VisibleEachSide), 0.0f, 1.0f);
	}

	UTextBlock* MakeHandLabel(UWidgetTree* Tree, int32 Size)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = Size;
		Label->SetFont(Font);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.98f, 0.95f, 0.86f, 1.0f)));
		Label->SetShadowOffset(FVector2D(1.5f, 1.5f));
		Label->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		return Label;
	}
}

void UOreDealHandViewWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!RootCanvas) BuildRoot();
	LoadTextures();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UOreDealHandViewWidget::BuildRoot()
{
	if (!WidgetTree) return;
	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("OreDealHandRoot"));
	WidgetTree->RootWidget = RootCanvas;

	HintText = MakeHandLabel(WidgetTree, 15);
	if (UCanvasPanelSlot* HintSlot = RootCanvas->AddChildToCanvas(HintText))
	{
		HintSlot->SetAnchors(FAnchors(0.0f, 1.0f));
		HintSlot->SetAlignment(FVector2D(0.0f, 1.0f));
		HintSlot->SetAutoSize(true);
		HintSlot->SetPosition(FVector2D(HandViewScreenMargin, -(HandViewScreenMargin + HandViewCardHeight * FocusScale + 10.0f)));
		HintSlot->SetZOrder(1000);
	}
}

void UOreDealHandViewWidget::LoadTextures()
{
	CardTextures.Reset();
	for (const TCHAR* Path : HandViewCardTexturePaths) CardTextures.Add(LoadObject<UTexture2D>(nullptr, Path));
	CardBackTexture = LoadObject<UTexture2D>(nullptr, CardBackTexturePath);
}

void UOreDealHandViewWidget::SetOpen(bool bInOpen)
{
	bOpen = bInOpen;
	// Never blocks mouse input: the view is display-only and the game keeps its normal controls.
	SetVisibility(bOpen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (bOpen)
	{
		FocusIndex = 0;
		PollHand(true);
		UpdateLayout(0.0f, true);
	}
}

void UOreDealHandViewWidget::StepFocus(int32 Delta)
{
	if (!bOpen || HandCards.IsEmpty()) return;
	FocusIndex = FMath::Clamp(FocusIndex + Delta, 0, HandCards.Num() - 1);
	UpdateHint();
}

void UOreDealHandViewWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bOpen) return;
	PollElapsed += InDeltaTime;
	if (PollElapsed >= 0.15f)
	{
		PollElapsed = 0.0f;
		PollHand(false);
	}
	UpdateLayout(InDeltaTime, false);
}

void UOreDealHandViewWidget::PollHand(bool bForce)
{
	TArray<int32> Cards;
	if (const AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer()))
		PC->GetLocalHandCards(Cards);
	if (!bForce && Cards == HandCards) return;
	HandCards = MoveTemp(Cards);
	FocusIndex = HandCards.IsEmpty() ? 0 : FMath::Clamp(FocusIndex, 0, HandCards.Num() - 1);
	RebuildCards();
}

void UOreDealHandViewWidget::RebuildCards()
{
	if (!RootCanvas || !WidgetTree) return;
	for (UBorder* Card : CardWidgets)
		if (Card) Card->RemoveFromParent();
	CardWidgets.Reset();
	CardNumbers.Reset();
	DisplayOffsets.Reset();

	for (int32 Index = 0; Index < HandCards.Num(); ++Index)
	{
		const int32 Resource = HandCards[Index];
		UTexture2D* Texture = CardTextures.IsValidIndex(Resource) ? CardTextures[Resource].Get() : nullptr;
		if (!Texture) Texture = CardBackTexture;

		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		if (Texture) Card->SetBrushFromTexture(Texture);
		Card->SetPadding(FMargin(10.0f, 8.0f));
		Card->SetHorizontalAlignment(HAlign_Left);
		Card->SetVerticalAlignment(VAlign_Top);
		UTextBlock* Number = MakeHandLabel(WidgetTree, 14);
	static const TCHAR* HandViewResourceNames[] = { TEXT("Rock"), TEXT("Coal"), TEXT("Copper"), TEXT("Iron"), TEXT("Silver"), TEXT("Gold"), TEXT("Diamond"), TEXT("Adamantium") };
	const FString ResourceName = Resource >= 0 && Resource < UE_ARRAY_COUNT(HandViewResourceNames) ? FString(HandViewResourceNames[Resource]) : FString(TEXT("?"));
		Number->SetText(FText::FromString(FString::Printf(TEXT("%s\n%02d"), *ResourceName, Index + 1)));
		Card->SetContent(Number);
		Card->SetRenderTransformPivot(FVector2D(0.5f, 1.0f));

		if (UCanvasPanelSlot* CardSlot = RootCanvas->AddChildToCanvas(Card))
		{
			CardSlot->SetAnchors(FAnchors(0.0f, 1.0f));
			CardSlot->SetAlignment(FVector2D(0.5f, 1.0f));
			CardSlot->SetSize(FVector2D(HandViewCardWidth, HandViewCardHeight));
		}
		CardWidgets.Add(Card);
		CardNumbers.Add(Number);
		DisplayOffsets.Add(static_cast<float>(Index - FocusIndex));
	}
	UpdateLayout(0.0f, true);
}

void UOreDealHandViewWidget::UpdateLayout(float DeltaTime, bool bSnap)
{
	for (int32 Index = 0; Index < CardWidgets.Num(); ++Index)
	{
		UBorder* Card = CardWidgets[Index];
		if (!Card || !DisplayOffsets.IsValidIndex(Index)) continue;
		const float Target = static_cast<float>(Index - FocusIndex);
		DisplayOffsets[Index] = bSnap ? Target : FMath::FInterpTo(DisplayOffsets[Index], Target, DeltaTime, HandViewMoveSpeed);
		const float Offset = DisplayOffsets[Index];
		const float Scale = ScaleForOffset(Offset);
		const float Opacity = OpacityForOffset(Offset);

		if (UCanvasPanelSlot* CardSlot = Cast<UCanvasPanelSlot>(Card->Slot))
		{
			CardSlot->SetPosition(FVector2D(FocusCenterX + Offset * CardStep, -HandViewScreenMargin));
			// The focused card is on top; cards further from it sit further behind.
			CardSlot->SetZOrder(100 - FMath::RoundToInt(FMath::Abs(Offset) * 10.0f));
		}
		Card->SetRenderScale(FVector2D(Scale, Scale));
		Card->SetRenderOpacity(Opacity);
		const float Shade = FMath::Lerp(1.0f, 0.78f, FMath::Clamp(FMath::Abs(Offset), 0.0f, 1.0f));
		Card->SetBrushColor(FLinearColor(Shade, Shade, Shade, 1.0f));
		Card->SetVisibility(Opacity > 0.01f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (bSnap) UpdateHint();
}

void UOreDealHandViewWidget::UpdateHint()
{
	if (!HintText) return;
	if (HandCards.IsEmpty())
	{
		HintText->SetText(FText::FromString(TEXT("손패가 없습니다   ·   Q 닫기")));
		return;
	}
	const FString Front = FocusIndex == 0 ? TEXT("  (맨 앞)") : TEXT("");
	HintText->SetText(FText::FromString(FString::Printf(TEXT("손패 %d / %d%s   ·   Z 이전  ·  X 다음  ·  Q 닫기"), FocusIndex + 1, HandCards.Num(), *Front)));
}
