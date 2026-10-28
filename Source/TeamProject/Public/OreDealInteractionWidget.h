#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "OreDealMarketBroker.h"
#include "OreDealInteractionWidget.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOreDealMarketButtonEvent, UButton*);

UCLASS()
class TEAMPROJECT_API UOreDealMarketButton : public UButton
{
	GENERATED_BODY()
public:
	UOreDealMarketButton();
	FOreDealMarketButtonEvent OnMarketClicked;
private:
	UFUNCTION() void ForwardMarketClick();
};

class UBorder;
class UButton;
class UCanvasPanel;
class UCanvasPanelSlot;
class UEditableTextBox;
class UScrollBox;
class USizeBox;
class USlider;
class UTextBlock;
class UVerticalBox;
class UHorizontalBox;
class UWidget;

UCLASS()
class TEAMPROJECT_API UOreDealInteractionWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	void SetChatExpanded(bool bExpanded);
	void SetSalesOpen(bool bOpen);
	bool IsChatExpanded() const { return bChatExpanded; }
	bool IsSalesOpen() const { return bSalesOpen; }
	void AddChatMessage(const FString& Sender, const FString& Message);
	void ShowSaleToast(const FString& Resource, int32 Count);
	FString GetChatDraft() const;
	UEditableTextBox* GetChatInputWidget() const { return ChatInput.Get(); }
	void RefreshSales();
	void SetHandRackOpen(bool bOpen);
	bool IsHandRackOpen() const { return bHandRackOpen; }
	void RefreshHandCards(const TArray<int32>& Cards);
	void SetHandRackScreenPosition(const FVector2D& RackScreenPosition, const FVector2D& ViewportSize);
	void MoveHandSelection(int32 Delta);
	void SetDrawPromptVisible(bool bVisible);
	void SetDrawPromptPosition(const FVector2D& ScreenPosition);
	void SetMarketOpen(bool bOpen);
	bool IsMarketOpen() const { return bMarketOpen; }
	void RefreshMarketTrade(const FOreDealMarketSnapshot& Snapshot, const TArray<int32>& LocalHand, APlayerState* LocalPlayer);
	void MoveMarketPlayerSelection(int32 Delta);
	void MoveMarketHandSelection(int32 Delta);
	void ToggleMarketHandSelection();
	void SetMarketReveal(bool bVisible, const TArray<int32>& Cards);
	void SetMarketRevealOrigin(const FVector2D& OriginOffset) { MarketRevealOrigin = OriginOffset; }
	bool IsMarketRevealActive() const { return bMarketRevealActive; }
	UFUNCTION()
	void OnChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION() void OnMerchantClicked();
	UFUNCTION() void OnCloseSalesClicked();
	UFUNCTION() void OnCloseHandRackClicked();
	UFUNCTION() void OnSell0Clicked();
	UFUNCTION() void OnSell1Clicked();
	UFUNCTION() void OnSell2Clicked();
	UFUNCTION() void OnSettingsClicked();
	UFUNCTION() void OnOpacityChanged(float Value);
	UFUNCTION() void OnScaleChanged(float Value);
	UFUNCTION() void OnMarketOfferClicked();
	UFUNCTION() void OnMarketAcceptClicked();
	UFUNCTION() void OnMarketDeclineClicked();

private:
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ReticleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DrawPromptText;
	UPROPERTY(Transient) TObjectPtr<UBorder> MarketPanel;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> MarketSlot;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> MarketRevealPanels;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MarketRevealLabels;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MarketTimerText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MarketStatusText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> MarketConfirmBody;
	UPROPERTY(Transient) TObjectPtr<UButton> MarketOfferButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> TradePlayerPanels;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> TradePlayerHeaders;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TradePlayerTitles;
	UPROPERTY(Transient) TArray<TObjectPtr<UHorizontalBox>> TradeOfferRows;
	UPROPERTY(Transient) TArray<TObjectPtr<UHorizontalBox>> TradeSecretRows;
	UPROPERTY(Transient) TObjectPtr<UBorder> ChatPanel;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> ChatSlot;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> HandRackSlot;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> ChatScroll;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> ChatInput;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ToastText;
	UPROPERTY(Transient) TObjectPtr<UBorder> ToastPanel;
	UPROPERTY(Transient) TObjectPtr<UBorder> SalesPanel;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> SalesSlot;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> SalesRows;
	UPROPERTY(Transient) TObjectPtr<UBorder> HandRackPanel;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> HandCardScroll;
	UPROPERTY(Transient) TObjectPtr<USizeBox> HandCardSizeBox;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> HandCardCanvas;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> HandCardBorders;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> HandCardLabels;
	TArray<int32> LastHandCards;
	UPROPERTY(Transient) TObjectPtr<USlider> OpacitySlider;
	UPROPERTY(Transient) TObjectPtr<USlider> ScaleSlider;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SalesLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> SalesButtons;
	TArray<FString> ChatHistory;
	bool bChatExpanded = false;
	bool bSalesOpen = false;
	bool bHandRackOpen = false;
	int32 HandSelectionIndex = 0;
	bool bSettingsOpen = false;
	float ToastElapsed = 1000.0f;
	float SalesRefreshElapsed = 0.0f;
	float MarketRevealElapsed = 0.0f;
	FVector2D MarketRevealOrigin = FVector2D::ZeroVector;
	float ChatOpacity = 0.78f;
	float ChatScale = 1.0f;
	bool bMarketOpen = false;
	bool bMarketRevealActive = false;
	TSet<int32> SelectedMarketIndices;
	TSet<int32> SelectedHandIndices;
	TArray<int32> CurrentLocalHand;
	TArray<int32> CurrentPublicMarketCards;
	TArray<FOreDealMarketPlayerView> CurrentMarketPlayers;
	TWeakObjectPtr<APlayerState> CurrentLocalPlayer;
	TWeakObjectPtr<APlayerState> CurrentActivePlayer;
	TWeakObjectPtr<APlayerState> CurrentTargetPlayer;
	TWeakObjectPtr<APlayerState> CurrentPendingPlayer;
	int32 FocusedMarketPlayerIndex = 0;
	int32 FocusedMarketHandIndex = 0;
	int32 LastMarketRevision = INDEX_NONE;

	void BuildInterface();
	void ApplyChatLayout();
	void ApplyChatSettings();
	void SubmitSale(int32 WagonIndex);
	void ApplyHandSelection();
	void RebuildMarketPanels(const FOreDealMarketSnapshot& Snapshot);
	void SetMarketPlayerTarget(int32 PlayerIndex);
	void ToggleMarketHandAt(int32 HandIndex);
	void RemoveCardFromMarketOffer(int32 Resource);
};
