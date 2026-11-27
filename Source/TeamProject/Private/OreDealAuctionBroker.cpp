#include "OreDealAuctionBroker.h"

#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "OreDealTablePlayerController.h"
#include "OreDealTurnBridge.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* DeckClassPath = TEXT("/Game/MaterialTraders/Blueprints/BP_CardDeck.BP_CardDeck_C");
	const TCHAR* BoardClassPath = TEXT("/Game/MaterialTraders/Blueprints/BP_CardBoard.BP_CardBoard_C");

	void CallIfExists(UObject* Object, const TCHAR* FunctionName)
	{
		if (!Object) return;
		if (UFunction* Function = Object->FindFunction(FName(FunctionName)))
			if (Function->ParmsSize == 0) Object->ProcessEvent(Function, nullptr);
	}

	/**
	 * Per-world cache of the table actors. FindDeck / FindBoardFor are called many times every frame by
	 * the HUD, the floating card display and the player controller, so they must not scan the level each time.
	 */
	struct FTableActorCache
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AActor> Deck;
		TArray<TWeakObjectPtr<AActor>> Boards;
		double BoardsTime = -1000.0;
	};

	// The board list is rebuilt once a second in case boards stream in after the first lookup.
	constexpr double BoardCacheSeconds = 1.0;

	FTableActorCache& GetTableActorCache(UWorld* World)
	{
		// A handful of entries at most: one per PIE world (server + clients share the editor process).
		static TArray<FTableActorCache> Caches;
		for (FTableActorCache& Cache : Caches)
			if (Cache.World.Get() == World) return Cache;
		Caches.RemoveAll([](const FTableActorCache& Cache) { return !Cache.World.IsValid(); });
		FTableActorCache& Cache = Caches.AddDefaulted_GetRef();
		Cache.World = World;
		return Cache;
	}

	bool IsValidUniqueIndexList(const TArray<int32>& Indices, int32 Num)
	{
		TSet<int32> Seen;
		for (int32 Index : Indices)
		{
			if (Index < 0 || Index >= Num || Seen.Contains(Index)) return false;
			Seen.Add(Index);
		}
		return true;
	}
}

AOreDealAuctionBroker::AOreDealAuctionBroker()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(20.0f);
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
}

void AOreDealAuctionBroker::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AOreDealAuctionBroker, Stage);
	DOREPLIFETIME(AOreDealAuctionBroker, ActivePlayer);
	DOREPLIFETIME(AOreDealAuctionBroker, FocusedPartner);
	DOREPLIFETIME(AOreDealAuctionBroker, LotIndices);
	DOREPLIFETIME(AOreDealAuctionBroker, Seats);
	DOREPLIFETIME(AOreDealAuctionBroker, Revision);
	DOREPLIFETIME(AOreDealAuctionBroker, RemainingDeckCount);
	DOREPLIFETIME(AOreDealAuctionBroker, DeckDepletionCount);
}

// ---------------------------------------------------------------------------------------------
// Static helpers

AOreDealAuctionBroker* AOreDealAuctionBroker::Find(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? Cast<AOreDealAuctionBroker>(UGameplayStatics::GetActorOfClass(World, AOreDealAuctionBroker::StaticClass())) : nullptr;
}

AActor* AOreDealAuctionBroker::FindDeck(UWorld* World)
{
	if (!World) return nullptr;
	FTableActorCache& Cache = GetTableActorCache(World);
	if (!Cache.Deck.IsValid())
	{
		UClass* DeckClass = StaticLoadClass(AActor::StaticClass(), nullptr, DeckClassPath);
		Cache.Deck = DeckClass ? UGameplayStatics::GetActorOfClass(World, DeckClass) : nullptr;
	}
	return Cache.Deck.Get();
}

void AOreDealAuctionBroker::GetDeckCards(UWorld* World, TArray<int32>& OutCards)
{
	OutCards.Reset();
	ReadIntArray(FindDeck(World), TEXT("RevealedCards"), OutCards);
	OutCards.RemoveAll([](int32 Resource) { return Resource < 0; });
}

void AOreDealAuctionBroker::WriteDeckCards(const TArray<int32>& Cards) const
{
	AActor* Deck = FindDeck(GetWorld());
	if (!Deck) return;
	WriteIntArray(Deck, TEXT("RevealedCards"), Cards);
	UpdateDeckVisual(Deck);
}

AActor* AOreDealAuctionBroker::FindBoardFor(UWorld* World, const APawn* Pawn)
{
	if (!World || !Pawn) return nullptr;
	FTableActorCache& Cache = GetTableActorCache(World);
	const double Now = World->GetRealTimeSeconds();
	if (Cache.Boards.IsEmpty() || Now - Cache.BoardsTime > BoardCacheSeconds || Now < Cache.BoardsTime)
	{
		Cache.Boards.Reset();
		Cache.BoardsTime = Now;
		if (UClass* BoardClass = StaticLoadClass(AActor::StaticClass(), nullptr, BoardClassPath))
		{
			TArray<AActor*> Boards;
			UGameplayStatics::GetAllActorsOfClass(World, BoardClass, Boards);
			for (AActor* Board : Boards)
			{
				Cache.Boards.Add(Board);
				// Clean up any oversized floating 3D text components on the boards (e.g. WagonText0, WagonText1)
				TArray<UTextRenderComponent*> TextComps;
				Board->GetComponents<UTextRenderComponent>(TextComps);
				for (UTextRenderComponent* TextComp : TextComps)
				{
					TextComp->SetText(FText::GetEmpty());
					TextComp->SetVisibility(false, true);
					TextComp->SetHiddenInGame(true);
					TextComp->SetWorldScale3D(FVector::ZeroVector);
				}
			}
		}
	}
	AActor* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (const TWeakObjectPtr<AActor>& WeakBoard : Cache.Boards)
	{
		AActor* Board = WeakBoard.Get();
		if (!Board) continue;
		const double Distance = FVector::DistSquared(Pawn->GetActorLocation(), Board->GetActorLocation());
		if (Distance < BestDistance) { BestDistance = Distance; Best = Board; }
	}
	return Best;
}

bool AOreDealAuctionBroker::ReadIntArray(const UObject* Object, FName Name, TArray<int32>& OutValues)
{
	OutValues.Reset();
	const FArrayProperty* Property = Object ? FindFProperty<FArrayProperty>(Object->GetClass(), Name) : nullptr;
	const FIntProperty* Inner = Property ? CastField<FIntProperty>(Property->Inner) : nullptr;
	if (!Property || !Inner) return false;
	FScriptArrayHelper Helper(Property, Property->ContainerPtrToValuePtr<void>(Object));
	OutValues.Reserve(Helper.Num());
	for (int32 I = 0; I < Helper.Num(); ++I) OutValues.Add(Inner->GetPropertyValue(Helper.GetRawPtr(I)));
	return true;
}

bool AOreDealAuctionBroker::WriteIntArray(UObject* Object, FName Name, const TArray<int32>& Values)
{
	FArrayProperty* Property = Object ? FindFProperty<FArrayProperty>(Object->GetClass(), Name) : nullptr;
	FIntProperty* Inner = Property ? CastField<FIntProperty>(Property->Inner) : nullptr;
	if (!Property || !Inner) return false;
	FScriptArrayHelper Helper(Property, Property->ContainerPtrToValuePtr<void>(Object));
	Helper.EmptyAndAddValues(Values.Num());
	for (int32 I = 0; I < Values.Num(); ++I) Inner->SetPropertyValue(Helper.GetRawPtr(I), Values[I]);
	return true;
}

FString AOreDealAuctionBroker::GetDisplayName(const APlayerState* Player)
{
	if (!Player) return TEXT("?");
	const FString Name = Player->GetPlayerName();
	return Name.IsEmpty() ? FString::Printf(TEXT("Player %d"), Player->GetPlayerId()) : Name;
}

// ---------------------------------------------------------------------------------------------
// Queries

const FOreDealAuctionSeat* AOreDealAuctionBroker::FindSeat(const APlayerState* Player) const
{
	return Player ? Seats.FindByPredicate([Player](const FOreDealAuctionSeat& Seat) { return Seat.Player == Player; }) : nullptr;
}

FOreDealAuctionSeat* AOreDealAuctionBroker::FindSeatMutable(const APlayerState* Player)
{
	return Player ? Seats.FindByPredicate([Player](const FOreDealAuctionSeat& Seat) { return Seat.Player == Player; }) : nullptr;
}

bool AOreDealAuctionBroker::IsParticipant(const FOreDealAuctionSeat& Seat) const
{
	// Players without a pawn (still loading / disconnected) never block the auction.
	return IsValid(Seat.Player) && Seat.Player->GetPawn() != nullptr;
}

void AOreDealAuctionBroker::GetOfferers(TArray<APlayerState*>& OutPlayers) const
{
	OutPlayers.Reset();
	for (const FOreDealAuctionSeat& Seat : Seats)
		if (Seat.Player != ActivePlayer && IsValid(Seat.Player) && Seat.bConfirmed && !Seat.RequestedRevealIndices.IsEmpty())
			OutPlayers.Add(Seat.Player);
}

FString AOreDealAuctionBroker::GetUnfinishedPlayerNames() const
{
	TArray<FString> Names;
	for (const FOreDealAuctionSeat& Seat : Seats)
		if (IsValid(Seat.Player) && !Seat.PendingCards.IsEmpty()) Names.Add(GetDisplayName(Seat.Player));
	return FString::Join(Names, TEXT(", "));
}

void AOreDealAuctionBroker::GetBiddingProgress(int32& OutConfirmed, int32& OutTotal) const
{
	OutConfirmed = 0;
	OutTotal = 0;
	for (const FOreDealAuctionSeat& Seat : Seats)
	{
		if (Seat.Player == ActivePlayer || !IsValid(Seat.Player)) continue;
		++OutTotal;
		if (Seat.bConfirmed) ++OutConfirmed;
	}
}

bool AOreDealAuctionBroker::AllOthersConfirmed() const
{
	for (const FOreDealAuctionSeat& Seat : Seats)
		if (Seat.Player != ActivePlayer && IsParticipant(Seat) && !Seat.bConfirmed) return false;
	return true;
}

bool AOreDealAuctionBroker::AllPendingEmpty() const
{
	for (const FOreDealAuctionSeat& Seat : Seats)
		if (IsValid(Seat.Player) && !Seat.PendingCards.IsEmpty()) return false;
	return true;
}

bool AOreDealAuctionBroker::ReadHand(const APlayerState* Player, TArray<int32>& OutCards) const
{
	const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	return Pawn && ReadIntArray(Pawn, TEXT("HandCards"), OutCards);
}

// ---------------------------------------------------------------------------------------------
// Flow

void AOreDealAuctionBroker::Touch()
{
	++Revision;
	ForceNetUpdate();
}

void AOreDealAuctionBroker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) return;

	AActor* Deck = FindDeck(GetWorld());
	const FObjectPropertyBase* TurnProperty = Deck ? FindFProperty<FObjectPropertyBase>(Deck->GetClass(), TEXT("TurnPS")) : nullptr;
	const FIntProperty* PhaseProperty = Deck ? FindFProperty<FIntProperty>(Deck->GetClass(), TEXT("TurnPhaseIndex")) : nullptr;
	if (!Deck || !TurnProperty || !PhaseProperty) return;
	APlayerState* TurnPlayer = Cast<APlayerState>(TurnProperty->GetObjectPropertyValue_InContainer(Deck));
	const int32 Phase = PhaseProperty->GetPropertyValue_InContainer(Deck);

	if (Stage == EOreDealAuctionStage::Idle)
	{
		if (Phase == 1 && IsValid(TurnPlayer)) Start(Deck, TurnPlayer);
		return;
	}
	if (TurnPlayer != ActivePlayer || (Phase != 1 && Phase != 2))
	{
		// Turn moved on outside this flow (game end, rematch, debug keys): drop the auction.
		ResetToIdle();
		return;
	}
	if (Stage == EOreDealAuctionStage::Consume)
	{
		if (AllPendingEmpty())
		{
			// 3/4 (consume) -> 4/4 (turn player draws three).
			if (Phase == 2) UOreDealTurnBridge::CompleteCurrentTurnStage(Deck);
			ResetToIdle();
		}
		return;
	}
	// A bidder may have left mid-auction; re-check so the table never waits for nobody.
	if (Stage == EOreDealAuctionStage::PickReveal)
	{
		TArray<int32> CentralCards;
		GetDeckCards(GetWorld(), CentralCards);
		if (CentralCards.IsEmpty() && !HasReturnablePending()) EnterConsume();
	}
}

void AOreDealAuctionBroker::Start(AActor* Deck, APlayerState* TurnPlayer)
{
	// The two revealed cards stay in the deck-top array (BP_CardDeck.RevealedCards); drop empty slots.
	TArray<int32> DeckCards;
	GetDeckCards(GetWorld(), DeckCards);

	while (DeckCards.Num() < 2)
	{
		const int32 NewCard = DrawCard();
		if (NewCard == INDEX_NONE) break;
		DeckCards.Add(NewCard);
	}
	WriteDeckCards(DeckCards);

	ActivePlayer = TurnPlayer;
	FocusedPartner = nullptr;
	LotIndices.Reset();
	Seats.Reset();
	if (AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr)
		for (APlayerState* Player : GameState->PlayerArray)
		{
			FOreDealAuctionSeat& Seat = Seats.AddDefaulted_GetRef();
			Seat.Player = Player;
		}
	if (!FindSeat(ActivePlayer))
	{
		FOreDealAuctionSeat& Seat = Seats.AddDefaulted_GetRef();
		Seat.Player = ActivePlayer;
	}
	// Nothing on the deck: skip straight to placing 가져온 카드.
	if (DeckCards.IsEmpty())
	{
		EnterConsume();
		return;
	}
	Stage = EOreDealAuctionStage::PickReveal;
	Touch();
}

void AOreDealAuctionBroker::ResetToIdle()
{
	Stage = EOreDealAuctionStage::Idle;
	ActivePlayer = nullptr;
	FocusedPartner = nullptr;
	LotIndices.Reset();
	Seats.Reset();
	Touch();
}

void AOreDealAuctionBroker::ClearOffers()
{
	for (FOreDealAuctionSeat& Seat : Seats)
	{
		Seat.OfferHandIndices.Reset();
		Seat.OfferCards.Reset();
		Seat.RequestedRevealIndices.Reset();
		Seat.bConfirmed = false;
	}
	FocusedPartner = nullptr;
}

void AOreDealAuctionBroker::PickReveal(APlayerState* Player, const TArray<int32>& RevealIndices)
{
	if (!HasAuthority() || Stage != EOreDealAuctionStage::PickReveal || Player != ActivePlayer) return;
	TArray<int32> DeckCards;
	GetDeckCards(GetWorld(), DeckCards);
	FOreDealAuctionSeat* Seat = FindSeatMutable(ActivePlayer);
	if (!Seat || RevealIndices.Num() > 2 || !IsValidUniqueIndexList(RevealIndices, DeckCards.Num())) return;
	// Taking zero cards means the active player is choosing to trade/review requests instead.
	if (RevealIndices.IsEmpty()) { Touch(); return; }
	TArray<int32> Sorted = RevealIndices;
	Sorted.Sort();
	for (int32 Index : Sorted) { Seat->PendingCards.Add(DeckCards[Index]); Seat->PendingReturnable.Add(1); }
	for (int32 I = Sorted.Num() - 1; I >= 0; --I) DeckCards.RemoveAt(Sorted[I]);
	WriteDeckCards(DeckCards);
	// Rebase live requests after cards are removed from the central row. A request that
	// loses all of its requested cards is withdrawn and can be edited again.
	for (FOreDealAuctionSeat& Requester : Seats)
	{
		if (Requester.Player == ActivePlayer || !Requester.bConfirmed) continue;
		TArray<int32> Rebased;
		for (int32 RequestedIndex : Requester.RequestedRevealIndices)
		{
			if (Sorted.Contains(RequestedIndex)) continue;
			int32 Shift = 0;
			for (int32 RemovedIndex : Sorted) if (RemovedIndex < RequestedIndex) ++Shift;
			Rebased.Add(RequestedIndex - Shift);
		}
		Requester.RequestedRevealIndices = MoveTemp(Rebased);
		if (Requester.RequestedRevealIndices.IsEmpty())
		{
			Requester.OfferHandIndices.Reset();
			Requester.OfferCards.Reset();
			Requester.bConfirmed = false;
		}
	}
	FocusedPartner = nullptr;
	if (DeckCards.IsEmpty() && !HasReturnablePending())
	{
		EnterConsume();
		return;
	}
	Touch();
}

void AOreDealAuctionBroker::SubmitRequest(APlayerState* Player, const TArray<int32>& RevealIndices, const TArray<int32>& HandIndices)
{
	if (!HasAuthority() || (Stage != EOreDealAuctionStage::PickReveal && Stage != EOreDealAuctionStage::RequestReview && Stage != EOreDealAuctionStage::ConfirmTrade) || !IsValid(Player) || Player == ActivePlayer || (Stage == EOreDealAuctionStage::ConfirmTrade && Player == FocusedPartner)) return;
	FOreDealAuctionSeat* Seat = FindSeatMutable(Player);
	TArray<int32> CentralCards;
	TArray<int32> Hand;
	GetDeckCards(GetWorld(), CentralCards);
	if (!Seat || !ReadHand(Player, Hand) || RevealIndices.Num() > 2 || !IsValidUniqueIndexList(RevealIndices, CentralCards.Num()) || !IsValidUniqueIndexList(HandIndices, Hand.Num())) return;
	if (RevealIndices.IsEmpty()) { CancelRequest(Player); return; }
	Seat->RequestedRevealIndices = RevealIndices;
	Seat->RequestedRevealIndices.Sort();
	Seat->OfferHandIndices = HandIndices;
	Seat->OfferHandIndices.Sort();
	Seat->OfferCards.Reset();
	for (int32 Index : Seat->OfferHandIndices) Seat->OfferCards.Add(Hand[Index]);
	Seat->bConfirmed = true;
	Touch();
}

void AOreDealAuctionBroker::CancelRequest(APlayerState* Player)
{
	if (!HasAuthority() || (Stage != EOreDealAuctionStage::PickReveal && Stage != EOreDealAuctionStage::RequestReview && Stage != EOreDealAuctionStage::ConfirmTrade) || !IsValid(Player) || Player == ActivePlayer || (Stage == EOreDealAuctionStage::ConfirmTrade && Player == FocusedPartner)) return;
	if (FOreDealAuctionSeat* Seat = FindSeatMutable(Player))
	{
		Seat->RequestedRevealIndices.Reset();
		Seat->OfferHandIndices.Reset();
		Seat->OfferCards.Reset();
		Seat->bConfirmed = false;
		Touch();
	}
}

void AOreDealAuctionBroker::FocusPartner(APlayerState* Player, APlayerState* Target)
{
	if (!HasAuthority() || Stage != EOreDealAuctionStage::PickReveal || Player != ActivePlayer) return;
	TArray<APlayerState*> Requests;
	GetOfferers(Requests);
	if (!Requests.Contains(Target) || FocusedPartner == Target) return;
	FocusedPartner = Target;
	Touch();
}

void AOreDealAuctionBroker::ChoosePartner(APlayerState* Player, APlayerState* Target)
{
	if (!HasAuthority() || Stage != EOreDealAuctionStage::PickReveal || Player != ActivePlayer) return;
	TArray<APlayerState*> Requests;
	GetOfferers(Requests);
	if (!Requests.Contains(Target)) return;
	FocusedPartner = Target;
	Stage = EOreDealAuctionStage::RequestReview;
	Touch();
}

void AOreDealAuctionBroker::ResolveRequest(APlayerState* Player, bool bAccept)
{
	if (!HasAuthority() || !IsValid(Player) || !IsValid(FocusedPartner)) return;
	if (Stage == EOreDealAuctionStage::RequestReview && Player == ActivePlayer)
	{
		if (bAccept) Stage = EOreDealAuctionStage::ConfirmTrade;
		else
		{
			if (FOreDealAuctionSeat* Requester = FindSeatMutable(FocusedPartner))
			{
				Requester->RequestedRevealIndices.Reset();
				Requester->OfferHandIndices.Reset();
				Requester->OfferCards.Reset();
				Requester->bConfirmed = false;
			}
			FocusedPartner = nullptr;
			Stage = EOreDealAuctionStage::PickReveal;
		}
		Touch();
		return;
	}
	if (Stage != EOreDealAuctionStage::ConfirmTrade || Player != FocusedPartner) return;
	FOreDealAuctionSeat* Requester = FindSeatMutable(FocusedPartner);
	if (!Requester) return;
	if (!bAccept)
	{
		// The request remains active and editable, exactly as it was before selection.
		FocusedPartner = nullptr;
		Stage = EOreDealAuctionStage::PickReveal;
		Touch();
		return;
	}

	TArray<int32> CentralCards;
	TArray<int32> Hand;
	GetDeckCards(GetWorld(), CentralCards);
	if (!ReadHand(FocusedPartner, Hand) || !IsValidUniqueIndexList(Requester->RequestedRevealIndices, CentralCards.Num()) || !IsValidUniqueIndexList(Requester->OfferHandIndices, Hand.Num()))
	{
		FocusedPartner = nullptr;
		Stage = EOreDealAuctionStage::PickReveal;
		Touch();
		return;
	}
	TArray<int32> Received;
	if (!RemoveHandIndices(FocusedPartner, Requester->OfferHandIndices, &Received)) return;
	TArray<int32> Taken;
	TArray<int32> SortedRequests = Requester->RequestedRevealIndices;
	SortedRequests.Sort();
	for (int32 Index : SortedRequests) Taken.Add(CentralCards[Index]);
	for (int32 Index = SortedRequests.Num() - 1; Index >= 0; --Index) CentralCards.RemoveAt(SortedRequests[Index]);
	WriteDeckCards(CentralCards);
	for (FOreDealAuctionSeat& Other : Seats)
	{
		if (&Other == Requester || Other.Player == ActivePlayer || !Other.bConfirmed) continue;
		TArray<int32> Rebased;
		for (int32 RequestedIndex : Other.RequestedRevealIndices)
		{
			if (SortedRequests.Contains(RequestedIndex)) continue;
			int32 Shift = 0;
			for (int32 RemovedIndex : SortedRequests) if (RemovedIndex < RequestedIndex) ++Shift;
			Rebased.Add(RequestedIndex - Shift);
		}
		Other.RequestedRevealIndices = MoveTemp(Rebased);
		if (Other.RequestedRevealIndices.IsEmpty())
		{
			Other.OfferHandIndices.Reset();
			Other.OfferCards.Reset();
			Other.bConfirmed = false;
		}
	}
	Requester->PendingCards.Append(Taken);
	for (int32 I = 0; I < Taken.Num(); ++I) Requester->PendingReturnable.Add(0);
	Requester->RequestedRevealIndices.Reset();
	Requester->OfferHandIndices.Reset();
	Requester->OfferCards.Reset();
	Requester->bConfirmed = false;
	if (FOreDealAuctionSeat* ActiveSeat = FindSeatMutable(ActivePlayer))
	{
		ActiveSeat->PendingCards.Append(Received);
		for (int32 I = 0; I < Received.Num(); ++I) ActiveSeat->PendingReturnable.Add(0);
	}
	FocusedPartner = nullptr;
	Stage = EOreDealAuctionStage::PickReveal;
	if (CentralCards.IsEmpty() && !HasReturnablePending()) EnterConsume();
	else Touch();
}

void AOreDealAuctionBroker::PlantHandCard(APlayerState* Player, int32 WagonIndex)
{
	// Any participant may plant a hand card while the trade stage is open.
	if (!HasAuthority() || !IsValid(Player) ||
		(Stage != EOreDealAuctionStage::PickReveal && Stage != EOreDealAuctionStage::RequestReview && Stage != EOreDealAuctionStage::ConfirmTrade)) return;
	APawn* Pawn = Player->GetPawn();
	AActor* Board = FindBoardFor(GetWorld(), Pawn);
	TArray<int32> Hand;
	if (!Board || !ReadHand(Player, Hand) || Hand.IsEmpty()) return;
	int32 PlacedIndex = INDEX_NONE;
	if (!UOreDealTurnBridge::ExecutePlantCard(Board, Player, Hand[0], WagonIndex, PlacedIndex)) return;
	Hand.RemoveAt(0);
	WriteIntArray(Pawn, TEXT("HandCards"), Hand);
	CallIfExists(Pawn, TEXT("OnRep_HandCards"));
	Pawn->ForceNetUpdate();
	if (FIntProperty* HandCount = FindFProperty<FIntProperty>(Pawn->GetClass(), TEXT("HandCount")))
	{
		HandCount->SetPropertyValue_InContainer(Pawn, Hand.Num());
		CallIfExists(Pawn, TEXT("OnRep_HandCount"));
	}
	if (FOreDealAuctionSeat* Seat = FindSeatMutable(Player); Seat && Seat->bConfirmed)
	{
		TArray<int32> Adjusted;
		for (int32 I = 0; I < Seat->OfferHandIndices.Num(); ++I)
		{
			if (Seat->OfferHandIndices[I] == 0) continue;
			Adjusted.Add(Seat->OfferHandIndices[I] - 1);
		}
		Seat->OfferHandIndices = MoveTemp(Adjusted);
		Seat->OfferCards.Reset();
		for (int32 Index : Seat->OfferHandIndices) if (Hand.IsValidIndex(Index)) Seat->OfferCards.Add(Hand[Index]);
		if (Seat->OfferCards.Num() != Seat->OfferHandIndices.Num())
		{
			Seat->OfferCards.Reset();
			Seat->OfferHandIndices.Reset();
			Seat->RequestedRevealIndices.Reset();
			Seat->bConfirmed = false;
		}
	}
	Touch();
}

bool AOreDealAuctionBroker::HasReturnablePending() const
{
	for (const FOreDealAuctionSeat& Seat : Seats)
		for (uint8 bReturnable : Seat.PendingReturnable)
			if (bReturnable != 0) return true;
	return false;
}

void AOreDealAuctionBroker::ReturnPending(APlayerState* Player)
{
	if (!HasAuthority() || Stage != EOreDealAuctionStage::PickReveal || Player != ActivePlayer) return;
	FOreDealAuctionSeat* Seat = FindSeatMutable(Player);
	if (!Seat) return;
	const int32 ReturnIndex = Seat->PendingReturnable.IndexOfByKey(static_cast<uint8>(1));
	if (ReturnIndex == INDEX_NONE || !Seat->PendingCards.IsValidIndex(ReturnIndex)) return;
	TArray<int32> CentralCards;
	GetDeckCards(GetWorld(), CentralCards);
	CentralCards.Add(Seat->PendingCards[ReturnIndex]);
	Seat->PendingCards.RemoveAt(ReturnIndex);
	Seat->PendingReturnable.RemoveAt(ReturnIndex);
	WriteDeckCards(CentralCards);
	Touch();
}

void AOreDealAuctionBroker::EnterConsume()
{
	ClearOffers();
	Stage = EOreDealAuctionStage::Consume;
	// 2/4 (trade) -> 3/4 (place taken cards). Tick moves on to 4/4 once every player is done.
	if (AActor* Deck = FindDeck(GetWorld()))
	{
		const FIntProperty* PhaseProperty = FindFProperty<FIntProperty>(Deck->GetClass(), TEXT("TurnPhaseIndex"));
		if (PhaseProperty && PhaseProperty->GetPropertyValue_InContainer(Deck) == 1) UOreDealTurnBridge::CompleteCurrentTurnStage(Deck);
	}
	Touch();
}

void AOreDealAuctionBroker::PlacePending(APlayerState* Player, int32 WagonIndex)
{
	// Players may plant acquired cards while requests are still live. Consume also waits for
	// every pending card before the turn advances.
	if (!HasAuthority() || Stage == EOreDealAuctionStage::Idle || !IsValid(Player)) return;
	FOreDealAuctionSeat* Seat = FindSeatMutable(Player);
	if (!Seat || Seat->PendingCards.IsEmpty()) return;
	AActor* Board = FindBoardFor(GetWorld(), Player->GetPawn());
	if (!Board) return;
	const int32 Resource = Seat->PendingCards[0];
	int32 PlacedWagonIndex = INDEX_NONE;
	if (!UOreDealTurnBridge::ExecutePlantCard(Board, Player, Resource, WagonIndex, PlacedWagonIndex))
	{
		Touch();
		return;
	}
	Seat->PendingCards.RemoveAt(0);
	if (!Seat->PendingReturnable.IsEmpty()) Seat->PendingReturnable.RemoveAt(0);
	if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(Player->GetPlayerController()))
	{
		if (PlacedWagonIndex != INDEX_NONE)
		{
			PC->ClientSetWagonFocus(PlacedWagonIndex);
		}
	}
	Touch();
}

// ---------------------------------------------------------------------------------------------
// Blueprint bridges

bool AOreDealAuctionBroker::RemoveHandIndices(APlayerState* Player, const TArray<int32>& Indices, TArray<int32>* OutRemoved)
{
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	TArray<int32> Hand;
	if (!Pawn || !ReadIntArray(Pawn, TEXT("HandCards"), Hand) || !IsValidUniqueIndexList(Indices, Hand.Num())) return false;
	TArray<int32> Ascending = Indices;
	Ascending.Sort();
	if (OutRemoved)
	{
		OutRemoved->Reset();
		for (int32 Index : Ascending) OutRemoved->Add(Hand[Index]);
	}
	// Remove from the back so the remaining cards keep their relative order.
	for (int32 I = Ascending.Num() - 1; I >= 0; --I) Hand.RemoveAt(Ascending[I]);
	if (!WriteIntArray(Pawn, TEXT("HandCards"), Hand)) return false;
	if (FIntProperty* CountProperty = FindFProperty<FIntProperty>(Pawn->GetClass(), TEXT("HandCount")))
		CountProperty->SetPropertyValue_InContainer(Pawn, Hand.Num());
	Pawn->ForceNetUpdate();
	CallIfExists(Pawn, TEXT("OnRep_HandCards"));
	CallIfExists(Pawn, TEXT("OnRep_HandCount"));
	return true;
}

bool AOreDealAuctionBroker::CallAddCard(AActor* Board, int32 Resource, int32 WagonIndex, APlayerState* Player) const
{
	int32 PlacedWagonIndex = INDEX_NONE;
	return UOreDealTurnBridge::ExecutePlantCard(Board, Player, Resource, WagonIndex, PlacedWagonIndex);
}

void AOreDealAuctionBroker::UpdateDeckVisual(AActor* Deck) const
{
	if (!Deck) return;
	Deck->ForceNetUpdate();
	CallIfExists(Deck, TEXT("OnRep_RevealedCards"));
	CallIfExists(Deck, TEXT("UpdateVisual"));
}

void AOreDealAuctionBroker::InitializeDeckIfNeeded()
{
	if (!HasAuthority() || !DrawDeck.IsEmpty() || !DiscardPile.IsEmpty() || DeckDepletionCount > 0) return;

	DrawDeck.Reset();

	// Data Asset 이 지정돼 있으면 그 값 사용, 없으면 기본 하드코딩 값으로 폴백
	if (CardInfoAsset && CardInfoAsset->CardInfos.Num() > 0)
	{
		for (const FOreDealCardInfo& Info : CardInfoAsset->CardInfos)
		{
			for (int32 C = 0; C < Info.DeckCopies; ++C)
			{
				DrawDeck.Add(Info.ResourceId);
			}
		}
	}
	else
	{
		// 폴백: Stone(0):12 Coal(1):10 Copper(2):8 Iron(3):8 Silver(4):6 Gold(5):6 Diamond(6):5 Adamantium(7):4
		static const int32 InitialCounts[8] = { 12, 10, 8, 8, 6, 6, 5, 4 };
		for (int32 Resource = 0; Resource < 8; ++Resource)
		{
			for (int32 C = 0; C < InitialCounts[Resource]; ++C)
			{
				DrawDeck.Add(Resource);
			}
		}
	}

	// Fisher-Yates shuffle
	for (int32 I = DrawDeck.Num() - 1; I > 0; --I)
	{
		const int32 J = FMath::RandRange(0, I);
		DrawDeck.Swap(I, J);
	}

	RemainingDeckCount = DrawDeck.Num();
	Touch();
}

int32 AOreDealAuctionBroker::DrawCard()
{
	if (!HasAuthority()) return INDEX_NONE;
	InitializeDeckIfNeeded();

	if (DrawDeck.IsEmpty())
	{
		if (DiscardPile.IsEmpty())
		{
			TriggerGameEnd();
			return INDEX_NONE;
		}

		// Refill DrawDeck from DiscardPile
		DrawDeck = MoveTemp(DiscardPile);
		DiscardPile.Reset();

		// Shuffle
		for (int32 I = DrawDeck.Num() - 1; I > 0; --I)
		{
			const int32 J = FMath::RandRange(0, I);
			DrawDeck.Swap(I, J);
		}

		++DeckDepletionCount;
		RemainingDeckCount = DrawDeck.Num();
		Touch();

		UWorld* World = GetWorld();
		if (World)
		{
			const FString Msg = FString::Printf(TEXT("덱 소진 (%d / %d회)! 판매·버려진 카드로 덱을 다시 채웠습니다."), DeckDepletionCount, MaxDeckDepletions);
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(It->Get()))
				{
					PC->ClientReceiveChat(TEXT("시스템"), Msg);
				}
			}
		}

		if (DeckDepletionCount >= MaxDeckDepletions)
		{
			TriggerGameEnd();
		}
	}

	if (DrawDeck.IsEmpty())
	{
		TriggerGameEnd();
		return INDEX_NONE;
	}

	const int32 Card = DrawDeck.Pop(EAllowShrinking::No);
	RemainingDeckCount = DrawDeck.Num();
	Touch();
	return Card;
}

void AOreDealAuctionBroker::AddToDiscardPile(int32 Resource, int32 Count)
{
	if (!HasAuthority() || Resource < 0 || Count <= 0) return;
	for (int32 I = 0; I < Count; ++I)
	{
		DiscardPile.Add(Resource);
	}
}

void AOreDealAuctionBroker::AddCardsToDiscardPile(const TArray<int32>& Cards)
{
	if (!HasAuthority()) return;
	for (int32 Resource : Cards)
	{
		if (Resource >= 0) DiscardPile.Add(Resource);
	}
}

bool AOreDealAuctionBroker::ShuffleAllCardsAcrossPlayers()
{
	if (!HasAuthority() || !GetWorld() || !GetWorld()->GetGameState()) return false;
	AActor* DeckActor = FindDeck(GetWorld());
	if (!DeckActor) return false;
	TArray<APlayerState*> Players = GetWorld()->GetGameState()->PlayerArray;
	Players.RemoveAll([](APlayerState* P) { return !P || !P->GetPawn(); });
	if (Players.IsEmpty()) return false;

	TArray<TArray<int32>> Hands;
	Hands.SetNum(Players.Num());
	TArray<int32> Pool;
	for (int32 I = 0; I < Players.Num(); ++I)
	{
		if (!ReadHand(Players[I], Hands[I])) return false;
		Pool.Append(Hands[I]);
	}
	TArray<int32> Revealed;
	ReadIntArray(DeckActor, TEXT("RevealedCards"), Revealed);
	Pool.Append(Revealed);
	Pool.Append(DrawDeck);
	Pool.Append(DiscardPile);
	if (Pool.IsEmpty()) return false;
	for (int32 I = Pool.Num() - 1; I > 0; --I) Pool.Swap(I, FMath::RandRange(0, I));

	for (TArray<int32>& Hand : Hands) Hand.Reset();
	for (int32 I = 0; I < Pool.Num(); ++I) Hands[I % Hands.Num()].Add(Pool[I]);
	for (int32 I = 0; I < Players.Num(); ++I)
	{
		if (!WriteIntArray(Players[I]->GetPawn(), TEXT("HandCards"), Hands[I])) return false;
		CallIfExists(Players[I]->GetPawn(), TEXT("OnRep_HandCards"));
		Players[I]->GetPawn()->ForceNetUpdate();
	}
	WriteIntArray(DeckActor, TEXT("RevealedCards"), {});
	CallIfExists(DeckActor, TEXT("OnRep_RevealedCards"));
	CallIfExists(DeckActor, TEXT("UpdateVisual"));
	DrawDeck.Reset();
	DiscardPile.Reset();
	RemainingDeckCount = 0;
	Touch();
	return true;
}

void AOreDealAuctionBroker::TriggerGameEnd()
{
	if (bGameEnded) return;
	bGameEnded = true;

	UWorld* World = GetWorld();
	if (!World) return;

	UE_LOG(LogTemp, Warning, TEXT("AOreDealAuctionBroker: Total depletions reached %d! Triggering Game End."), DeckDepletionCount);

	const FString EndMsg = TEXT("덱이 총 3회 소진되어 게임이 종료되었습니다! 최종 결과를 계산합니다.");
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(It->Get()))
		{
			PC->ClientReceiveChat(TEXT("시스템"), EndMsg);
		}
	}

	// 1. Notify GameMode to EndGame (BP_GM_Table.EndGame)
	if (AGameModeBase* GM = UGameplayStatics::GetGameMode(World))
	{
		if (UFunction* EndGameFn = GM->FindFunction(TEXT("EndGame")))
		{
			GM->ProcessEvent(EndGameFn, nullptr);
		}
	}

	// 2. 우승자(골드가 가장 많은 플레이어) 결정
	APlayerState* Winner = nullptr;
	int32 MaxGold = -1;

	if (AGameStateBase* GS = World->GetGameState())
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (!PS) continue;
			int32 Gold = 0;
			if (FIntProperty* GoldProp = FindFProperty<FIntProperty>(PS->GetClass(), TEXT("Gold")))
			{
				Gold = GoldProp->GetPropertyValue_InContainer(PS);
			}
			else if (FIntProperty* GoldLower = FindFProperty<FIntProperty>(PS->GetClass(), TEXT("gold")))
			{
				Gold = GoldLower->GetPropertyValue_InContainer(PS);
			}

			if (Gold > MaxGold)
			{
				MaxGold = Gold;
				Winner = PS;
			}
		}
	}

	// 3. 모든 플레이어에게 게임 종료 세레머니(Winner_Camera 연출 및 결과 시퀀스) 시작 브로드캐스트
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (AOreDealTablePlayerController* TablePC = Cast<AOreDealTablePlayerController>(It->Get()))
		{
			TablePC->ClientStartGameOverCeremony(Winner);
		}
	}
}
