#include "OreDealMarketBroker.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "OreDealTurnBridge.h"
#include "OreDealTablePlayerController.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr int32 ResourceCount = 8;
	const TCHAR* ResourceNames[ResourceCount] = { TEXT("Wood"), TEXT("Coal"), TEXT("Copper"), TEXT("Iron"), TEXT("Silver"), TEXT("Gold"), TEXT("Diamond"), TEXT("Adamantium") };
	int32 ResourceSaleReward(int32 Resource, int32 Count)
	{
		static const int32 Thresholds[ResourceCount][4] = {
			{4,6,8,10}, {3,5,7,8}, {3,4,6,7}, {2,4,5,6},
			{2,4,6,7}, {2,4,5,6}, {2,3,4,5}, {2,3,0,0}
		};
		static const int32 Rewards[ResourceCount][4] = {
			{1,2,3,4}, {1,2,3,4}, {1,2,3,4}, {1,2,3,4},
			{1,2,3,4}, {1,2,3,4}, {1,2,3,4}, {2,3,0,0}
		};
		if (Resource < 0 || Resource >= ResourceCount) return 0;
		int32 Result = 0;
		for (int32 I = 0; I < 4 && Thresholds[Resource][I] > 0; ++I)
			if (Count >= Thresholds[Resource][I]) Result = Rewards[Resource][I];
		return Result;
	}

	AActor* FindDeckInWorld(UWorld* World)
	{
		if (!World) return nullptr;
		UClass* Class = StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/Game/MaterialTraders/Blueprints/BP_CardDeck.BP_CardDeck_C"));
		return Class ? UGameplayStatics::GetActorOfClass(World, Class) : nullptr;
	}

	int32 ReadInt(UObject* Object, FName Name, int32 Fallback = 0)
	{
		const FIntProperty* Property = Object ? FindFProperty<FIntProperty>(Object->GetClass(), Name) : nullptr;
		return Property ? Property->GetPropertyValue_InContainer(Object) : Fallback;
	}
}

AOreDealMarketBroker::AOreDealMarketBroker()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	NetUpdateFrequency = 10.0f;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.15f;
}

void AOreDealMarketBroker::BeginPlay()
{
	Super::BeginPlay();
	RefreshPublicOffers();
}

void AOreDealMarketBroker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) return;
	AActor* Deck = FindCardDeck();
	const FObjectPropertyBase* TurnPlayerProperty = Deck ? FindFProperty<FObjectPropertyBase>(Deck->GetClass(), TEXT("TurnPS")) : nullptr;
	const FIntProperty* PhaseProperty = Deck ? FindFProperty<FIntProperty>(Deck->GetClass(), TEXT("TurnPhaseIndex")) : nullptr;
	if (!Deck || !TurnPlayerProperty || !PhaseProperty) return;
	APlayerState* DeckTurnPlayer = Cast<APlayerState>(TurnPlayerProperty->GetObjectPropertyValue_InContainer(Deck));
	const int32 Phase = PhaseProperty->GetPropertyValue_InContainer(Deck);
	if (MarketState == EOreDealMarketState::Waiting)
	{
		if (Phase == 1 && IsValid(DeckTurnPlayer)) StartMarket(Deck, DeckTurnPlayer);
		return;
	}
	if ((Phase != 1 || DeckTurnPlayer != ActivePlayer) && MarketState != EOreDealMarketState::Waiting)
	{
		FinishMarket(false);
	}
}

void AOreDealMarketBroker::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AOreDealMarketBroker, MarketState);
	DOREPLIFETIME(AOreDealMarketBroker, ActivePlayer);
	DOREPLIFETIME(AOreDealMarketBroker, SelectedTarget);
	DOREPLIFETIME(AOreDealMarketBroker, PendingTarget);
	DOREPLIFETIME(AOreDealMarketBroker, MarketCards);
	DOREPLIFETIME(AOreDealMarketBroker, PendingMarketCards);
	DOREPLIFETIME(AOreDealMarketBroker, PublicOffers);
	DOREPLIFETIME(AOreDealMarketBroker, SecondsRemaining);
	DOREPLIFETIME(AOreDealMarketBroker, bActiveAccepted);
	DOREPLIFETIME(AOreDealMarketBroker, bTargetAccepted);
	DOREPLIFETIME(AOreDealMarketBroker, Revision);
}

void AOreDealMarketBroker::OnRep_MarketState() {}

AActor* AOreDealMarketBroker::FindCardDeck() const
{
	return FindDeckInWorld(GetWorld());
}

bool AOreDealMarketBroker::ReadIntArray(UObject* Object, FName Name, TArray<int32>& OutValues) const
{
	OutValues.Reset();
	FArrayProperty* Property = Object ? FindFProperty<FArrayProperty>(Object->GetClass(), Name) : nullptr;
	FIntProperty* Inner = Property ? CastField<FIntProperty>(Property->Inner) : nullptr;
	if (!Property || !Inner) return false;
	FScriptArrayHelper Helper(Property, Property->ContainerPtrToValuePtr<void>(Object));
	OutValues.Reserve(Helper.Num());
	for (int32 I = 0; I < Helper.Num(); ++I) OutValues.Add(Inner->GetPropertyValue(Helper.GetRawPtr(I)));
	return true;
}

bool AOreDealMarketBroker::WriteIntArray(UObject* Object, FName Name, const TArray<int32>& Values) const
{
	FArrayProperty* Property = Object ? FindFProperty<FArrayProperty>(Object->GetClass(), Name) : nullptr;
	FIntProperty* Inner = Property ? CastField<FIntProperty>(Property->Inner) : nullptr;
	if (!Property || !Inner) return false;
	FScriptArrayHelper Helper(Property, Property->ContainerPtrToValuePtr<void>(Object));
	Helper.EmptyAndAddValues(Values.Num());
	for (int32 I = 0; I < Values.Num(); ++I) Inner->SetPropertyValue(Helper.GetRawPtr(I), Values[I]);
	return true;
}

APawn* AOreDealMarketBroker::FindPawn(APlayerState* Player) const
{
	if (!GetWorld() || !Player) return nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (APlayerController* PC = It->Get(); PC && PC->PlayerState == Player) return PC->GetPawn();
	return nullptr;
}

AActor* AOreDealMarketBroker::FindBoard(APawn* Pawn) const
{
	if (!Pawn || !GetWorld()) return nullptr;
	UClass* BoardClass = StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/Game/MaterialTraders/Blueprints/BP_CardBoard.BP_CardBoard_C"));
	if (!BoardClass) return nullptr;
	TArray<AActor*> Boards;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), BoardClass, Boards);
	AActor* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (AActor* Board : Boards)
	{
		const double Distance = FVector::DistSquared(Pawn->GetActorLocation(), Board->GetActorLocation());
		if (Distance < BestDistance) { BestDistance = Distance; Best = Board; }
	}
	return Best;
}

bool AOreDealMarketBroker::GetHand(APlayerState* Player, APawn*& OutPawn, TArray<int32>& OutCards) const
{
	OutPawn = FindPawn(Player);
	return OutPawn && ReadIntArray(OutPawn, TEXT("HandCards"), OutCards);
}

void AOreDealMarketBroker::StartMarket(AActor* CardDeck, APlayerState* TurnPlayer)
{
	if (!HasAuthority() || !IsValid(CardDeck) || !IsValid(TurnPlayer)) return;
	TArray<int32> Revealed;
	if (!ReadIntArray(CardDeck, TEXT("RevealedCards"), Revealed) || Revealed.IsEmpty()) return;
	ActivePlayer = TurnPlayer;
	SelectedTarget = nullptr;
	PendingTarget = nullptr;
	MarketCards = MoveTemp(Revealed);
	PendingMarketCards.Reset();
	PendingMarketIndices.Reset();
	PendingActiveIndices.Reset();
	PrivateOfferIndices.Reset();
	SecondsRemaining = 120;
	bActiveAccepted = false;
	bTargetAccepted = false;
	RefreshPublicOffers();
	MarketState = EOreDealMarketState::Reveal;
	++Revision;
	ForceNetUpdate();
	GetWorldTimerManager().SetTimer(IntroTimer, this, &AOreDealMarketBroker::FinishIntro, 1.0f, false);
}

void AOreDealMarketBroker::FinishIntro()
{
	if (!HasAuthority() || MarketState != EOreDealMarketState::Reveal) return;
	MarketState = EOreDealMarketState::Trading;
	++Revision;
	ForceNetUpdate();
	GetWorldTimerManager().SetTimer(CountdownTimer, this, &AOreDealMarketBroker::TickCountdown, 1.0f, true);
}

void AOreDealMarketBroker::TickCountdown()
{
	if (!HasAuthority() || MarketState == EOreDealMarketState::Waiting) return;
	SecondsRemaining = FMath::Max(SecondsRemaining - 1, 0);
	ForceNetUpdate();
	if (SecondsRemaining <= 0) FinishMarket(true);
}

void AOreDealMarketBroker::RefreshPublicOffers()
{
	if (!HasAuthority() || !GetWorld()) return;
	AGameStateBase* GS = GetWorld()->GetGameState();
	if (!GS) return;
	PublicOffers.Reset();
	for (APlayerState* Player : GS->PlayerArray)
	{
		FOreDealPublicOffer& View = PublicOffers.AddDefaulted_GetRef();
		View.Player = Player;
		View.HandCount = 0;
		APawn* Pawn = FindPawn(Player);
		TArray<int32> Hand;
		if (ReadIntArray(Pawn, TEXT("HandCards"), Hand)) View.HandCount = Hand.Num();
		if (const TArray<int32>* Indices = PrivateOfferIndices.Find(Player))
			for (int32 Index : *Indices)
				if (Hand.IsValidIndex(Index)) View.Cards.Add(Hand[Index]);
	}
	++Revision;
	ForceNetUpdate();
}

void AOreDealMarketBroker::SetPlayerOffer(APlayerState* Player, const TArray<int32>& HandIndices)
{
	if (!HasAuthority() || MarketState != EOreDealMarketState::Trading || !IsValid(Player) || !GetWorld()) return;
	AGameStateBase* GS = GetWorld()->GetGameState();
	if (!GS || !GS->PlayerArray.Contains(Player)) return;
	APawn* Pawn = nullptr;
	TArray<int32> Hand;
	if (!GetHand(Player, Pawn, Hand)) return;
	TSet<int32> Unique;
	for (int32 Index : HandIndices)
		if (!Hand.IsValidIndex(Index) || Unique.Contains(Index) || Hand[Index] < 0 || Hand[Index] >= ResourceCount) return;
		else Unique.Add(Index);
	PrivateOfferIndices.Add(Player, HandIndices);
	RefreshPublicOffers();
}

void AOreDealMarketBroker::SelectTarget(APlayerState* Player, APlayerState* Target)
{
	if (!HasAuthority() || MarketState != EOreDealMarketState::Trading || Player != ActivePlayer || !IsValid(Target) || !GetWorld()) return;
	AGameStateBase* GS = GetWorld()->GetGameState();
	if (!GS || !GS->PlayerArray.Contains(Target)) return;
	SelectedTarget = Target;
	++Revision;
	ForceNetUpdate();
}

void AOreDealMarketBroker::BeginDeal(APlayerState* Player, APlayerState* Target, const TArray<int32>& MarketIndices)
{
	if (!HasAuthority() || MarketState != EOreDealMarketState::Trading || Player != ActivePlayer || !IsValid(Target) || Target == ActivePlayer || Target != SelectedTarget) return;
	TSet<int32> Unique;
	for (int32 Index : MarketIndices)
		if (!MarketCards.IsValidIndex(Index) || Unique.Contains(Index)) return;
		else Unique.Add(Index);
	const FOreDealPublicOffer* TargetOffer = PublicOffers.FindByPredicate([Target](const FOreDealPublicOffer& Offer) { return Offer.Player == Target; });
	const FOreDealPublicOffer* ActiveOffer = PublicOffers.FindByPredicate([this](const FOreDealPublicOffer& Offer) { return Offer.Player == ActivePlayer; });
	if (MarketIndices.IsEmpty() && (!TargetOffer || TargetOffer->Cards.IsEmpty()) && (!ActiveOffer || ActiveOffer->Cards.IsEmpty())) return;
	PendingTarget = Target;
	PendingMarketIndices = MarketIndices;
	PendingMarketIndices.Sort(TGreater<int32>());
	PendingActiveIndices = PrivateOfferIndices.FindRef(ActivePlayer);
	PendingActiveIndices.Sort(TGreater<int32>());
	PendingMarketCards.Reset();
	TArray<int32> MarketCardsInDisplayOrder = MarketIndices;
	MarketCardsInDisplayOrder.Sort();
	for (int32 Index : MarketCardsInDisplayOrder) PendingMarketCards.Add(MarketCards[Index]);
	bActiveAccepted = false;
	bTargetAccepted = false;
	MarketState = EOreDealMarketState::AwaitingConsent;
	++Revision;
	ForceNetUpdate();
}

void AOreDealMarketBroker::CastConsent(APlayerState* Player, bool bAccept)
{
	if (!HasAuthority() || MarketState != EOreDealMarketState::AwaitingConsent || (Player != ActivePlayer && Player != PendingTarget)) return;
	if (!bAccept) { CancelDeal(); return; }
	if (Player == ActivePlayer) bActiveAccepted = true;
	if (Player == PendingTarget) bTargetAccepted = true;
	++Revision;
	ForceNetUpdate();
	if (bActiveAccepted && bTargetAccepted) CommitDeal();
}

void AOreDealMarketBroker::CancelDeal()
{
	if (!HasAuthority()) return;
	PendingTarget = nullptr;
	PendingMarketIndices.Reset();
	PendingActiveIndices.Reset();
	PendingMarketCards.Reset();
	bActiveAccepted = false;
	bTargetAccepted = false;
	SelectedTarget = nullptr;
	MarketState = EOreDealMarketState::Trading;
	++Revision;
	ForceNetUpdate();
}

bool AOreDealMarketBroker::CanPlaceCards(APlayerState* Player, const TArray<int32>& Cards) const
{
	if (Cards.IsEmpty()) return true;
	AActor* Board = FindBoard(FindPawn(Player));
	TArray<int32> Types, Counts;
	if (!ReadIntArray(Board, TEXT("WagonTypes"), Types) || !ReadIntArray(Board, TEXT("WagonCounts"), Counts) || Types.Num() != Counts.Num()) return false;
	for (int32 Resource : Cards)
		if (Resource < 0 || Resource >= ResourceCount || !SimulatePlace(Board, Types, Counts, Resource)) return false;
	return true;
}

bool AOreDealMarketBroker::SimulatePlace(AActor* Board, TArray<int32>& Types, TArray<int32>& Counts, int32 Resource) const
{
	int32 Slot = INDEX_NONE;
	for (int32 I = 0; I < Counts.Num(); ++I)
		if (Counts[I] > 0 && Types[I] == Resource) { Slot = I; break; }
	if (Slot == INDEX_NONE)
		for (int32 I = 0; I < Counts.Num(); ++I)
			if (Counts[I] <= 0) { Slot = I; break; }
	if (Slot == INDEX_NONE)
	{
		Slot = FindForcedHarvestSlot(Types, Counts);
		if (Slot == INDEX_NONE) return false;
		Types[Slot] = INDEX_NONE;
		Counts[Slot] = 0;
	}
	Types[Slot] = Resource;
	++Counts[Slot];
	return true;
}

int32 AOreDealMarketBroker::FindForcedHarvestSlot(const TArray<int32>& Types, const TArray<int32>& Counts) const
{
	bool bHasStack = false;
	for (int32 Count : Counts) if (Count >= 2) { bHasStack = true; break; }
	int32 Best = INDEX_NONE;
	int32 BestReward = MAX_int32;
	int32 BestCount = MAX_int32;
	for (int32 I = 0; I < Counts.Num(); ++I)
	{
		const bool bCanSell = bHasStack ? Counts[I] >= 2 : Counts[I] == 1;
		if (!bCanSell || Types[I] < 0 || Types[I] >= ResourceCount) continue;
		const int32 Reward = ResourceSaleReward(Types[I], Counts[I]);
		if (Reward < BestReward || (Reward == BestReward && Counts[I] < BestCount))
		{
			Best = I; BestReward = Reward; BestCount = Counts[I];
		}
	}
	return Best;
}

void AOreDealMarketBroker::HarvestSlot(APlayerState* Player, AActor* Board, TArray<int32>& Types, TArray<int32>& Counts, int32 Slot) const
{
	if (!Types.IsValidIndex(Slot) || !Counts.IsValidIndex(Slot)) return;
	const int32 Resource = Types[Slot];
	const int32 Count = Counts[Slot];
	if (Resource < 0 || Resource >= ResourceCount || Count <= 0) return;
	if (FIntProperty* Gold = FindFProperty<FIntProperty>(Player->GetClass(), TEXT("Gold")))
		Gold->SetPropertyValue_InContainer(Player, Gold->GetPropertyValue_InContainer(Player) + ResourceSaleReward(Resource, Count));
	if (FIntProperty* Grave = FindFProperty<FIntProperty>(Board->GetClass(), TEXT("GraveCount")))
		Grave->SetPropertyValue_InContainer(Board, Grave->GetPropertyValue_InContainer(Board) + Count);
	Types[Slot] = INDEX_NONE;
	Counts[Slot] = 0;
	if (GetWorld())
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(It->Get()))
				PC->ClientShowSaleToast(ResourceNames[Resource], Count);
}

bool AOreDealMarketBroker::PlaceCards(APlayerState* Player, const TArray<int32>& Cards)
{
	if (Cards.IsEmpty()) return true;
	AActor* Board = FindBoard(FindPawn(Player));
	TArray<int32> Types, Counts;
	if (!ReadIntArray(Board, TEXT("WagonTypes"), Types) || !ReadIntArray(Board, TEXT("WagonCounts"), Counts) || Types.Num() != Counts.Num()) return false;
	for (int32 Resource : Cards)
	{
		int32 Slot = INDEX_NONE;
		for (int32 I = 0; I < Counts.Num(); ++I) if (Counts[I] > 0 && Types[I] == Resource) { Slot = I; break; }
		if (Slot == INDEX_NONE) for (int32 I = 0; I < Counts.Num(); ++I) if (Counts[I] <= 0) { Slot = I; break; }
		if (Slot == INDEX_NONE)
		{
			Slot = FindForcedHarvestSlot(Types, Counts);
			if (Slot == INDEX_NONE) return false;
			HarvestSlot(Player, Board, Types, Counts, Slot);
		}
		Types[Slot] = Resource;
		++Counts[Slot];
	}
	WriteIntArray(Board, TEXT("WagonTypes"), Types);
	WriteIntArray(Board, TEXT("WagonCounts"), Counts);
	Board->ForceNetUpdate();
	Player->ForceNetUpdate();
	UpdateBoardVisual(Board);
	return true;
}

void AOreDealMarketBroker::UpdateBoardVisual(AActor* Board) const
{
	if (!Board) return;
	for (const FName FunctionName : {FName(TEXT("OnRep_WagonTypes")), FName(TEXT("OnRep_WagonCounts")), FName(TEXT("UpdateVisual"))})
		if (UFunction* Function = Board->FindFunction(FunctionName)) Board->ProcessEvent(Function, nullptr);
}

bool AOreDealMarketBroker::RemoveHandIndices(APlayerState* Player, const TArray<int32>& Indices)
{
	APawn* Pawn = nullptr;
	TArray<int32> Hand;
	if (!GetHand(Player, Pawn, Hand)) return false;
	TArray<int32> Sorted = Indices;
	Sorted.Sort(TGreater<int32>());
	for (int32 Index : Sorted)
	{
		if (!Hand.IsValidIndex(Index)) return false;
		Hand.RemoveAt(Index);
	}
	if (!WriteIntArray(Pawn, TEXT("HandCards"), Hand)) return false;
	Pawn->ForceNetUpdate();
	for (const FName FunctionName : {FName(TEXT("OnRep_HandCards")), FName(TEXT("UpdateVisual"))})
		if (UFunction* Function = Pawn->FindFunction(FunctionName)) Pawn->ProcessEvent(Function, nullptr);
	return true;
}

void AOreDealMarketBroker::UpdateDeckVisual(AActor* Deck) const
{
	if (!Deck) return;
	Deck->ForceNetUpdate();
	if (UFunction* OnRep = Deck->FindFunction(TEXT("OnRep_RevealedCards"))) Deck->ProcessEvent(OnRep, nullptr);
	if (UFunction* Update = Deck->FindFunction(TEXT("UpdateVisual"))) Deck->ProcessEvent(Update, nullptr);
}

void AOreDealMarketBroker::CommitDeal()
{
	if (!HasAuthority() || !IsValid(PendingTarget)) { CancelDeal(); return; }
	TArray<int32> TargetCards;
	TArray<int32> ActiveCards;
	const TArray<int32>* TargetIndices = PrivateOfferIndices.Find(PendingTarget);
	APawn* TargetPawn = nullptr;
	TArray<int32> TargetHand;
	if (TargetIndices && GetHand(PendingTarget, TargetPawn, TargetHand))
		for (int32 Index : *TargetIndices)
		{
			if (!TargetHand.IsValidIndex(Index)) { CancelDeal(); return; }
			TargetCards.Add(TargetHand[Index]);
		}
	else if (TargetIndices && !TargetIndices->IsEmpty()) { CancelDeal(); return; }
	APawn* ActivePawn = nullptr;
	TArray<int32> ActiveHand;
	if (!PendingActiveIndices.IsEmpty() && GetHand(ActivePlayer, ActivePawn, ActiveHand))
		for (int32 Index : PendingActiveIndices)
		{
			if (!ActiveHand.IsValidIndex(Index)) { CancelDeal(); return; }
			ActiveCards.Add(ActiveHand[Index]);
		}
	else if (!PendingActiveIndices.IsEmpty()) { CancelDeal(); return; }
	TArray<int32> CardsToTarget = PendingMarketCards;
	CardsToTarget.Append(ActiveCards);
	if (!CanPlaceCards(PendingTarget, CardsToTarget) || !CanPlaceCards(ActivePlayer, TargetCards)) { CancelDeal(); return; }
	if (PendingMarketIndices.Num() != PendingMarketCards.Num()) { CancelDeal(); return; }

	AActor* Deck = FindCardDeck();
	TArray<int32> Revealed;
	if (!ReadIntArray(Deck, TEXT("RevealedCards"), Revealed)) { CancelDeal(); return; }
	for (int32 Index : PendingMarketIndices)
		if (!MarketCards.IsValidIndex(Index) || !Revealed.Contains(MarketCards[Index])) { CancelDeal(); return; }

	if (!TargetCards.IsEmpty() && !RemoveHandIndices(PendingTarget, *TargetIndices)) { CancelDeal(); return; }
	if (!ActiveCards.IsEmpty() && !RemoveHandIndices(ActivePlayer, PendingActiveIndices)) { CancelDeal(); return; }
	for (int32 Index : PendingMarketIndices)
	{
		const int32 Resource = MarketCards[Index];
		MarketCards.RemoveAt(Index);
		const int32 DeckIndex = Revealed.IndexOfByKey(Resource);
		if (DeckIndex != INDEX_NONE) Revealed.RemoveAt(DeckIndex);
	}
	WriteIntArray(Deck, TEXT("RevealedCards"), Revealed);
	UpdateDeckVisual(Deck);
	if (!PlaceCards(PendingTarget, CardsToTarget) || !PlaceCards(ActivePlayer, TargetCards))
	{
		UE_LOG(LogTemp, Error, TEXT("Ore Deal trade transfer failed after preflight; investigate player board slot arrays."));
		CancelDeal();
		return;
	}
	PrivateOfferIndices.Remove(PendingTarget);
	PrivateOfferIndices.Remove(ActivePlayer);
	PendingTarget = nullptr;
	PendingMarketCards.Reset();
	PendingMarketIndices.Reset();
	PendingActiveIndices.Reset();
	bActiveAccepted = false;
	bTargetAccepted = false;
	SelectedTarget = nullptr;
	RefreshPublicOffers();
	if (MarketCards.IsEmpty()) FinishMarket(true);
	else { MarketState = EOreDealMarketState::Trading; ++Revision; ForceNetUpdate(); }
}

void AOreDealMarketBroker::FinishMarket(bool bAdvanceToPlanting)
{
	if (!HasAuthority()) return;
	GetWorldTimerManager().ClearTimer(IntroTimer);
	GetWorldTimerManager().ClearTimer(CountdownTimer);
	AActor* Deck = FindCardDeck();
	if (bAdvanceToPlanting && Deck) AdvanceDeckToPlanting(Deck);
	MarketState = EOreDealMarketState::Waiting;
	ActivePlayer = nullptr;
	SelectedTarget = nullptr;
	PendingTarget = nullptr;
	MarketCards.Reset();
	PendingMarketCards.Reset();
	PendingMarketIndices.Reset();
	PendingActiveIndices.Reset();
	PrivateOfferIndices.Reset();
	PublicOffers.Reset();
	SecondsRemaining = 0;
	bActiveAccepted = false;
	bTargetAccepted = false;
	++Revision;
	ForceNetUpdate();
}

void AOreDealMarketBroker::AdvanceDeckToPlanting(AActor* Deck)
{
	UOreDealTurnBridge::CompleteCurrentTurnStage(Deck);
}

void AOreDealMarketBroker::MakeSnapshot(APlayerState* ForPlayer, FOreDealMarketSnapshot& OutSnapshot) const
{
	OutSnapshot = FOreDealMarketSnapshot();
	OutSnapshot.bVisible = MarketState == EOreDealMarketState::Trading || MarketState == EOreDealMarketState::AwaitingConsent;
	OutSnapshot.bAwaitingConsent = MarketState == EOreDealMarketState::AwaitingConsent;
	OutSnapshot.State = static_cast<int32>(MarketState);
	OutSnapshot.Revision = Revision;
	OutSnapshot.SecondsRemaining = SecondsRemaining;
	OutSnapshot.bActiveAccepted = bActiveAccepted;
	OutSnapshot.bTargetAccepted = bTargetAccepted;
	OutSnapshot.ActivePlayer = ActivePlayer;
	OutSnapshot.SelectedTarget = SelectedTarget;
	OutSnapshot.PendingTarget = PendingTarget;
	OutSnapshot.MarketCards = MarketCards;
	OutSnapshot.PendingMarketCards = PendingMarketCards;
	if (!GetWorld()) return;
	AGameStateBase* GS = GetWorld()->GetGameState();
	if (!GS) return;
	for (APlayerState* Player : GS->PlayerArray)
	{
		FOreDealMarketPlayerView& View = OutSnapshot.Players.AddDefaulted_GetRef();
		View.Player = Player;
		View.Name = Player ? Player->GetPlayerName() : TEXT("Player");
		const FOreDealPublicOffer* Offer = PublicOffers.FindByPredicate([Player](const FOreDealPublicOffer& Candidate) { return Candidate.Player == Player; });
		if (Offer) { View.PublicOffer = Offer->Cards; View.HandCount = Offer->HandCount; }
	}
	(void)ForPlayer;
}
