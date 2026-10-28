#include "OreDealInteractionWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/Widget.h"
#include "Blueprint/WidgetTree.h"
#include "Rendering/DrawElements.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/ConfigCacheIni.h"
#include "OreDealTablePlayerController.h"

namespace
{
	const TCHAR* MarketResourceNames[] = { TEXT("Wood"), TEXT("Coal"), TEXT("Copper"), TEXT("Iron"), TEXT("Silver"), TEXT("Gold"), TEXT("Diamond"), TEXT("Adamantium") };
	FString MarketResourceName(int32 Resource)
	{
		return Resource >= 0 && Resource < UE_ARRAY_COUNT(MarketResourceNames) ? FString(MarketResourceNames[Resource]) : TEXT("Unknown");
	}

	UTextBlock* MakeLabel(UWidgetTree* Tree, const FString& Text, int32 Size = 18)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(FText::FromString(Text));
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = Size;
		Label->SetFont(Font);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.93f, 0.84f, 1.0f)));
		return Label;
	}

	UButton* MakeButton(UWidgetTree* Tree, const FString& Text, int32 Size = 17)
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass());
	FSlateBrush NormalBrush;
	NormalBrush.DrawAs = ESlateBrushDrawType::Box;
	NormalBrush.TintColor = FSlateColor(FLinearColor(0.18f, 0.13f, 0.09f, 0.98f));
	NormalBrush.Margin = FMargin(0.08f);
	FSlateBrush HoveredBrush = NormalBrush;
	HoveredBrush.TintColor = FSlateColor(FLinearColor(0.32f, 0.22f, 0.12f, 1.0f));
	FSlateBrush PressedBrush = NormalBrush;
	PressedBrush.TintColor = FSlateColor(FLinearColor(0.42f, 0.27f, 0.12f, 1.0f));
	FButtonStyle Style;
	Style.SetNormal(NormalBrush);
	Style.SetHovered(HoveredBrush);
	Style.SetPressed(PressedBrush);
	Button->SetStyle(Style);
	Button->SetBackgroundColor(FLinearColor::White);
		Button->AddChild(MakeLabel(Tree, Text, Size));
		return Button;
	}

	UOreDealMarketButton* MakeMarketButton(UWidgetTree* Tree, const FString& Text, int32 Size = 17)
	{
		UOreDealMarketButton* Button = Tree->ConstructWidget<UOreDealMarketButton>(UOreDealMarketButton::StaticClass());
	FSlateBrush NormalBrush;
	NormalBrush.DrawAs = ESlateBrushDrawType::Box;
	NormalBrush.TintColor = FSlateColor(FLinearColor(0.18f, 0.13f, 0.09f, 0.98f));
	NormalBrush.Margin = FMargin(0.08f);
	FButtonStyle Style;
	Style.SetNormal(NormalBrush);
	Style.SetHovered(NormalBrush);
	Style.SetPressed(NormalBrush);
	Button->SetStyle(Style);
	Button->SetBackgroundColor(FLinearColor::White);
		Button->AddChild(MakeLabel(Tree, Text, Size));
		return Button;
	}

	void SetCanvasRect(UCanvasPanel* Canvas, UWidget* Widget, const FVector2D& Position, const FVector2D& Size,
		const FVector2D& Anchor = FVector2D(0.0f, 0.0f), const FVector2D& Alignment = FVector2D::ZeroVector)
	{
		if (UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget))
		{
		Slot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
			Slot->SetAlignment(Alignment);
			Slot->SetPosition(Position);
			Slot->SetSize(Size);
		}
	}
}

UOreDealMarketButton::UOreDealMarketButton()
{
	OnClicked.AddDynamic(this, &UOreDealMarketButton::ForwardMarketClick);
}

void UOreDealMarketButton::ForwardMarketClick()
{
	OnMarketClicked.Broadcast(this);
}

void UOreDealInteractionWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (GConfig)
	{
		GConfig->GetFloat(TEXT("OreDeal.Chat"), TEXT("Opacity"), ChatOpacity, GGameUserSettingsIni);
		GConfig->GetFloat(TEXT("OreDeal.Chat"), TEXT("Scale"), ChatScale, GGameUserSettingsIni);
	}
	ChatOpacity = FMath::Clamp(ChatOpacity, 0.3f, 1.0f);
	ChatScale = FMath::Clamp(ChatScale, 0.75f, 1.35f);
	if (OpacitySlider) OpacitySlider->SetValue(ChatOpacity);
	if (ScaleSlider) ScaleSlider->SetValue((ChatScale - 0.75f) / 0.6f);
	ApplyChatLayout();
	ApplyChatSettings();
}

void UOreDealInteractionWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Build the widget tree before UUserWidget creates its Slate representation.
	// Building it from NativeConstruct is too late: NativePaint still runs, which
	// made the reticle appear, but the CanvasPanel children never entered the live UI.
	if (!RootCanvas) BuildInterface();
}

int32 UOreDealInteractionWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 PaintedLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!IsInViewport() || bChatExpanded || bSalesOpen || bHandRackOpen || bMarketOpen || bMarketRevealActive) return PaintedLayer;

	const FVector2D Center = AllottedGeometry.GetLocalSize() * 0.5f;
	TArray<FVector2D> CirclePoints;
	CirclePoints.Reserve(33);
	for (int32 Index = 0; Index <= 32; ++Index)
	{
		const float Angle = 2.0f * PI * static_cast<float>(Index) / 32.0f;
		CirclePoints.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 9.0f);
	}
	FSlateDrawElement::MakeLines(OutDrawElements, PaintedLayer + 1, AllottedGeometry.ToPaintGeometry(), CirclePoints,
		ESlateDrawEffect::None, FLinearColor(0.02f, 0.02f, 0.015f, 0.95f), true, 5.0f);
	FSlateDrawElement::MakeLines(OutDrawElements, PaintedLayer + 2, AllottedGeometry.ToPaintGeometry(), CirclePoints,
		ESlateDrawEffect::None, FLinearColor(1.0f, 0.93f, 0.66f, 0.98f), true, 2.5f);
	return PaintedLayer + 2;
}

void UOreDealInteractionWidget::BuildInterface()
{
	if (!WidgetTree) return;
	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("OreDealRoot"));
	WidgetTree->RootWidget = RootCanvas;

	// A fixed center-screen reticle for the table's center-ray interaction model.
	ReticleText = MakeLabel(WidgetTree, TEXT("•"), 20);
	ReticleText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.96f, 0.72f, 1.0f)));
	ReticleText->SetJustification(ETextJustify::Center);
	ReticleText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
	ReticleText->SetShadowOffset(FVector2D(1.0f, 1.0f));
	SetCanvasRect(RootCanvas, ReticleText, FVector2D::ZeroVector, FVector2D(28.0f, 28.0f), FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f));
	ReticleText->SetVisibility(ESlateVisibility::HitTestInvisible);
	ReticleText->SetRenderOpacity(1.0f);

	DrawPromptText = MakeLabel(WidgetTree, TEXT("4단계: 카드를 뽑으세요!"), 28);
	DrawPromptText->SetJustification(ETextJustify::Center);
	DrawPromptText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.83f, 0.35f, 1.0f)));
	SetCanvasRect(RootCanvas, DrawPromptText, FVector2D::ZeroVector, FVector2D(520.0f, 48.0f));
	DrawPromptText->SetVisibility(ESlateVisibility::Collapsed);

	ChatPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ChatPanel"));
	ChatPanel->SetBrushColor(FLinearColor(0.025f, 0.02f, 0.018f, ChatOpacity));
	UVerticalBox* ChatBody = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBox* ChatHeader = WidgetTree->ConstructWidget<UHorizontalBox>();
	ChatHeader->AddChildToHorizontalBox(MakeLabel(WidgetTree, TEXT("대화"), 20));
	UButton* SettingsButton = MakeButton(WidgetTree, TEXT("⚙"), 16);
	SettingsButton->OnClicked.AddDynamic(this, &UOreDealInteractionWidget::OnSettingsClicked);
	ChatHeader->AddChildToHorizontalBox(SettingsButton);
	ChatBody->AddChildToVerticalBox(ChatHeader);
	ChatScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ChatScroll"));
	ChatScroll->SetAlwaysShowScrollbar(true);
	ChatScroll->SetScrollBarVisibility(ESlateVisibility::Visible);
	USizeBox* ChatScrollSize = WidgetTree->ConstructWidget<USizeBox>();
	ChatScrollSize->SetHeightOverride(100.0f);
	ChatScrollSize->AddChild(ChatScroll);
	ChatBody->AddChildToVerticalBox(ChatScrollSize);

	UVerticalBox* SettingsPanel = WidgetTree->ConstructWidget<UVerticalBox>();
	SettingsPanel->AddChildToVerticalBox(MakeLabel(WidgetTree, TEXT("창 투명도"), 14));
	OpacitySlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
	OpacitySlider->SetMinValue(0.3f); OpacitySlider->SetMaxValue(1.0f);
	OpacitySlider->OnValueChanged.AddDynamic(this, &UOreDealInteractionWidget::OnOpacityChanged);
	SettingsPanel->AddChildToVerticalBox(OpacitySlider);
	SettingsPanel->AddChildToVerticalBox(MakeLabel(WidgetTree, TEXT("창 크기"), 14));
	ScaleSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
	ScaleSlider->SetMinValue(0.0f); ScaleSlider->SetMaxValue(1.0f);
	ScaleSlider->OnValueChanged.AddDynamic(this, &UOreDealInteractionWidget::OnScaleChanged);
	SettingsPanel->AddChildToVerticalBox(ScaleSlider);
	SettingsPanel->SetVisibility(ESlateVisibility::Collapsed);
	ChatBody->AddChildToVerticalBox(SettingsPanel);
	ChatPanel->SetContent(ChatBody);
	SetCanvasRect(RootCanvas, ChatPanel, FVector2D(20.0f, 18.0f), FVector2D(410.0f, 145.0f));
	ChatSlot = Cast<UCanvasPanelSlot>(ChatPanel->Slot);

	ChatInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("ChatInput"));
	ChatInput->SetHintText(FText::FromString(TEXT("메시지를 입력하고 Enter를 누르세요")));
	ChatInput->SetVisibility(ESlateVisibility::Collapsed);
	ChatInput->OnTextCommitted.AddDynamic(this, &UOreDealInteractionWidget::OnChatTextCommitted);
	ChatBody->AddChildToVerticalBox(ChatInput);

	ToastPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SaleToast"));
	ToastPanel->SetBrushColor(FLinearColor(0.025f, 0.02f, 0.018f, 0.88f));
	ToastText = MakeLabel(WidgetTree, TEXT(""), 25);
	ToastText->SetJustification(ETextJustify::Center);
	ToastPanel->SetContent(ToastText);
	ToastPanel->SetVisibility(ESlateVisibility::Collapsed);
	SetCanvasRect(RootCanvas, ToastPanel, FVector2D(-22.0f, 22.0f), FVector2D(470.0f, 72.0f), FVector2D(1.0f, 0.0f), FVector2D(1.0f, 0.0f));

	UButton* MerchantButton = MakeButton(WidgetTree, TEXT("🤠 상인 · 자원 판매 (M)"), 20);
	MerchantButton->OnClicked.AddDynamic(this, &UOreDealInteractionWidget::OnMerchantClicked);
	// Keep the merchant control away from the lobby's centered start prompt and the
	// gameplay phase HUD. This button is the reliable screen-space entry point even
	// when the 3D merchant mesh has no interaction tag.
	MerchantButton->SetVisibility(ESlateVisibility::Visible);
	SetCanvasRect(RootCanvas, MerchantButton, FVector2D(24.0f, 112.0f), FVector2D(300.0f, 64.0f));

	SalesPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SalesPanel"));
	SalesPanel->SetBrushColor(FLinearColor(0.035f, 0.025f, 0.02f, 0.96f));
	UVerticalBox* SalesBody = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBox* SalesHeader = WidgetTree->ConstructWidget<UHorizontalBox>();
	SalesHeader->AddChildToHorizontalBox(MakeLabel(WidgetTree, TEXT("마차 자원 판매"), 24));
	UButton* CloseButton = MakeButton(WidgetTree, TEXT("닫기"));
	CloseButton->OnClicked.AddDynamic(this, &UOreDealInteractionWidget::OnCloseSalesClicked);
	SalesHeader->AddChildToHorizontalBox(CloseButton);
	SalesBody->AddChildToVerticalBox(SalesHeader);
	SalesBody->AddChildToVerticalBox(MakeLabel(WidgetTree, TEXT("내 턴의 1~3단계에서 판매할 수 있습니다."), 16));
	SalesRows = WidgetTree->ConstructWidget<UVerticalBox>();
	for (int32 I = 0; I < 3; ++I)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UTextBlock* Info = MakeLabel(WidgetTree, TEXT("마차"), 18);
		UButton* SellButton = MakeButton(WidgetTree, TEXT("판매"));
		if (I == 0) SellButton->OnClicked.AddDynamic(this, &UOreDealInteractionWidget::OnSell0Clicked);
		else if (I == 1) SellButton->OnClicked.AddDynamic(this, &UOreDealInteractionWidget::OnSell1Clicked);
		else SellButton->OnClicked.AddDynamic(this, &UOreDealInteractionWidget::OnSell2Clicked);
		Row->AddChildToHorizontalBox(Info);
		Row->AddChildToHorizontalBox(SellButton);
		SalesRows->AddChildToVerticalBox(Row);
		SalesLabels.Add(Info);
		SalesButtons.Add(SellButton);
	}
	SalesBody->AddChildToVerticalBox(SalesRows);
	SalesPanel->SetContent(SalesBody);
	SalesPanel->SetVisibility(ESlateVisibility::Collapsed);
	// Center the modal on the viewport. Offsetting by the negative half-size while
	// also using centered alignment pushed almost the entire panel off-screen.
	SetCanvasRect(RootCanvas, SalesPanel, FVector2D::ZeroVector, FVector2D(820.0f, 600.0f), FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f));
	SalesSlot = Cast<UCanvasPanelSlot>(SalesPanel->Slot);

	HandRackPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HandRackPanel"));
	HandRackPanel->SetBrushColor(FLinearColor(0.025f, 0.025f, 0.028f, 0.96f));
	UVerticalBox* HandBody = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBox* HandHeader = WidgetTree->ConstructWidget<UHorizontalBox>();
	HandHeader->AddChildToHorizontalBox(MakeLabel(WidgetTree, TEXT("카드 랙 · 손패 순서 (맨 왼쪽이 맨 앞)"), 21));
	UButton* CloseHandRackButton = MakeButton(WidgetTree, TEXT("닫기 ×"), 15);
	CloseHandRackButton->OnClicked.AddDynamic(this, &UOreDealInteractionWidget::OnCloseHandRackClicked);
	HandHeader->AddChildToHorizontalBox(CloseHandRackButton);
	HandBody->AddChildToVerticalBox(HandHeader);
	HandCardScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("HandCardScroll"));
	HandCardScroll->SetOrientation(EOrientation::Orient_Horizontal);
	HandCardScroll->SetAlwaysShowScrollbar(true);
	HandCardScroll->SetScrollBarVisibility(ESlateVisibility::Visible);
	HandCardSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("HandCardSizeBox"));
	HandCardSizeBox->SetHeightOverride(170.0f);
	HandCardCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HandCardCanvas"));
	HandCardSizeBox->AddChild(HandCardCanvas);
	HandCardScroll->AddChild(HandCardSizeBox);
	HandBody->AddChildToVerticalBox(HandCardScroll);
	HandBody->AddChildToVerticalBox(MakeLabel(WidgetTree, TEXT("마우스를 올리거나 ← / → 키로 카드를 확인하세요 · Esc로 닫기"), 14));
	HandRackPanel->SetContent(HandBody);
	HandRackPanel->SetVisibility(ESlateVisibility::Collapsed);
	SetCanvasRect(RootCanvas, HandRackPanel, FVector2D::ZeroVector, FVector2D(1000.0f, 300.0f), FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f));
	HandRackSlot = Cast<UCanvasPanelSlot>(HandRackPanel->Slot);

	MarketPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MarketTradePanel"));
	MarketPanel->SetBrushColor(FLinearColor(0.025f, 0.018f, 0.014f, 0.97f));
	UVerticalBox* MarketBody = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBox* MarketHeader = WidgetTree->ConstructWidget<UHorizontalBox>();
	MarketTimerText = MakeLabel(WidgetTree, TEXT("⏳ 02:00"), 28);
	MarketTimerText->SetJustification(ETextJustify::Center);
	MarketHeader->AddChildToHorizontalBox(MarketTimerText);
	MarketBody->AddChildToVerticalBox(MarketHeader);
	MarketStatusText = MakeLabel(WidgetTree, TEXT("장터 거래"), 17);
	MarketStatusText->SetJustification(ETextJustify::Center);
	MarketBody->AddChildToVerticalBox(MarketStatusText);
	for (int32 RowIndex = 0; RowIndex < 2; ++RowIndex)
	{
		UHorizontalBox* PlayerRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 ColumnIndex = 0; ColumnIndex < 2; ++ColumnIndex)
		{
			UBorder* PlayerPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			PlayerPanel->SetBrushColor(FLinearColor(0.07f, 0.055f, 0.045f, 0.94f));
			UVerticalBox* PlayerBody = WidgetTree->ConstructWidget<UVerticalBox>();
			UButton* PlayerHeader = MakeMarketButton(WidgetTree, TEXT("Player"), 18);
			UTextBlock* PlayerTitle = Cast<UTextBlock>(Cast<UButton>(PlayerHeader)->GetContent());
			UHorizontalBox* OfferRow = WidgetTree->ConstructWidget<UHorizontalBox>();
			UHorizontalBox* SecretRow = WidgetTree->ConstructWidget<UHorizontalBox>();
			PlayerBody->AddChildToVerticalBox(PlayerHeader);
			PlayerBody->AddChildToVerticalBox(MakeLabel(WidgetTree, TEXT("공개 제안 구역"), 13));
			PlayerBody->AddChildToVerticalBox(OfferRow);
			PlayerBody->AddChildToVerticalBox(MakeLabel(WidgetTree, TEXT("비공개 손패 구역"), 13));
			PlayerBody->AddChildToVerticalBox(SecretRow);
			PlayerPanel->SetContent(PlayerBody);
			PlayerPanel->SetPadding(FMargin(8.0f));
			if (UHorizontalBoxSlot* PlayerSlot = PlayerRow->AddChildToHorizontalBox(PlayerPanel))
				PlayerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TradePlayerPanels.Add(PlayerPanel);
			TradePlayerHeaders.Add(PlayerHeader);
			TradePlayerTitles.Add(PlayerTitle);
			TradeOfferRows.Add(OfferRow);
			TradeSecretRows.Add(SecretRow);
		}
		MarketBody->AddChildToVerticalBox(PlayerRow);
	}
	MarketOfferButton = MakeButton(WidgetTree, TEXT("판매 올리기!"), 20);
	MarketOfferButton->OnClicked.AddDynamic(this, &UOreDealInteractionWidget::OnMarketOfferClicked);
	MarketBody->AddChildToVerticalBox(MarketOfferButton);
	MarketConfirmBody = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBox* ConsentButtons = WidgetTree->ConstructWidget<UHorizontalBox>();
	UButton* AcceptButton = MakeButton(WidgetTree, TEXT("예, 거래합니다"), 18);
	AcceptButton->OnClicked.AddDynamic(this, &UOreDealInteractionWidget::OnMarketAcceptClicked);
	UButton* DeclineButton = MakeButton(WidgetTree, TEXT("아니오 / 취소하기"), 18);
	DeclineButton->OnClicked.AddDynamic(this, &UOreDealInteractionWidget::OnMarketDeclineClicked);
	ConsentButtons->AddChildToHorizontalBox(AcceptButton);
	ConsentButtons->AddChildToHorizontalBox(DeclineButton);
	MarketConfirmBody->AddChildToVerticalBox(MakeLabel(WidgetTree, TEXT("거래하시겠습니까?"), 19));
	MarketConfirmBody->AddChildToVerticalBox(ConsentButtons);
	MarketConfirmBody->SetVisibility(ESlateVisibility::Collapsed);
	MarketBody->AddChildToVerticalBox(MarketConfirmBody);
	MarketPanel->SetContent(MarketBody);
	MarketPanel->SetVisibility(ESlateVisibility::Collapsed);
	SetCanvasRect(RootCanvas, MarketPanel, FVector2D::ZeroVector, FVector2D(1400.0f, 820.0f), FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f));
	MarketSlot = Cast<UCanvasPanelSlot>(MarketPanel->Slot);
	for (int32 CardIndex = 0; CardIndex < 2; ++CardIndex)
	{
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Card->SetBrushColor(FLinearColor(0.32f, 0.22f, 0.12f, 1.0f));
		Card->SetPadding(FMargin(8.0f));
		UTextBlock* CardLabel = MakeLabel(WidgetTree, TEXT("🎴"), 20);
		CardLabel->SetJustification(ETextJustify::Center);
		Card->SetContent(CardLabel);
		Card->SetVisibility(ESlateVisibility::Collapsed);
		SetCanvasRect(RootCanvas, Card, FVector2D::ZeroVector, FVector2D(112.0f, 150.0f), FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f));
		MarketRevealPanels.Add(Card);
		MarketRevealLabels.Add(CardLabel);
	}
}

void UOreDealInteractionWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (bHandRackOpen)
	{
		for (int32 I = 0; I < HandCardBorders.Num(); ++I)
			if (HandCardBorders[I] && HandCardBorders[I]->IsHovered() && HandSelectionIndex != I)
			{
				HandSelectionIndex = I;
				ApplyHandSelection();
				break;
			}
	}
	if (bMarketRevealActive)
	{
		MarketRevealElapsed = FMath::Min(MarketRevealElapsed + DeltaTime, 1.0f);
		for (int32 I = 0; I < MarketRevealPanels.Num(); ++I)
		{
			UBorder* Card = MarketRevealPanels[I];
			if (!Card) continue;
			const float MoveAlpha = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(MarketRevealElapsed / 0.38f, 0.0f, 1.0f));
			const float Direction = I == 0 ? -1.0f : 1.0f;
			const FVector2D TargetOffset = MarketRevealOrigin + FVector2D(Direction * 62.0f, -18.0f);
			const FVector2D FlightPosition = FMath::Lerp(MarketRevealOrigin, TargetOffset, MoveAlpha) + FVector2D(0.0f, -42.0f * FMath::Sin(PI * MoveAlpha));
			Card->SetRenderTranslation(FlightPosition);
			const float FlipStart = 0.34f + I * 0.25f;
			const float FlipAlpha = FMath::Clamp((MarketRevealElapsed - FlipStart) / 0.25f, 0.0f, 1.0f);
			const float FlipScale = FMath::Max(0.06f, FMath::Abs(FMath::Cos(FlipAlpha * PI)));
			Card->SetRenderScale(FVector2D(FlipScale, 1.0f));
			if (MarketRevealLabels.IsValidIndex(I) && CurrentPublicMarketCards.IsValidIndex(I))
				MarketRevealLabels[I]->SetText(FText::FromString(FlipAlpha >= 0.5f ? MarketResourceName(CurrentPublicMarketCards[I]) : TEXT("🎴")));
		}
	}
	if (bSalesOpen)
	{
		SalesRefreshElapsed += DeltaTime;
		if (SalesRefreshElapsed >= 0.35f) { SalesRefreshElapsed = 0.0f; RefreshSales(); }
	}
	if (ToastPanel && ToastPanel->GetVisibility() != ESlateVisibility::Collapsed)
	{
		ToastElapsed += DeltaTime;
		const float Alpha = ToastElapsed <= 10.0f ? 1.0f : FMath::Clamp(1.0f - (ToastElapsed - 10.0f) / 1.5f, 0.0f, 1.0f);
		ToastPanel->SetRenderOpacity(Alpha);
		if (Alpha <= 0.0f) ToastPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UOreDealInteractionWidget::SetChatExpanded(bool bExpanded)
{
	bChatExpanded = bExpanded;
	if (ChatInput)
	{
		ChatInput->SetVisibility(bExpanded ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (!bExpanded) ChatInput->SetText(FText::GetEmpty());
	}
	ApplyChatLayout();
	if (bExpanded && ChatScroll) ChatScroll->ScrollToEnd();
}

void UOreDealInteractionWidget::SetSalesOpen(bool bOpen)
{
	bSalesOpen = bOpen;
	if (SalesPanel) SalesPanel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bOpen && SalesSlot)
	{
		int32 Width = 0, Height = 0;
		if (APlayerController* PC = GetOwningPlayer()) PC->GetViewportSize(Width, Height);
		if (Width > 0 && Height > 0)
		{
			SalesSlot->SetSize(FVector2D(FMath::Min(900.0f, Width - 40.0f), FMath::Min(680.0f, Height - 40.0f)));
			SalesSlot->SetPosition(FVector2D::ZeroVector);
		}
	}
	if (bOpen) RefreshSales();
}

void UOreDealInteractionWidget::SetHandRackOpen(bool bOpen)
{
	bHandRackOpen = bOpen;
	if (HandRackPanel) HandRackPanel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bOpen) { HandSelectionIndex = 0; ApplyHandSelection(); }
}

void UOreDealInteractionWidget::SetHandRackScreenPosition(const FVector2D& RackScreenPosition, const FVector2D& ViewportSize)
{
	if (!HandRackSlot || ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f) return;
	const float PanelWidth = FMath::Clamp(ViewportSize.X - 40.0f, 280.0f, 1000.0f);
	const float PanelHeight = FMath::Clamp(ViewportSize.Y - 40.0f, 160.0f, 300.0f);
	HandRackSlot->SetSize(FVector2D(PanelWidth, PanelHeight));
	const FVector2D Center(
		FMath::Clamp(RackScreenPosition.X, PanelWidth * 0.5f + 20.0f, ViewportSize.X - PanelWidth * 0.5f - 20.0f),
		FMath::Clamp(RackScreenPosition.Y - PanelHeight * 0.5f - 24.0f, PanelHeight * 0.5f + 20.0f, ViewportSize.Y - PanelHeight * 0.5f - 20.0f));
	HandRackSlot->SetPosition(Center - ViewportSize * 0.5f);
}

void UOreDealInteractionWidget::SetMarketOpen(bool bOpen)
{
	bMarketOpen = bOpen;
	if (MarketPanel) MarketPanel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bOpen && MarketSlot)
	{
		int32 Width = 0, Height = 0;
		if (APlayerController* PC = GetOwningPlayer()) PC->GetViewportSize(Width, Height);
		if (Width > 0 && Height > 0)
		{
			MarketSlot->SetSize(FVector2D(FMath::Min(Width - 32.0f, 1700.0f), FMath::Min(Height - 32.0f, 1000.0f)));
			MarketSlot->SetPosition(FVector2D::ZeroVector);
		}
	}
	if (!bOpen)
	{
		LastMarketRevision = INDEX_NONE;
		SelectedMarketIndices.Reset();
		SelectedHandIndices.Reset();
	}
}

void UOreDealInteractionWidget::SetMarketReveal(bool bVisible, const TArray<int32>& Cards)
{
	if (bVisible && !bMarketRevealActive) MarketRevealElapsed = 0.0f;
	bMarketRevealActive = bVisible;
	CurrentPublicMarketCards = Cards;
	for (int32 I = 0; I < MarketRevealPanels.Num(); ++I)
	{
		UBorder* Card = MarketRevealPanels[I];
		if (!Card) continue;
		Card->SetVisibility(bVisible && Cards.IsValidIndex(I) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (!bVisible)
		{
			Card->SetRenderTranslation(FVector2D::ZeroVector);
			Card->SetRenderScale(FVector2D(1.0f, 1.0f));
		}
		else if (MarketRevealLabels.IsValidIndex(I)) MarketRevealLabels[I]->SetText(FText::FromString(TEXT("🎴")));
	}
}

void UOreDealInteractionWidget::RefreshMarketTrade(const FOreDealMarketSnapshot& Snapshot, const TArray<int32>& LocalHand, APlayerState* LocalPlayer)
{
	if (!bMarketOpen || !MarketPanel) return;
	const bool bHandChanged = CurrentLocalHand != LocalHand;
	const bool bMarketChanged = CurrentPublicMarketCards != Snapshot.MarketCards;
	if (bHandChanged) SelectedHandIndices.Reset();
	if (bMarketChanged) SelectedMarketIndices.Reset();
	CurrentLocalHand = LocalHand;
	CurrentPublicMarketCards = Snapshot.MarketCards;
	CurrentMarketPlayers = Snapshot.Players;
	CurrentLocalPlayer = LocalPlayer;
	CurrentActivePlayer = Snapshot.ActivePlayer;
	CurrentTargetPlayer = Snapshot.SelectedTarget;
	CurrentPendingPlayer = Snapshot.PendingTarget;
	if (Snapshot.Revision != LastMarketRevision || bHandChanged || bMarketChanged)
	{
		LastMarketRevision = Snapshot.Revision;
		RebuildMarketPanels(Snapshot);
	}
	if (MarketTimerText)
		MarketTimerText->SetText(FText::FromString(FString::Printf(TEXT("⏳ %02d:%02d"), Snapshot.SecondsRemaining / 60, Snapshot.SecondsRemaining % 60)));
	if (MarketStatusText)
	{
		FString Status = TEXT("A / D 상대 선택 · ← / → 카드 이동 · Space 제안");
		if (Snapshot.State == static_cast<int32>(EOreDealMarketState::AwaitingConsent))
		{
			const FString ActiveName = Snapshot.ActivePlayer ? Snapshot.ActivePlayer->GetPlayerName() : TEXT("활성 플레이어");
			const FString TargetName = Snapshot.PendingTarget ? Snapshot.PendingTarget->GetPlayerName() : TEXT("상대 플레이어");
			Status = FString::Printf(TEXT("%s ↔ %s 거래 동의 대기 중"), *ActiveName, *TargetName);
		}
		else if (Snapshot.State == static_cast<int32>(EOreDealMarketState::Reveal)) Status = TEXT("공개 카드 준비 중...");
		MarketStatusText->SetText(FText::FromString(Status));
	}
}

void UOreDealInteractionWidget::RebuildMarketPanels(const FOreDealMarketSnapshot& Snapshot)
{
	APlayerController* PC = GetOwningPlayer();
	const bool bLocalActive = CurrentLocalPlayer.IsValid() && CurrentLocalPlayer == CurrentActivePlayer;
	const bool bAwaiting = Snapshot.bAwaitingConsent;
	for (int32 PanelIndex = 0; PanelIndex < TradePlayerPanels.Num(); ++PanelIndex)
	{
		UBorder* Panel = TradePlayerPanels[PanelIndex];
		UButton* Header = TradePlayerHeaders.IsValidIndex(PanelIndex) ? TradePlayerHeaders[PanelIndex] : nullptr;
		UTextBlock* Title = TradePlayerTitles.IsValidIndex(PanelIndex) ? TradePlayerTitles[PanelIndex] : nullptr;
		UHorizontalBox* OfferRow = TradeOfferRows.IsValidIndex(PanelIndex) ? TradeOfferRows[PanelIndex] : nullptr;
		UHorizontalBox* SecretRow = TradeSecretRows.IsValidIndex(PanelIndex) ? TradeSecretRows[PanelIndex] : nullptr;
		if (!Panel || !OfferRow || !SecretRow) continue;
		OfferRow->ClearChildren();
		SecretRow->ClearChildren();
		if (!CurrentMarketPlayers.IsValidIndex(PanelIndex))
		{
			Panel->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		Panel->SetVisibility(ESlateVisibility::Visible);
		const FOreDealMarketPlayerView& PlayerView = CurrentMarketPlayers[PanelIndex];
		const bool bIsActive = PlayerView.Player == CurrentActivePlayer;
		const bool bIsLocal = PlayerView.Player == CurrentLocalPlayer;
		const bool bSelectedTarget = PlayerView.Player == CurrentTargetPlayer;
		Panel->SetBrushColor(bSelectedTarget ? FLinearColor(0.20f, 0.035f, 0.025f, 0.98f) : FLinearColor(0.07f, 0.055f, 0.045f, 0.94f));
		if (Title) Title->SetText(FText::FromString(FString::Printf(TEXT("%s%s  ·  손패 %d장"), *PlayerView.Name, bIsActive ? TEXT(" (활성)" ) : TEXT(""), PlayerView.HandCount)));
		if (Header)
		{
			if (UOreDealMarketButton* MarketHeader = Cast<UOreDealMarketButton>(Header))
			{
				MarketHeader->OnMarketClicked.Clear();
				MarketHeader->OnMarketClicked.AddLambda([this, PanelIndex](UButton*) { SetMarketPlayerTarget(PanelIndex); });
			}
			Header->SetIsEnabled(!bAwaiting);
		}

		if (bIsActive)
		{
			for (int32 MarketIndex = 0; MarketIndex < CurrentPublicMarketCards.Num(); ++MarketIndex)
			{
				const bool bSelected = SelectedMarketIndices.Contains(MarketIndex);
				UButton* CardButton = MakeMarketButton(WidgetTree, FString::Printf(TEXT("🔒 %s%s"), *MarketResourceName(CurrentPublicMarketCards[MarketIndex]), bSelected ? TEXT(" ✓") : TEXT("")), 14);
				CardButton->SetBackgroundColor(bSelected ? FLinearColor(0.45f, 0.15f, 0.07f, 1.0f) : FLinearColor(0.18f, 0.13f, 0.09f, 0.96f));
				CardButton->SetIsEnabled(bLocalActive && !bAwaiting);
				if (UOreDealMarketButton* MarketButton = Cast<UOreDealMarketButton>(CardButton))
					MarketButton->OnMarketClicked.AddLambda([this, MarketIndex](UButton*)
					{
						if (SelectedMarketIndices.Contains(MarketIndex)) SelectedMarketIndices.Remove(MarketIndex);
						else SelectedMarketIndices.Add(MarketIndex);
						LastMarketRevision = INDEX_NONE;
					});
				OfferRow->AddChildToHorizontalBox(CardButton);
			}
			for (int32 OfferIndex = 0; OfferIndex < PlayerView.PublicOffer.Num(); ++OfferIndex)
			{
				const int32 Resource = PlayerView.PublicOffer[OfferIndex];
				UButton* CardButton = MakeMarketButton(WidgetTree, FString::Printf(TEXT("제안 · %s"), *MarketResourceName(Resource)), 14);
				CardButton->SetBackgroundColor(FLinearColor(0.28f, 0.20f, 0.11f, 1.0f));
				CardButton->SetIsEnabled(bIsLocal && !bAwaiting);
				if (bIsLocal) Cast<UOreDealMarketButton>(CardButton)->OnMarketClicked.AddLambda([this, Resource](UButton*) { RemoveCardFromMarketOffer(Resource); });
				OfferRow->AddChildToHorizontalBox(CardButton);
			}
		}
		else
		{
			for (int32 OfferIndex = 0; OfferIndex < PlayerView.PublicOffer.Num(); ++OfferIndex)
			{
				const int32 Resource = PlayerView.PublicOffer[OfferIndex];
				UButton* CardButton = MakeMarketButton(WidgetTree, MarketResourceName(Resource), 14);
				CardButton->SetIsEnabled(bIsLocal && !bAwaiting);
				if (bIsLocal) Cast<UOreDealMarketButton>(CardButton)->OnMarketClicked.AddLambda([this, Resource](UButton*) { RemoveCardFromMarketOffer(Resource); });
				OfferRow->AddChildToHorizontalBox(CardButton);
			}
		}

		if (bIsLocal)
		{
			for (int32 HandIndex = 0; HandIndex < CurrentLocalHand.Num(); ++HandIndex)
			{
				const bool bSelected = SelectedHandIndices.Contains(HandIndex);
			const bool bCanOffer = !bAwaiting;
				UButton* CardButton = MakeMarketButton(WidgetTree, FString::Printf(TEXT("%s%s"), *MarketResourceName(CurrentLocalHand[HandIndex]), bSelected ? TEXT(" ✓") : TEXT("")), 13);
				CardButton->SetBackgroundColor(bSelected ? FLinearColor(0.23f, 0.28f, 0.18f, 1.0f) : FLinearColor(0.12f, 0.10f, 0.08f, 0.96f));
				CardButton->SetIsEnabled(bCanOffer);
				Cast<UOreDealMarketButton>(CardButton)->OnMarketClicked.AddLambda([this, HandIndex](UButton*) { ToggleMarketHandAt(HandIndex); });
				SecretRow->AddChildToHorizontalBox(CardButton);
			}
		}
		else
		{
			const int32 HiddenCards = FMath::Min(PlayerView.HandCount, 10);
			for (int32 I = 0; I < HiddenCards; ++I)
				SecretRow->AddChildToHorizontalBox(MakeLabel(WidgetTree, TEXT("🎴"), 20));
			if (PlayerView.HandCount > HiddenCards) SecretRow->AddChildToHorizontalBox(MakeLabel(WidgetTree, TEXT("…"), 18));
		}
	}
	if (MarketOfferButton)
	{
		MarketOfferButton->SetVisibility(bLocalActive && !bAwaiting ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		const FOreDealMarketPlayerView* TargetView = CurrentMarketPlayers.FindByPredicate([this](const FOreDealMarketPlayerView& View) { return View.Player == CurrentTargetPlayer.Get(); });
		const FOreDealMarketPlayerView* ActiveView = CurrentMarketPlayers.FindByPredicate([this](const FOreDealMarketPlayerView& View) { return View.Player == CurrentActivePlayer.Get(); });
		const bool bHasAnythingToTrade = !SelectedMarketIndices.IsEmpty() || (ActiveView && !ActiveView->PublicOffer.IsEmpty()) || (TargetView && !TargetView->PublicOffer.IsEmpty());
		MarketOfferButton->SetIsEnabled(CurrentTargetPlayer.IsValid() && CurrentTargetPlayer != CurrentActivePlayer && bHasAnythingToTrade);
	}
	if (MarketConfirmBody)
	{
		const bool bLocalParticipant = CurrentLocalPlayer.IsValid() && (CurrentLocalPlayer == CurrentActivePlayer || CurrentLocalPlayer == CurrentPendingPlayer);
		MarketConfirmBody->SetVisibility(bAwaiting && bLocalParticipant ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UOreDealInteractionWidget::SetMarketPlayerTarget(int32 PlayerIndex)
{
	if (!CurrentMarketPlayers.IsValidIndex(PlayerIndex)) return;
	FocusedMarketPlayerIndex = PlayerIndex;
	if (CurrentLocalPlayer != CurrentActivePlayer) return;
	if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer()))
		PC->RequestMarketSelectTarget(CurrentMarketPlayers[PlayerIndex].Player);
}

void UOreDealInteractionWidget::MoveMarketPlayerSelection(int32 Delta)
{
	if (CurrentLocalPlayer != CurrentActivePlayer || CurrentMarketPlayers.IsEmpty()) return;
	FocusedMarketPlayerIndex = (FocusedMarketPlayerIndex + Delta + CurrentMarketPlayers.Num()) % CurrentMarketPlayers.Num();
	SetMarketPlayerTarget(FocusedMarketPlayerIndex);
}

void UOreDealInteractionWidget::MoveMarketHandSelection(int32 Delta)
{
	if (CurrentLocalHand.IsEmpty()) { FocusedMarketHandIndex = 0; return; }
	FocusedMarketHandIndex = (FocusedMarketHandIndex + Delta + CurrentLocalHand.Num()) % CurrentLocalHand.Num();
}

void UOreDealInteractionWidget::ToggleMarketHandSelection()
{
	ToggleMarketHandAt(FocusedMarketHandIndex);
}

void UOreDealInteractionWidget::ToggleMarketHandAt(int32 HandIndex)
{
	if (!CurrentLocalHand.IsValidIndex(HandIndex) || (CurrentPendingPlayer.IsValid() && CurrentLocalPlayer == CurrentPendingPlayer)) return;
	if (SelectedHandIndices.Contains(HandIndex)) SelectedHandIndices.Remove(HandIndex);
	else SelectedHandIndices.Add(HandIndex);
	FocusedMarketHandIndex = HandIndex;
	TArray<int32> Selected;
	for (int32 Index : SelectedHandIndices) Selected.Add(Index);
	Selected.Sort();
	if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer())) PC->RequestMarketSetOffer(Selected);
	LastMarketRevision = INDEX_NONE;
}

void UOreDealInteractionWidget::RemoveCardFromMarketOffer(int32 Resource)
{
	int32 FoundIndex = INDEX_NONE;
	for (int32 Index : SelectedHandIndices)
		if (CurrentLocalHand.IsValidIndex(Index) && CurrentLocalHand[Index] == Resource)
		{
			FoundIndex = Index;
			break;
		}
	if (FoundIndex != INDEX_NONE) SelectedHandIndices.Remove(FoundIndex);
	TArray<int32> Selected;
	for (int32 Index : SelectedHandIndices) Selected.Add(Index);
	Selected.Sort();
	if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer())) PC->RequestMarketSetOffer(Selected);
	LastMarketRevision = INDEX_NONE;
}

void UOreDealInteractionWidget::OnMarketOfferClicked()
{
	if (CurrentLocalPlayer != CurrentActivePlayer || !CurrentTargetPlayer.IsValid() || CurrentTargetPlayer == CurrentActivePlayer) return;
	TArray<int32> Selected;
	for (int32 Index : SelectedMarketIndices) Selected.Add(Index);
	Selected.Sort();
	if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer())) PC->RequestMarketBeginDeal(CurrentTargetPlayer.Get(), Selected);
}

void UOreDealInteractionWidget::OnMarketAcceptClicked()
{
	if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer())) PC->RequestMarketConsent(true);
}

void UOreDealInteractionWidget::OnMarketDeclineClicked()
{
	if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer())) PC->RequestMarketConsent(false);
}

void UOreDealInteractionWidget::SetDrawPromptVisible(bool bVisible)
{
	if (DrawPromptText)
	{
		DrawPromptText->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UOreDealInteractionWidget::SetDrawPromptPosition(const FVector2D& ScreenPosition)
{
	if (UCanvasPanelSlot* PromptCanvasSlot = DrawPromptText ? Cast<UCanvasPanelSlot>(DrawPromptText->Slot) : nullptr)
	{
		PromptCanvasSlot->SetPosition(ScreenPosition);
	}
}

void UOreDealInteractionWidget::RefreshHandCards(const TArray<int32>& Cards)
{
	if (!HandCardCanvas || !WidgetTree) return;
	if (LastHandCards == Cards) return;
	LastHandCards = Cards;
	HandCardCanvas->ClearChildren();
	HandCardBorders.Reset();
	HandCardLabels.Reset();
	static const TCHAR* Names[] = { TEXT("Wood"), TEXT("Coal"), TEXT("Copper"), TEXT("Iron"), TEXT("Silver"), TEXT("Gold"), TEXT("Diamond"), TEXT("Adamantium") };
	const float CardWidth = 230.0f;
	const float Step = CardWidth / 3.0f;
	const float CanvasWidth = Cards.Num() > 0 ? Step * (Cards.Num() - 1) + CardWidth : CardWidth;
	if (HandCardSizeBox) HandCardSizeBox->SetWidthOverride(CanvasWidth);
	for (int32 I = 0; I < Cards.Num(); ++I)
	{
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Card->SetBrushColor(FLinearColor(0.10f, 0.095f, 0.085f, 1.0f));
		UVerticalBox* Face = WidgetTree->ConstructWidget<UVerticalBox>();
		const int32 Resource = Cards[I];
		const FString Name = Resource >= 0 && Resource < UE_ARRAY_COUNT(Names) ? Names[Resource] : FString::Printf(TEXT("자원 %d"), Resource);
		Face->AddChildToVerticalBox(MakeLabel(WidgetTree, FString::Printf(TEXT("%02d"), I + 1), 14));
		UTextBlock* Label = MakeLabel(WidgetTree, Name, 19);
		Label->SetAutoWrapText(true);
		Face->AddChildToVerticalBox(Label);
		Card->SetContent(Face);
		SetCanvasRect(HandCardCanvas, Card, FVector2D(Step * I, 0.0f), FVector2D(CardWidth, 170.0f));
		HandCardBorders.Add(Card);
		HandCardLabels.Add(Label);
	}
	HandSelectionIndex = Cards.IsEmpty() ? 0 : FMath::Clamp(HandSelectionIndex, 0, Cards.Num() - 1);
	ApplyHandSelection();
}

void UOreDealInteractionWidget::MoveHandSelection(int32 Delta)
{
	if (HandCardBorders.IsEmpty()) return;
	HandSelectionIndex = FMath::Clamp(HandSelectionIndex + Delta, 0, HandCardBorders.Num() - 1);
	ApplyHandSelection();
	if (HandCardScroll && HandCardBorders.IsValidIndex(HandSelectionIndex)) HandCardScroll->ScrollWidgetIntoView(HandCardBorders[HandSelectionIndex], true, EDescendantScrollDestination::IntoView);
}

void UOreDealInteractionWidget::ApplyHandSelection()
{
	for (int32 I = 0; I < HandCardBorders.Num(); ++I)
	{
		if (!HandCardBorders[I]) continue;
		const bool bSelected = I == HandSelectionIndex;
		HandCardBorders[I]->SetBrushColor(bSelected ? FLinearColor(0.48f, 0.43f, 0.30f, 1.0f) : FLinearColor(0.10f, 0.095f, 0.085f, 1.0f));
		HandCardBorders[I]->SetRenderTransform(FWidgetTransform(FVector2D(0.0f, bSelected ? -13.0f : 0.0f), FVector2D(bSelected ? 1.06f : 1.0f, bSelected ? 1.06f : 1.0f), FVector2D::ZeroVector, 0.0f));
	}
}

void UOreDealInteractionWidget::ApplyChatLayout()
{
	if (ChatSlot) ChatSlot->SetSize(FVector2D(410.0f, bChatExpanded ? 500.0f : 112.0f));
	if (ChatPanel) ChatPanel->SetVisibility(ESlateVisibility::Visible);
	if (ChatScroll)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(ChatScroll->GetParent())) SizeBox->SetHeightOverride(bChatExpanded ? 395.0f : 68.0f);
	}
}

void UOreDealInteractionWidget::ApplyChatSettings()
{
	if (ChatPanel) ChatPanel->SetBrushColor(FLinearColor(0.025f, 0.02f, 0.018f, ChatOpacity));
	if (ChatPanel) ChatPanel->SetRenderScale(FVector2D(ChatScale, ChatScale));
}

void UOreDealInteractionWidget::AddChatMessage(const FString& Sender, const FString& Message)
{
	if (!ChatScroll || !WidgetTree) return;
	const bool bWasAtEnd = !bChatExpanded || ChatScroll->GetScrollOffset() + 8.0f >= ChatScroll->GetScrollOffsetOfEnd();
	const float PreviousScrollOffset = ChatScroll->GetScrollOffset();
	const FString Line = FString::Printf(TEXT("%s: %s"), *Sender, *Message);
	ChatHistory.Add(Line);
	if (ChatHistory.Num() > 100) ChatHistory.RemoveAt(0, ChatHistory.Num() - 100);
	ChatScroll->ClearChildren();
	for (const FString& Item : ChatHistory) ChatScroll->AddChild(MakeLabel(WidgetTree, Item, 16));
	if (bWasAtEnd) ChatScroll->ScrollToEnd();
	else ChatScroll->SetScrollOffset(PreviousScrollOffset);
}

void UOreDealInteractionWidget::OnChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod != ETextCommit::OnEnter) return;
	if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer()))
		PC->HandleChatEnter();
}

void UOreDealInteractionWidget::ShowSaleToast(const FString& Resource, int32 Count)
{
	if (!ToastPanel || !ToastText) return;
	ToastText->SetText(FText::FromString(FString::Printf(TEXT("%s X %d개를 팔았습니다"), *Resource, Count)));
	ToastPanel->SetRenderOpacity(1.0f);
	ToastPanel->SetVisibility(ESlateVisibility::Visible);
	ToastElapsed = 0.0f;
}

FString UOreDealInteractionWidget::GetChatDraft() const
{
	return ChatInput ? ChatInput->GetText().ToString() : FString();
}

void UOreDealInteractionWidget::RefreshSales()
{
	AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer());
	for (int32 I = 0; I < SalesLabels.Num(); ++I)
	{
		FString Info;
		bool bCanSell = false;
		if (PC) PC->GetSaleOptionText(I, Info, bCanSell);
		if (SalesLabels[I]) SalesLabels[I]->SetText(FText::FromString(Info.IsEmpty() ? FString::Printf(TEXT("마차 %d: 비어 있음"), I + 1) : Info));
		if (SalesButtons.IsValidIndex(I) && SalesButtons[I]) SalesButtons[I]->SetIsEnabled(bCanSell);
	}
}

void UOreDealInteractionWidget::OnMerchantClicked()
{
	if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer())) PC->ToggleSalesMenu();
}

void UOreDealInteractionWidget::OnCloseSalesClicked()
{
	if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer())) PC->ToggleSalesMenu();
}

void UOreDealInteractionWidget::OnCloseHandRackClicked()
{
	if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer())) PC->ToggleHandRack();
}

void UOreDealInteractionWidget::SubmitSale(int32 WagonIndex)
{
	if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(GetOwningPlayer())) PC->RequestSellWagon(WagonIndex);
}
void UOreDealInteractionWidget::OnSell0Clicked() { SubmitSale(0); }
void UOreDealInteractionWidget::OnSell1Clicked() { SubmitSale(1); }
void UOreDealInteractionWidget::OnSell2Clicked() { SubmitSale(2); }

void UOreDealInteractionWidget::OnSettingsClicked()
{
	bSettingsOpen = !bSettingsOpen;
	if (ChatPanel && ChatPanel->GetContent())
	{
		if (UVerticalBox* Body = Cast<UVerticalBox>(ChatPanel->GetContent()))
			if (Body->GetChildrenCount() > 2) Body->GetChildAt(2)->SetVisibility(bSettingsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UOreDealInteractionWidget::OnOpacityChanged(float Value)
{
	ChatOpacity = FMath::Clamp(Value, 0.3f, 1.0f);
	ApplyChatSettings();
	if (GConfig) { GConfig->SetFloat(TEXT("OreDeal.Chat"), TEXT("Opacity"), ChatOpacity, GGameUserSettingsIni); GConfig->Flush(false, GGameUserSettingsIni); }
}

void UOreDealInteractionWidget::OnScaleChanged(float Value)
{
	ChatScale = FMath::Lerp(0.75f, 1.35f, FMath::Clamp(Value, 0.0f, 1.0f));
	ApplyChatSettings();
	if (GConfig) { GConfig->SetFloat(TEXT("OreDeal.Chat"), TEXT("Scale"), ChatScale, GGameUserSettingsIni); GConfig->Flush(false, GGameUserSettingsIni); }
}
