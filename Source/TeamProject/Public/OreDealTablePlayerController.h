#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "OreDealStoreCatalog.h"
#include "OreDealTablePlayerController.generated.h"

class APawn;
class AActor;
class APlayerState;
class UInputMappingContext;
class UPrimitiveComponent;
class UMaterialInterface;
class UOreDealCeremonyWidget;
class ASpotLight;
class UNiagaraSystem;
class UOreDealInteractionWidget;
class UOreDealHandViewWidget;
class UOreDealPreGameComponent;
class AOreDealMarketBroker;
class UOreDealAuctionComponent;
class UOreDealStoreComponent;
class UOreDealStoreWidget;
class UOreDealInventoryWidget;
class UUserWidget;
class APlayerState;
struct FInputKeyEventArgs;

UENUM(BlueprintType)
enum class EOreDealSlotEffect : uint8
{
	CoinBurst,
	MineralShatter
};

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
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Opens/closes the store window (F). Kept under the old name for the HUD merchant button. */
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Store")
	void ToggleSalesMenu();
	/** Called by the store component: switches cursor/input mode and closes the other local panels. */
	void OnStoreOpenChanged(bool bOpen);
	/** What the store's sell tab shows for each of my wagons (same rules as the server-side sale). */
	void GetStoreWagons(TArray<FOreDealStoreWagonView>& OutWagons) const;
	const TArray<FOreDealStoreItem>& GetStoreItems() const { return StoreItems; }
	const FOreDealStoreItem* FindStoreItem(FName ItemId) const;
	UFUNCTION(BlueprintPure, Category="Ore Deal|Store")
	void GetStoreCatalog(TArray<FOreDealStoreItem>& OutItems) const;
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Ore Deal|Store")
	bool IsStoreGameStarted() const;
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Ore Deal|Store")
	int32 GetStorePlayerGold() const;
	int32 GetStorePlayerWood() const;
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Ore Deal|Store")
	bool HasEnoughStoreGold(int32 Cost) const;
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Ore Deal|Store")
	bool CanGrantStoreItem(FName GrantProperty, int32 Amount) const;
	UFUNCTION(BlueprintNativeEvent, Category="Ore Deal|Store")
	bool ValidateStorePurchase(FName ItemId, int32 Cost, int32 Amount, FName GrantProperty, FString& OutMessage);
	virtual bool ValidateStorePurchase_Implementation(FName ItemId, int32 Cost, int32 Amount, FName GrantProperty, FString& OutMessage);
	UOreDealStoreComponent* GetStoreComponent() const { return StoreComponent; }
	TSubclassOf<UOreDealStoreWidget> GetStoreWidgetClass() const;
	void NormalizeStoreCatalog();
	void HandleChatEnter();
	void RequestSellWagon(int32 WagonIndex);
	void ToggleHandRack(AActor* RackActor = nullptr);
	/** Q: toggles the local bottom-left hand viewer. */
	void ToggleHandView();
	void GetLocalHandCards(TArray<int32>& OutCards) const;
	void GetSaleOptionText(int32 WagonIndex, FString& OutText, bool& bOutCanSell) const;
	void RequestMarketSelectTarget(APlayerState* Target);
	void RequestMarketSetOffer(const TArray<int32>& HandIndices);
	void RequestMarketBeginDeal(APlayerState* Target, const TArray<int32>& MarketIndices);
	void RequestMarketConsent(bool bAccept);
	UFUNCTION(BlueprintPure, Category="Ore Deal|Store")
	AActor* GetStorePlayerBoard() const { return GetPlayerBoard(); }
	UFUNCTION(BlueprintImplementableEvent, Category="Ore Deal|Store")
	void BP_GrantStoreWagonSlotUpgrade();
	UFUNCTION(Client, Reliable) void ClientShowSaleToast(const FString& Resource, int32 Count);
	UFUNCTION(Client, Reliable) void ClientReceiveChat(const FString& Sender, const FString& Message);
	UFUNCTION(Client, Reliable) void ClientPlayProtectedCardFeedback(int32 WagonIndex);
	UFUNCTION(Client, Reliable) void ClientPlayWagonEffect(AActor* Board, int32 WagonIndex, EOreDealSlotEffect EffectType);
	static void BroadcastWagonEffect(UWorld* World, AActor* Board, int32 WagonIndex, EOreDealSlotEffect EffectType);
	static int32 CalculateSaleReward(int32 Resource, int32 Count, const UObject* WorldContext = nullptr);
	UFUNCTION(Client, Reliable) void ClientSetWagonFocus(int32 WagonIndex);

	/** 우승자 결정 및 게임 종료 세레머니 시작 RPC */
	UFUNCTION(Client, Reliable) void ClientStartGameOverCeremony(APlayerState* WinnerPlayerState);
	UFUNCTION(BlueprintPure, Category="Ore Deal|Game Over") bool ShouldShowResultPrompt() const { return bResultPromptActive; }
	void SetAuctionModalInput(bool bOpen);
	/** Records a hand-card placement on the server when the owning remote client plants. */
	UFUNCTION(Server, Reliable) void ServerRecordMandatoryHandPlacement(AActor* CardDeck, APlayerState* PlacingPlayer);
	/** Plants the active hand card with auto-matching, 1-card waste protection, and sale/shatter settlement. */
	UFUNCTION(Server, Reliable) void ServerPlantHandCard(int32 DesiredWagonIndex);
	/** Advances only the current turn phase; the last phase also hands off to the next player. */
	UFUNCTION(Server, Reliable) void ServerCompleteTurnStage();
	/** Starts the existing animated supply draw, only while the local player is in phase four. */
	UFUNCTION(Server, Reliable) void ServerRequestSupplyDraw();
	UFUNCTION(Server, Reliable) void ServerPickupBottle(AActor* Bottle);
	UFUNCTION(Server, Reliable) void ServerThrowBottle();
	UFUNCTION(Server, Reliable) void ServerDrinkBottle();
	UFUNCTION(Client, Reliable) void ClientSetHoldingBottle(bool bHolding);
	UOreDealAuctionComponent* GetAuctionComponent() const { return AuctionComponent; }
	/** Wagon the A/D focus is on (0 = 마차 1; drives BP_PC_Table.FocusIndex from 2). */
	int32 GetWagonFocus() const;
	void SetWagonFocus(int32 WagonIndex);
	/** Opens the store directly on its inventory page from the always-visible card pile. */
	void OpenStoreInventory();
	void GetStoreInventoryCounts(TArray<int32>& OutCounts) const;
	/** Server-side grant for the one-time store wagon-slot upgrade. */
	bool GrantStoreWagonSlotUpgrade();

	/** 상점 아이템 효과 실행 함수들 (서버 권한) */
	bool ApplyAIGlasses(APlayerState* PurchasingPlayer);
	bool ApplyWoodAxe(APlayerState* PurchasingPlayer);
	bool ApplySpawnBottles(APlayerState* PurchasingPlayer);
	bool ApplyChaosCard(APlayerState* PurchasingPlayer);

	/** 더 많은 나무 효과(나무 획득량 2배) 활성화 여부 */
	UPROPERTY(Replicated)
	bool bHasWoodAxe = false;
	UPROPERTY(Transient) TObjectPtr<AActor> HeldBottle;
	bool bHoldingBottleLocal = false;

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
	UPROPERTY(Transient)
	TObjectPtr<UOreDealHandViewWidget> HandViewWidget;
	UPROPERTY(Transient)
	TObjectPtr<UOreDealPreGameComponent> PreGameComponent;
	/** Auction-style trade phase: RPCs, input, HUD and floating cards. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ore Deal")
	TObjectPtr<UOreDealAuctionComponent> AuctionComponent;
	/** Store window (F): sell wagons, buy items. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ore Deal")
	TObjectPtr<UOreDealStoreComponent> StoreComponent;
	UPROPERTY(Transient)
	TObjectPtr<UOreDealInventoryWidget> InventoryWidget;
	/** Ordered shop products; editable in BP_PC_Table's defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ore Deal|Store")
	TArray<FOreDealStoreItem> StoreItems;
	/** Optional WBP subclass for the store UI; falls back to the native implementation when unset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ore Deal|Store")
	TSubclassOf<UOreDealStoreWidget> StoreWidgetClass;

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
	/** True while the camera eases back inside the yaw limit after an auction camera turn. */
	bool bCameraReturning = false;
	bool bSupplyDrawRequested = false;
	int32 LastObservedHandCount = INDEX_NONE;
	int32 SupplyDrawInitialHandCount = 0;
	int32 SupplyDrawRequestedCount = 0;
	UPROPERTY() TArray<int32> SupplyDrawPendingCards;
	float HandRackRefreshElapsed = 0.0f;
	float TurnUIRefreshElapsed = 0.0f;
	mutable TWeakObjectPtr<AActor> CachedPlayerBoard;
	TWeakObjectPtr<AActor> CachedCardDeck;
	float MarketRefreshElapsed = 0.0f;
	float ProtectedFeedbackElapsed = 0.0f;
	float StackLabelRefreshElapsed = 0.0f;
	int32 ProtectedFeedbackWagonIndex = INDEX_NONE;
	FVector ProtectedCardStartLocation = FVector::ZeroVector;
	TWeakObjectPtr<UPrimitiveComponent> ProtectedGlowComponent;
	TWeakObjectPtr<UPrimitiveComponent> ProtectedCardComponent;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> ProtectedGlowOriginalMaterials;

	// --- 게임 종료 세레머니 & 결과창 상태 ---
	bool bInGameOverCeremony = false;
	bool bWinnerCameraPhase = false;
	bool bRandomCameraPhase = false;
	bool bResultWidgetOpen = false;
	bool bCanPressEnterForMainMenu = false;
	bool bResultPromptActive = false;
	int32 CeremonyStage = 0;
	float CeremonyTimeElapsed = 0.0f;
	float WinnerPhaseDuration = 5.0f; // 우승자 카메라 연출 시간(초)
	float CameraSwitchInterval = 4.0f; // 카메라 전환 간격(초)
	float CurrentCameraTimer = 0.0f;
	int32 CurrentCameraIndex = 0; // 2, 3, 4
	FVector CameraOriginalLocation;
	TWeakObjectPtr<AActor> CurrentActiveCamera;
	TMap<int32, FVector> CameraInitialLocations;
	TWeakObjectPtr<APlayerState> WinnerState;
	TWeakObjectPtr<ASpotLight> CeremonySpotlight;
	TArray<TWeakObjectPtr<APawn>> CeremonyPlayers;
	int32 SpotlightIndex = 0;
	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> WinnerCoinSystem;
	FTimerHandle WinnerCoinTimerHandle;

	UPROPERTY()
	TObjectPtr<UUserWidget> ResultWidgetInstance;
	UPROPERTY(Transient)
	TObjectPtr<UOreDealCeremonyWidget> CeremonyWidget;

	void UpdateGameOverCeremony(float DeltaTime);
	void SwitchToNextRandomCamera();
	void ShowResultScreen();
	void BindResultButtons();
	void ReturnToMainMenu();
	void RestartCurrentMatch();
	void SpawnWinnerCoinBurst();
	void UpdateCeremonySpotlight(float DeltaTime);
	UFUNCTION() void HandleResultRematchClicked();
	UFUNCTION() void HandleResultMainMenuClicked();

	void CleanUpBoardWagonTexts();
	void EnsureSeatMerchantBells();
	void UpdateWagonStackLabels();
	void UpdateProtectedCardFeedback(float DeltaTime);

	AActor* GetPlayerBoard() const;
	int32 GetWagonSlotCount() const;
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
};
