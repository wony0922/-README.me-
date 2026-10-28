#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"
#include "OreDealTradeManager.generated.h"

class UTextBlock;

UENUM(BlueprintType)
enum class EOreDealTurnPhase : uint8 { Waiting, MandatoryBurial, MarketTrade, SlotPlacement, SupplyDraw, GameOver };

USTRUCT(BlueprintType)
struct FTradeResourceAmount
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Resource = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Amount = 1;
};

USTRUCT(BlueprintType)
struct FOreDealHarvestPayout
{
	GENERATED_BODY()
	// Number of cards required in a wagon to receive this payout.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Price", meta=(ClampMin="1", DisplayName="최소 카드 수")) int32 MinimumCards = 1;
	// Coins awarded when the wagon reaches MinimumCards.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Price", meta=(ClampMin="0", DisplayName="코인 보상")) int32 Gold = 0;
};

USTRUCT(BlueprintType)
struct FOreDealResourceRule
{
	GENERATED_BODY()
	// The resource's identifier. Use the same name when adding cards to queues.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource", meta=(DisplayName="자원 이름")) FName Resource = NAME_None;
	// Total copies of this resource added to the deck.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource", meta=(ClampMin="0", DisplayName="덱 카드 수")) int32 DeckCopies = 0;
	// Minimum wagon size that can be harvested. This can be lower than the first payout threshold.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource", meta=(ClampMin="1", DisplayName="최소 수확 카드 수")) int32 MinimumSellCount = 1;
	// Each entry is a pair: minimum cards required and coins awarded. The highest eligible coin value is paid.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource", meta=(DisplayName="가격 정보", TitleProperty="MinimumCards")) TArray<FOreDealHarvestPayout> HarvestPayouts;
};

USTRUCT(BlueprintType)
struct FTradeWagon
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName Resource = NAME_None;
	UPROPERTY(BlueprintReadOnly) int32 Amount = 0;
};

USTRUCT(BlueprintType)
struct FTradePlayerInventory
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName PlayerId = NAME_None;
	UPROPERTY(BlueprintReadOnly) TArray<FName> Queue;
	UPROPERTY(BlueprintReadOnly) int32 Supplies = 0;
	UPROPERTY(BlueprintReadOnly) int32 Gold = 0;
	UPROPERTY(BlueprintReadOnly) TArray<FTradeWagon> Wagons;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOreDealPhaseChanged, EOreDealTurnPhase, NewPhase, FName, ActivePlayer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOreDealPlayerChanged, FName, PlayerId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOreDealHarvested, FName, PlayerId, int32, WagonIndex, int32, GoldEarned);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOreDealDeckChanged, int32, DeckCount, int32, DiscardCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOreDealGameEnded, const TArray<FName>&, WinningPlayers);

UCLASS(BlueprintType, Blueprintable)
class TEAMPROJECT_API AOreDealTradeManager : public AActor
{
	GENERATED_BODY()
public:
	AOreDealTradeManager();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category="Ore Deal|Events") FOreDealPhaseChanged OnPhaseChanged;
	UPROPERTY(BlueprintAssignable, Category="Ore Deal|Events") FOreDealPlayerChanged OnPlayerStateChanged;
	UPROPERTY(BlueprintAssignable, Category="Ore Deal|Events") FOreDealHarvested OnWagonHarvested;
	UPROPERTY(BlueprintAssignable, Category="Ore Deal|Events") FOreDealDeckChanged OnDeckChanged;
	UPROPERTY(BlueprintAssignable, Category="Ore Deal|Events") FOreDealGameEnded OnGameEnded;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ore Deal|Rules", meta=(DisplayName="자원 규칙", TitleProperty="Resource")) TArray<FOreDealResourceRule> ResourceRules;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ore Deal|Rules", meta=(ClampMin="0")) int32 StartingSupplies = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ore Deal|Rules", meta=(ClampMin="1")) int32 StartingWagons = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ore Deal|Rules", meta=(ClampMin="1")) int32 StartingHandSize = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ore Deal|Rules", meta=(ClampMin="0")) int32 CardsPerTurn = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ore Deal|Rules", meta=(ClampMin="0")) int32 PublicMarketCount = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ore Deal|Rules", meta=(ClampMin="0")) int32 HarvestSupplyCost = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ore Deal|Rules", meta=(ClampMin="0")) int32 ThirdWagonGoldCost = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ore Deal|Rules", meta=(ClampMin="0", DisplayName="4단계 후 다음 턴까지 대기(초)")) float SupplyDrawPhaseDuration = 2.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ore Deal|Rules", meta=(ClampMin="1")) int32 MinimumPlantCount = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ore Deal|Rules", meta=(ClampMin="1")) int32 MaximumPlantCount = 2;

	UFUNCTION(BlueprintCallable, Category="Ore Deal|Game") bool RegisterPlayer(FName PlayerId, const TArray<FName>& Queue, int32 Supplies, int32 WagonCount = 2);
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Game") bool StartGame(int32 RandomSeed = 0);
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Rules") bool ResetResourceRulesToDefaults();
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Game") bool BeginPlayerTurn(FName PlayerId);
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Turn") bool PlantQueueFront(FName PlayerId, int32 WagonIndex = -1);
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Turn") bool FinishPlanting(FName PlayerId);
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Trade") void BeginTradePhase(FName Player, const TArray<FName>& Market);
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Trade") bool ResolveDeal(FName FromPlayer, FName ToPlayer, const TArray<FTradeResourceAmount>& OfferedResources, const TArray<FTradeResourceAmount>& RequestedResources, int32 OfferedSupplies, int32 RequestedSupplies, bool bAccepted);
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Trade") bool EndTradePhase();
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Turn") bool FinishSlotPlacement(FName PlayerId);
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Wagon") bool HarvestWagon(FName PlayerId, int32 WagonIndex);
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Wagon") bool BuyThirdWagon(FName PlayerId);
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Game") bool AdvanceTurn();
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Trade") bool AddQueueResource(FName PlayerId, FName Resource);

	UFUNCTION(BlueprintPure, Category="Ore Deal|State") TArray<FName> GetQueue(FName PlayerId) const;
	UFUNCTION(BlueprintPure, Category="Ore Deal|State") TArray<FName> GetPublicMarket() const { return PublicMarket; }
	UFUNCTION(BlueprintPure, Category="Ore Deal|State") FName GetPublicMarketPlayerId() const { return FName(TEXT("__PUBLIC_MARKET__")); }
	UFUNCTION(BlueprintPure, Category="Ore Deal|State") TArray<FTradeWagon> GetWagons(FName PlayerId) const;
	UFUNCTION(BlueprintPure, Category="Ore Deal|State") int32 GetSupplies(FName PlayerId) const;
	UFUNCTION(BlueprintPure, Category="Ore Deal|State") int32 GetGold(FName PlayerId) const;
	UFUNCTION(BlueprintPure, Category="Ore Deal|State") int32 GetDeckCount() const { return Deck.Num(); }
	UFUNCTION(BlueprintPure, Category="Ore Deal|State") int32 GetDiscardCount() const { return DiscardPile.Num(); }
	UFUNCTION(BlueprintPure, Category="Ore Deal|State") EOreDealTurnPhase GetTurnPhase() const { return TurnPhase; }
	UFUNCTION(BlueprintPure, Category="Ore Deal|State") FText GetTurnPhaseLabel() const;
	UFUNCTION(BlueprintPure, Category="Ore Deal|State") FName GetActivePlayer() const { return ActivePlayer; }
	UFUNCTION(BlueprintPure, Category="Ore Deal|State") bool IsTradePhaseActive() const { return bTradePhaseActive; }
	UFUNCTION(BlueprintPure, Category="Ore Deal|State") bool IsGameOver() const { return bGameOver; }

protected:
	UPROPERTY(ReplicatedUsing=OnRep_Players, BlueprintReadOnly, Category="Ore Deal|State") TArray<FTradePlayerInventory> Players;
	UPROPERTY(ReplicatedUsing=OnRep_DeckState, BlueprintReadOnly, Category="Ore Deal|State") TArray<FName> PublicMarket;
	UPROPERTY(ReplicatedUsing=OnRep_DeckState, BlueprintReadOnly, Category="Ore Deal|State") TArray<FName> Deck;
	UPROPERTY(ReplicatedUsing=OnRep_DeckState, BlueprintReadOnly, Category="Ore Deal|State") TArray<FName> DiscardPile;
	UPROPERTY(ReplicatedUsing=OnRep_TurnPhase, BlueprintReadOnly, Category="Ore Deal|State") FName ActivePlayer;
	UPROPERTY(ReplicatedUsing=OnRep_TurnPhase, BlueprintReadOnly, Category="Ore Deal|State") EOreDealTurnPhase TurnPhase = EOreDealTurnPhase::Waiting;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Ore Deal|State") bool bTradePhaseActive = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Ore Deal|State") bool bGameOver = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Ore Deal|State") int32 PlantedThisTurn = 0;
private:
	UFUNCTION() void OnRep_Players();
	UFUNCTION() void OnRep_DeckState();
	UFUNCTION() void OnRep_TurnPhase();
	int32 FindPlayer(FName PlayerId) const;
	int32 FindRule(FName Resource) const;
	bool HasValidResourceRules() const;
	bool HarvestWagonInternal(int32 PlayerIndex, int32 WagonIndex, bool bAllowBelowMinimum);
	bool CanLoad(int32 PlayerIndex, const TArray<FTradeResourceAmount>& Items) const;
	void LoadItems(int32 PlayerIndex, const TArray<FTradeResourceAmount>& Items);
	bool CanRemoveItems(FName PlayerId, const TArray<FTradeResourceAmount>& Items) const;
	void RemoveItems(FName PlayerId, const TArray<FTradeResourceAmount>& Items);
	bool CanRemoveMarket(const TArray<FTradeResourceAmount>& Items) const;
	void RemoveMarket(const TArray<FTradeResourceAmount>& Items);
	void CompleteSupplyDrawPhase();
	void AdvanceTurnAfterDraw();
	void CheckGameOver();
	FTimerHandle TurnAdvanceTimer;
};

UCLASS(BlueprintType, Blueprintable)
class TEAMPROJECT_API UOreDealPhaseHUDWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TxtPhase;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
};
