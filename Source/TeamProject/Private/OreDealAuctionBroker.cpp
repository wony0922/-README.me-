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
		if (Seat.Player != ActivePlayer && IsValid(Seat.Player) && Seat.bConfirmed && !Seat.OfferCards.IsEmpty())
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
	if (Stage == EOreDealAuctionStage::Bidding && AllOthersConfirmed()) ResolveBids();
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
	if (!Seat || !IsValidUniqueIndexList(RevealIndices, DeckCards.Num())) return;
	// Taken cards become 가져온 카드 in deck order; the rest stay on the deck.
	TArray<int32> Sorted = RevealIndices;
	Sorted.Sort();
	for (int32 Index : Sorted) Seat->PendingCards.Add(DeckCards[Index]);
	for (int32 I = Sorted.Num() - 1; I >= 0; --I) DeckCards.RemoveAt(Sorted[I]);
	WriteDeckCards(DeckCards);
	// Nothing left on the deck: skip the trade and go straight to placing 가져온 카드.
	if (DeckCards.IsEmpty())
	{
		EnterConsume();
		return;
	}
	Stage = EOreDealAuctionStage::SelectLot;
	Touch();
}

void AOreDealAuctionBroker::SubmitOffer(APlayerState* Player, const TArray<int32>& HandIndices)
{
	if (!HasAuthority() || !IsValid(Player)) return;
	TArray<int32> Hand;
	if (!ReadHand(Player, Hand) || !IsValidUniqueIndexList(HandIndices, Hand.Num())) return;

	if (Stage == EOreDealAuctionStage::ActiveOffer && Player == ActivePlayer)
	{
		// The turn player's offered cards leave the hand now and are pushed onto the deck-top array.
		TArray<int32> Removed;
		if (!HandIndices.IsEmpty() && !RemoveHandIndices(Player, HandIndices, &Removed)) return;
		TArray<int32> DeckCards;
		GetDeckCards(GetWorld(), DeckCards);
		DeckCards.Append(Removed);
		WriteDeckCards(DeckCards);
		if (DeckCards.IsEmpty()) EnterConsume();
		else
		{
			Stage = EOreDealAuctionStage::SelectLot;
			Touch();
		}
		return;
	}

	if (Stage == EOreDealAuctionStage::Bidding && Player != ActivePlayer)
	{
		FOreDealAuctionSeat* Seat = FindSeatMutable(Player);
		if (!Seat || Seat->bConfirmed) return;
		TArray<int32> Sorted = HandIndices;
		Sorted.Sort();
		Seat->OfferHandIndices = Sorted;
		Seat->OfferCards.Reset();
		for (int32 Index : Sorted) Seat->OfferCards.Add(Hand[Index]);
		Seat->bConfirmed = true;
		Touch();
		if (AllOthersConfirmed()) ResolveBids();
	}
}

void AOreDealAuctionBroker::SelectLot(APlayerState* Player, const TArray<int32>& PoolIndices)
{
	if (!HasAuthority() || Stage != EOreDealAuctionStage::SelectLot || Player != ActivePlayer) return;
	TArray<int32> DeckCards;
	GetDeckCards(GetWorld(), DeckCards);
	if (PoolIndices.IsEmpty() || !IsValidUniqueIndexList(PoolIndices, DeckCards.Num())) return;
	ClearOffers();
	LotIndices = PoolIndices;
	LotIndices.Sort();
	Stage = EOreDealAuctionStage::Bidding;
	Touch();
	if (AllOthersConfirmed()) ResolveBids();
}

void AOreDealAuctionBroker::ResolveBids()
{
	TArray<APlayerState*> Offerers;
	GetOfferers(Offerers);
	if (Offerers.IsEmpty())
	{
		// Nobody wants the highlighted cards: they go to the turn player's taken cards.
		TArray<int32> Lot;
		CollectLot(Lot);
		if (FOreDealAuctionSeat* ActiveSeat = FindSeatMutable(ActivePlayer)) ActiveSeat->PendingCards.Append(Lot);
		AfterLotResolved();
		return;
	}
	int32 Others = 0;
	for (const FOreDealAuctionSeat& Seat : Seats)
		if (Seat.Player != ActivePlayer && IsValid(Seat.Player)) ++Others;
	if (Others == 1)
	{
		// Two-player game: the only opponent offered, so the trade happens without a pick.
		ExecuteTrade(Offerers[0]);
		return;
	}
	Stage = EOreDealAuctionStage::ChoosePartner;
	FocusedPartner = Offerers[0];
	Touch();
}

void AOreDealAuctionBroker::FocusPartner(APlayerState* Player, APlayerState* Target)
{
	if (!HasAuthority() || Stage != EOreDealAuctionStage::ChoosePartner || Player != ActivePlayer) return;
	TArray<APlayerState*> Offerers;
	GetOfferers(Offerers);
	if (!Offerers.Contains(Target) || FocusedPartner == Target) return;
	FocusedPartner = Target;
	Touch();
}

void AOreDealAuctionBroker::ChoosePartner(APlayerState* Player, APlayerState* Target)
{
	if (!HasAuthority() || Stage != EOreDealAuctionStage::ChoosePartner || Player != ActivePlayer) return;
	TArray<APlayerState*> Offerers;
	GetOfferers(Offerers);
	if (!Offerers.Contains(Target)) return;
	ExecuteTrade(Target);
}

void AOreDealAuctionBroker::ExecuteTrade(APlayerState* Target)
{
	FOreDealAuctionSeat* TargetSeat = FindSeatMutable(Target);
	if (!TargetSeat) return;

	// The offer was validated when confirmed; re-check in case the hand changed since.
	TArray<int32> Hand;
	bool bValid = ReadHand(Target, Hand) && TargetSeat->OfferHandIndices.Num() == TargetSeat->OfferCards.Num();
	for (int32 I = 0; bValid && I < TargetSeat->OfferHandIndices.Num(); ++I)
		bValid = Hand.IsValidIndex(TargetSeat->OfferHandIndices[I]) && Hand[TargetSeat->OfferHandIndices[I]] == TargetSeat->OfferCards[I];
	TArray<int32> Received;
	if (!bValid || !RemoveHandIndices(Target, TargetSeat->OfferHandIndices, &Received))
	{
		// Withdraw the stale offer and decide again with whoever is left.
		TargetSeat->OfferHandIndices.Reset();
		TargetSeat->OfferCards.Reset();
		ResolveBids();
		return;
	}

	TArray<int32> Lot;
	CollectLot(Lot);
	// Look the seats up again: neither call above resizes Seats, but keep this robust.
	if (FOreDealAuctionSeat* PartnerSeat = FindSeatMutable(Target)) PartnerSeat->PendingCards.Append(Lot);
	if (FOreDealAuctionSeat* ActiveSeat = FindSeatMutable(ActivePlayer)) ActiveSeat->PendingCards.Append(Received);
	AfterLotResolved();
}

void AOreDealAuctionBroker::CollectLot(TArray<int32>& OutCards)
{
	OutCards.Reset();
	TArray<int32> DeckCards;
	GetDeckCards(GetWorld(), DeckCards);
	TArray<int32> Sorted = LotIndices;
	Sorted.Sort();
	for (int32 Index : Sorted)
		if (DeckCards.IsValidIndex(Index)) OutCards.Add(DeckCards[Index]);
	for (int32 I = Sorted.Num() - 1; I >= 0; --I)
		if (DeckCards.IsValidIndex(Sorted[I])) DeckCards.RemoveAt(Sorted[I]);
	LotIndices.Reset();
	WriteDeckCards(DeckCards);
}

void AOreDealAuctionBroker::AfterLotResolved()
{
	ClearOffers();
	LotIndices.Reset();
	TArray<int32> DeckCards;
	GetDeckCards(GetWorld(), DeckCards);
	if (DeckCards.IsEmpty())
	{
		EnterConsume();
		return;
	}
	Stage = EOreDealAuctionStage::SelectLot;
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
	// 가져온 카드 are placed only after every 덱 위 카드 is gone (3/4), never during the trade itself.
	if (!HasAuthority() || Stage != EOreDealAuctionStage::Consume || !IsValid(Player)) return;
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
		else if (APlayerController* BasePC = It->Get())
		{
			if (UFunction* ShowResultFn = BasePC->FindFunction(TEXT("ClientShowResult")))
			{
				BasePC->ProcessEvent(ShowResultFn, nullptr);
			}
		}
	}
}
