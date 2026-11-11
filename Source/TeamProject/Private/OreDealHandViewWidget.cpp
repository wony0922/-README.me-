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
	// Same order as BP_CardBoard.FloatMats / BP_SeatedPlayer card materials.
	const TCHAR* CardTexturePaths[] = {
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Stone.T_Card_Stone"),
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Coal.T_Card_Coal"),
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Gold.T_Card_Gold"),
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Iron.T_Card_Iron"),
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Diamond.T_Card_Diamond"),
	};
	const TCHAR* CardBackTexturePath = TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Back.T_Card_Back");

	constexpr float CardWidth = 150.0f;
	constexpr float CardHeight = 210.0f;          // T_Card_* is 744 x 1039
	constexpr float CardStep = 63.0f;             // 42% of the width: neighbours overlap the focus card by more than half
	constexpr float ScreenMargin = 30.0f;
	constexpr int32 VisibleEachSide = 3;
	constexpr float FocusScale = 1.12f;
	constexpr float NeighbourScale = 0.86f;
	constexpr float ScaleFalloff = 0.05f;
	constexpr float MinScale = 0.72f;
	constexpr float MoveSpeed = 14.0f;
	// Leaves room on the left for three earlier cards at their smallest visible scale.
	constexpr float FocusCenterX = ScreenMargin + VisibleEachSide * CardStep + (NeighbourScale - ScaleFalloff * (VisibleEachSide - 1)) * CardWidth * 0.5f;

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
		HintSlot->SetPosition(FVector2D(ScreenMargin, -(ScreenMargin + CardHeight * FocusScale + 10.0f)));
		HintSlot->SetZOrder(1000);
	}
}

void UOreDealHandViewWidget::LoadTextures()
{
	CardTextures.Reset();
	for (const TCHAR* Path : CardTexturePaths) CardTextures.Add(LoadObject<UTexture2D>(nullptr, Path));
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
		Number->SetText(FText::FromString(FString::Printf(TEXT("%02d"), Index + 1)));
		Card->SetContent(Number);
		Card->SetRenderTransformPivot(FVector2D(0.5f, 1.0f));

		if (UCanvasPanelSlot* CardSlot = RootCanvas->AddChildToCanvas(Card))
		{
			CardSlot->SetAnchors(FAnchors(0.0f, 1.0f));
			CardSlot->SetAlignment(FVector2D(0.5f, 1.0f));
			CardSlot->SetSize(FVector2D(CardWidth, CardHeight));
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
		DisplayOffsets[Index] = bSnap ? Target : FMath::FInterpTo(DisplayOffsets[Index], Target, DeltaTime, MoveSpeed);
		const float Offset = DisplayOffsets[Index];
		const float Scale = ScaleForOffset(Offset);
		const float Opacity = OpacityForOffset(Offset);

		if (UCanvasPanelSlot* CardSlot = Cast<UCanvasPanelSlot>(Card->Slot))
		{
			CardSlot->SetPosition(FVector2D(FocusCenterX + Offset * CardStep, -ScreenMargin));
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
