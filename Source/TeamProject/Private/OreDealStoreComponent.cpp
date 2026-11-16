#include "OreDealStoreComponent.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "OreDealAuctionBroker.h"
#include "OreDealStoreCatalog.h"
#include "OreDealStoreWidget.h"
#include "OreDealTablePlayerController.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr float WagonRefreshInterval = 0.1f;
	constexpr int32 StoreZOrder = 32500; // above the interaction HUD (32000) and the auction HUD (30500)

	FIntProperty* FindPlayerInt(const APlayerState* Player, FName Name)
	{
		return Player ? FindFProperty<FIntProperty>(Player->GetClass(), Name) : nullptr;
	}
}

UOreDealStoreComponent::UOreDealStoreComponent()
{
	// Ticks only while the store is open on the owning client.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

// ---------------------------------------------------------------------------------------------
// Helpers

APlayerController* UOreDealStoreComponent::GetOwnerController() const
{
	return Cast<APlayerController>(GetOwner());
}

APlayerState* UOreDealStoreComponent::GetOwnerPlayerState() const
{
	const APlayerController* PC = GetOwnerController();
	return PC ? PC->PlayerState.Get() : nullptr;
}

bool UOreDealStoreComponent::IsLocallyOwned() const
{
	const APlayerController* PC = GetOwnerController();
	return PC && PC->IsLocalController();
}

bool UOreDealStoreComponent::IsGameStarted() const
{
	const AActor* Deck = AOreDealAuctionBroker::FindDeck(GetWorld());
	const FBoolProperty* Started = Deck ? FindFProperty<FBoolProperty>(Deck->GetClass(), TEXT("bGameStarted")) : nullptr;
	return Started && Started->GetPropertyValue_InContainer(Deck);
}

int32 UOreDealStoreComponent::ReadGold() const
{
	const APlayerState* Player = GetOwnerPlayerState();
	const FIntProperty* Gold = FindPlayerInt(Player, TEXT("Gold"));
	return Gold ? Gold->GetPropertyValue_InContainer(Player) : 0;
}

// ---------------------------------------------------------------------------------------------
// Local: open / close / input

void UOreDealStoreComponent::ToggleStore()
{
	SetStoreOpen(!bStoreOpen);
}

void UOreDealStoreComponent::SetStoreOpen(bool bOpen)
{
	if (!IsLocallyOwned() || bStoreOpen == bOpen) return;
	if (bOpen)
	{
		// The store only exists once the game is running (there are no wagons or gold before that).
		if (!IsGameStarted()) return;
		if (!Widget)
		{
			Widget = CreateWidget<UOreDealStoreWidget>(GetOwnerController(), UOreDealStoreWidget::StaticClass());
			if (!Widget) return;
			Widget->AddToViewport(StoreZOrder);
			Widget->OnBuyRequested.BindUObject(this, &UOreDealStoreComponent::RequestBuy);
			Widget->OnSellRequested.BindUObject(this, &UOreDealStoreComponent::RequestSell);
			Widget->OnCloseRequested.BindUObject(this, &UOreDealStoreComponent::RequestClose);
		}
	}
	bStoreOpen = bOpen;
	if (Widget)
	{
		if (bOpen) PushState(true);
		// Closing drops any purchase that was waiting for confirmation.
		Widget->SetStoreOpen(bOpen);
	}
	SetComponentTickEnabled(bOpen);
	if (AOreDealTablePlayerController* TablePC = Cast<AOreDealTablePlayerController>(GetOwnerController()))
		TablePC->OnStoreOpenChanged(bOpen);
}

bool UOreDealStoreComponent::HandleInputKey(const FKey& Key, EInputEvent Event)
{
	if (!IsLocallyOwned() || Key.IsMouseButton() || Key.IsGamepadKey()) return false;
	const bool bPressed = Event == IE_Pressed;
	if (!bStoreOpen)
	{
		if (Key != EKeys::F) return false;
		if (bPressed) SetStoreOpen(true);
		return true;
	}
	// Key releases pass through so nothing that was held before opening stays stuck down.
	if (Event == IE_Released) return false;
	if (bPressed && Key == EKeys::F) SetStoreOpen(false);
	else if (Widget && (bPressed || Event == IE_Repeat)) Widget->HandleKey(Key, !bPressed);
	// The store is modal: no other keyboard input reaches the table while it is open.
	return true;
}

void UOreDealStoreComponent::RequestClose()
{
	SetStoreOpen(false);
}

void UOreDealStoreComponent::RequestBuy(FName ItemId)
{
	ServerBuyItem(ItemId);
	WagonRefreshElapsed = WagonRefreshInterval; // refresh right away on a listen-server host
}

void UOreDealStoreComponent::RequestSell(int32 WagonIndex)
{
	if (AOreDealTablePlayerController* TablePC = Cast<AOreDealTablePlayerController>(GetOwnerController()))
		TablePC->RequestSellWagon(WagonIndex);
	WagonRefreshElapsed = WagonRefreshInterval;
}

void UOreDealStoreComponent::PushState(bool bRefreshWagons)
{
	if (!Widget) return;
	const int32 Gold = ReadGold();
	if (!bRefreshWagons && Gold == LastPushedGold) return;
	LastPushedGold = Gold;
	FOreDealStoreState State;
	State.Gold = Gold;
	if (const AOreDealTablePlayerController* TablePC = Cast<AOreDealTablePlayerController>(GetOwnerController()))
		TablePC->GetStoreWagons(State.Wagons);
	Widget->ApplyState(State);
}

void UOreDealStoreComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bStoreOpen) { SetComponentTickEnabled(false); return; }
	// Gold is checked every frame (one int read) so spending/earning shows immediately; the wagons
	// are re-read a few times a second.
	WagonRefreshElapsed += DeltaTime;
	const bool bRefreshWagons = WagonRefreshElapsed >= WagonRefreshInterval;
	if (bRefreshWagons)
	{
		WagonRefreshElapsed = 0.0f;
		if (!IsGameStarted()) { SetStoreOpen(false); return; }
	}
	PushState(bRefreshWagons);
}

void UOreDealStoreComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Widget) Widget->RemoveFromParent();
	Widget = nullptr;
	bStoreOpen = false;
	Super::EndPlay(EndPlayReason);
}

void UOreDealStoreComponent::ClientStoreNotice_Implementation(const FString& Message, bool bSuccess)
{
	if (Widget && bStoreOpen) Widget->ShowNotice(Message, bSuccess);
}

// ---------------------------------------------------------------------------------------------
// Server: purchases

void UOreDealStoreComponent::ServerBuyItem_Implementation(FName ItemId)
{
	APlayerState* Player = GetOwnerPlayerState();
	const FOreDealStoreItem* Item = OreDealStore::FindItem(ItemId);
	if (!Player || !Item || Item->Cost < 0 || Item->Amount <= 0) return;
	if (!IsGameStarted())
	{
		ClientStoreNotice(TEXT("게임이 시작된 뒤에 구매할 수 있습니다"), false);
		return;
	}
	FIntProperty* GoldProperty = FindPlayerInt(Player, TEXT("Gold"));
	if (!GoldProperty) return;
	const int32 Gold = GoldProperty->GetPropertyValue_InContainer(Player);
	if (Gold < Item->Cost)
	{
		ClientStoreNotice(TEXT("골드가 부족합니다"), false);
		return;
	}
	// Hand the item over first; gold is only taken when that worked.
	if (!GrantItem(*Item, Player)) return;
	GoldProperty->SetPropertyValue_InContainer(Player, Gold - Item->Cost);
	Player->ForceNetUpdate();
	ClientStoreNotice(FString::Printf(TEXT("%s 구매 완료  (-%dg)"), *Item->Name, Item->Cost), true);
}

bool UOreDealStoreComponent::GrantItem(const FOreDealStoreItem& Item, APlayerState* Player) const
{
	switch (Item.Grant)
	{
	case EOreDealStoreGrant::PlayerStateInt:
	{
		FIntProperty* Property = FindPlayerInt(Player, Item.GrantProperty);
		if (!Property) return false;
		Property->SetPropertyValue_InContainer(Player, Property->GetPropertyValue_InContainer(Player) + Item.Amount);
		return true;
	}
	default:
		return false;
	}
}
