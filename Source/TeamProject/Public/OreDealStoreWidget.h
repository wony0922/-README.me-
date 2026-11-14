#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "OreDealStoreCatalog.h"
#include "OreDealStoreWidget.generated.h"

class UCanvasPanel;
class UImage;
class UTextBlock;
class UTexture2D;
class UWidget;

/** Everything the store shows for the local player. Pushed by UOreDealStoreComponent while the store is open. */
struct FOreDealStoreState
{
	int32 Gold = 0;
	TArray<FOreDealStoreWagonView> Wagons;
};

DECLARE_DELEGATE_OneParam(FOreDealStoreBuyRequest, FName);
DECLARE_DELEGATE_OneParam(FOreDealStoreSellRequest, int32);
DECLARE_DELEGATE(FOreDealStoreCloseRequest);

/**
 * The F-key store window (local only). Layout follows the STORE mock-up: sign on top, three tab icons
 * on the left (sell / buy / unused), a 4x2 slot grid on the right and the gold plate at the bottom.
 * Mouse: every icon, slot and button is clickable. Keyboard: arrows move the highlight, Space activates.
 */
UCLASS()
class TEAMPROJECT_API UOreDealStoreWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	void SetStoreOpen(bool bOpen);
	bool IsStoreOpen() const { return bStoreOpen; }
	void ApplyState(const FOreDealStoreState& State);
	/** Arrow keys / Space / Escape. bRepeat is true for key-repeat events (only movement repeats). */
	void HandleKey(const FKey& Key, bool bRepeat);
	void ShowNotice(const FString& Message, bool bGood);
	/** Drops a purchase that is waiting for confirmation. Called when the store closes for any reason. */
	void CancelPending();

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
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Panel;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> BuyPage;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> SellPage;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> ConfirmLayer;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> TabImages;
	UPROPERTY(Transient) TObjectPtr<UImage> TabMark;
	UPROPERTY(Transient) TObjectPtr<UImage> FocusImage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GoldText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NoticeText;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> BuyCostTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SellTitles;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SellFrames;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SellCards;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SellEmptyTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SellBadges;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SellCountTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SellButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SellButtonTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SellNotes;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ConfirmTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ConfirmBody;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> CardTextures;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> CardBackTexture;
	/** Keeps every UI texture alive while the widget exists. */
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> LoadedTextures;

	FOreDealStoreState Current;
	bool bStoreOpen = false;
	bool bHasState = false;
	int32 Tab = 0;               // 0 = sell, 1 = buy, 2 = unused
	EZone Zone = EZone::Content;
	int32 TabCursor = 0;
	int32 Cursor = 0;
	bool bConfirmOpen = false;
	int32 ConfirmCursor = 0;     // 0 = buy, 1 = cancel
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
	void SelectTab(int32 NewTab);
	void MoveFocus(int32 DX, int32 DY);
	void Activate();
	void RefreshView();
	UTexture2D* CardTextureFor(int32 Resource) const;
};
