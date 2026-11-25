#include "OreDealWagonStateComponent.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UOreDealWagonStateComponent::UOreDealWagonStateComponent()
{
	SetIsReplicatedByDefault(true);
}

void UOreDealWagonStateComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UOreDealWagonStateComponent, BrokenSlots);
}

void UOreDealWagonStateComponent::EnsureSlotCount(int32 Count)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	Count = FMath::Clamp(Count, 1, 3);
	if (BrokenSlots.Num() == Count) return;
	BrokenSlots.SetNum(Count);
	GetOwner()->ForceNetUpdate();
}

bool UOreDealWagonStateComponent::IsBroken(int32 Index) const
{
	return BrokenSlots.IsValidIndex(Index) && BrokenSlots[Index] != 0;
}

bool UOreDealWagonStateComponent::SetBroken(int32 Index, bool bBroken)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !BrokenSlots.IsValidIndex(Index)) return false;
	BrokenSlots[Index] = bBroken ? 1 : 0;
	GetOwner()->ForceNetUpdate();
	return true;
}

UOreDealWagonStateComponent* UOreDealWagonStateComponent::FindOrAdd(AActor* Board, int32 SlotCount)
{
	if (!IsValid(Board)) return nullptr;
	if (UOreDealWagonStateComponent* Existing = Board->FindComponentByClass<UOreDealWagonStateComponent>())
	{
		Existing->EnsureSlotCount(SlotCount);
		return Existing;
	}
	if (!Board->HasAuthority()) return nullptr;
	UOreDealWagonStateComponent* State = NewObject<UOreDealWagonStateComponent>(Board, TEXT("OreDealWagonState"));
	if (!State) return nullptr;
	Board->AddInstanceComponent(State);
	State->RegisterComponent();
	State->SetIsReplicated(true);
	State->EnsureSlotCount(SlotCount);
	return State;
}

void UOreDealWagonStateComponent::OnRep_BrokenSlots()
{
	if (AActor* OwnerActor = GetOwner()) OwnerActor->ForceNetUpdate();
}
