#include "OreDealTradeManager.h"
#include "OreDealTablePlayerController.h"
#include "Components/TextBlock.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const FName MarketId(TEXT("__PUBLIC_MARKET__"));
	TArray<FOreDealResourceRule> MakeDefaultResourceRules()
	{
		TArray<FOreDealResourceRule> Rules;
		auto AddRule = [&Rules](FName Resource, int32 DeckCopies, int32 MinimumSellCount, std::initializer_list<FOreDealHarvestPayout> Payouts)
		{
			FOreDealResourceRule& Rule = Rules.AddDefaulted_GetRef();
			Rule.Resource = Resource;
			Rule.DeckCopies = DeckCopies;
			Rule.MinimumSellCount = MinimumSellCount;
			for (const FOreDealHarvestPayout& Payout : Payouts)
				Rule.HarvestPayouts.Add(Payout);
		};
		AddRule(TEXT("Wood"), 20, 4, {{4, 1}, {6, 2}, {8, 3}, {10, 4}});
		AddRule(TEXT("Coal"), 18, 3, {{3, 1}, {5, 2}, {7, 3}, {8, 4}});
		AddRule(TEXT("Copper"), 16, 3, {{3, 1}, {4, 2}, {6, 3}, {7, 4}});
		AddRule(TEXT("Iron"), 14, 2, {{2, 1}, {4, 2}, {5, 3}, {6, 4}});
		AddRule(TEXT("Silver"), 12, 2, {{2, 1}, {4, 2}, {6, 3}, {7, 4}});
		AddRule(TEXT("Gold"), 10, 2, {{2, 1}, {4, 2}, {5, 3}, {6, 4}});
		AddRule(TEXT("Diamond"), 8, 1, {{2, 1}, {3, 2}, {4, 3}, {5, 4}});
		AddRule(TEXT("Adamantium"), 6, 1, {{2, 2}, {3, 3}});
		return Rules;
	}

	bool IsValidItems(const TArray<FTradeResourceAmount>& Items)
	{
		for (const FTradeResourceAmount& Item : Items)
			if (Item.Resource.IsNone() || Item.Amount <= 0) return false;
		return true;
	}
}

AOreDealTradeManager::AOreDealTradeManager()
{
	bReplicates = true;
	SetReplicateMovement(false);
	ResourceRules = MakeDefaultResourceRules();
}

void AOreDealTradeManager::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && !HasValidResourceRules())
	{
		ResourceRules = MakeDefaultResourceRules();
		UE_LOG(LogTemp, Warning, TEXT("Ore Deal: BP_TradeManager had invalid resource rules; restored the built-in rules."));
	}
}

bool AOreDealTradeManager::HasValidResourceRules() const
{
	if (ResourceRules.IsEmpty()) return false;
	TSet<FName> DefinedResources;
	int32 TotalCards = 0;
	for (const FOreDealResourceRule& Rule : ResourceRules)
	{
		if (Rule.Resource.IsNone() || Rule.DeckCopies < 0 || Rule.MinimumSellCount < 1 || DefinedResources.Contains(Rule.Resource)) return false;
		TSet<int32> PayoutMinimums;
		for (const FOreDealHarvestPayout& Payout : Rule.HarvestPayouts)
		{
			if (Payout.MinimumCards < 1 || Payout.Gold < 0 || PayoutMinimums.Contains(Payout.MinimumCards)) return false;
			PayoutMinimums.Add(Payout.MinimumCards);
		}
		DefinedResources.Add(Rule.Resource);
		TotalCards += Rule.DeckCopies;
	}
	return TotalCards > 0;
}

bool AOreDealTradeManager::ResetResourceRulesToDefaults()
{
	if (!HasAuthority() || TurnPhase != EOreDealTurnPhase::Waiting) return false;
	ResourceRules = MakeDefaultResourceRules();
	return true;
}

void AOreDealTradeManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AOreDealTradeManager, Players);
	DOREPLIFETIME(AOreDealTradeManager, PublicMarket);
	DOREPLIFETIME(AOreDealTradeManager, Deck);
	DOREPLIFETIME(AOreDealTradeManager, DiscardPile);
	DOREPLIFETIME(AOreDealTradeManager, ActivePlayer);
	DOREPLIFETIME(AOreDealTradeManager, TurnPhase);
	DOREPLIFETIME(AOreDealTradeManager, bTradePhaseActive);
	DOREPLIFETIME(AOreDealTradeManager, bGameOver);
	DOREPLIFETIME(AOreDealTradeManager, PlantedThisTurn);
}

int32 AOreDealTradeManager::FindPlayer(FName Id) const
{
	return Players.IndexOfByPredicate([Id](const FTradePlayerInventory& P) { return P.PlayerId == Id; });
}

int32 AOreDealTradeManager::FindRule(FName Resource) const
{
	return ResourceRules.IndexOfByPredicate([Resource](const FOreDealResourceRule& Rule) { return Rule.Resource == Resource; });
}

FText AOreDealTradeManager::GetTurnPhaseLabel() const
{
	switch (TurnPhase)
	{
	case EOreDealTurnPhase::MandatoryBurial: return FText::FromString(TEXT("[1단계: 필수 매립]"));
	case EOreDealTurnPhase::MarketTrade: return FText::FromString(TEXT("[2단계: 장터 거래]"));
	case EOreDealTurnPhase::SlotPlacement: return FText::FromString(TEXT("[3단계: 슬롯 배치]"));
	case EOreDealTurnPhase::SupplyDraw: return FText::FromString(TEXT("[4단계: 보급 드로우]"));
	case EOreDealTurnPhase::GameOver: return FText::FromString(TEXT("[게임 종료]"));
	default: return FText::GetEmpty();
	}
}

bool AOreDealTradeManager::RegisterPlayer(FName Id, const TArray<FName>& Queue, int32 SupplyCount, int32 WagonCount)
{
	if (!HasAuthority() || TurnPhase != EOreDealTurnPhase::Waiting || Id.IsNone() || Id == MarketId || SupplyCount < 0 || WagonCount < 1 || FindPlayer(Id) != INDEX_NONE || Players.Num() >= 4) return false;
	for (FName Card : Queue) if (Card.IsNone() || FindRule(Card) == INDEX_NONE) return false;
	FTradePlayerInventory& P = Players.AddDefaulted_GetRef();
	P.PlayerId = Id; P.Queue = Queue; P.Supplies = SupplyCount; P.Wagons.SetNum(WagonCount);
	OnPlayerStateChanged.Broadcast(Id);
	return true;
}

bool AOreDealTradeManager::StartGame(int32 RandomSeed)
{
	if (!HasAuthority() || bGameOver || TurnPhase != EOreDealTurnPhase::Waiting || Players.Num() < 2 || Players.Num() > 4) return false;
	if (!HasValidResourceRules()) ResourceRules = MakeDefaultResourceRules();
	Deck.Reset(); DiscardPile.Reset(); PublicMarket.Reset();
	for (const FOreDealResourceRule& Rule : ResourceRules)
		for (int32 I = 0; I < Rule.DeckCopies; ++I) Deck.Add(Rule.Resource);
	if (Deck.IsEmpty()) return false;
	FRandomStream Stream(RandomSeed == 0 ? FMath::Rand() : RandomSeed);
	for (int32 I = Deck.Num() - 1; I > 0; --I) Deck.Swap(I, Stream.RandRange(0, I));
	for (FTradePlayerInventory& P : Players)
	{
		if (P.Supplies == 0) P.Supplies = StartingSupplies;
		if (P.Wagons.Num() == 0) P.Wagons.SetNum(StartingWagons);
		for (int32 I = 0; I < StartingHandSize; ++I)
		{
			if (Deck.IsEmpty()) break;
			P.Queue.Add(Deck.Pop(EAllowShrinking::No));
		}
	}
	ActivePlayer = Players[0].PlayerId;
	OnDeckChanged.Broadcast(Deck.Num(), DiscardPile.Num());
	OnPlayerStateChanged.Broadcast(ActivePlayer);
	return BeginPlayerTurn(ActivePlayer);
}

bool AOreDealTradeManager::BeginPlayerTurn(FName PlayerId)
{
	if (!HasAuthority() || bGameOver || (TurnPhase != EOreDealTurnPhase::Waiting && TurnPhase != EOreDealTurnPhase::SupplyDraw) || FindPlayer(PlayerId) == INDEX_NONE) return false;
	if (TurnPhase == EOreDealTurnPhase::SupplyDraw)
	{
		const int32 CurrentIndex = FindPlayer(ActivePlayer);
		if (CurrentIndex == INDEX_NONE || Players[(CurrentIndex + 1) % Players.Num()].PlayerId != PlayerId) return false;
	}
	GetWorldTimerManager().ClearTimer(TurnAdvanceTimer);
	ActivePlayer = PlayerId; PlantedThisTurn = 0; PublicMarket.Reset();
	TurnPhase = EOreDealTurnPhase::MandatoryBurial; bTradePhaseActive = false;
	OnPhaseChanged.Broadcast(TurnPhase, ActivePlayer);
	return true;
}

bool AOreDealTradeManager::CanLoad(int32 Index, const TArray<FTradeResourceAmount>& Items) const
{
	if (!Players.IsValidIndex(Index)) return false;
	TArray<FTradeWagon> Simulated = Players[Index].Wagons;
	for (const FTradeResourceAmount& Item : Items)
	{
		if (Item.Resource.IsNone() || Item.Amount <= 0 || FindRule(Item.Resource) == INDEX_NONE) return false;
		int32 Wagon = Simulated.IndexOfByPredicate([&Item](const FTradeWagon& W) { return W.Amount > 0 && W.Resource == Item.Resource; });
		if (Wagon == INDEX_NONE) Wagon = Simulated.IndexOfByPredicate([](const FTradeWagon& W) { return W.Amount == 0; });
		if (Wagon == INDEX_NONE) return false;
		Simulated[Wagon].Resource = Item.Resource; Simulated[Wagon].Amount += Item.Amount;
	}
	return true;
}

void AOreDealTradeManager::LoadItems(int32 Index, const TArray<FTradeResourceAmount>& Items)
{
	for (const FTradeResourceAmount& Item : Items)
	{
		int32 Wagon = Players[Index].Wagons.IndexOfByPredicate([&Item](const FTradeWagon& W) { return W.Amount > 0 && W.Resource == Item.Resource; });
		if (Wagon == INDEX_NONE) Wagon = Players[Index].Wagons.IndexOfByPredicate([](const FTradeWagon& W) { return W.Amount == 0; });
		if (Wagon != INDEX_NONE) { Players[Index].Wagons[Wagon].Resource = Item.Resource; Players[Index].Wagons[Wagon].Amount += Item.Amount; }
	}
}

bool AOreDealTradeManager::CanRemoveItems(FName Id, const TArray<FTradeResourceAmount>& Items) const
{
	if (Items.IsEmpty()) return true;
	if (!IsValidItems(Items)) return false;
	if (Id == MarketId) return CanRemoveMarket(Items);
	const int32 Index = FindPlayer(Id);
	if (Index == INDEX_NONE) return false;
	TMap<FName, int32> Counts;
	for (FName Resource : Players[Index].Queue) ++Counts.FindOrAdd(Resource);
	for (const FTradeResourceAmount& Item : Items)
	{
		if (Counts.FindRef(Item.Resource) < Item.Amount) return false;
		Counts[Item.Resource] -= Item.Amount;
	}
	return true;
}

bool AOreDealTradeManager::CanRemoveMarket(const TArray<FTradeResourceAmount>& Items) const
{
	TMap<FName, int32> Counts;
	for (FName Resource : PublicMarket) ++Counts.FindOrAdd(Resource);
	for (const FTradeResourceAmount& Item : Items)
	{
		if (Item.Resource.IsNone() || Item.Amount <= 0 || Counts.FindRef(Item.Resource) < Item.Amount) return false;
		Counts[Item.Resource] -= Item.Amount;
	}
	return true;
}

void AOreDealTradeManager::RemoveItems(FName Id, const TArray<FTradeResourceAmount>& Items)
{
	const int32 Index = FindPlayer(Id);
	if (Index == INDEX_NONE) return;
	for (const FTradeResourceAmount& Item : Items)
	{
		int32 Remaining = Item.Amount;
		for (int32 I = Players[Index].Queue.Num() - 1; I >= 0 && Remaining > 0; --I)
			if (Players[Index].Queue[I] == Item.Resource) { Players[Index].Queue.RemoveAt(I); --Remaining; }
	}
}

void AOreDealTradeManager::RemoveMarket(const TArray<FTradeResourceAmount>& Items)
{
	for (const FTradeResourceAmount& Item : Items)
	{
		int32 Remaining = Item.Amount;
		for (int32 I = PublicMarket.Num() - 1; I >= 0 && Remaining > 0; --I)
			if (PublicMarket[I] == Item.Resource) { PublicMarket.RemoveAt(I); --Remaining; }
	}
}

bool AOreDealTradeManager::PlantQueueFront(FName PlayerId, int32 WagonIndex)
{
	if (!HasAuthority() || TurnPhase != EOreDealTurnPhase::MandatoryBurial || PlayerId != ActivePlayer) return false;
	const int32 Index = FindPlayer(PlayerId);
	if (Index == INDEX_NONE || Players[Index].Queue.IsEmpty()) return false;
	const FName Resource = Players[Index].Queue[0];
	if (PlantedThisTurn >= MaximumPlantCount)
	{
		// The standard turn allows one mandatory and one optional card. When all
		// occupied wagons contain a single resource kind, keep allowing matching
		// queue-front cards to be planted into that same kind.
		FName SoleWagonResource = NAME_None;
		bool bHasOccupiedWagon = false;
		bool bOnlyOneResourceKind = true;
		for (const FTradeWagon& Wagon : Players[Index].Wagons)
		{
			if (Wagon.Amount <= 0) continue;
			bHasOccupiedWagon = true;
			if (SoleWagonResource.IsNone()) SoleWagonResource = Wagon.Resource;
			else if (SoleWagonResource != Wagon.Resource) { bOnlyOneResourceKind = false; break; }
		}
		if (!bHasOccupiedWagon || !bOnlyOneResourceKind || Resource != SoleWagonResource) return false;
	}
	TArray<FTradeWagon> Sim = Players[Index].Wagons;
	int32 Target = Sim.IndexOfByPredicate([Resource](const FTradeWagon& W) { return W.Amount > 0 && W.Resource == Resource; });
	if (WagonIndex != INDEX_NONE)
	{
		if (!Sim.IsValidIndex(WagonIndex) || (Target != INDEX_NONE && Target != WagonIndex)) return false;
		Target = WagonIndex;
	}
	else if (Target == INDEX_NONE) Target = Sim.IndexOfByPredicate([](const FTradeWagon& W) { return W.Amount == 0; });
	if (Target != INDEX_NONE && Sim[Target].Amount > 0 && Sim[Target].Resource != Resource)
	{
		if (!HarvestWagonInternal(Index, Target, true)) return false;
		Sim = Players[Index].Wagons;
	}
	if (Target == INDEX_NONE)
	{
		// No compatible or empty wagon remains. Force-harvest the first wagon
		// that can legally be cleared, then use that slot for the queue-front card.
		for (int32 Candidate = 0; Candidate < Sim.Num(); ++Candidate)
		{
			const FTradeWagon& Wagon = Sim[Candidate];
			if (Wagon.Amount <= 0 || Players[Index].Supplies < HarvestSupplyCost) continue;
			if (Wagon.Amount == 1)
			{
				bool bBlockedByLargerWagon = false;
				for (int32 Other = 0; Other < Sim.Num(); ++Other)
					if (Other != Candidate && Sim[Other].Amount >= 2) { bBlockedByLargerWagon = true; break; }
				if (bBlockedByLargerWagon) continue;
			}
			if (FindRule(Wagon.Resource) != INDEX_NONE) { Target = Candidate; break; }
		}
		if (Target == INDEX_NONE || !HarvestWagonInternal(Index, Target, true)) return false;
		Sim = Players[Index].Wagons;
	}
	if (Target == INDEX_NONE) return false;
	Sim[Target].Resource = Resource;
	++Sim[Target].Amount;
	Players[Index].Wagons = MoveTemp(Sim);
	Players[Index].Queue.RemoveAt(0); ++PlantedThisTurn;
	OnPlayerStateChanged.Broadcast(PlayerId);
	return true;
}

bool AOreDealTradeManager::FinishPlanting(FName PlayerId)
{
	if (!HasAuthority() || PlayerId != ActivePlayer || TurnPhase != EOreDealTurnPhase::MandatoryBurial) return false;
	const int32 ActiveIndex = FindPlayer(PlayerId);
	if (ActiveIndex == INDEX_NONE || (!Players[ActiveIndex].Queue.IsEmpty() && PlantedThisTurn < MinimumPlantCount)) return false;
	TurnPhase = EOreDealTurnPhase::MarketTrade;
	PublicMarket.Reset();
	for (int32 I = 0; I < PublicMarketCount; ++I)
	{
		if (!Deck.IsEmpty()) PublicMarket.Add(Deck.Pop(EAllowShrinking::No));
	}
	OnDeckChanged.Broadcast(Deck.Num(), DiscardPile.Num());
	OnPhaseChanged.Broadcast(TurnPhase, ActivePlayer);
	bTradePhaseActive = true;
	return true;
}

void AOreDealTradeManager::BeginTradePhase(FName Player, const TArray<FName>& Market)
{
	if (!HasAuthority() || TurnPhase != EOreDealTurnPhase::MarketTrade || Player != ActivePlayer || Market.Num() > PublicMarketCount) return;
	// Compatibility entry point for existing Blueprint graphs; never permits injected cards.
	if (Market != PublicMarket) return;
	bTradePhaseActive = true;
}

bool AOreDealTradeManager::ResolveDeal(FName From, FName To, const TArray<FTradeResourceAmount>& Offered,
	const TArray<FTradeResourceAmount>& Requested, int32 OfferedSupply, int32 RequestedSupply, bool bAccepted)
{
	if (!HasAuthority() || TurnPhase != EOreDealTurnPhase::MarketTrade || !bTradePhaseActive || !bAccepted || From.IsNone() || To.IsNone() || From == To ||
		OfferedSupply < 0 || RequestedSupply < 0 || (Offered.IsEmpty() && Requested.IsEmpty() && OfferedSupply == 0 && RequestedSupply == 0)) return false;
	const int32 FromIndex = FindPlayer(From), ToIndex = FindPlayer(To);
	const bool bFromMarket = From == MarketId, bToMarket = To == MarketId;
	if ((FromIndex == INDEX_NONE && !bFromMarket) || (ToIndex == INDEX_NONE && !bToMarket) ||
		(From != ActivePlayer && To != ActivePlayer) || (bFromMarket && OfferedSupply != 0) || (bToMarket && RequestedSupply != 0) ||
		!CanRemoveItems(From, Offered) || !CanRemoveItems(To, Requested)) return false;
	if (!bFromMarket && Players[FromIndex].Supplies < OfferedSupply) return false;
	if (!bToMarket && Players[ToIndex].Supplies < RequestedSupply) return false;
	if (!bFromMarket && !CanLoad(FromIndex, Requested)) return false;
	if (!bToMarket && !CanLoad(ToIndex, Offered)) return false;
	if (bFromMarket) RemoveMarket(Offered); else RemoveItems(From, Offered);
	if (bToMarket) RemoveMarket(Requested); else RemoveItems(To, Requested);
	if (!bFromMarket)
	{
		Players[FromIndex].Supplies += RequestedSupply - OfferedSupply;
		LoadItems(FromIndex, Requested);
	}
	if (!bToMarket)
	{
		Players[ToIndex].Supplies += OfferedSupply - RequestedSupply;
		LoadItems(ToIndex, Offered);
	}
	if (bFromMarket) for (const FTradeResourceAmount& Item : Requested) for (int32 I=0; I<Item.Amount; ++I) PublicMarket.Add(Item.Resource);
	if (bToMarket) for (const FTradeResourceAmount& Item : Offered) for (int32 I=0; I<Item.Amount; ++I) PublicMarket.Add(Item.Resource);
	if (FromIndex != INDEX_NONE) OnPlayerStateChanged.Broadcast(From);
	if (ToIndex != INDEX_NONE) OnPlayerStateChanged.Broadcast(To);
	return true;
}

bool AOreDealTradeManager::EndTradePhase()
{
	if (!HasAuthority() || TurnPhase != EOreDealTurnPhase::MarketTrade || !bTradePhaseActive) return false;
	const int32 Index = FindPlayer(ActivePlayer);
	if (Index == INDEX_NONE) return false;
	TurnPhase = EOreDealTurnPhase::SlotPlacement;
	OnPhaseChanged.Broadcast(TurnPhase, ActivePlayer);
	return true;
}

bool AOreDealTradeManager::FinishSlotPlacement(FName PlayerId)
{
	if (!HasAuthority() || TurnPhase != EOreDealTurnPhase::SlotPlacement || PlayerId != ActivePlayer) return false;
	const int32 Index = FindPlayer(PlayerId);
	if (Index == INDEX_NONE) return false;
	TArray<FTradeResourceAmount> RemainingMarket;
	for (FName Resource : PublicMarket)
	{
		int32 Entry = RemainingMarket.IndexOfByPredicate([Resource](const FTradeResourceAmount& Item) { return Item.Resource == Resource; });
		if (Entry == INDEX_NONE) { Entry = RemainingMarket.AddDefaulted(); RemainingMarket[Entry].Resource = Resource; }
		++RemainingMarket[Entry].Amount;
	}
	if (!CanLoad(Index, RemainingMarket)) return false;
	LoadItems(Index, RemainingMarket);
	PublicMarket.Reset();
	bTradePhaseActive = false;
	OnPlayerStateChanged.Broadcast(PlayerId);
	OnDeckChanged.Broadcast(Deck.Num(), DiscardPile.Num());
	CompleteSupplyDrawPhase();
	return true;
}

void AOreDealTradeManager::CompleteSupplyDrawPhase()
{
	TurnPhase = EOreDealTurnPhase::SupplyDraw;
	OnPhaseChanged.Broadcast(TurnPhase, ActivePlayer);
	const int32 Index = FindPlayer(ActivePlayer);
	if (Index == INDEX_NONE) { CheckGameOver(); return; }
	int32 Drawn = 0;
	for (; Drawn < CardsPerTurn; ++Drawn)
	{
		if (Deck.IsEmpty()) break;
		Players[Index].Queue.Add(Deck.Pop(EAllowShrinking::No));
	}
	OnPlayerStateChanged.Broadcast(ActivePlayer);
	OnDeckChanged.Broadcast(Deck.Num(), DiscardPile.Num());
	if (Deck.IsEmpty()) { CheckGameOver(); return; }
	if (SupplyDrawPhaseDuration <= 0.0f)
	{
		AdvanceTurnAfterDraw();
	}
	else
	{
		GetWorldTimerManager().SetTimer(TurnAdvanceTimer, this, &AOreDealTradeManager::AdvanceTurnAfterDraw, SupplyDrawPhaseDuration, false);
	}
}

void AOreDealTradeManager::AdvanceTurnAfterDraw()
{
	AdvanceTurn();
}

bool AOreDealTradeManager::HarvestWagon(FName PlayerId, int32 WagonIndex)
{
	if (!HasAuthority() || bGameOver || PlayerId != ActivePlayer || TurnPhase != EOreDealTurnPhase::SlotPlacement) return false;
	const int32 Index = FindPlayer(PlayerId);
	return Index != INDEX_NONE && HarvestWagonInternal(Index, WagonIndex, false);
}

bool AOreDealTradeManager::HarvestWagonInternal(int32 PlayerIndex, int32 WagonIndex, bool bAllowBelowMinimum)
{
	if (!Players.IsValidIndex(PlayerIndex) || !Players[PlayerIndex].Wagons.IsValidIndex(WagonIndex)) return false;
	FTradeWagon& Wagon = Players[PlayerIndex].Wagons[WagonIndex];
	if (Wagon.Amount <= 0 || Players[PlayerIndex].Supplies < HarvestSupplyCost) return false;
	if (Wagon.Amount == 1)
		for (int32 I = 0; I < Players[PlayerIndex].Wagons.Num(); ++I)
			if (Players[PlayerIndex].Wagons[I].Amount >= 2) return false;
	const int32 RuleIndex = FindRule(Wagon.Resource);
	if (RuleIndex == INDEX_NONE) return false;
	const FOreDealResourceRule& Rule = ResourceRules[RuleIndex];
	if (!bAllowBelowMinimum && Wagon.Amount < 2)
	{
		for (const FTradeWagon& OtherWagon : Players[PlayerIndex].Wagons)
			if (OtherWagon.Amount >= 2) return false;
	}
	int32 Earned = 0;
	for (const FOreDealHarvestPayout& Payout : Rule.HarvestPayouts)
		if (Wagon.Amount >= Payout.MinimumCards)
			Earned = FMath::Max(Earned, Payout.Gold);
	const int32 Count = Wagon.Amount;
	const FName HarvestedResource = Wagon.Resource;
	Players[PlayerIndex].Supplies -= HarvestSupplyCost; Players[PlayerIndex].Gold += Earned;
	for (int32 I=0; I<Count; ++I) DiscardPile.Add(Wagon.Resource);
	Wagon = FTradeWagon();
	const FName PlayerId = Players[PlayerIndex].PlayerId;
	OnWagonHarvested.Broadcast(PlayerId, WagonIndex, Earned); OnPlayerStateChanged.Broadcast(PlayerId);
	OnDeckChanged.Broadcast(Deck.Num(), DiscardPile.Num());
	if (UWorld* World = GetWorld())
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(It->Get()))
				PC->ClientShowSaleToast(HarvestedResource.ToString(), Count);
	return true;
}

bool AOreDealTradeManager::BuyThirdWagon(FName PlayerId)
{
	if (!HasAuthority() || bGameOver) return false;
	const int32 Index = FindPlayer(PlayerId);
	if (Index == INDEX_NONE || Players[Index].Wagons.Num() != 2 || Players[Index].Gold < ThirdWagonGoldCost) return false;
	Players[Index].Gold -= ThirdWagonGoldCost; Players[Index].Wagons.AddDefaulted();
	OnPlayerStateChanged.Broadcast(PlayerId);
	return true;
}

bool AOreDealTradeManager::AdvanceTurn()
{
	if (!HasAuthority() || TurnPhase != EOreDealTurnPhase::SupplyDraw || Players.IsEmpty()) return false;
	const int32 Current = FindPlayer(ActivePlayer);
	if (Current == INDEX_NONE) return false;
	const FName Next = Players[(Current + 1) % Players.Num()].PlayerId;
	return BeginPlayerTurn(Next);
}

bool AOreDealTradeManager::AddQueueResource(FName Id, FName Resource)
{
	if (!HasAuthority() || Resource.IsNone() || FindRule(Resource) == INDEX_NONE) return false;
	const int32 Index = FindPlayer(Id);
	if (Index == INDEX_NONE) return false;
	Players[Index].Queue.Add(Resource); OnPlayerStateChanged.Broadcast(Id); return true;
}

void AOreDealTradeManager::CheckGameOver()
{
	bGameOver = true; TurnPhase = EOreDealTurnPhase::GameOver;
	int32 Best = MIN_int32; TArray<FName> Winners;
	for (const FTradePlayerInventory& P : Players)
	{
		if (P.Gold > Best) { Best = P.Gold; Winners.Reset(); Winners.Add(P.PlayerId); }
		else if (P.Gold == Best) Winners.Add(P.PlayerId);
	}
	OnPhaseChanged.Broadcast(TurnPhase, ActivePlayer); OnGameEnded.Broadcast(Winners);
}

void AOreDealTradeManager::OnRep_Players()
{
	for (const FTradePlayerInventory& P : Players) OnPlayerStateChanged.Broadcast(P.PlayerId);
}
void AOreDealTradeManager::OnRep_DeckState() { OnDeckChanged.Broadcast(Deck.Num(), DiscardPile.Num()); }
void AOreDealTradeManager::OnRep_TurnPhase()
{
	OnPhaseChanged.Broadcast(TurnPhase, ActivePlayer);
	if (TurnPhase == EOreDealTurnPhase::GameOver)
	{
		int32 Best = MIN_int32; TArray<FName> Winners;
		for (const FTradePlayerInventory& P : Players)
		{
			if (P.Gold > Best) { Best = P.Gold; Winners.Reset(); Winners.Add(P.PlayerId); }
			else if (P.Gold == Best) Winners.Add(P.PlayerId);
		}
		OnGameEnded.Broadcast(Winners);
	}
}

void UOreDealPhaseHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!TxtPhase || !GetWorld()) return;

	FText PhaseLabel = FText::GetEmpty();
	for (TActorIterator<AOreDealTradeManager> It(GetWorld()); It; ++It)
	{
		PhaseLabel = It->GetTurnPhaseLabel();
		break;
	}

	if (!TxtPhase->GetText().EqualTo(PhaseLabel)) TxtPhase->SetText(PhaseLabel);
	TxtPhase->SetVisibility(PhaseLabel.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

TArray<FName> AOreDealTradeManager::GetQueue(FName Id) const { const int32 I=FindPlayer(Id); return I==INDEX_NONE ? TArray<FName>() : Players[I].Queue; }
TArray<FTradeWagon> AOreDealTradeManager::GetWagons(FName Id) const { const int32 I=FindPlayer(Id); return I==INDEX_NONE ? TArray<FTradeWagon>() : Players[I].Wagons; }
int32 AOreDealTradeManager::GetSupplies(FName Id) const { const int32 I=FindPlayer(Id); return I==INDEX_NONE ? 0 : Players[I].Supplies; }
int32 AOreDealTradeManager::GetGold(FName Id) const { const int32 I=FindPlayer(Id); return I==INDEX_NONE ? 0 : Players[I].Gold; }
