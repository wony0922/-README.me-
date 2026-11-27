#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "OreDealStoreCatalog.h"
#include "OreDealStoreWidget.generated.h"

class UCanvasPanel;
class UButton;
class UImage;
class UTextBlock;
class UTexture2D;
class UWidget;
class APlayerState;

/** Everything the store shows for the local player. Pushed by UOreDealStoreComponent while the store is open. */
struct FOreDealStoreState
{
	int32 Gold = 0;
	int32 Wood = 0;
	TArray<FOreDealStoreWagonView> Wagons;
	/** Current stock for each item, aligned to BP_PC_Table.StoreItems. */
	TArray<int32> ItemCounts;
	TArray<TObjectPtr<APlayerState>> OtherPlayers;
	TArray<int32> OtherPlayerWagonCounts;
	struct FPlayerStatus
	{
		FString Name;
		int32 Gold = 0;
		int32 Wood = 0;
		bool bHasAxe = false;
		bool bHasWagonSlot = false;
		bool operator==(const FPlayerStatus& Other) const { return Name == Other.Name && Gold == Other.Gold && Wood == Other.Wood && bHasAxe == Other.bHasAxe && bHasWagonSlot == Other.bHasWagonSlot; }
	};
	TArray<FPlayerStatus> PlayerStatuses;
};

DECLARE_DELEGATE_ThreeParams(FOreDealStoreBuyRequest, FName, APlayerState*, int32);
DECLARE_DELEGATE_OneParam(FOreDealStoreSellRequest, int32);
DECLARE_DELEGATE(FOreDealStoreCloseRequest);

/**
 * The F-key store window (local only). Layout follows the STORE mock-up: sign on top, three tab icons
 * on the left (sell / buy / unused), a 4x2 slot grid on the right and the gold plate at the bottom.
 * Mouse: every icon, slot and button is clickable. Keyboard: arrows move the highlight, Space activates.
 */
UCLASS(BlueprintType, Blueprintable)
class TEAMPROJECT_API UOreDealStoreWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	void SetStoreOpen(bool bOpen);
	void OpenInventoryTab();
	bool IsStoreOpen() const { return bStoreOpen; }
	/** Direct mouse route used while the viewport is in captured gameplay mode. */
	void HandleMouseClickAtScreenPosition(const FVector2D& AbsoluteScreenPosition);
	void ApplyState(const FOreDealStoreState& State);
	/** Arrow keys / Space / Escape. bRepeat is true for key-repeat events (only movement repeats). */
	void HandleKey(const FKey& Key, bool bRepeat);
	void ShowNotice(const FString& Message, bool bGood);
	/** Drops a purchase that is waiting for confirmation. Called when the store closes for any reason. */
	void CancelPending();

	UFUNCTION(BlueprintCallable, Category="Ore Deal|Store")
	void SelectStoreTab(int32 NewTab);
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Store")
	void ActivateStoreEntry(int32 Index);
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Store")
	void ChooseStoreConfirmation(int32 Index);
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Store")
	void ConfirmStorePurchase(bool bConfirm);
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Store")
	void CloseStoreFromBlueprint();
	UFUNCTION() void OnStoreInputRouterClicked();

	UFUNCTION(BlueprintImplementableEvent, Category="Ore Deal|Store")
	void BP_RefreshStore(int32 Gold, const TArray<FOreDealStoreWagonView>& Wagons, const TArray<FOreDealStoreItem>& Items, int32 ActiveTab, int32 SelectedIndex, bool bConfirmationOpen, FName PendingItemId);
	UFUNCTION(BlueprintImplementableEvent, Category="Ore Deal|Store")
	void BP_SetStoreOpen(bool bOpen);
	UFUNCTION(BlueprintImplementableEvent, Category="Ore Deal|Store")
	void BP_ShowStoreNotice(const FString& Message, bool bGood);

	FOreDealStoreBuyRequest OnBuyRequested;
	FOreDealStoreSellRequest OnSellRequested;
	FOreDealStoreCloseRequest OnCloseRequested;

private:
	enum class EZone : uint8 { Tabs, Content };
	enum class EHit : uint8 { Tab, Content, Confirm };
	struct FHit
	{
		float X = 0.0f;
		float Y = 0.0f;
		float W = 0.0f;
		float H = 0.0f;
		EHit Kind = EHit::Tab;
		int32 Index = 0;
		bool Contains(const FVector2D& Point) const { return Point.X >= X && Point.X <= X + W && Point.Y >= Y && Point.Y <= Y + H; }
	};

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Root;
	UPROPERTY(Transient) TObjectPtr<UButton> InputRouter;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Panel;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> BuyPage;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> InventoryPage;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> SellPage;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> ConfirmLayer;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> TabImages;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TabLabels;
	UPROPERTY(Transient) TObjectPtr<UImage> TabMark;
	UPROPERTY(Transient) TObjectPtr<UImage> FocusImage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GoldText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WoodText;
	UPROPERTY(Transient) TObjectPtr<UImage> GoldIcon;
	UPROPERTY(Transient) TObjectPtr<UImage> WoodIcon;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NoticeText;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> BuyCostTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> BuyCostIcons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PlayerNameTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PlayerGoldTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PlayerWoodTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PlayerAxeTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PlayerWagonTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SellTitles;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SellFrames;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SellCards;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SellResourceTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SellEmptyTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SellBadges;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SellCountTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SellButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SellButtonTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SellNotes;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ConfirmTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ConfirmBody;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ConfirmButtonTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> CardTextures;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> CardBackTexture;
	UPROPERTY(Transient) TArray<FOreDealStoreItem> StoreItems;
	/** Keeps every UI texture alive while the widget exists. */
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> LoadedTextures;

	FOreDealStoreState Current;
	bool bStoreOpen = false;
	bool bHasState = false;
	int32 Tab = 0;               // 0 = sell, 1 = buy, 2 = player status
	EZone Zone = EZone::Content;
	int32 TabCursor = 0;
	int32 Cursor = 0;
	bool bConfirmOpen = false;
	bool bChoosingTarget = false;
	int32 ConfirmCursor = 0;     // 0 = buy, 1 = cancel
	int32 TargetCursor = 0;
	int32 TargetWagonCursor = 0;
	TObjectPtr<APlayerState> PendingTarget;
	FName PendingItem;
	float GoldPulse = 0.0f;
	bool bGoldGained = true;
	float NoticeRemaining = 0.0f;
	/** Viewport size the panel was last fitted to (see NativeTick). */
	float LastViewWidth = 0.0f;
	float LastViewHeight = 0.0f;

	void Build();
	UTexture2D* LoadTexture(const TCHAR* Path);
	UImage* AddImage(UCanvasPanel* Parent, UTexture2D* Texture, float X, float Y, float W, float H, int32 ZOrder = 0, const FMargin& BoxMargin = FMargin(0.0f));
	UTextBlock* AddText(UCanvasPanel* Parent, int32 FontSize, const FLinearColor& Color, float X, float Y, const FVector2D& Alignment, int32 ZOrder = 0);
	UCanvasPanel* AddPage(int32 ZOrder);

	int32 GetWagonCount() const;
	int32 GetContentCount() const;
	float GetWagonCenterX(int32 Index, int32 Count) const;
	void CollectHits(TArray<FHit>& OutHits) const;
	bool FindHit(const FPointerEvent& MouseEvent, FHit& OutHit) const;
	bool FindHitAtScreenPosition(const FVector2D& AbsoluteScreenPosition, FHit& OutHit) const;
	void SelectTab(int32 NewTab);
	void MoveFocus(int32 DX, int32 DY);
	void Activate();
	void RefreshView();
	const FOreDealStoreItem* FindStoreItem(FName ItemId) const;
	void StepTarget(int32 Delta);
	UTexture2D* CardTextureFor(int32 Resource) const;
};
