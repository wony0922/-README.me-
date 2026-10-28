#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OreDealMarketBroker.generated.h"

class APlayerState;

UENUM()
enum class EOreDealMarketState : uint8
{
	Waiting,
	Reveal,
	Trading,
	AwaitingConsent
};

USTRUCT()
struct FOreDealPublicOffer
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<APlayerState> Player = nullptr;
	UPROPERTY() TArray<int32> Cards;
	UPROPERTY() int32 HandCount = 0;
};

USTRUCT()
struct FOreDealMarketPlayerView
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<APlayerState> Player = nullptr;
	UPROPERTY() FString Name;
	UPROPERTY() TArray<int32> PublicOffer;
	UPROPERTY() int32 HandCount = 0;
};

USTRUCT()
struct FOreDealMarketSnapshot
{
	GENERATED_BODY()
	UPROPERTY() bool bVisible = false;
	UPROPERTY() bool bAwaitingConsent = false;
	UPROPERTY() int32 State = 0;
	UPROPERTY() int32 Revision = 0;
	UPROPERTY() int32 SecondsRemaining = 0;
	UPROPERTY() bool bActiveAccepted = false;
	UPROPERTY() bool bTargetAccepted = false;
	UPROPERTY() TObjectPtr<APlayerState> ActivePlayer = nullptr;
	UPROPERTY() TObjectPtr<APlayerState> SelectedTarget = nullptr;
	UPROPERTY() TObjectPtr<APlayerState> PendingTarget = nullptr;
	UPROPERTY() TArray<int32> MarketCards;
	UPROPERTY() TArray<int32> PendingMarketCards;
	UPROPERTY() TArray<FOreDealMarketPlayerView> Players;
};

/** Server-owned coordinator for the live 2nd phase negotiation. */
UCLASS()
class TEAMPROJECT_API AOreDealMarketBroker : public AActor
{
	GENERATED_BODY()
public:
	AOreDealMarketBroker();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetPlayerOffer(APlayerState* Player, const TArray<int32>& HandIndices);
	void SelectTarget(APlayerState* Player, APlayerState* Target);
	void BeginDeal(APlayerState* Player, APlayerState* Target, const TArray<int32>& MarketIndices);
	void CastConsent(APlayerState* Player, bool bAccept);

	void MakeSnapshot(APlayerState* ForPlayer, FOreDealMarketSnapshot& OutSnapshot) const;

private:
	UPROPERTY(ReplicatedUsing=OnRep_MarketState) EOreDealMarketState MarketState = EOreDealMarketState::Waiting;
	UPROPERTY(ReplicatedUsing=OnRep_MarketState) TObjectPtr<APlayerState> ActivePlayer = nullptr;
	UPROPERTY(ReplicatedUsing=OnRep_MarketState) TObjectPtr<APlayerState> SelectedTarget = nullptr;
	UPROPERTY(ReplicatedUsing=OnRep_MarketState) TObjectPtr<APlayerState> PendingTarget = nullptr;
	UPROPERTY(ReplicatedUsing=OnRep_MarketState) TArray<int32> MarketCards;
	UPROPERTY(ReplicatedUsing=OnRep_MarketState) TArray<int32> PendingMarketCards;
	UPROPERTY(ReplicatedUsing=OnRep_MarketState) TArray<FOreDealPublicOffer> PublicOffers;
	UPROPERTY(ReplicatedUsing=OnRep_MarketState) int32 SecondsRemaining = 0;
	UPROPERTY(ReplicatedUsing=OnRep_MarketState) bool bActiveAccepted = false;
	UPROPERTY(ReplicatedUsing=OnRep_MarketState) bool bTargetAccepted = false;
	UPROPERTY(ReplicatedUsing=OnRep_MarketState) int32 Revision = 0;

	TMap<TWeakObjectPtr<APlayerState>, TArray<int32>> PrivateOfferIndices;
	TArray<int32> PendingMarketIndices;
	TArray<int32> PendingActiveIndices;
	FTimerHandle IntroTimer;
	FTimerHandle CountdownTimer;

	UFUNCTION() void OnRep_MarketState();
	void StartMarket(AActor* CardDeck, APlayerState* TurnPlayer);
	void FinishIntro();
	void TickCountdown();
	void CancelDeal();
	void CommitDeal();
	void FinishMarket(bool bAdvanceToPlanting);
	void RefreshPublicOffers();
	AActor* FindCardDeck() const;
	APawn* FindPawn(APlayerState* Player) const;
	AActor* FindBoard(APawn* Pawn) const;
	bool ReadIntArray(UObject* Object, FName Name, TArray<int32>& OutValues) const;
	bool WriteIntArray(UObject* Object, FName Name, const TArray<int32>& Values) const;
	bool CanPlaceCards(APlayerState* Player, const TArray<int32>& Cards) const;
	bool PlaceCards(APlayerState* Player, const TArray<int32>& Cards);
	bool SimulatePlace(AActor* Board, TArray<int32>& Types, TArray<int32>& Counts, int32 Resource) const;
	int32 FindForcedHarvestSlot(const TArray<int32>& Types, const TArray<int32>& Counts) const;
	void HarvestSlot(APlayerState* Player, AActor* Board, TArray<int32>& Types, TArray<int32>& Counts, int32 Slot) const;
	void UpdateBoardVisual(AActor* Board) const;
	bool GetHand(APlayerState* Player, APawn*& OutPawn, TArray<int32>& OutCards) const;
	bool RemoveHandIndices(APlayerState* Player, const TArray<int32>& Indices);
	void UpdateDeckVisual(AActor* Deck) const;
	void AdvanceDeckToPlanting(AActor* Deck);
};
