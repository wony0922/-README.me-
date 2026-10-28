#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "OreDealTablePlayerController.generated.h"

class APawn;
class AActor;
class APlayerState;
class UInputMappingContext;
class UOreDealInteractionWidget;
class AOreDealMarketBroker;
class APlayerState;
struct FInputKeyEventArgs;

/** Keeps table-game camera rotation modest and lets the center-screen pointer choose cards. */
UCLASS(BlueprintType, Blueprintable)
class TEAMPROJECT_API AOreDealTablePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AOreDealTablePlayerController();
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void UpdateRotation(float DeltaTime) override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	void ToggleSalesMenu();
	void HandleChatEnter();
	void RequestSellWagon(int32 WagonIndex);
	void ToggleHandRack(AActor* RackActor = nullptr);
	void GetLocalHandCards(TArray<int32>& OutCards) const;
	void GetSaleOptionText(int32 WagonIndex, FString& OutText, bool& bOutCanSell) const;
	void RequestMarketSelectTarget(APlayerState* Target);
	void RequestMarketSetOffer(const TArray<int32>& HandIndices);
	void RequestMarketBeginDeal(APlayerState* Target, const TArray<int32>& MarketIndices);
	void RequestMarketConsent(bool bAccept);
	UFUNCTION(Client, Reliable) void ClientShowSaleToast(const FString& Resource, int32 Count);
	/** Records a hand-card placement on the server when the owning remote client plants. */
	UFUNCTION(Server, Reliable) void ServerRecordMandatoryHandPlacement(AActor* CardDeck, APlayerState* PlacingPlayer);
	/** Advances only the current turn phase; the last phase also hands off to the next player. */
	UFUNCTION(Server, Reliable) void ServerCompleteTurnStage();
	/** Starts the existing animated supply draw, only while the local player is in phase four. */
	UFUNCTION(Server, Reliable) void ServerRequestSupplyDraw();

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Ore Deal|Camera", meta=(ClampMin="0", ClampMax="90"))
	float MaxRelativeYawDegrees = 35.0f;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> DefaultMappingContext;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MouseLookMappingContext;
	UPROPERTY(Transient)
	TObjectPtr<UOreDealInteractionWidget> InteractionWidget;
	UPROPERTY(Transient)
	TObjectPtr<AOreDealMarketBroker> MarketBroker;

private:
	TWeakObjectPtr<APawn> YawReferencePawn;
	float ReferenceYaw = 0.0f;
	bool bMouseSelectionActive = false;
	int32 PointerFocusIndex = INDEX_NONE;
	int32 KeyboardFocusIndex = 0;
	int32 KeyboardSelectedReveal = INDEX_NONE;
	bool bHasKeyboardFocusSnapshot = false;
	bool bPointerCentered = false;
	bool bChatEnterConsumed = false;
	bool bSupplyDrawRequested = false;
	int32 SupplyDrawInitialHandCount = 0;
	int32 SupplyDrawRequestedCount = 0;
	float HandRackRefreshElapsed = 0.0f;
	float TurnUIRefreshElapsed = 0.0f;
	TWeakObjectPtr<AActor> CachedCardDeck;
	float MarketRefreshElapsed = 0.0f;

	void UpdatePointerSelection();
	bool TraceCenterPointer(FHitResult& OutHit) const;
	int32 GetCardFocusIndex(const FHitResult& Hit) const;
	int32 GetBlueprintInt(const FName PropertyName, int32 FallbackValue) const;
	void SetBlueprintInt(const FName PropertyName, int32 Value);
	void RefreshBlueprintGlow();
	void ApplyPointerFocus(int32 FocusIndex);
	void ToggleChat();
	void ClearChatEnterGuard();
	void ApplyInteractionInputMode(bool bUIActive, bool bFocusChatInput);

	UFUNCTION(Server, Reliable) void ServerSellWagon(int32 WagonIndex);
	UFUNCTION(Server, Reliable) void ServerMarketSelectTarget(APlayerState* Target);
	UFUNCTION(Server, Reliable) void ServerMarketSetOffer(const TArray<int32>& HandIndices);
	UFUNCTION(Server, Reliable) void ServerMarketBeginDeal(APlayerState* Target, const TArray<int32>& MarketIndices);
	UFUNCTION(Server, Reliable) void ServerMarketConsent(bool bAccept);
	UFUNCTION(Server, Reliable) void ServerSendChat(const FString& Message);
	UFUNCTION(Client, Reliable) void ClientReceiveChat(const FString& Sender, const FString& Message);
};
