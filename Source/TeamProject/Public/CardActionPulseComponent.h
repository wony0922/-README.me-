#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CardActionPulseComponent.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** Plays a short expanding, fading outline when one of an actor's *Glow components is shown. */
UCLASS(ClassGroup=(OreDeal), meta=(BlueprintSpawnableComponent))
class TEAMPROJECT_API UCardActionPulseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCardActionPulseComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FPulseTarget
	{
		TWeakObjectPtr<UStaticMeshComponent> Component;
		FVector BaseScale = FVector::OneVector;
		TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
		float Elapsed = 0.0f;
		bool bWasVisible = false;
		bool bAnimating = false;
	};

	TArray<FPulseTarget> Targets;

	static constexpr float PulseDuration = 0.2f;
	static constexpr float PulseScale = 1.18f;
	static const FName PulseOpacityParameter;
	void ResetTarget(FPulseTarget& Target, bool bHide);
};
