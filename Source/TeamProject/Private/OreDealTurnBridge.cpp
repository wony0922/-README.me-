#include "OreDealTurnBridge.h"

#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
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
	case 0: return FText::FromString(TEXT("▶ 내 턴   ·   1/4 광산 매립 (맨 앞 1장 필수, 이후 추가 등록 자유)   ·   E : 단계 확정"));
	case 1: return FText::FromString(TEXT("▶ 내 턴   ·   2/4 장터 거래   ·   E : 다음 단계"));
	case 2: return FText::FromString(TEXT("▶ 내 턴   ·   3/4 광물 적재   ·   E : 다음 단계"));
	case 3: return FText::FromString(TEXT("▶ 내 턴   ·   4/4 보급 수령   ·   E : 턴 종료"));
	default: return FText::FromString(TEXT("▶ 내 턴   ·   E : 단계 진행"));
	}
}
