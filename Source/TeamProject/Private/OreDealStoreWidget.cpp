#include "OreDealStoreWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "Styling/SlateBrush.h"

namespace
{
	// ---- Layout, in panel units. The panel is the STORE mock-up at half its pixel size (1141 x 841).
	constexpr float PanelW = 1141.0f;
	constexpr float PanelH = 841.0f;

	// Left column: three tab icons (sell / buy / unused).
	constexpr float TabX = 65.5f;
	constexpr float TabW = 144.0f;
	constexpr float TabH = 131.0f;
	const float TabY[3] = { 182.0f, 324.5f, 467.0f };

	// Right side: 4 x 2 slot grid, each with a name on the left and a cost tag on its lower right corner.
	const float SlotX[OreDealStore::SlotColumns] = { 276.5f, 480.5f, 684.0f, 888.0f };
	const float SlotY[OreDealStore::SlotRows] = { 165.5f, 423.0f };
	constexpr float SlotW = 193.0f;
	constexpr float SlotH = 198.0f;
	constexpr float TagOffsetX = 103.0f;
	constexpr float TagOffsetY = 178.5f;
	constexpr float TagW = 90.0f;
	constexpr float TagH = 72.0f;

	// Middle of the tan content area (used by the sell page and the confirm box).
	constexpr float ContentCenterX = 679.0f;
	constexpr float ContentCenterY = 419.0f;

	// Sell page: one card-sized frame per wagon with a sell button under it.
	constexpr int32 MaxWagons = 3;
	constexpr float SellFrameW = 178.0f;
	constexpr float SellFrameH = 246.0f;
	constexpr float SellFrameY = 196.0f;
	constexpr float SellCardW = 142.0f;
	constexpr float SellCardH = 199.0f;
	constexpr float SellButtonY = 456.0f;
	constexpr float SellButtonH = 52.0f;

	// Confirm box.
	constexpr float ConfirmW = 440.0f;
	constexpr float ConfirmH = 232.0f;
	constexpr float ConfirmButtonW = 160.0f;
	constexpr float ConfirmButtonH = 52.0f;
	constexpr float ConfirmButtonY = ContentCenterY + 44.0f;

	// Gold plate at the bottom.
	constexpr float GoldTextX = 652.0f;
	constexpr float GoldTextY = 762.0f;

	constexpr float FocusPad = 9.0f;

	// Strip that continues the sign above the background texture (overlaps its first rows).
	constexpr float SignTopX = 300.0f;
	constexpr float SignTopY = -20.0f;
	constexpr float SignTopW = 540.0f;
	constexpr float SignTopH = 26.0f;

	// Room kept free around the panel when it has to shrink to fit the viewport (sign top, key hint).
	constexpr float FitMarginX = 24.0f;
	constexpr float FitMarginY = 100.0f;

	const TCHAR* StoreCardTexturePaths[] = {
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Stone.T_Card_Stone"),
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Coal.T_Card_Coal"),
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Gold.T_Card_Gold"),
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Iron.T_Card_Iron"),
		TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Diamond.T_Card_Diamond"),
	};

	FLinearColor Ink() { return FLinearColor::FromSRGBColor(FColor(43, 26, 20)); }
	FLinearColor Cream() { return FLinearColor::FromSRGBColor(FColor(250, 240, 214)); }
	FLinearColor Good() { return FLinearColor::FromSRGBColor(FColor(38, 110, 46)); }
	FLinearColor Bad() { return FLinearColor::FromSRGBColor(FColor(170, 40, 28)); }

	void SetRect(UWidget* Widget, float X, float Y, float W, float H)
	{
		if (UCanvasPanelSlot* CanvasSlot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr)
		{
			CanvasSlot->SetPosition(FVector2D(X, Y));
			CanvasSlot->SetSize(FVector2D(W, H));
		}
	}

	void SetPoint(UWidget* Widget, float X, float Y)
	{
		if (UCanvasPanelSlot* CanvasSlot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr)
			CanvasSlot->SetPosition(FVector2D(X, Y));
	}

	void SetShown(UWidget* Widget, bool bShown)
	{
		if (Widget) Widget->SetVisibility(bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	void SetLabel(UTextBlock* Label, const FString& Text)
	{
		if (Label) Label->SetText(FText::FromString(Text));
	}

	// Same helpers for the TObjectPtr members.
	template <typename T> void SetRect(const TObjectPtr<T>& Widget, float X, float Y, float W, float H) { SetRect(static_cast<UWidget*>(Widget.Get()), X, Y, W, H); }
	template <typename T> void SetPoint(const TObjectPtr<T>& Widget, float X, float Y) { SetPoint(static_cast<UWidget*>(Widget.Get()), X, Y); }
	template <typename T> void SetShown(const TObjectPtr<T>& Widget, bool bShown) { SetShown(static_cast<UWidget*>(Widget.Get()), bShown); }
	void SetLabel(const TObjectPtr<UTextBlock>& Label, const FString& Text) { SetLabel(Label.Get(), Text); }
}

// ---------------------------------------------------------------------------------------------
// Construction

void UOreDealStoreWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!Root) Build();
	SetVisibility(ESlateVisibility::Collapsed);
}

UTexture2D* UOreDealStoreWidget::LoadTexture(const TCHAR* Path)
{
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, Path);
	if (Texture) LoadedTextures.AddUnique(Texture);
	return Texture;
}

UImage* UOreDealStoreWidget::AddImage(UCanvasPanel* Parent, UTexture2D* Texture, float X, float Y, float W, float H, int32 ZOrder, const FMargin& BoxMargin)
{
	UImage* Image = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	FSlateBrush Brush;
	if (Texture) Brush.SetResourceObject(Texture);
	const bool bBox = BoxMargin.Left > 0.0f || BoxMargin.Top > 0.0f;
	Brush.DrawAs = bBox ? ESlateBrushDrawType::Box : ESlateBrushDrawType::Image;
	if (bBox) Brush.Margin = BoxMargin;
	Image->SetBrush(Brush);
	Image->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* CanvasSlot = Parent->AddChildToCanvas(Image))
	{
		CanvasSlot->SetPosition(FVector2D(X, Y));
		CanvasSlot->SetSize(FVector2D(W, H));
		CanvasSlot->SetZOrder(ZOrder);
	}
	return Image;
}

UTextBlock* UOreDealStoreWidget::AddText(UCanvasPanel* Parent, int32 FontSize, const FLinearColor& Color, float X, float Y, const FVector2D& Alignment, int32 ZOrder)
{
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	FSlateFontInfo Font = Label->GetFont();
	Font.Size = FontSize;
	Label->SetFont(Font);
	Label->SetColorAndOpacity(FSlateColor(Color));
	Label->SetJustification(Alignment.X > 0.75f ? ETextJustify::Right : (Alignment.X > 0.25f ? ETextJustify::Center : ETextJustify::Left));
	Label->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* CanvasSlot = Parent->AddChildToCanvas(Label))
	{
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetAlignment(Alignment);
		CanvasSlot->SetPosition(FVector2D(X, Y));
		CanvasSlot->SetZOrder(ZOrder);
	}
	return Label;
}

UCanvasPanel* UOreDealStoreWidget::AddPage(int32 ZOrder)
{
	UCanvasPanel* Page = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	Page->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* CanvasSlot = Panel->AddChildToCanvas(Page))
	{
		CanvasSlot->SetPosition(FVector2D::ZeroVector);
		CanvasSlot->SetSize(FVector2D(PanelW, PanelH));
		CanvasSlot->SetZOrder(ZOrder);
	}
	return Page;
}

void UOreDealStoreWidget::Build()
{
	if (!WidgetTree) return;

	UTexture2D* BackgroundTexture = LoadTexture(TEXT("/Game/MaterialTraders/UI/Store/T_Store_BG.T_Store_BG"));
	UTexture2D* SlotTexture = LoadTexture(TEXT("/Game/MaterialTraders/UI/Store/T_Store_Slot.T_Store_Slot"));
	UTexture2D* TagTexture = LoadTexture(TEXT("/Game/MaterialTraders/UI/Store/T_Store_Tag.T_Store_Tag"));
	UTexture2D* ButtonTexture = LoadTexture(TEXT("/Game/MaterialTraders/UI/Store/T_Store_Button.T_Store_Button"));
	UTexture2D* PaperTexture = LoadTexture(TEXT("/Game/MaterialTraders/UI/Store/T_Store_Paper.T_Store_Paper"));
	UTexture2D* FocusTexture = LoadTexture(TEXT("/Game/MaterialTraders/UI/Store/T_Store_Focus.T_Store_Focus"));
	UTexture2D* TabTextures[3] = {
		LoadTexture(TEXT("/Game/MaterialTraders/UI/Store/T_Store_Tab_Sell.T_Store_Tab_Sell")),
		LoadTexture(TEXT("/Game/MaterialTraders/UI/Store/T_Store_Tab_Buy.T_Store_Tab_Buy")),
		LoadTexture(TEXT("/Game/MaterialTraders/UI/Store/T_Store_Tab_Empty.T_Store_Tab_Empty")),
	};
	for (const TCHAR* Path : StoreCardTexturePaths) CardTextures.Add(LoadTexture(Path));
	CardBackTexture = LoadTexture(TEXT("/Game/MaterialTraders/Cards/Textures/T_Card_Back.T_Card_Back"));

	const FMargin ButtonMargin(0.2f, 0.4f);
	const FMargin PaperMargin(0.3f);
	const FMargin FocusMargin(0.36f);

	Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("OreDealStoreRoot"));
	WidgetTree->RootWidget = Root;

	// Dim the table behind the store.
	UImage* Dim = AddImage(Root, nullptr, 0.0f, 0.0f, 0.0f, 0.0f);
	Dim->SetColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
	if (UCanvasPanelSlot* DimSlot = Cast<UCanvasPanelSlot>(Dim->Slot))
	{
		DimSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		DimSlot->SetOffsets(FMargin(0.0f));
	}

	Panel = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("OreDealStorePanel"));
	Panel->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		PanelSlot->SetSize(FVector2D(PanelW, PanelH));
		PanelSlot->SetPosition(FVector2D(0.0f, -14.0f));
		PanelSlot->SetZOrder(1);
	}

	// Key hint just under the panel (inside it, so it scales and moves with the panel).
	UTextBlock* Hint = AddText(Panel, 15, Cream(), PanelW * 0.5f, PanelH + 8.0f, FVector2D(0.5f, 0.0f), 2);
	Hint->SetShadowOffset(FVector2D(1.5f, 1.5f));
	Hint->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
	SetLabel(Hint, TEXT("방향키 : 이동   ·   Space : 선택   ·   F : 닫기"));

	AddImage(Panel, BackgroundTexture, 0.0f, 0.0f, PanelW, PanelH, 0);
	// The top edge of the STORE sign sticks out above the frame, like in the mock-up.
	AddImage(Panel, LoadTexture(TEXT("/Game/MaterialTraders/UI/Store/T_Store_SignTop.T_Store_SignTop")), SignTopX, SignTopY, SignTopW, SignTopH, 1);

	// Tabs.
	for (int32 Index = 0; Index < 3; ++Index)
		TabImages.Add(AddImage(Panel, TabTextures[Index], TabX, TabY[Index], TabW, TabH, 2));
	TabMark = AddImage(Panel, FocusTexture, 0.0f, 0.0f, 0.0f, 0.0f, 3, FocusMargin);
	TabMark->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.55f));

	// ---- Buy page: the 4 x 2 grid. Items come from OreDealStore::GetItems(), one per slot in order.
	BuyPage = AddPage(10);
	const TArray<FOreDealStoreItem>& Items = OreDealStore::GetItems();
	for (int32 Index = 0; Index < OreDealStore::SlotCount; ++Index)
	{
		const float X = SlotX[Index % OreDealStore::SlotColumns];
		const float Y = SlotY[Index / OreDealStore::SlotColumns];
		AddImage(BuyPage, SlotTexture, X, Y, SlotW, SlotH, 0);
		if (!Items.IsValidIndex(Index))
		{
			BuyCostTexts.Add(nullptr);
			continue;
		}
		const FOreDealStoreItem& Item = Items[Index];
		AddImage(BuyPage, PaperTexture, X + (SlotW - 118.0f) * 0.5f, Y + (SlotH - 118.0f) * 0.5f, 118.0f, 118.0f, 1, PaperMargin);
		AddImage(BuyPage, LoadTexture(*Item.IconPath), X + (SlotW - 96.0f) * 0.5f, Y + (SlotH - 96.0f) * 0.5f, 96.0f, 96.0f, 2);
		SetLabel(AddText(BuyPage, 18, Ink(), X + TagOffsetX - 6.0f, Y + 221.0f, FVector2D(1.0f, 0.5f), 3), Item.Name);
		AddImage(BuyPage, TagTexture, X + TagOffsetX, Y + TagOffsetY, TagW, TagH, 4);
		UTextBlock* Cost = AddText(BuyPage, 20, Ink(), X + TagOffsetX + 43.0f, Y + TagOffsetY + 47.0f, FVector2D(0.0f, 0.5f), 5);
		SetLabel(Cost, FString::FromInt(Item.Cost));
		BuyCostTexts.Add(Cost);
	}

	// ---- Sell page: one card frame per wagon.
	SellPage = AddPage(10);
	for (int32 Index = 0; Index < MaxWagons; ++Index)
	{
		SellTitles.Add(AddText(SellPage, 18, Ink(), 0.0f, 0.0f, FVector2D(0.5f, 0.5f), 1));
		SetLabel(SellTitles.Last(), FString::Printf(TEXT("마차 %d"), Index + 1));
		SellFrames.Add(AddImage(SellPage, SlotTexture, 0.0f, 0.0f, SellFrameW, SellFrameH, 1, FMargin(0.16f)));
		SellCards.Add(AddImage(SellPage, CardBackTexture, 0.0f, 0.0f, SellCardW, SellCardH, 2));
		SellEmptyTexts.Add(AddText(SellPage, 16, Cream(), 0.0f, 0.0f, FVector2D(0.5f, 0.5f), 2));
		SetLabel(SellEmptyTexts.Last(), TEXT("비어 있음"));
		SellBadges.Add(AddImage(SellPage, PaperTexture, 0.0f, 0.0f, 66.0f, 40.0f, 3, PaperMargin));
		SellCountTexts.Add(AddText(SellPage, 18, Ink(), 0.0f, 0.0f, FVector2D(0.5f, 0.5f), 4));
		SellButtons.Add(AddImage(SellPage, ButtonTexture, 0.0f, 0.0f, SellFrameW, SellButtonH, 1, ButtonMargin));
		SellButtonTexts.Add(AddText(SellPage, 18, Cream(), 0.0f, 0.0f, FVector2D(0.5f, 0.5f), 2));
		SellNotes.Add(AddText(SellPage, 12, Ink(), 0.0f, 0.0f, FVector2D(0.5f, 0.0f), 1));
	}

	// ---- Confirm box for a purchase.
	ConfirmLayer = AddPage(40);
	AddImage(ConfirmLayer, nullptr, 0.0f, 0.0f, PanelW, PanelH, 0)->SetColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
	AddImage(ConfirmLayer, PaperTexture, ContentCenterX - ConfirmW * 0.5f, ContentCenterY - ConfirmH * 0.5f, ConfirmW, ConfirmH, 1, PaperMargin);
	ConfirmTitle = AddText(ConfirmLayer, 24, Ink(), ContentCenterX, ContentCenterY - 70.0f, FVector2D(0.5f, 0.5f), 2);
	ConfirmBody = AddText(ConfirmLayer, 16, Ink(), ContentCenterX, ContentCenterY - 22.0f, FVector2D(0.5f, 0.5f), 2);
	const TCHAR* ConfirmLabels[2] = { TEXT("구매"), TEXT("취소") };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const float X = ContentCenterX + (Index == 0 ? -(ConfirmButtonW + 10.0f) : 10.0f);
		AddImage(ConfirmLayer, ButtonTexture, X, ConfirmButtonY, ConfirmButtonW, ConfirmButtonH, 2, ButtonMargin);
		SetLabel(AddText(ConfirmLayer, 18, Cream(), X + ConfirmButtonW * 0.5f, ConfirmButtonY + ConfirmButtonH * 0.5f, FVector2D(0.5f, 0.5f), 3), ConfirmLabels[Index]);
	}

	// Gold plate text, notice line and the moving highlight.
	GoldText = AddText(Panel, 42, Ink(), GoldTextX, GoldTextY, FVector2D(0.0f, 0.5f), 5);
	GoldText->SetRenderTransformPivot(FVector2D(0.0f, 0.5f));
	SetLabel(GoldText, TEXT("0g"));
	NoticeText = AddText(Panel, 15, Bad(), 318.0f, GoldTextY, FVector2D(0.5f, 0.5f), 5);
	NoticeText->SetVisibility(ESlateVisibility::Collapsed);
	FocusImage = AddImage(Panel, FocusTexture, 0.0f, 0.0f, 0.0f, 0.0f, 60, FocusMargin);
}

// ---------------------------------------------------------------------------------------------
// State

void UOreDealStoreWidget::SetStoreOpen(bool bOpen)
{
	if (bStoreOpen == bOpen) return;
	bStoreOpen = bOpen;
	CancelPending();
	if (bOpen)
	{
		// Always start on the sell tab with the first wagon highlighted.
		Tab = 0;
		TabCursor = 0;
		Zone = EZone::Content;
		Cursor = 0;
		NoticeRemaining = 0.0f;
		if (NoticeText) NoticeText->SetVisibility(ESlateVisibility::Collapsed);
		RefreshView();
	}
	// Visible (not HitTestInvisible) so the widget itself receives mouse clicks and hover.
	SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UOreDealStoreWidget::CancelPending()
{
	if (!bConfirmOpen && PendingItem.IsNone()) return;
	bConfirmOpen = false;
	PendingItem = NAME_None;
	ConfirmCursor = 0;
	if (bStoreOpen) RefreshView();
	else SetShown(ConfirmLayer, false);
}

void UOreDealStoreWidget::ApplyState(const FOreDealStoreState& State)
{
	const bool bGoldChanged = !bHasState || State.Gold != Current.Gold;
	const bool bWagonsChanged = !bHasState || State.Wagons != Current.Wagons;
	if (!bGoldChanged && !bWagonsChanged) return;
	if (bGoldChanged && GoldText)
	{
		SetLabel(GoldText, FString::Printf(TEXT("%dg"), State.Gold));
		if (bHasState)
		{
			// Immediate feedback: the number pops and flashes green (earned) or red (spent).
			bGoldGained = State.Gold > Current.Gold;
			GoldPulse = 1.0f;
		}
	}
	Current = State;
	bHasState = true;
	// A purchase waiting for confirmation is dropped when it is no longer affordable.
	if (bConfirmOpen)
	{
		const FOreDealStoreItem* Item = OreDealStore::FindItem(PendingItem);
		if (!Item || Item->Cost > Current.Gold) { CancelPending(); return; }
	}
	RefreshView();
}

void UOreDealStoreWidget::ShowNotice(const FString& Message, bool bGood)
{
	if (!NoticeText) return;
	SetLabel(NoticeText, Message);
	NoticeText->SetColorAndOpacity(FSlateColor(bGood ? Good() : Bad()));
	NoticeText->SetVisibility(ESlateVisibility::HitTestInvisible);
	NoticeRemaining = 2.5f;
}

void UOreDealStoreWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// Shrink the panel when the viewport is too small or too narrow for it, so nothing is cut off.
	const auto ViewSize = MyGeometry.GetLocalSize();
	if (Panel && (FMath::Abs(ViewSize.X - LastViewWidth) > 0.5f || FMath::Abs(ViewSize.Y - LastViewHeight) > 0.5f))
	{
		LastViewWidth = ViewSize.X;
		LastViewHeight = ViewSize.Y;
		const float Fit = FMath::Clamp(FMath::Min(LastViewWidth / (PanelW + FitMarginX), LastViewHeight / (PanelH + FitMarginY)), 0.2f, 1.0f);
		Panel->SetRenderScale(FVector2D(Fit, Fit));
	}
	if (GoldPulse > 0.0f && GoldText)
	{
		GoldPulse = FMath::Max(GoldPulse - InDeltaTime / 0.45f, 0.0f);
		const float Scale = 1.0f + 0.22f * GoldPulse;
		GoldText->SetRenderScale(FVector2D(Scale, Scale));
		GoldText->SetColorAndOpacity(FSlateColor(FMath::Lerp(Ink(), bGoldGained ? Good() : Bad(), GoldPulse)));
	}
	if (NoticeRemaining > 0.0f)
	{
		NoticeRemaining -= InDeltaTime;
		if (NoticeRemaining <= 0.0f && NoticeText) NoticeText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

UTexture2D* UOreDealStoreWidget::CardTextureFor(int32 Resource) const
{
	UTexture2D* Texture = CardTextures.IsValidIndex(Resource) ? CardTextures[Resource].Get() : nullptr;
	return Texture ? Texture : CardBackTexture.Get();
}

int32 UOreDealStoreWidget::GetWagonCount() const
{
	return FMath::Min(Current.Wagons.Num(), MaxWagons);
}

int32 UOreDealStoreWidget::GetContentCount() const
{
	if (Tab == 0) return GetWagonCount();
	if (Tab == 1) return OreDealStore::SlotCount;
	return 0;
}

float UOreDealStoreWidget::GetWagonCenterX(int32 Index, int32 Count) const
{
	const float Step = Count >= 3 ? 236.0f : 270.0f;
	return ContentCenterX + (Index - (Count - 1) * 0.5f) * Step;
}

// ---------------------------------------------------------------------------------------------
// Hit areas (shared by mouse and keyboard highlight)

void UOreDealStoreWidget::CollectHits(TArray<FHit>& OutHits) const
{
	OutHits.Reset();
	const auto Add = [&OutHits](float X, float Y, float W, float H, EHit Kind, int32 Index)
	{
		FHit& Hit = OutHits.AddDefaulted_GetRef();
		Hit.X = X; Hit.Y = Y; Hit.W = W; Hit.H = H; Hit.Kind = Kind; Hit.Index = Index;
	};
	if (bConfirmOpen)
	{
		Add(ContentCenterX - (ConfirmButtonW + 10.0f), ConfirmButtonY, ConfirmButtonW, ConfirmButtonH, EHit::Confirm, 0);
		Add(ContentCenterX + 10.0f, ConfirmButtonY, ConfirmButtonW, ConfirmButtonH, EHit::Confirm, 1);
		return;
	}
	for (int32 Index = 0; Index < 3; ++Index) Add(TabX, TabY[Index], TabW, TabH, EHit::Tab, Index);
	if (Tab == 0)
	{
		const int32 Count = GetWagonCount();
		for (int32 Index = 0; Index < Count; ++Index)
			Add(GetWagonCenterX(Index, Count) - SellFrameW * 0.5f, SellButtonY, SellFrameW, SellButtonH, EHit::Content, Index);
	}
	else if (Tab == 1)
	{
		for (int32 Index = 0; Index < OreDealStore::SlotCount; ++Index)
			Add(SlotX[Index % OreDealStore::SlotColumns], SlotY[Index / OreDealStore::SlotColumns], SlotW, SlotH, EHit::Content, Index);
	}
}

bool UOreDealStoreWidget::FindHit(const FPointerEvent& MouseEvent, FHit& OutHit) const
{
	if (!Panel) return false;
	const auto LocalPoint = Panel->GetCachedGeometry().AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2D Local(LocalPoint.X, LocalPoint.Y);
	TArray<FHit> Hits;
	CollectHits(Hits);
	for (const FHit& Hit : Hits)
		if (Hit.Contains(Local)) { OutHit = Hit; return true; }
	return false;
}

// ---------------------------------------------------------------------------------------------
// Input

void UOreDealStoreWidget::SelectTab(int32 NewTab)
{
	NewTab = FMath::Clamp(NewTab, 0, 2);
	TabCursor = NewTab;
	if (Tab == NewTab) return;
	Tab = NewTab;
	Cursor = 0;
}

void UOreDealStoreWidget::MoveFocus(int32 DX, int32 DY)
{
	if (bConfirmOpen)
	{
		if (DX != 0) ConfirmCursor = DX > 0 ? 1 : 0;
		return;
	}
	if (Zone == EZone::Tabs)
	{
		// Up/Down walks the sell / buy / unused icons; Right goes back into the page.
		if (DY != 0) SelectTab(TabCursor + DY);
		else if (DX > 0 && GetContentCount() > 0)
		{
			Zone = EZone::Content;
			Cursor = FMath::Clamp(Cursor, 0, GetContentCount() - 1);
		}
		return;
	}
	const int32 Count = GetContentCount();
	if (Count <= 0) { Zone = EZone::Tabs; return; }
	if (Tab == 1)
	{
		int32 Column = Cursor % OreDealStore::SlotColumns;
		int32 Row = Cursor / OreDealStore::SlotColumns;
		if (DX < 0 && Column == 0) { Zone = EZone::Tabs; TabCursor = Tab; return; }
		Column = FMath::Clamp(Column + DX, 0, OreDealStore::SlotColumns - 1);
		Row = FMath::Clamp(Row + DY, 0, OreDealStore::SlotRows - 1);
		Cursor = Row * OreDealStore::SlotColumns + Column;
		return;
	}
	if (DX < 0 && Cursor == 0) { Zone = EZone::Tabs; TabCursor = Tab; return; }
	Cursor = FMath::Clamp(Cursor + DX, 0, Count - 1);
}

void UOreDealStoreWidget::Activate()
{
	if (bConfirmOpen)
	{
		const FName Item = PendingItem;
		const bool bBuy = ConfirmCursor == 0;
		CancelPending();
		if (bBuy) OnBuyRequested.ExecuteIfBound(Item);
		return;
	}
	if (Zone == EZone::Tabs)
	{
		SelectTab(TabCursor);
		if (GetContentCount() > 0) { Zone = EZone::Content; Cursor = 0; }
		return;
	}
	if (Tab == 0)
	{
		if (!Current.Wagons.IsValidIndex(Cursor)) return;
		const FOreDealStoreWagonView& Wagon = Current.Wagons[Cursor];
		if (Wagon.bCanSell) OnSellRequested.ExecuteIfBound(Cursor);
		else ShowNotice(Wagon.Note.IsEmpty() ? FString(TEXT("지금은 판매할 수 없습니다")) : Wagon.Note, false);
		return;
	}
	if (Tab == 1)
	{
		const TArray<FOreDealStoreItem>& Items = OreDealStore::GetItems();
		if (!Items.IsValidIndex(Cursor)) return;
		if (Items[Cursor].Cost > Current.Gold) { ShowNotice(TEXT("골드가 부족합니다"), false); return; }
		PendingItem = Items[Cursor].Id;
		ConfirmCursor = 0;
		bConfirmOpen = true;
	}
}

void UOreDealStoreWidget::HandleKey(const FKey& Key, bool bRepeat)
{
	if (!bStoreOpen) return;
	if (Key == EKeys::Left) MoveFocus(-1, 0);
	else if (Key == EKeys::Right) MoveFocus(1, 0);
	else if (Key == EKeys::Up) MoveFocus(0, -1);
	else if (Key == EKeys::Down) MoveFocus(0, 1);
	else if (bRepeat) return;
	else if (Key == EKeys::SpaceBar || Key == EKeys::Enter) Activate();
	else if (Key == EKeys::Escape)
	{
		if (bConfirmOpen) CancelPending();
		else OnCloseRequested.ExecuteIfBound();
		return;
	}
	else return;
	RefreshView();
}

FReply UOreDealStoreWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Hovering moves the same highlight the arrow keys move. Only react to real mouse movement so a
	// resting cursor never fights the keyboard.
	FHit Hit;
	if (bStoreOpen && !InMouseEvent.GetCursorDelta().IsNearlyZero() && FindHit(InMouseEvent, Hit))
	{
		bool bChanged = false;
		if (Hit.Kind == EHit::Confirm) { bChanged = ConfirmCursor != Hit.Index; ConfirmCursor = Hit.Index; }
		else if (Hit.Kind == EHit::Tab) { bChanged = Zone != EZone::Tabs || TabCursor != Hit.Index; Zone = EZone::Tabs; TabCursor = Hit.Index; }
		else { bChanged = Zone != EZone::Content || Cursor != Hit.Index; Zone = EZone::Content; Cursor = Hit.Index; }
		if (bChanged) RefreshView();
	}
	return FReply::Handled();
}

FReply UOreDealStoreWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	FHit Hit;
	if (bStoreOpen && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && FindHit(InMouseEvent, Hit))
	{
		if (Hit.Kind == EHit::Confirm) ConfirmCursor = Hit.Index;
		else if (Hit.Kind == EHit::Tab) { Zone = EZone::Tabs; TabCursor = Hit.Index; }
		else { Zone = EZone::Content; Cursor = Hit.Index; }
		if (Hit.Kind == EHit::Tab)
		{
			// Clicking an icon only switches the page; the highlight stays on the icon.
			SelectTab(Hit.Index);
		}
		else Activate();
		RefreshView();
	}
	// The store is modal: clicks never fall through to the table behind it.
	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------------
// View

void UOreDealStoreWidget::RefreshView()
{
	if (!Panel) return;

	for (int32 Index = 0; Index < TabImages.Num(); ++Index)
		if (TabImages[Index]) TabImages[Index]->SetColorAndOpacity(Index == Tab ? FLinearColor::White : FLinearColor(0.62f, 0.62f, 0.62f, 1.0f));
	SetRect(TabMark, TabX - FocusPad, TabY[Tab] - FocusPad, TabW + FocusPad * 2.0f, TabH + FocusPad * 2.0f);
	SetShown(SellPage, Tab == 0);
	SetShown(BuyPage, Tab == 1);
	SetShown(ConfirmLayer, bConfirmOpen);

	if (Tab == 0)
	{
		const int32 Count = GetWagonCount();
		for (int32 Index = 0; Index < MaxWagons; ++Index)
		{
			const bool bShown = Index < Count;
			SetShown(SellTitles[Index], bShown);
			SetShown(SellFrames[Index], bShown);
			SetShown(SellButtons[Index], bShown);
			SetShown(SellButtonTexts[Index], bShown);
			SetShown(SellNotes[Index], bShown);
			if (!bShown)
			{
				SetShown(SellCards[Index], false);
				SetShown(SellEmptyTexts[Index], false);
				SetShown(SellBadges[Index], false);
				SetShown(SellCountTexts[Index], false);
				continue;
			}
			const FOreDealStoreWagonView& Wagon = Current.Wagons[Index];
			const bool bLoaded = Wagon.Count > 0;
			const float CenterX = GetWagonCenterX(Index, Count);
			const float Left = CenterX - SellFrameW * 0.5f;
			SetPoint(SellTitles[Index], CenterX, SellFrameY - 18.0f);
			SetRect(SellFrames[Index], Left, SellFrameY, SellFrameW, SellFrameH);
			SetRect(SellCards[Index], CenterX - SellCardW * 0.5f, SellFrameY + (SellFrameH - SellCardH) * 0.5f, SellCardW, SellCardH);
			SetPoint(SellEmptyTexts[Index], CenterX, SellFrameY + SellFrameH * 0.5f);
			SetRect(SellBadges[Index], Left + SellFrameW - 54.0f, SellFrameY + SellFrameH - 32.0f, 66.0f, 40.0f);
			SetPoint(SellCountTexts[Index], Left + SellFrameW - 21.0f, SellFrameY + SellFrameH - 12.0f);
			SetRect(SellButtons[Index], Left, SellButtonY, SellFrameW, SellButtonH);
			SetPoint(SellButtonTexts[Index], CenterX, SellButtonY + SellButtonH * 0.5f);
			SetPoint(SellNotes[Index], CenterX, SellButtonY + SellButtonH + 8.0f);

			SetShown(SellCards[Index], bLoaded);
			SetShown(SellEmptyTexts[Index], !bLoaded);
			SetShown(SellBadges[Index], bLoaded);
			SetShown(SellCountTexts[Index], bLoaded);
			if (bLoaded)
			{
				FSlateBrush CardBrush;
				CardBrush.SetResourceObject(CardTextureFor(Wagon.Resource));
				SellCards[Index]->SetBrush(CardBrush);
				SetLabel(SellCountTexts[Index], FString::Printf(TEXT("x %d"), Wagon.Count));
			}
			SetLabel(SellButtonTexts[Index], bLoaded ? FString::Printf(TEXT("판매  +%dg"), Wagon.Reward) : FString(TEXT("판매")));
			const float Shade = Wagon.bCanSell ? 1.0f : 0.5f;
			SellButtons[Index]->SetColorAndOpacity(FLinearColor(Shade, Shade, Shade, 1.0f));
			SellButtonTexts[Index]->SetColorAndOpacity(FSlateColor(Wagon.bCanSell ? Cream() : Cream() * 0.6f));
			SetLabel(SellNotes[Index], Wagon.Note);
		}
	}
	else if (Tab == 1)
	{
		const TArray<FOreDealStoreItem>& Items = OreDealStore::GetItems();
		for (int32 Index = 0; Index < BuyCostTexts.Num(); ++Index)
			if (BuyCostTexts[Index] && Items.IsValidIndex(Index))
				BuyCostTexts[Index]->SetColorAndOpacity(FSlateColor(Items[Index].Cost > Current.Gold ? Bad() : Ink()));
	}

	if (bConfirmOpen)
	{
		if (const FOreDealStoreItem* Item = OreDealStore::FindItem(PendingItem))
		{
			SetLabel(ConfirmTitle, FString::Printf(TEXT("%s 구매"), *Item->Name));
			SetLabel(ConfirmBody, FString::Printf(TEXT("%d골드를 사용합니다  (보유 %dg → %dg)"), Item->Cost, Current.Gold, Current.Gold - Item->Cost));
		}
	}

	// Move the single highlight frame onto whatever currently has the focus.
	TArray<FHit> Hits;
	CollectHits(Hits);
	const EHit WantedKind = bConfirmOpen ? EHit::Confirm : (Zone == EZone::Tabs ? EHit::Tab : EHit::Content);
	const int32 WantedIndex = bConfirmOpen ? ConfirmCursor : (Zone == EZone::Tabs ? TabCursor : Cursor);
	const FHit* Focus = Hits.FindByPredicate([WantedKind, WantedIndex](const FHit& Hit) { return Hit.Kind == WantedKind && Hit.Index == WantedIndex; });
	SetShown(FocusImage, Focus != nullptr);
	if (Focus) SetRect(FocusImage, Focus->X - FocusPad, Focus->Y - FocusPad, Focus->W + FocusPad * 2.0f, Focus->H + FocusPad * 2.0f);
}
