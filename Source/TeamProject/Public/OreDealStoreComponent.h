#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "OreDealStoreComponent.generated.h"

class APlayerController;
class APlayerState;
class UOreDealStoreWidget;
struct FOreDealStoreItem;

/**
 * Lives on every AOreDealTablePlayerController. On the owning client it opens the store window with F,
 * feeds it the player's gold and wagons, and routes its input. On the server it validates purchases.
 * Selling reuses the controller's existing wagon sale request.
 */
UCLASS(ClassGroup=(OreDeal))
class TEAMPROJECT_API UOreDealStoreComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOreDealStoreComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Returns true when the store consumed the key (the controller must then stop processing it). */
	bool HandleInputKey(const FKey& Key, EInputEvent Event);
	bool IsStoreOpen() const { return bStoreOpen; }
	void ToggleStore();
	/** Closing always cancels whatever was in progress (e.g. a purchase waiting for confirmation). */
	void SetStoreOpen(bool bOpen);

	UFUNCTION(Server, Reliable) void ServerBuyItem(FName ItemId);
	UFUNCTION(Client, Reliable) void ClientStoreNotice(const FString& Message, bool bSuccess);

private:
	UPROPERTY(Transient) TObjectPtr<UOreDealStoreWidget> Widget;
	bool bStoreOpen = false;
	float WagonRefreshElapsed = 0.0f;
	int32 LastPushedGold = MIN_int32;

	APlayerController* GetOwnerController() const;
	APlayerState* GetOwnerPlayerState() const;
	bool IsLocallyOwned() const;
	bool IsGameStarted() const;
	int32 ReadGold() const;
	void PushState(bool bRefreshWagons);
	void RequestBuy(FName ItemId);
	void RequestSell(int32 WagonIndex);
	void RequestClose();
	bool GrantItem(const FOreDealStoreItem& Item, APlayerState* Player) const;
};
