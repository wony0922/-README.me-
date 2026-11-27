#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "OreDealAuctionComponent.generated.h"

class AOreDealAuctionBroker;
class AOreDealAuctionDisplay;
class APlayerController;
class APlayerState;
class UOreDealAuctionHudWidget;
class UTextRenderComponent;

/** What the local player is currently being asked to do in the auction. */
enum class EOreDealAuctionLocalMode : uint8
{
	None,        // No auction running.
	Waiting,     // Auction running but nothing to choose right now (taken cards can be placed).
	PickReveal,  // Turn player: take 0-2 of the revealed cards.
	RequestLotPick,  // Other players choose 0-2 central cards for a live request.
	RequestOfferPick,// Other players choose hand cards to offer.
	PartnerPick,     // Turn player reviews active trade requests.
	RequestReview,   // Turn player accepts or rejects a selected request.
	ConfirmTrade,    // Requesting player makes the final decision.
	HandPlant        // Any player plants the first card in their hand.
};

/**
 * Lives on every AOreDealTablePlayerController. Carries the auction's client->server RPCs and, on the
 * owning client, drives input, the HUD, the floating card display and the partner-pick camera turn.
 */
UCLASS(ClassGroup=(OreDeal))
class TEAMPROJECT_API UOreDealAuctionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOreDealAuctionComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Returns true when the auction consumed the key (the controller must then stop processing it). */
	bool HandleInputKey(const FKey& Key, EInputEvent Event);
	bool HandlePlayerClick(APlayerState* TargetPlayer);
	bool IsTradeModalOpen() const { return LocalMode == EOreDealAuctionLocalMode::RequestReview || LocalMode == EOreDealAuctionLocalMode::ConfirmTrade; }
	/** During partner picking the camera turns to the highlighted player. */
	bool GetCameraOverride(FRotator& OutRotation) const;
	bool IsPickerOpen() const { return LocalMode == EOreDealAuctionLocalMode::RequestLotPick || LocalMode == EOreDealAuctionLocalMode::RequestOfferPick; }
	bool IsRequestHandPickerOpen() const { return LocalMode == EOreDealAuctionLocalMode::RequestOfferPick; }
	bool IsAuctionRunning() const;

	UFUNCTION(Server, Reliable) void ServerPickReveal(const TArray<int32>& RevealIndices);
	UFUNCTION(Server, Reliable) void ServerSubmitTradeRequest(const TArray<int32>& RevealIndices, const TArray<int32>& HandIndices);
	UFUNCTION(Server, Reliable) void ServerCancelTradeRequest();
	UFUNCTION(Server, Reliable) void ServerResolveTradeRequest(bool bAccept);
	UFUNCTION(Server, Reliable) void ServerFocusPartner(APlayerState* Target);
	UFUNCTION(Server, Reliable) void ServerChoosePartner(APlayerState* Target);
	UFUNCTION(Server, Reliable) void ServerPlacePending(int32 WagonIndex);
	UFUNCTION(Server, Reliable) void ServerReturnPending();

private:
	UPROPERTY(Transient) TObjectPtr<UOreDealAuctionHudWidget> Hud;
	TWeakObjectPtr<AOreDealAuctionBroker> Broker;
	TWeakObjectPtr<AOreDealAuctionDisplay> Display;
	TMap<TWeakObjectPtr<APawn>, TWeakObjectPtr<UTextRenderComponent>> RequestMarkers;

	EOreDealAuctionLocalMode LocalMode = EOreDealAuctionLocalMode::None;
	uint8 LastStage = 0;
	int32 LastSeenBrokerRevision = INDEX_NONE;
	int32 Cursor = 0;
	TArray<int32> Selection;
	int32 PartnerCursor = 0;
	int32 WagonFocus = 0;
	bool bAwaitingServer = false;
	int32 AwaitRevision = INDEX_NONE;
	double AwaitStartTime = 0.0;
	float HandCardMaterialRefreshElapsed = 0.0f;
	bool bRequestEditing = true;
	bool bRequestOfferPhase = false;
	bool bPlantHandMode = false;
	bool bActiveRequestBrowser = false;
	TArray<int32> RequestRevealSelection;
	TArray<int32> RequestOfferSelection;

	APlayerController* GetOwnerController() const;
	APlayerState* GetOwnerPlayerState() const;
	AOreDealAuctionBroker* GetBroker();
	bool IsLocallyOwned() const;
	bool IsActivePlayer() const;
	EOreDealAuctionLocalMode ComputeMode() const;
	int32 GetModeItemCount() const;
	/** 덱 위 카드 indices ordered as this player sees them: bottom row first, then left to right on screen. */
	void GetDeckVisualOrder(TArray<int32>& OutOrder) const;
	void GetLocalHand(TArray<int32>& OutCards) const;
	void GetMyPending(TArray<int32>& OutCards) const;
	int32 GetMyWagonCount() const;
	void GetOfferers(TArray<APlayerState*>& OutPlayers) const;
	void ToggleSelection(int32 Index);
	void SubmitCurrent();
	void BeginAwait();
	void PrepareRequestEditor();
	void Flash(const FString& Message);
	void UpdateLocalState();
	void RefreshHud();
	void RefreshDisplay();
};
