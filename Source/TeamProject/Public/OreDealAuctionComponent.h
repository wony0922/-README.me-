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

/** What the local player is currently being asked to do in the auction. */
enum class EOreDealAuctionLocalMode : uint8
{
	None,        // No auction running.
	Waiting,     // Auction running but nothing to choose right now (taken cards can be placed).
	PickReveal,  // Turn player: take one or more of the revealed cards (C multi-select).
	HandPick,    // Choose hand cards to offer (turn player puts them on the deck, others bid).
	LotPick,     // Turn player: choose which deck cards are traded next.
	PartnerPick  // Turn player: choose who to trade with.
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
	/** During partner picking the camera turns to the highlighted player. */
	bool GetCameraOverride(FRotator& OutRotation) const;
	bool IsPickerOpen() const { return LocalMode == EOreDealAuctionLocalMode::HandPick; }
	bool IsAuctionRunning() const;

	UFUNCTION(Server, Reliable) void ServerPickReveal(const TArray<int32>& RevealIndices);
	UFUNCTION(Server, Reliable) void ServerSubmitOffer(const TArray<int32>& HandIndices);
	UFUNCTION(Server, Reliable) void ServerSelectLot(const TArray<int32>& PoolIndices);
	UFUNCTION(Server, Reliable) void ServerFocusPartner(APlayerState* Target);
	UFUNCTION(Server, Reliable) void ServerChoosePartner(APlayerState* Target);
	UFUNCTION(Server, Reliable) void ServerPlacePending(int32 WagonIndex);

private:
	UPROPERTY(Transient) TObjectPtr<UOreDealAuctionHudWidget> Hud;
	TWeakObjectPtr<AOreDealAuctionBroker> Broker;
	TWeakObjectPtr<AOreDealAuctionDisplay> Display;

	EOreDealAuctionLocalMode LocalMode = EOreDealAuctionLocalMode::None;
	uint8 LastStage = 0;
	int32 Cursor = 0;
	TArray<int32> Selection;
	int32 PartnerCursor = 0;
	int32 WagonFocus = 0;
	bool bAwaitingServer = false;
	int32 AwaitRevision = INDEX_NONE;
	double AwaitStartTime = 0.0;

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
	void Flash(const FString& Message);
	void UpdateLocalState();
	void RefreshHud();
	void RefreshDisplay();
};
