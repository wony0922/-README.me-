#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OreDealTurnBridge.generated.h"

/** Server-side bridge for advancing the legacy table game's four turn stages. */
UCLASS()
class TEAMPROJECT_API UOreDealTurnBridge : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Advances the replicated TurnPhaseIndex on the card deck. Returns true only
	 *  when stage four was completed and the caller may advance the active player.
	 */
	UFUNCTION(BlueprintCallable, Category="Ore Deal|Turn")
	static bool CompleteCurrentTurnStage(AActor* CardDeck);

	UFUNCTION(BlueprintPure, Category="Ore Deal|Turn")
	static bool CanPlaceInCurrentTurnStage(AActor* CardDeck, APlayerState* Player, bool bFromReveal);

	UFUNCTION(BlueprintCallable, Category="Ore Deal|Turn")
	static void RecordMandatoryHandPlacement(AActor* CardDeck, APlayerState* Player);

	/** Returns the localized HUD prompt for the current legacy turn stage. */
	UFUNCTION(BlueprintPure, Category="Ore Deal|Turn")
	static FText GetTurnStageLabel(AActor* CardDeck);
};
