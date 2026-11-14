#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OreDealAuctionBroker.generated.h"

class APawn;
class APlayerState;

/**
 * Stages of the auction-style trade phase (turn phase index 1, followed by the
 * consume wait in phase index 2).
 */
UENUM()
enum class EOreDealAuctionStage : uint8
{
	Idle,
	PickReveal,     // Turn player takes one or more of the deck-top cards (덱 위 카드).
	ActiveOffer,    // Turn player pushes hand cards (손패) onto the deck-top array.
	SelectLot,      // Turn player highlights deck-top cards to trade next (거래 카드).
	Bidding,        // Everyone else offers hand cards (or confirms with nothing).
	ChoosePartner,  // Turn player picks one of the players who offered.
	Consume         // Trading done: everyone must place their taken cards.
};

/** Per-player auction state. Offers are public once confirmed (they are shown at the table). */
USTRUCT()
struct FOreDealAuctionSeat
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<APlayerState> Player = nullptr;
	/** Hand indices of the confirmed offer, ascending. Cards stay in the hand until a trade happens. */
	UPROPERTY() TArray<int32> OfferHandIndices;
	UPROPERTY() TArray<int32> OfferCards;
	UPROPERTY() bool bConfirmed = false;
	/** Cards taken during this trade phase, in the order they were taken. Must all be placed on wagons. */
	UPROPERTY() TArray<int32> PendingCards;
};

/**
 * Server-owned coordinator for the auction trade phase. Replaces the old market window flow.
 * All cards on top of the deck (덱 위 카드) live in ONE array: BP_CardDeck.RevealedCards. The two
 * revealed cards come first; hand cards the turn player offers are pushed onto its end.
 */
UCLASS()
class TEAMPROJECT_API AOreDealAuctionBroker : public AActor
{
	GENERATED_BODY()

public:
	AOreDealAuctionBroker();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Server actions (validated; called from UOreDealAuctionComponent server RPCs).
	void PickReveal(APlayerState* Player, const TArray<int32>& RevealIndices);
	void SubmitOffer(APlayerState* Player, const TArray<int32>& HandIndices);
	void SelectLot(APlayerState* Player, const TArray<int32>& PoolIndices);
	void FocusPartner(APlayerState* Player, APlayerState* Target);
	void ChoosePartner(APlayerState* Player, APlayerState* Target);
	void PlacePending(APlayerState* Player, int32 WagonIndex);

	// Read access for clients.
	EOreDealAuctionStage GetStage() const { return Stage; }
	APlayerState* GetActivePlayer() const { return ActivePlayer; }
	APlayerState* GetFocusedPartner() const { return FocusedPartner; }
	/** Indices into the deck-top array that are being traded right now (highlighted). */
	const TArray<int32>& GetLotIndices() const { return LotIndices; }
	const TArray<FOreDealAuctionSeat>& GetSeats() const { return Seats; }
	int32 GetRevision() const { return Revision; }
	const FOreDealAuctionSeat* FindSeat(const APlayerState* Player) const;
	/** Players (other than the turn player) who confirmed a non-empty offer, in seat order. */
	void GetOfferers(TArray<APlayerState*>& OutPlayers) const;
	/** Comma separated names of players who still hold cards they must place. */
	FString GetUnfinishedPlayerNames() const;
	/** Number of players other than the turn player who have confirmed their offer. */
	void GetBiddingProgress(int32& OutConfirmed, int32& OutTotal) const;

	static AOreDealAuctionBroker* Find(const UObject* WorldContext);
	static AActor* FindDeck(UWorld* World);
	/** Reads BP_CardDeck.RevealedCards (the deck-top array), skipping empty (-1) entries. */
	static void GetDeckCards(UWorld* World, TArray<int32>& OutCards);
	static AActor* FindBoardFor(UWorld* World, const APawn* Pawn);
	static bool ReadIntArray(const UObject* Object, FName Name, TArray<int32>& OutValues);
	static bool WriteIntArray(UObject* Object, FName Name, const TArray<int32>& Values);
	static FString GetDisplayName(const APlayerState* Player);

private:
	UPROPERTY(Replicated) EOreDealAuctionStage Stage = EOreDealAuctionStage::Idle;
	UPROPERTY(Replicated) TObjectPtr<APlayerState> ActivePlayer = nullptr;
	UPROPERTY(Replicated) TObjectPtr<APlayerState> FocusedPartner = nullptr;
	UPROPERTY(Replicated) TArray<int32> LotIndices;
	UPROPERTY(Replicated) TArray<FOreDealAuctionSeat> Seats;
	UPROPERTY(Replicated) int32 Revision = 0;

	FOreDealAuctionSeat* FindSeatMutable(const APlayerState* Player);
	bool IsParticipant(const FOreDealAuctionSeat& Seat) const;
	void Touch();
	void Start(AActor* Deck, APlayerState* TurnPlayer);
	void ResetToIdle();
	void ClearOffers();
	void ResolveBids();
	void ExecuteTrade(APlayerState* Target);
	void AfterLotResolved();
	void EnterConsume();
	void CollectLot(TArray<int32>& OutCards);
	bool AllOthersConfirmed() const;
	bool AllPendingEmpty() const;
	bool ReadHand(const APlayerState* Player, TArray<int32>& OutCards) const;
	bool RemoveHandIndices(APlayerState* Player, const TArray<int32>& Indices, TArray<int32>* OutRemoved);
	bool CallAddCard(AActor* Board, int32 Resource, int32 WagonIndex, APlayerState* Player) const;
	void UpdateDeckVisual(AActor* Deck) const;
	void WriteDeckCards(const TArray<int32>& Cards) const;
};
