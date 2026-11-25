#include "OreDealTurnBridge.h"

#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
#include "OreDealAuctionBroker.h"
#include "OreDealTablePlayerController.h"
#include "UObject/UnrealType.h"

namespace
{
	FIntProperty* GetPhaseProperty(AActor* CardDeck)
	{
		return IsValid(CardDeck) ? FindFProperty<FIntProperty>(CardDeck->GetClass(), TEXT("TurnPhaseIndex")) : nullptr;
	}

	bool IsCurrentPlayer(AActor* CardDeck, APlayerState* Player)
	{
		if (!IsValid(CardDeck) || !IsValid(Player)) return false;
		const FObjectPropertyBase* TurnPlayerProperty = FindFProperty<FObjectPropertyBase>(CardDeck->GetClass(), TEXT("TurnPS"));
		return TurnPlayerProperty && TurnPlayerProperty->GetObjectPropertyValue_InContainer(CardDeck) == Player;
	}

	bool CanPlantRepeatedResource(APlayerState* Player)
	{
		APawn* Pawn = IsValid(Player) ? Player->GetPawn() : nullptr;
		if (!IsValid(Pawn) || !Pawn->GetWorld()) return false;

		const FArrayProperty* HandProperty = FindFProperty<FArrayProperty>(Pawn->GetClass(), TEXT("HandCards"));
		const FIntProperty* HandElementProperty = HandProperty ? CastField<FIntProperty>(HandProperty->Inner) : nullptr;
		if (!HandProperty || !HandElementProperty) return false;
		FScriptArrayHelper Hand(HandProperty, HandProperty->ContainerPtrToValuePtr<void>(Pawn));
		if (Hand.Num() <= 0) return false;
		const int32 FrontResource = HandElementProperty->GetPropertyValue(Hand.GetRawPtr(0));

		UClass* BoardClass = StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/Game/MaterialTraders/Blueprints/BP_CardBoard.BP_CardBoard_C"));
		if (!BoardClass) return false;
		AActor* NearestBoard = nullptr;
		double NearestDistanceSq = TNumericLimits<double>::Max();
		for (TActorIterator<AActor> It(Pawn->GetWorld(), BoardClass); It; ++It)
		{
			const double DistanceSq = FVector::DistSquared(Pawn->GetActorLocation(), It->GetActorLocation());
			if (DistanceSq < NearestDistanceSq) { NearestDistanceSq = DistanceSq; NearestBoard = *It; }
		}
		if (!IsValid(NearestBoard)) return false;

		const FArrayProperty* TypeProperty = FindFProperty<FArrayProperty>(NearestBoard->GetClass(), TEXT("wagonTypes"));
		const FArrayProperty* CountProperty = FindFProperty<FArrayProperty>(NearestBoard->GetClass(), TEXT("wagonCounts"));
		const FIntProperty* TypeElementProperty = TypeProperty ? CastField<FIntProperty>(TypeProperty->Inner) : nullptr;
		const FIntProperty* CountElementProperty = CountProperty ? CastField<FIntProperty>(CountProperty->Inner) : nullptr;
		if (!TypeProperty || !CountProperty || !TypeElementProperty || !CountElementProperty) return false;
		FScriptArrayHelper Types(TypeProperty, TypeProperty->ContainerPtrToValuePtr<void>(NearestBoard));
		FScriptArrayHelper Counts(CountProperty, CountProperty->ContainerPtrToValuePtr<void>(NearestBoard));
		const int32 Slots = FMath::Min(Types.Num(), Counts.Num());
		int32 SoleResource = INDEX_NONE;
		bool bHasOccupiedWagon = false;
		for (int32 Index = 0; Index < Slots; ++Index)
		{
			if (CountElementProperty->GetPropertyValue(Counts.GetRawPtr(Index)) <= 0) continue;
			const int32 Resource = TypeElementProperty->GetPropertyValue(Types.GetRawPtr(Index));
			if (!bHasOccupiedWagon) { SoleResource = Resource; bHasOccupiedWagon = true; }
			else if (SoleResource != Resource) return false;
		}
		return bHasOccupiedWagon && SoleResource == FrontResource;
	}
}

bool UOreDealTurnBridge::CompleteCurrentTurnStage(AActor* CardDeck)
{
	if (!IsValid(CardDeck) || !CardDeck->HasAuthority())
	{
		return false;
	}

	FIntProperty* PhaseProperty = GetPhaseProperty(CardDeck);
	if (!PhaseProperty)
	{
		return false;
	}

	int32 PhaseIndex = PhaseProperty->GetPropertyValue_InContainer(CardDeck);
	if (PhaseIndex < 0 || PhaseIndex > 3)
	{
		PhaseProperty->SetPropertyValue_InContainer(CardDeck, 0);
		CardDeck->ForceNetUpdate();
		return false;
	}

	if (PhaseIndex == 0)
	{
		FIntProperty* PlacementCountProperty = FindFProperty<FIntProperty>(CardDeck->GetClass(), TEXT("TurnPhaseActionCount"));
		if (!PlacementCountProperty || PlacementCountProperty->GetPropertyValue_InContainer(CardDeck) < 1)
		{
			return false;
		}
		PlacementCountProperty->SetPropertyValue_InContainer(CardDeck, 0);
	}

	if (PhaseIndex < 3)
	{
		PhaseProperty->SetPropertyValue_InContainer(CardDeck, PhaseIndex + 1);
		CardDeck->ForceNetUpdate();
		return false;
	}

	PhaseProperty->SetPropertyValue_InContainer(CardDeck, 0);
	CardDeck->ForceNetUpdate();
	return true;
}

bool UOreDealTurnBridge::CanPlaceInCurrentTurnStage(AActor* CardDeck, APlayerState* Player, bool bFromReveal)
{
	const FIntProperty* PhaseProperty = GetPhaseProperty(CardDeck);
	if (!IsValid(CardDeck) || !CardDeck->HasAuthority() || !PhaseProperty || !IsCurrentPlayer(CardDeck, Player)) return false;
	const int32 PhaseIndex = PhaseProperty->GetPropertyValue_InContainer(CardDeck);
	if (PhaseIndex == 0 && !bFromReveal)
	{
		const FIntProperty* CountProperty = FindFProperty<FIntProperty>(CardDeck->GetClass(), TEXT("TurnPhaseActionCount"));
		if (!CountProperty) return false;
		return true;
	}
	return PhaseIndex == 2 && bFromReveal;
}

void UOreDealTurnBridge::RecordMandatoryHandPlacement(AActor* CardDeck, APlayerState* Player)
{
	if (!IsValid(CardDeck) || !IsValid(Player)) return;
	if (!CardDeck->HasAuthority())
	{
		UWorld* World = CardDeck->GetWorld();
		AOreDealTablePlayerController* LocalPC = World ? Cast<AOreDealTablePlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (LocalPC && LocalPC->IsLocalController()) LocalPC->ServerRecordMandatoryHandPlacement(CardDeck, Player);
		return;
	}
	FIntProperty* PhaseProperty = GetPhaseProperty(CardDeck);
	if (!PhaseProperty || !IsCurrentPlayer(CardDeck, Player)) return;
	if (PhaseProperty->GetPropertyValue_InContainer(CardDeck) != 0) return;
	FIntProperty* CountProperty = FindFProperty<FIntProperty>(CardDeck->GetClass(), TEXT("TurnPhaseActionCount"));
	if (!CountProperty) return;
	CountProperty->SetPropertyValue_InContainer(CardDeck, CountProperty->GetPropertyValue_InContainer(CardDeck) + 1);
	CardDeck->ForceNetUpdate();
}

FText UOreDealTurnBridge::GetTurnStageLabel(AActor* CardDeck)
{
	if (!IsValid(CardDeck))
	{
		return FText::GetEmpty();
	}

	const FIntProperty* PhaseProperty = GetPhaseProperty(CardDeck);
	if (!PhaseProperty)
	{
		return FText::GetEmpty();
	}

	switch (PhaseProperty->GetPropertyValue_InContainer(CardDeck))
	{
	// Short two-line key guide shown under the wood/gold bar (WBP_HUD TxtTurn).
	case 0: return FText::FromString(TEXT("▶ 내 턴 (1/4 매립)\nA/D 마차 · Space 배치 · E 확정"));
	case 1: return FText::FromString(TEXT("▶ 내 턴 (2/4 경매)\nA/D 이동 · C 선택 · Space 결정"));
	case 2: return FText::FromString(TEXT("▶ 내 턴 (3/4 배치)\nA/D 마차 · Space 배치"));
	case 3: return FText::FromString(TEXT("▶ 내 턴 (4/4 보급)\nW 드로우 · E 턴 종료"));
	default: return FText::FromString(TEXT("▶ 내 턴\nE 단계 진행"));
	}
}

namespace
{
	bool ReadWagonArrays(AActor* Board, TArray<int32>& OutTypes, TArray<int32>& OutCounts)
	{
		if (!IsValid(Board)) return false;
		const FArrayProperty* TypeProp = FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonTypes"));
		const FArrayProperty* CountProp = FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonCounts"));
		const FIntProperty* TypeInner = TypeProp ? CastField<FIntProperty>(TypeProp->Inner) : nullptr;
		const FIntProperty* CountInner = CountProp ? CastField<FIntProperty>(CountProp->Inner) : nullptr;
		if (!TypeProp || !CountProp || !TypeInner || !CountInner) return false;

		FScriptArrayHelper TypeHelper(TypeProp, TypeProp->ContainerPtrToValuePtr<void>(Board));
		FScriptArrayHelper CountHelper(CountProp, CountProp->ContainerPtrToValuePtr<void>(Board));

		OutTypes.SetNum(TypeHelper.Num());
		for (int32 I = 0; I < TypeHelper.Num(); ++I)
			OutTypes[I] = TypeInner->GetPropertyValue(TypeHelper.GetRawPtr(I));

		OutCounts.SetNum(CountHelper.Num());
		for (int32 I = 0; I < CountHelper.Num(); ++I)
			OutCounts[I] = CountInner->GetPropertyValue(CountHelper.GetRawPtr(I));

		return true;
	}

	bool WriteWagonArrays(AActor* Board, const TArray<int32>& InTypes, const TArray<int32>& InCounts)
	{
		if (!IsValid(Board)) return false;
		const FArrayProperty* TypeProp = FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonTypes"));
		const FArrayProperty* CountProp = FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonCounts"));
		const FIntProperty* TypeInner = TypeProp ? CastField<FIntProperty>(TypeProp->Inner) : nullptr;
		const FIntProperty* CountInner = CountProp ? CastField<FIntProperty>(CountProp->Inner) : nullptr;
		if (!TypeProp || !CountProp || !TypeInner || !CountInner) return false;

		FScriptArrayHelper TypeHelper(TypeProp, TypeProp->ContainerPtrToValuePtr<void>(Board));
		FScriptArrayHelper CountHelper(CountProp, CountProp->ContainerPtrToValuePtr<void>(Board));

		for (int32 I = 0; I < InTypes.Num() && I < TypeHelper.Num(); ++I)
			TypeInner->SetPropertyValue(TypeHelper.GetRawPtr(I), InTypes[I]);

		for (int32 I = 0; I < InCounts.Num() && I < CountHelper.Num(); ++I)
			CountInner->SetPropertyValue(CountHelper.GetRawPtr(I), InCounts[I]);

		return true;
	}

	bool CanSellSingleCardSlot(const TArray<int32>& Counts, int32 WagonIndex)
	{
		if (!Counts.IsValidIndex(WagonIndex) || Counts[WagonIndex] != 1) return false;
		int32 OccupiedWagons = 0;
		for (int32 I = 0; I < Counts.Num(); ++I)
		{
			const int32 Count = Counts[I];
			if (Count <= 0) continue;
			if (Count != 1) return false;
			++OccupiedWagons;
		}
		return OccupiedWagons >= 2;
	}

	void AddPlayerGold(APlayerState* Player, int32 Amount)
	{
		if (!IsValid(Player) || Amount <= 0) return;
		if (FIntProperty* GoldProp = FindFProperty<FIntProperty>(Player->GetClass(), TEXT("gold")))
		{
			GoldProp->SetPropertyValue_InContainer(Player, GoldProp->GetPropertyValue_InContainer(Player) + Amount);
			Player->ForceNetUpdate();
		}
	}

	void BroadcastSaleToast(UWorld* World, int32 Resource, int32 Count)
	{
		if (!World || Resource < 0 || Resource >= 8) return;
		static const TCHAR* ResourceNames[] = {
			TEXT("Rock"), TEXT("Coal"), TEXT("Copper"), TEXT("Iron"),
			TEXT("Silver"), TEXT("Gold"), TEXT("Diamond"), TEXT("Adamantium")
		};
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(It->Get()))
			{
				PC->ClientShowSaleToast(ResourceNames[Resource], Count);
			}
		}
	}
}

bool UOreDealTurnBridge::ExecutePlantCard(AActor* Board, APlayerState* Player, int32 Resource, int32 DesiredWagonIndex, int32& OutPlacedWagonIndex)
{
	OutPlacedWagonIndex = INDEX_NONE;
	if (!IsValid(Board) || !IsValid(Player) || Resource < 0 || Resource >= 8) return false;

	TArray<int32> Types, Counts;
	if (!ReadWagonArrays(Board, Types, Counts)) return false;
	const int32 NumSlots = FMath::Min(Types.Num(), Counts.Num());
	if (NumSlots <= 0) return false;

	// 규칙 1: 패에 있는 카드와 슬롯 위에 있는 카드가 일치하면 알아서 똑같은 카드의 슬롯으로 자동 지정
	for (int32 I = 0; I < NumSlots; ++I)
	{
		if (Counts[I] > 0 && Types[I] == Resource)
		{
			Counts[I] += 1;
			WriteWagonArrays(Board, Types, Counts);
			OutPlacedWagonIndex = I;
			if (UFunction* UpdateVisual = Board->FindFunction(TEXT("UpdateVisual")))
			{
				Board->ProcessEvent(UpdateVisual, nullptr);
			}
			Board->ForceNetUpdate();
			Player->ForceNetUpdate();
			return true;
		}
	}

	// 규칙 2: 동일 카드가 없는 경우, 지정 슬롯 또는 빈 슬롯으로 배치
	DesiredWagonIndex = FMath::Clamp(DesiredWagonIndex, 0, NumSlots - 1);
	int32 TargetSlot = DesiredWagonIndex;

	if (Counts[DesiredWagonIndex] > 0)
	{
		// 규칙 3: 자원 낭비 보호제 (1장 보호 규칙)
		const int32 OldCount = Counts[DesiredWagonIndex];
		if (OldCount == 1 && !CanSellSingleCardSlot(Counts, DesiredWagonIndex))
		{
			// 목표 슬롯이 1장 보호 대상임. 빈 슬롯이 있으면 빈 슬롯으로 자동 우회하여 자원 낭비 방지
			int32 EmptySlot = INDEX_NONE;
			for (int32 I = 0; I < NumSlots; ++I)
			{
				if (Counts[I] <= 0)
				{
					EmptySlot = I;
					break;
				}
			}

			if (EmptySlot != INDEX_NONE)
			{
				TargetSlot = EmptySlot;
			}
			else
			{
				// 빈 슬롯도 없고, 해당 마차가 보호 대상이면 덮어쓰기/파쇄 불가
				if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(Player->GetPlayerController()))
				{
					PC->ClientReceiveChat(TEXT("보호"), TEXT("자원 낭비 보호: 1장만 실린 마차는 다른 마차가 2장 이상일 때 버리거나 팔 수 없습니다."));
				}
				return false;
			}
		}
	}

	const int32 OldResource = (TargetSlot < Types.Num()) ? Types[TargetSlot] : INDEX_NONE;
	const int32 OldCount = (TargetSlot < Counts.Num()) ? Counts[TargetSlot] : 0;

	// 기존 슬롯에 다른 카드가 있던 경우: 판매 또는 광물 파쇄 정산 및 나이아가라 이펙트
	if (OldCount > 0 && OldResource >= 0 && OldResource != Resource)
	{
		const int32 Reward = AOreDealTablePlayerController::CalculateSaleReward(OldResource, OldCount, Board);
		if (Reward > 0)
		{
			AddPlayerGold(Player, Reward);
			AOreDealTablePlayerController::BroadcastWagonEffect(Board->GetWorld(), Board, TargetSlot, EOreDealSlotEffect::CoinBurst);
			BroadcastSaleToast(Board->GetWorld(), OldResource, OldCount);
		}
		else
		{
			// 최소 개수 미충족: 0G 광물 파쇄
			AOreDealTablePlayerController::BroadcastWagonEffect(Board->GetWorld(), Board, TargetSlot, EOreDealSlotEffect::MineralShatter);
		}

		if (AOreDealAuctionBroker* Broker = AOreDealAuctionBroker::Find(Board))
		{
			Broker->AddToDiscardPile(OldResource, OldCount);
		}
	}

	// 새 카드 적재
	Types[TargetSlot] = Resource;
	Counts[TargetSlot] = 1;

	WriteWagonArrays(Board, Types, Counts);
	OutPlacedWagonIndex = TargetSlot;

	if (UFunction* UpdateVisual = Board->FindFunction(TEXT("UpdateVisual")))
	{
		Board->ProcessEvent(UpdateVisual, nullptr);
	}
	Board->ForceNetUpdate();
	Player->ForceNetUpdate();
	return true;
}
