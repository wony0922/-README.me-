#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SlateWrapperTypes.h"
#include "OreDealPreGameComponent.generated.h"

class UAnimationAsset;
class USceneComponent;
class UWidget;

/**
 * Local-only presentation for the waiting room (before BP_CardDeck.bGameStarted).
 * Hides the deck, wagon boards, merchant stands and the HUD resource bar / phase label,
 * and rests every seated player's left hand on the table like the right hand.
 * Everything is restored as soon as the host starts the game.
 */
UCLASS(ClassGroup=(OreDeal))
class TEAMPROJECT_API UOreDealPreGameComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOreDealPreGameComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FSavedHand
	{
		FTransform RelativeTransform;
		TWeakObjectPtr<UAnimationAsset> Animation;
	};

	TWeakObjectPtr<AActor> CachedDeck;
	TMap<TWeakObjectPtr<UWidget>, ESlateVisibility> SavedWidgetVisibility;
	TMap<TWeakObjectPtr<USceneComponent>, FSavedHand> SavedLeftHands;
	int32 LastAppliedState = INDEX_NONE;

	AActor* FindDeck();
	void ApplyTableActors(bool bWaiting);
	void ApplyHud(bool bWaiting);
	void ApplyHands(bool bWaiting);
};
