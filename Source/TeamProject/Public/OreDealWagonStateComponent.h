#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OreDealWagonStateComponent.generated.h"

UCLASS(ClassGroup=(OreDeal), meta=(BlueprintSpawnableComponent))
class TEAMPROJECT_API UOreDealWagonStateComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UOreDealWagonStateComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void EnsureSlotCount(int32 Count);
	bool IsBroken(int32 Index) const;
	bool SetBroken(int32 Index, bool bBroken);
	static UOreDealWagonStateComponent* FindOrAdd(AActor* Board, int32 SlotCount);

private:
	UPROPERTY(ReplicatedUsing=OnRep_BrokenSlots) TArray<uint8> BrokenSlots;
	UFUNCTION() void OnRep_BrokenSlots();
};
