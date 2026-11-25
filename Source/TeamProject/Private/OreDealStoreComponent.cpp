#include "OreDealStoreComponent.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "OreDealAuctionBroker.h"
#include "OreDealWagonStateComponent.h"
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

int32 UOreDealStoreComponent::ReadWood() const
{
	const APlayerState* Player = GetOwnerPlayerState();
	const FIntProperty* Wood = FindPlayerInt(Player, TEXT("Wood"));
	return Wood ? Wood->GetPropertyValue_InContainer(Player) : 0;
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
			const AOreDealTablePlayerController* TablePC = Cast<AOreDealTablePlayerController>(GetOwnerController());
			TSubclassOf<UOreDealStoreWidget> WidgetClass = UOreDealStoreWidget::StaticClass();
			if (TablePC) WidgetClass = TablePC->GetStoreWidgetClass();
			Widget = CreateWidget<UOreDealStoreWidget>(GetOwnerController(), WidgetClass);
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

void UOreDealStoreComponent::RequestBuy(FName ItemId, APlayerState* Target, int32 WagonIndex)
{
	ServerBuyItem(ItemId, Target, WagonIndex);
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
	const int32 Wood = ReadWood();
	if (!bRefreshWagons && Gold == LastPushedGold && Wood == LastPushedWood) return;
	LastPushedGold = Gold;
	LastPushedWood = Wood;
	FOreDealStoreState State;
	State.Gold = Gold;
	State.Wood = Wood;
	if (const AOreDealTablePlayerController* TablePC = Cast<AOreDealTablePlayerController>(GetOwnerController()))
		TablePC->GetStoreWagons(State.Wagons);
	if (UWorld* World = GetWorld())
		if (AGameStateBase* GS = World->GetGameState())
			for (APlayerState* Other : GS->PlayerArray)
				if (Other && Other != GetOwnerPlayerState())
				{
					State.OtherPlayers.Add(Other);
					int32 Count = 2;
					if (AActor* Board = AOreDealAuctionBroker::FindBoardFor(World, Other->GetPawn()))
						if (const FArrayProperty* TypesProp = FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonTypes")))
							Count = FMath::Clamp(FScriptArrayHelper(TypesProp, TypesProp->ContainerPtrToValuePtr<void>(Board)).Num(), 1, 3);
					State.OtherPlayerWagonCounts.Add(Count);
				}
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

void UOreDealStoreComponent::ServerBuyItem_Implementation(FName ItemId, APlayerState* Target, int32 WagonIndex)
{
	APlayerState* Player = GetOwnerPlayerState();
	AOreDealTablePlayerController* TablePC = Cast<AOreDealTablePlayerController>(GetOwnerController());
	if (!Player || !TablePC || !TablePC->HasAuthority() || !IsGameStarted()) return;
	const FOreDealStoreItem* Item = TablePC->FindStoreItem(ItemId);
	if (!Item || Item->Cost < 0 || Item->Amount <= 0) return;
	FIntProperty* CostProperty = FindPlayerInt(Player, Item->Currency == EOreDealStoreCurrency::Wood ? TEXT("Wood") : TEXT("Gold"));
	if (!CostProperty || CostProperty->GetPropertyValue_InContainer(Player) < Item->Cost)
	{
		ClientStoreNotice(Item->Currency == EOreDealStoreCurrency::Wood ? TEXT("나무가 부족합니다") : TEXT("골드가 부족합니다"), false);
		return;
	}
	const int32 Balance = CostProperty->GetPropertyValue_InContainer(Player);
	const bool bOil = ItemId == TEXT("oil");
	const bool bRepair = ItemId == TEXT("fix");
	UOreDealWagonStateComponent* WagonState = nullptr;
	if (bOil || bRepair)
	{
		if (bOil && (!Target || Target == Player || !GetWorld()->GetGameState() || !GetWorld()->GetGameState()->PlayerArray.Contains(Target))) return;
		APlayerState* StateOwner = bOil ? Target : Player;
		AActor* Board = AOreDealAuctionBroker::FindBoardFor(GetWorld(), StateOwner->GetPawn());
		const FArrayProperty* TypesProp = Board ? FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonTypes")) : nullptr;
		const int32 SlotCount = TypesProp ? FScriptArrayHelper(TypesProp, TypesProp->ContainerPtrToValuePtr<void>(Board)).Num() : 0;
		WagonState = UOreDealWagonStateComponent::FindOrAdd(Board, SlotCount);
		if (!WagonState || WagonIndex < 0 || WagonIndex >= SlotCount || WagonState->IsBroken(WagonIndex) == bOil) return;
	}
	if (ItemId == TEXT("WagonSlotUpgrade") && !TablePC->CanGrantStoreItem(TEXT("WagonSlotUpgrade"), 1))
	{
		ClientStoreNotice(TEXT("마차 슬롯 증가는 플레이어당 한 번만 구매할 수 있습니다"), false);
		return;
	}
	if (bOil)
	{
		CostProperty->SetPropertyValue_InContainer(Player, Balance - Item->Cost);
		if (FMath::FRand() < 0.70f)
		{
			WagonState->SetBroken(WagonIndex, true);
			ClientStoreNotice(FString::Printf(TEXT("%s의 마차 %d번을 고장냈습니다"), *Target->GetPlayerName(), WagonIndex + 1), true);
		}
		else
		{
			FIntProperty* AttackerWood = FindPlayerInt(Player, TEXT("Wood"));
			FIntProperty* DefenderWood = FindPlayerInt(Target, TEXT("Wood"));
			if (AttackerWood) AttackerWood->SetPropertyValue_InContainer(Player, FMath::Max(-2, AttackerWood->GetPropertyValue_InContainer(Player) - 2));
			if (DefenderWood) DefenderWood->SetPropertyValue_InContainer(Target, DefenderWood->GetPropertyValue_InContainer(Target) + 2);
			Target->ForceNetUpdate();
			ClientStoreNotice(TEXT("들켰습니다. 나무 2개를 빼앗겼습니다"), false);
		}
	}
	else if (bRepair)
	{
		WagonState->SetBroken(WagonIndex, false);
		CostProperty->SetPropertyValue_InContainer(Player, Balance - Item->Cost);
		ClientStoreNotice(FString::Printf(TEXT("마차 %d번을 수리했습니다"), WagonIndex + 1), true);
	}
	else
	{
		if (!GrantItem(*Item, Player)) return;
		CostProperty->SetPropertyValue_InContainer(Player, Balance - Item->Cost);
		ClientStoreNotice(FString::Printf(TEXT("%s 구매 완료"), *Item->Name), true);
	}
	Player->ForceNetUpdate();
}

bool UOreDealStoreComponent::GrantItem(const FOreDealStoreItem& Item, APlayerState* Player) const
{
	AOreDealTablePlayerController* TablePC = Cast<AOreDealTablePlayerController>(GetOwnerController());
	if (Item.Id == TEXT("wood"))
	{
		FIntProperty* Wood = FindPlayerInt(Player, TEXT("Wood"));
		if (!Wood) return false;
		Wood->SetPropertyValue_InContainer(Player, Wood->GetPropertyValue_InContainer(Player) + Item.Amount);
		return true;
	}
	if (Item.Id == TEXT("AIglasses")) return TablePC && TablePC->ApplyAIGlasses(Player);
	if (Item.Id == TEXT("axe")) return TablePC && TablePC->ApplyWoodAxe(Player);
	if (Item.Id == TEXT("bottle")) return TablePC && TablePC->ApplySpawnBottles(Player);
	if (Item.Id == TEXT("ChaosCard")) return TablePC && TablePC->ApplyChaosCard(Player);
	if (Item.Id == TEXT("WagonSlotUpgrade")) return TablePC && TablePC->GrantStoreWagonSlotUpgrade();

	switch (Item.Grant)
	{
	case EOreDealStoreGrant::PlayerStateInt:
	{
		FIntProperty* Property = FindPlayerInt(Player, Item.GrantProperty);
		if (!Property) return false;
		Property->SetPropertyValue_InContainer(Player, Property->GetPropertyValue_InContainer(Player) + Item.Amount);
		return true;
	}
	case EOreDealStoreGrant::WagonSlotUpgrade:
		if (Item.GrantProperty != TEXT("WagonSlotUpgrade") || Item.Amount != 1) return false;
		if (TablePC)
			return TablePC->GrantStoreWagonSlotUpgrade();
		return false;
	case EOreDealStoreGrant::AIGlasses:
		if (TablePC)
			return TablePC->ApplyAIGlasses(Player);
		return false;
	case EOreDealStoreGrant::WoodAxe:
		if (TablePC)
			return TablePC->ApplyWoodAxe(Player);
		return false;
	case EOreDealStoreGrant::SpawnBottles:
		if (TablePC)
			return TablePC->ApplySpawnBottles(Player);
		return false;
	case EOreDealStoreGrant::ChaosCard:
		if (TablePC)
			return TablePC->ApplyChaosCard(Player);
		return false;
	default:
		return false;
	}
}
