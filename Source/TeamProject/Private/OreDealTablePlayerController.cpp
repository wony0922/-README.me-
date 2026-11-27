#include "OreDealTablePlayerController.h"

#include "Components/TextRenderComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Framework/Application/SlateApplication.h"
#include "CollisionQueryParams.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "InputMappingContext.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "Components/EditableTextBox.h"
#include "Components/Button.h"
#include "Components/ContentWidget.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "TimerManager.h"
#include "OreDealInteractionWidget.h"
#include "OreDealCeremonyWidget.h"
#include "OreDealHandViewWidget.h"
#include "OreDealPreGameComponent.h"
#include "OreDealMarketBroker.h"
#include "OreDealAuctionBroker.h"
#include "OreDealAuctionComponent.h"
#include "OreDealStoreCatalog.h"
#include "OreDealStoreComponent.h"
#include "OreDealStoreWidget.h"
#include "OreDealInventoryWidget.h"
#include "OreDealTurnBridge.h"
#include "OreDealCardInfo.h"
#include "OreDealWagonStateComponent.h"
#include "OreDealThrowableBottle.h"
#include "Blueprint/UserWidget.h"
#include "Components/LightComponent.h"
#include "Engine/SpotLight.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* ControllerResourceNames[] = { TEXT("Rock"), TEXT("Coal"), TEXT("Copper"), TEXT("Iron"), TEXT("Silver"), TEXT("Gold"), TEXT("Diamond"), TEXT("Adamantium") };

	int32 SaleReward(int32 Resource, int32 Count, const UOreDealCardInfoAsset* Asset = nullptr)
	{
		// --- Data Asset 경로 ---
		if (Asset && Asset->CardInfos.IsValidIndex(Resource))
		{
			const FOreDealCardInfo& Info = Asset->CardInfos[Resource];
			int32 Reward = 0;
			for (const FOreDealSaleTier& Tier : Info.SaleTiers)
			{
				if (Count >= Tier.MinCount)
					Reward = Tier.Reward;
			}
			return Reward;
		}

		// --- 폴백: 하드코딩 테이블 ---
		static const int32 Thresholds[8][4] = {
			{4, 6, 8, 10}, {3, 5, 7, 8}, {3, 4, 6, 7}, {2, 4, 5, 6},
			{2, 4, 6, 7}, {2, 4, 5, 6}, {2, 3, 4, 5}, {2, 3, 0, 0}
		};
		static const int32 Rewards[8][4] = {
			{1, 2, 3, 4}, {1, 2, 3, 4}, {1, 2, 3, 4}, {1, 2, 3, 4},
			{1, 2, 3, 4}, {1, 2, 3, 4}, {1, 2, 3, 4}, {2, 3, 0, 0}
		};
		if (Resource < 0 || Resource >= UE_ARRAY_COUNT(ControllerResourceNames)) return 0;
		int32 Reward = 0;
		for (int32 I = 0; I < 4 && Thresholds[Resource][I] > 0; ++I)
			if (Count >= Thresholds[Resource][I]) Reward = Rewards[Resource][I];
		return Reward;
	}

	// AuctionBroker 에서 CardInfoAsset 을 가져오는 헬퍼
	const UOreDealCardInfoAsset* FindCardInfoAsset(const UObject* WorldContext)
	{
		if (AOreDealAuctionBroker* Broker = AOreDealAuctionBroker::Find(WorldContext))
			return Broker->GetCardInfoAsset();
		return nullptr;
	}

	bool CanSellSingleCardSlot(FScriptArrayHelper& Counts, const FIntProperty* CountsInner, int32 WagonIndex)
	{
		if (!Counts.IsValidIndex(WagonIndex) || !CountsInner || CountsInner->GetPropertyValue(Counts.GetRawPtr(WagonIndex)) != 1) return false;
		int32 OccupiedWagons = 0;
		for (int32 I = 0; I < Counts.Num(); ++I)
		{
			const int32 Count = CountsInner->GetPropertyValue(Counts.GetRawPtr(I));
			if (Count <= 0) continue;
			if (Count != 1) return false;
			++OccupiedWagons;
		}
		return OccupiedWagons >= 2;
	}

	// Both lookups go through the auction broker's per-world cache, so calling them every frame is cheap.
	AActor* FindBoardForPawn(UWorld* World, APawn* Pawn)
	{
		return AOreDealAuctionBroker::FindBoardFor(World, Pawn);
	}

	AActor* FindCardDeck(UWorld* World)
	{
		return AOreDealAuctionBroker::FindDeck(World);
	}

	int32 GetGlobalTurnPhase(AActor* Deck)
	{
		const FIntProperty* Phase = IsValid(Deck) ? FindFProperty<FIntProperty>(Deck->GetClass(), TEXT("TurnPhaseIndex")) : nullptr;
		return Phase ? Phase->GetPropertyValue_InContainer(Deck) : INDEX_NONE;
	}

	bool IsPlayersTurn(AActor* Deck, APlayerState* PlayerState, int32& OutPhase)
	{
		if (!IsValid(Deck) || !IsValid(PlayerState)) return false;
		const FObjectPropertyBase* TurnPlayer = FindFProperty<FObjectPropertyBase>(Deck->GetClass(), TEXT("TurnPS"));
		const FIntProperty* Phase = FindFProperty<FIntProperty>(Deck->GetClass(), TEXT("TurnPhaseIndex"));
		if (!TurnPlayer || !Phase || TurnPlayer->GetObjectPropertyValue_InContainer(Deck) != PlayerState) return false;
		OutPhase = Phase->GetPropertyValue_InContainer(Deck);
		return true;
	}
}

AOreDealTablePlayerController::AOreDealTablePlayerController()
{
	bShowMouseCursor = false;
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> DefaultContext(TEXT("/Game/Input/IMC_Default.IMC_Default"));
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> MouseLookContext(TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook"));
	if (DefaultContext.Succeeded()) DefaultMappingContext = DefaultContext.Object;
	if (MouseLookContext.Succeeded()) MouseLookMappingContext = MouseLookContext.Object;
	AuctionComponent = CreateDefaultSubobject<UOreDealAuctionComponent>(TEXT("OreDealAuction"));
	StoreComponent = CreateDefaultSubobject<UOreDealStoreComponent>(TEXT("OreDealStore"));
}

void AOreDealTablePlayerController::BeginPlay()
{
	Super::BeginPlay();
	NormalizeStoreCatalog();
	CleanUpBoardWagonTexts();
	EnsureSeatMerchantBells();
	GetPlayerBoard();
	if (HasAuthority() && GetWorld())
	{
		// The trade phase now runs as an auction (AOreDealAuctionBroker). The old market window
		// broker is no longer spawned, so its GUI never opens.
		if (!AOreDealAuctionBroker::Find(this))
		{
			FActorSpawnParameters SpawnInfo;
			SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			GetWorld()->SpawnActor<AOreDealAuctionBroker>(AOreDealAuctionBroker::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo);
		}
	}
	if (IsLocalController())
	{
		if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (MouseLookMappingContext) InputSubsystem->AddMappingContext(MouseLookMappingContext, 0);
				if (DefaultMappingContext) InputSubsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
		bShowMouseCursor = false;
		bEnableClickEvents = true;
		bEnableMouseOverEvents = true;
		FInputModeGameOnly InputMode;
		InputMode.SetConsumeCaptureMouseDown(false);
		SetInputMode(InputMode);
		// In-game A/D focus only lands on wagon slots (FocusIndex starts at 2). Deck cards are picked by the auction.
		KeyboardFocusIndex = FMath::Max(GetBlueprintInt(TEXT("FocusIndex"), 2), 2);
		KeyboardSelectedReveal = INDEX_NONE;
		SetBlueprintInt(TEXT("FocusIndex"), KeyboardFocusIndex);
		SetBlueprintInt(TEXT("SelectedReveal"), INDEX_NONE);
		bHasKeyboardFocusSnapshot = true;
		InteractionWidget = CreateWidget<UOreDealInteractionWidget>(this, UOreDealInteractionWidget::StaticClass());
		if (InteractionWidget) InteractionWidget->AddToViewport(32000);
		HandViewWidget = CreateWidget<UOreDealHandViewWidget>(this, UOreDealHandViewWidget::StaticClass());
		if (HandViewWidget) HandViewWidget->AddToViewport(31000);
		InventoryWidget = CreateWidget<UOreDealInventoryWidget>(this, UOreDealInventoryWidget::StaticClass());
		if (InventoryWidget) InventoryWidget->AddToViewport(33000);
		CachedCardDeck = FindCardDeck(GetWorld());
	}
}

void AOreDealTablePlayerController::NormalizeStoreCatalog()
{
	for (FOreDealStoreItem& Item : StoreItems)
	{
		Item.Currency = EOreDealStoreCurrency::Gold;
		Item.GrantProperty = NAME_None;
		if (Item.Id == TEXT("AIglasses")) Item.Grant = EOreDealStoreGrant::AIGlasses;
		else if (Item.Id == TEXT("wood")) { Item.Cost = 1; Item.Amount = 1; Item.Grant = EOreDealStoreGrant::PlayerStateInt; Item.GrantProperty = TEXT("Wood"); }
		else if (Item.Id == TEXT("WagonSlotUpgrade")) { Item.Cost = 5; Item.Amount = 1; Item.Grant = EOreDealStoreGrant::WagonSlotUpgrade; Item.GrantProperty = TEXT("WagonSlotUpgrade"); }
		else if (Item.Id == TEXT("axe")) { Item.Name = TEXT("도끼"); Item.Cost = 3; Item.Amount = 1; Item.Grant = EOreDealStoreGrant::WoodAxe; Item.Description = FText::FromString(TEXT("카드를 뽑을 때 나무를 2개 얻습니다.")); }
	else if (Item.Id == TEXT("bottle")) { Item.Cost = 1; Item.Amount = 1; Item.Grant = EOreDealStoreGrant::SpawnBottles; Item.Description = FText::FromString(TEXT("마차 왼쪽에 병 3개가 놓입니다. 왼쪽 클릭으로 들고, 오른쪽 클릭으로 마시거나 Space로 던집니다.")); }
		else if (Item.Id == TEXT("ChaosCard")) Item.Grant = EOreDealStoreGrant::ChaosCard;
		else if (Item.Id == TEXT("oil"))
		{
			Item.Name = TEXT("은근슬쩍");
			Item.Cost = 2;
			Item.Grant = EOreDealStoreGrant::PlayerStateInt;
			Item.Description = FText::FromString(TEXT("코인 2개. 상대의 마차 슬롯을 고장낼 확률 70%, 들킬 확률 30%. 들키면 나무 2개를 빼앗깁니다."));
		}
		else if (Item.Id == TEXT("fix"))
		{
			Item.Cost = 2;
			Item.Currency = EOreDealStoreCurrency::Wood;
			Item.Grant = EOreDealStoreGrant::PlayerStateInt;
			Item.Description = FText::FromString(TEXT("나무 2개로 고장난 마차 슬롯 하나를 수리합니다."));
		}
	}
}

AActor* AOreDealTablePlayerController::GetPlayerBoard() const
{
	if (CachedPlayerBoard.IsValid()) return CachedPlayerBoard.Get();
	AActor* Board = FindBoardForPawn(GetWorld(), GetPawn());
	if (IsValid(Board))
	{
		CachedPlayerBoard = Board;
		if (HasAuthority())
		{
			const FArrayProperty* TypesProp = FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonTypes"));
			const int32 SlotCount = TypesProp ? FScriptArrayHelper(TypesProp, TypesProp->ContainerPtrToValuePtr<void>(Board)).Num() : 2;
			UOreDealWagonStateComponent::FindOrAdd(Board, SlotCount);
		}
	}
	return Board;
}

void AOreDealTablePlayerController::PlayerTick(float DeltaTime)
{
	CleanUpBoardWagonTexts();
	GetPlayerBoard();
	StackLabelRefreshElapsed += DeltaTime;
	if (StackLabelRefreshElapsed >= 0.1f)
	{
		StackLabelRefreshElapsed = 0.0f;
		UpdateWagonStackLabels();
	}
	UpdateProtectedCardFeedback(DeltaTime);

	if (bInGameOverCeremony)
	{
		UpdateGameOverCeremony(DeltaTime);
		Super::PlayerTick(DeltaTime);
		return;
	}

	// If server has pending supply draw cards and the pawn received new cards, overwrite with central deck cards
	if (HasAuthority() && bSupplyDrawRequested && !SupplyDrawPendingCards.IsEmpty())
	{
		if (APawn* PlayerPawn = GetPawn())
		{
			if (const FArrayProperty* HandProp = FindFProperty<FArrayProperty>(PlayerPawn->GetClass(), TEXT("HandCards")))
			{
				if (const FIntProperty* HandInner = CastField<FIntProperty>(HandProp->Inner))
				{
					FScriptArrayHelper HandHelper(HandProp, HandProp->ContainerPtrToValuePtr<void>(PlayerPawn));
					if (HandHelper.Num() >= SupplyDrawInitialHandCount + SupplyDrawPendingCards.Num())
					{
						for (int32 I = 0; I < SupplyDrawPendingCards.Num(); ++I)
						{
							const int32 TargetIdx = SupplyDrawInitialHandCount + I;
							if (HandHelper.IsValidIndex(TargetIdx))
							{
								HandInner->SetPropertyValue(HandHelper.GetRawPtr(TargetIdx), SupplyDrawPendingCards[I]);
							}
						}
						PlayerPawn->ForceNetUpdate();
						if (UFunction* OnRepHand = PlayerPawn->FindFunction(TEXT("OnRep_HandCards")))
						{
							PlayerPawn->ProcessEvent(OnRepHand, nullptr);
						}
						SupplyDrawPendingCards.Reset();
					}
				}
			}
		}
	}

	// Some Blueprint controllers become locally owned after BeginPlay in PIE/network
	// startup. Retry creation here so a late local-player assignment cannot leave the
	// interaction HUD permanently missing.
	if (IsLocalController() && !InteractionWidget)
	{
		InteractionWidget = CreateWidget<UOreDealInteractionWidget>(this, UOreDealInteractionWidget::StaticClass());
		if (InteractionWidget) InteractionWidget->AddToViewport(32000);
	}
	if (IsLocalController() && !HandViewWidget)
	{
		HandViewWidget = CreateWidget<UOreDealHandViewWidget>(this, UOreDealHandViewWidget::StaticClass());
		if (HandViewWidget) HandViewWidget->AddToViewport(31000);
	}
	if (IsLocalController() && !InventoryWidget)
	{
		InventoryWidget = CreateWidget<UOreDealInventoryWidget>(this, UOreDealInventoryWidget::StaticClass());
		if (InventoryWidget) InventoryWidget->AddToViewport(33000);
	}
	if (IsLocalController())
	{
		TArray<int32> ObservedHand;
		GetLocalHandCards(ObservedHand);
		int32 Phase = INDEX_NONE;
		const bool bSupplyDrawPhase = IsPlayersTurn(FindCardDeck(GetWorld()), PlayerState, Phase) && Phase == 3;
		if (LastObservedHandCount != INDEX_NONE && bSupplyDrawPhase && ObservedHand.Num() > LastObservedHandCount && InventoryWidget)
		{
			const int32 DrawnCount = ObservedHand.Num() - LastObservedHandCount;
			InventoryWidget->QueueWoodCardReveal(DrawnCount * (bHasWoodAxe ? 2 : 1));
		}
		LastObservedHandCount = ObservedHand.Num();
	}
	if (IsLocalController() && !PreGameComponent)
	{
		// Waiting-room presentation (hide table pieces / resource HUD, rest left hands) until the game starts.
		PreGameComponent = NewObject<UOreDealPreGameComponent>(this, TEXT("OreDealPreGame"));
		if (PreGameComponent) PreGameComponent->RegisterComponent();
	}

	if (IsLocalController())
	{
		if (!bPointerCentered)
		{
			int32 Width = 0;
			int32 Height = 0;
			GetViewportSize(Width, Height);
			if (Width > 0 && Height > 0)
			{
				SetMouseLocation(Width / 2, Height / 2);
				bPointerCentered = true;
			}
		}

		float MouseDeltaX = 0.0f;
		float MouseDeltaY = 0.0f;
		GetInputMouseDelta(MouseDeltaX, MouseDeltaY);
		if (FMath::Abs(MouseDeltaX) > KINDA_SMALL_NUMBER || FMath::Abs(MouseDeltaY) > KINDA_SMALL_NUMBER)
		{
			bMouseSelectionActive = true;
		}
	}

	Super::PlayerTick(DeltaTime);
	if (IsLocalController() && InteractionWidget && InteractionWidget->IsHandRackOpen())
	{
		HandRackRefreshElapsed += DeltaTime;
		if (HandRackRefreshElapsed >= 0.5f)
		{
			HandRackRefreshElapsed = 0.0f;
			TArray<int32> Cards;
			GetLocalHandCards(Cards);
			InteractionWidget->RefreshHandCards(Cards);
		}
	}
	if (IsLocalController() && InteractionWidget)
	{
		TurnUIRefreshElapsed += DeltaTime;
		if (TurnUIRefreshElapsed >= 0.2f)
		{
			TurnUIRefreshElapsed = 0.0f;
			if (!CachedCardDeck.IsValid()) CachedCardDeck = FindCardDeck(GetWorld());
			int32 Phase = INDEX_NONE;
			const bool bLocalPlayerDrawPhase = IsPlayersTurn(CachedCardDeck.Get(), PlayerState, Phase) && Phase == 3;
			InteractionWidget->SetDrawPromptVisible(bLocalPlayerDrawPhase);
			if (bLocalPlayerDrawPhase && CachedCardDeck.IsValid())
			{
				FVector2D DeckScreenPosition;
				if (ProjectWorldLocationToScreen(CachedCardDeck->GetActorLocation(), DeckScreenPosition, true))
					InteractionWidget->SetDrawPromptPosition(DeckScreenPosition + FVector2D(-260.0f, -64.0f));
			}
		}
		MarketRefreshElapsed += DeltaTime;
		if (MarketRefreshElapsed >= 0.2f)
		{
			MarketRefreshElapsed = 0.0f;
			if (!MarketBroker && GetWorld())
				MarketBroker = Cast<AOreDealMarketBroker>(UGameplayStatics::GetActorOfClass(GetWorld(), AOreDealMarketBroker::StaticClass()));
			if (MarketBroker)
			{
				FOreDealMarketSnapshot Snapshot;
				MarketBroker->MakeSnapshot(PlayerState, Snapshot);
				InteractionWidget->SetMarketReveal(Snapshot.State == static_cast<int32>(EOreDealMarketState::Reveal), Snapshot.MarketCards);
				if (Snapshot.State == static_cast<int32>(EOreDealMarketState::Reveal) && CachedCardDeck.IsValid())
				{
					int32 Width = 0;
					int32 Height = 0;
					GetViewportSize(Width, Height);
					FVector2D DeckScreenPosition;
					if (Width > 0 && Height > 0 && ProjectWorldLocationToScreen(CachedCardDeck->GetActorLocation(), DeckScreenPosition, true))
						InteractionWidget->SetMarketRevealOrigin(DeckScreenPosition - FVector2D(Width * 0.5f, Height * 0.5f));
				}
				if (Snapshot.bVisible)
				{
					if (!InteractionWidget->IsMarketOpen())
					{
						InteractionWidget->SetChatExpanded(false);
						InteractionWidget->SetSalesOpen(false);
						InteractionWidget->SetHandRackOpen(false);
						if (HandViewWidget) HandViewWidget->SetOpen(false);
						InteractionWidget->SetMarketOpen(true);
						if (!InteractionWidget->IsInViewport()) InteractionWidget->AddToViewport(32000);
						InteractionWidget->SetVisibility(ESlateVisibility::Visible);
						ApplyInteractionInputMode(true, false);
					}
					TArray<int32> LocalHand;
					GetLocalHandCards(LocalHand);
					InteractionWidget->RefreshMarketTrade(Snapshot, LocalHand, PlayerState);
				}
				else if (InteractionWidget->IsMarketOpen())
				{
					InteractionWidget->SetMarketOpen(false);
					const bool bOtherUIOpen = InteractionWidget->IsChatExpanded() || InteractionWidget->IsSalesOpen() || InteractionWidget->IsHandRackOpen();
					ApplyInteractionInputMode(bOtherUIOpen, InteractionWidget->IsChatExpanded());
				}
			}
		}
	}

	if (IsLocalController() && HandViewWidget && HandViewWidget->IsOpen() && AuctionComponent && AuctionComponent->IsPickerOpen())
	{
		// The auction's hand picker replaces the Q viewer while it is open.
		HandViewWidget->SetOpen(false);
	}

	if (IsLocalController() && bMouseSelectionActive)
	{
		UpdatePointerSelection();
	}
	else if (IsLocalController())
	{
		KeyboardFocusIndex = GetBlueprintInt(TEXT("FocusIndex"), KeyboardFocusIndex);
		KeyboardSelectedReveal = GetBlueprintInt(TEXT("SelectedReveal"), KeyboardSelectedReveal);
		if (KeyboardFocusIndex < 2 || KeyboardSelectedReveal != INDEX_NONE)
		{
			// Never focus/highlight the deck-top cards through the old Blueprint focus.
			KeyboardFocusIndex = FMath::Max(KeyboardFocusIndex, 2);
			KeyboardSelectedReveal = INDEX_NONE;
			SetBlueprintInt(TEXT("FocusIndex"), KeyboardFocusIndex);
			SetBlueprintInt(TEXT("SelectedReveal"), INDEX_NONE);
			RefreshBlueprintGlow();
		}

		bHasKeyboardFocusSnapshot = true;
	}
}

int32 AOreDealTablePlayerController::GetWagonFocus() const
{
	return FMath::Clamp(GetBlueprintInt(TEXT("FocusIndex"), 2) - 2, 0, GetWagonSlotCount() - 1);
}

void AOreDealTablePlayerController::SetWagonFocus(int32 WagonIndex)
{
	if (WagonIndex < 0 || WagonIndex >= GetWagonSlotCount()) return;
	KeyboardFocusIndex = 2 + WagonIndex;
	KeyboardSelectedReveal = INDEX_NONE;
	PointerFocusIndex = INDEX_NONE;
	bMouseSelectionActive = false;
	SetBlueprintInt(TEXT("FocusIndex"), KeyboardFocusIndex);
	SetBlueprintInt(TEXT("SelectedReveal"), INDEX_NONE);
	RefreshBlueprintGlow();
}

int32 AOreDealTablePlayerController::GetWagonSlotCount() const
{
	const AActor* Board = GetPlayerBoard();
	const FArrayProperty* TypesProp = Board ? FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonTypes")) : nullptr;
	const FArrayProperty* CountsProp = Board ? FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonCounts")) : nullptr;
	if (!TypesProp || !CountsProp) return 2;
	const FScriptArrayHelper Types(TypesProp, TypesProp->ContainerPtrToValuePtr<void>(Board));
	const FScriptArrayHelper Counts(CountsProp, CountsProp->ContainerPtrToValuePtr<void>(Board));
	return FMath::Clamp(FMath::Min(Types.Num(), Counts.Num()), 1, 3);
}

bool AOreDealTablePlayerController::GrantStoreWagonSlotUpgrade()
{
	if (!HasAuthority() || !CanGrantStoreItem(TEXT("WagonSlotUpgrade"), 1)) return false;
	BP_GrantStoreWagonSlotUpgrade();
	if (GetWagonSlotCount() != 3)
	{
		AActor* Board = GetPlayerBoard();
		const FArrayProperty* TypesProp = Board ? FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonTypes")) : nullptr;
		const FArrayProperty* CountsProp = Board ? FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonCounts")) : nullptr;
		const FIntProperty* TypesInner = TypesProp ? CastField<FIntProperty>(TypesProp->Inner) : nullptr;
		const FIntProperty* CountsInner = CountsProp ? CastField<FIntProperty>(CountsProp->Inner) : nullptr;
		if (!Board || !TypesProp || !CountsProp || !TypesInner || !CountsInner) return false;
		FScriptArrayHelper Types(TypesProp, TypesProp->ContainerPtrToValuePtr<void>(Board));
		FScriptArrayHelper Counts(CountsProp, CountsProp->ContainerPtrToValuePtr<void>(Board));
		Types.AddValue(); Counts.AddValue();
		TypesInner->SetPropertyValue(Types.GetRawPtr(2), INDEX_NONE);
		CountsInner->SetPropertyValue(Counts.GetRawPtr(2), 0);
		Board->ForceNetUpdate();
		UOreDealWagonStateComponent::FindOrAdd(Board, 3);
	}
	return GetWagonSlotCount() == 3;
}

void AOreDealTablePlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AOreDealTablePlayerController, bHasWoodAxe);
}

bool AOreDealTablePlayerController::ApplyAIGlasses(APlayerState* PurchasingPlayer)
{
	if (!HasAuthority() || PurchasingPlayer != PlayerState || !GetWorld() || !GetWorld()->GetGameState()) return false;
	const UOreDealCardInfoAsset* CardInfo = FindCardInfoAsset(this);
	for (APlayerState* Other : GetWorld()->GetGameState()->PlayerArray)
	{
		if (!Other || Other == PurchasingPlayer || !Other->GetPawn()) continue;
		const FArrayProperty* HandProp = FindFProperty<FArrayProperty>(Other->GetPawn()->GetClass(), TEXT("HandCards"));
		const FIntProperty* HandInner = HandProp ? CastField<FIntProperty>(HandProp->Inner) : nullptr;
		if (!HandProp || !HandInner) continue;
		FScriptArrayHelper Hand(HandProp, HandProp->ContainerPtrToValuePtr<void>(Other->GetPawn()));
		TMap<int32, int32> Counts;
		for (int32 I = 0; I < Hand.Num(); ++I) ++Counts.FindOrAdd(HandInner->GetPropertyValue(Hand.GetRawPtr(I)));
		FString Summary;
		for (int32 Resource = 0; Resource < 8; ++Resource)
		{
			const int32 Count = Counts.FindRef(Resource);
			if (Count <= 0) continue;
			const FString Name = CardInfo && CardInfo->CardInfos.IsValidIndex(Resource) ? CardInfo->CardInfos[Resource].DisplayName.ToString() : FString(ControllerResourceNames[Resource]);
			if (!Summary.IsEmpty()) Summary += TEXT(", ");
			Summary += FString::Printf(TEXT("%s %d장"), *Name, Count);
		}
		ClientReceiveChat(TEXT("AI 안경"), FString::Printf(TEXT("%s의 패: %s"), *Other->GetPlayerName(), Summary.IsEmpty() ? TEXT("없음") : *Summary));
	}
	return true;
}

bool AOreDealTablePlayerController::ApplyWoodAxe(APlayerState* PurchasingPlayer)
{
	if (!HasAuthority() || PurchasingPlayer != PlayerState) return false;
	bHasWoodAxe = true;
	ForceNetUpdate();
	return true;
}

bool AOreDealTablePlayerController::ApplySpawnBottles(APlayerState* PurchasingPlayer)
{
	if (!HasAuthority() || PurchasingPlayer != PlayerState || !GetWorld()) return false;
	AActor* Board = GetPlayerBoard();
	AActor* SpawnAnchor = Board ? Board : GetPawn();
	if (!SpawnAnchor)
	{
		UE_LOG(LogTemp, Warning, TEXT("Ore Deal: bottle purchase failed because this player has no board or pawn: %s"), *GetNameSafe(PlayerState));
		return false;
	}
	FVector SlotLocation = SpawnAnchor->GetActorLocation();
	if (Board)
		for (UActorComponent* Component : Board->GetComponents())
			if (Component && Component->GetFName() == TEXT("WagonPivot0"))
				if (USceneComponent* Pivot = Cast<USceneComponent>(Component))
				{
					SlotLocation = Pivot->GetComponentLocation();
					break;
				}
	if (!Board)
		if (const APawn* Pawn = Cast<APawn>(SpawnAnchor))
			if (const UCapsuleComponent* Capsule = Pawn->FindComponentByClass<UCapsuleComponent>())
				SlotLocation.Z -= Capsule->GetScaledCapsuleHalfHeight() - 25.0f;
	const FVector RightVector = SpawnAnchor->GetActorRightVector();
	const FVector LeftOfFirstWagon = -RightVector;
	int32 Spawned = 0;
	for (int32 I = 0; I < 3; ++I)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Location = SlotLocation + LeftOfFirstWagon * 88.0f + RightVector * ((I - 1) * 27.0f) + FVector(0.0f, 0.0f, 35.0f);
		AOreDealThrowableBottle* Bottle = GetWorld()->SpawnActor<AOreDealThrowableBottle>(Location, FRotator::ZeroRotator, Params);
		if (!Bottle) continue;
		Bottle->SetOwner(this);
		Bottle->InitializeBottle();
		++Spawned;
	}
	if (Spawned != 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("Ore Deal: bottle purchase spawned %d of 3 bottles for %s"), Spawned, *GetNameSafe(GetPawn()));
		for (TActorIterator<AOreDealThrowableBottle> It(GetWorld()); It; ++It)
			if (It->GetOwner() == this && It->ActorHasTag(TEXT("OreDealBottle"))) It->Destroy();
		return false;
	}
	return true;
}

bool AOreDealTablePlayerController::ApplyChaosCard(APlayerState* PurchasingPlayer)
{
	if (!HasAuthority() || PurchasingPlayer != PlayerState) return false;
	AOreDealAuctionBroker* Broker = AOreDealAuctionBroker::Find(this);
	return Broker && Broker->ShuffleAllCardsAcrossPlayers();
}

void AOreDealTablePlayerController::ServerPickupBottle_Implementation(AActor* Bottle)
{
	APawn* ControlledPawn = GetPawn();
	if (!HasAuthority() || !IsValid(ControlledPawn) || !IsValid(Bottle) || !Bottle->ActorHasTag(TEXT("OreDealBottle")) || HeldBottle ||
		FVector::DistSquared(ControlledPawn->GetActorLocation(), Bottle->GetActorLocation()) > FMath::Square(300.0f)) return;
	AOreDealThrowableBottle* Throwable = Cast<AOreDealThrowableBottle>(Bottle);
	if (!Throwable) return;
	Throwable->SetHeld(true);
	Bottle->AttachToActor(ControlledPawn, FAttachmentTransformRules::KeepWorldTransform);
	Bottle->SetActorRelativeLocation(FVector(35.0f, 0.0f, 55.0f));
	HeldBottle = Bottle;
	ClientSetHoldingBottle(true);
}

void AOreDealTablePlayerController::ServerThrowBottle_Implementation()
{
	if (!HasAuthority() || !IsValid(HeldBottle)) return;
	AActor* Bottle = HeldBottle;
	HeldBottle = nullptr;
	Bottle->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (AOreDealThrowableBottle* Throwable = Cast<AOreDealThrowableBottle>(Bottle))
	{
		Throwable->SetHeld(false);
		if (APawn* ControlledPawn = GetPawn()) Throwable->GetBottleMesh()->AddImpulse(ControlledPawn->GetActorForwardVector() * 1700.0f + FVector(0.0f, 0.0f, 350.0f), NAME_None, true);
	}
	ClientSetHoldingBottle(false);
	if (UWorld* World = GetWorld())
	{
		const FString Message = FString::Printf(TEXT("@@ %s ** 술병 사용!"), PlayerState ? *PlayerState->GetPlayerName() : TEXT("플레이어"));
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			if (AOreDealTablePlayerController* Recipient = Cast<AOreDealTablePlayerController>(It->Get())) Recipient->ClientReceiveChat(TEXT("상점"), Message);
	}
}

void AOreDealTablePlayerController::ServerDrinkBottle_Implementation()
{
	if (!HasAuthority() || !IsValid(HeldBottle)) return;
	AOreDealThrowableBottle* Bottle = Cast<AOreDealThrowableBottle>(HeldBottle);
	if (!Bottle) return;
	HeldBottle = nullptr;
	Bottle->BeginDrink();
	ClientSetHoldingBottle(false);
	if (UWorld* World = GetWorld())
	{
		const FString Message = FString::Printf(TEXT("@@ %s ** 술병 사용!"), PlayerState ? *PlayerState->GetPlayerName() : TEXT("플레이어"));
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			if (AOreDealTablePlayerController* Recipient = Cast<AOreDealTablePlayerController>(It->Get())) Recipient->ClientReceiveChat(TEXT("상점"), Message);
	}
}

void AOreDealTablePlayerController::ClientSetHoldingBottle_Implementation(bool bHolding)
{
	bHoldingBottleLocal = bHolding;
}

void AOreDealTablePlayerController::UpdateRotation(float DeltaTime)
{
	APawn* CurrentPawn = GetPawn();
	if (CurrentPawn && YawReferencePawn.Get() != CurrentPawn)
	{
		YawReferencePawn = CurrentPawn;
		ReferenceYaw = CurrentPawn->GetActorRotation().Yaw;
	}

	Super::UpdateRotation(DeltaTime);

	if (CurrentPawn && IsLocalController())
	{
		FRotator Rotation = GetControlRotation();
		FRotator AuctionLook;
		if (AuctionComponent && AuctionComponent->GetCameraOverride(AuctionLook))
		{
			// Partner pick: turn smoothly towards the highlighted player, beyond the normal yaw limit.
			SetControlRotation(FMath::RInterpTo(Rotation, AuctionLook, DeltaTime, 6.0f));
			bCameraReturning = true;
			return;
		}
		const float RelativeYaw = FMath::FindDeltaAngleDegrees(ReferenceYaw, Rotation.Yaw);
		const float ClampedYaw = ReferenceYaw + FMath::Clamp(RelativeYaw, -MaxRelativeYawDegrees, MaxRelativeYawDegrees);
		if (bCameraReturning && FMath::Abs(RelativeYaw) > MaxRelativeYawDegrees + 0.5f)
		{
			// Ease back inside the limit instead of snapping after the auction camera turn.
			SetControlRotation(FMath::RInterpTo(Rotation, FRotator(Rotation.Pitch, ClampedYaw, 0.0f), DeltaTime, 6.0f));
			return;
		}
		bCameraReturning = false;
		Rotation.Yaw = ClampedYaw;
		SetControlRotation(Rotation);
	}
}

bool AOreDealTablePlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	// The end screen is a modal UI. Route every press and release to Slate before the chat,
	// camera, auction, or world-click handlers can consume it.
	if (IsLocalController() && bInGameOverCeremony)
	{
		if (bResultWidgetOpen)
		{
			if (Params.Event == IE_Pressed)
			{
				if (Params.Key == EKeys::Enter || Params.Key == EKeys::M || Params.Key == EKeys::Escape)
				{
					ReturnToMainMenu();
					return true;
				}
			}
			return Super::InputKey(Params);
		}
		if (Params.Event == IE_Pressed && bRandomCameraPhase && Params.Key == EKeys::SpaceBar)
		{
			ShowResultScreen();
			return true;
		}
		return true;
	}
	if (IsLocalController() && AuctionComponent && AuctionComponent->IsTradeModalOpen() && Params.Key.IsMouseButton())
		return Super::InputKey(Params);

	// Clicking the expanded chat restores text focus. Clicking elsewhere dismisses it and
	// continues through the usual game/UI path so the click still activates its target.
	if (IsLocalController() && InteractionWidget && Params.Event == IE_Pressed && Params.Key.IsMouseButton())
	{
		float MouseX = 0.0f, MouseY = 0.0f;
		const bool bClickInChat = GetMousePosition(MouseX, MouseY) &&
			InteractionWidget->IsScreenPositionInChat(FVector2D(MouseX, MouseY));
		if (bClickInChat && !InteractionWidget->IsChatExpanded())
		{
			InteractionWidget->SetChatExpanded(true);
			ApplyInteractionInputMode(true, true);
			return Super::InputKey(Params);
		}
		if (bClickInChat && InteractionWidget->IsChatExpanded())
		{
			ApplyInteractionInputMode(true, true);
			return Super::InputKey(Params);
		}
		if (InteractionWidget->IsChatExpanded())
		{
			InteractionWidget->SetChatExpanded(false);
			const bool bOtherUIOpen = (StoreComponent && StoreComponent->IsStoreOpen()) ||
				InteractionWidget->IsSalesOpen() || InteractionWidget->IsHandRackOpen() || InteractionWidget->IsMarketOpen();
			ApplyInteractionInputMode(bOtherUIOpen, false);
		}
	}
	// Store (F): opens/closes with F and, while open, takes every keyboard key before anything else.
	if (IsLocalController() && StoreComponent && (!InteractionWidget || !InteractionWidget->IsChatExpanded()) &&
		StoreComponent->HandleInputKey(Params.Key, Params.Event))
		return true;
	// A modal store owns mouse input. Let Slate process clicks on its tabs and entries
	// instead of treating them as clicks on the merchant, deck, or other world actors.
	if (IsLocalController() && StoreComponent && StoreComponent->IsStoreOpen() && Params.Key.IsMouseButton())
		return true;
	if (IsLocalController() && AuctionComponent &&
		(!InteractionWidget || (!InteractionWidget->IsChatExpanded() && !InteractionWidget->IsSalesOpen())) &&
		AuctionComponent->HandleInputKey(Params.Key, Params.Event))
		return true;
	if (IsLocalController() && bHoldingBottleLocal && Params.Event == IE_Pressed && Params.Key == EKeys::SpaceBar)
	{
		ServerThrowBottle();
		return true;
	}
	if (IsLocalController() && bHoldingBottleLocal && Params.Event == IE_Pressed && Params.Key == EKeys::RightMouseButton)
	{
		ServerDrinkBottle();
		return true;
	}
	if (IsLocalController() && (Params.Key == EKeys::A || Params.Key == EKeys::D) &&
		(!InteractionWidget || (!InteractionWidget->IsChatExpanded() && !InteractionWidget->IsSalesOpen() && !InteractionWidget->IsHandRackOpen())))
	{
		// A/D moves the in-game focus across the currently unlocked wagon slots.
		int32 Phase = INDEX_NONE;
		AActor* Deck = FindCardDeck(GetWorld());
		if (!IsPlayersTurn(Deck, PlayerState, Phase) && GetGlobalTurnPhase(Deck) != 0) return Super::InputKey(Params);
		if (Params.Event == IE_Pressed || Params.Event == IE_Repeat)
			SetWagonFocus(FMath::Clamp(GetWagonFocus() + (Params.Key == EKeys::D ? 1 : -1), 0, GetWagonSlotCount() - 1));
		return true;
	}
	if (IsLocalController() && Params.Event == IE_Pressed && Params.Key == EKeys::SpaceBar &&
		(!InteractionWidget || (!InteractionWidget->IsChatExpanded() && !InteractionWidget->IsSalesOpen() && !InteractionWidget->IsHandRackOpen() && !InteractionWidget->IsMarketOpen())) &&
		(!StoreComponent || !StoreComponent->IsStoreOpen()))
	{
		if (GetGlobalTurnPhase(FindCardDeck(GetWorld())) == 0)
		{
			ServerPlantHandCard(GetWagonFocus());
			return true;
		}
	}
	if (IsLocalController() && InteractionWidget && InteractionWidget->IsMarketRevealActive() && Params.Event == IE_Pressed)
		return true;
	if (IsLocalController() && InteractionWidget && InteractionWidget->IsMarketOpen() && Params.Event == IE_Pressed)
	{
		if (Params.Key == EKeys::A) { InteractionWidget->MoveMarketPlayerSelection(-1); return true; }
		if (Params.Key == EKeys::D) { InteractionWidget->MoveMarketPlayerSelection(1); return true; }
		if (Params.Key == EKeys::Left) { InteractionWidget->MoveMarketHandSelection(-1); return true; }
		if (Params.Key == EKeys::Right) { InteractionWidget->MoveMarketHandSelection(1); return true; }
		if (Params.Key == EKeys::SpaceBar) { InteractionWidget->ToggleMarketHandSelection(); return true; }
		if (Params.Key == EKeys::Escape && MarketBroker)
		{
			FOreDealMarketSnapshot Snapshot;
			MarketBroker->MakeSnapshot(PlayerState, Snapshot);
			if (Snapshot.bAwaitingConsent && (Snapshot.ActivePlayer == PlayerState || Snapshot.PendingTarget == PlayerState)) RequestMarketConsent(false);
			return true;
		}
		if (!Params.Key.IsMouseButton() && !Params.Key.IsGamepadKey()) return true;
	}
	if (IsLocalController() && Params.Event == IE_Pressed && HandViewWidget && (!InteractionWidget || !InteractionWidget->IsChatExpanded()))
	{
		// Hand viewer: Q opens/closes it, Z/X step through the cards while it is open.
		if (Params.Key == EKeys::Q) { ToggleHandView(); return true; }
		if (HandViewWidget->IsOpen() && Params.Key == EKeys::Z) { HandViewWidget->StepFocus(-1); return true; }
		if (HandViewWidget->IsOpen() && Params.Key == EKeys::X) { HandViewWidget->StepFocus(1); return true; }
	}
	if (IsLocalController() && Params.Event == IE_Pressed)
	{
		if (Params.Key == EKeys::LeftMouseButton && (!InteractionWidget || !InteractionWidget->IsSalesOpen()) && (!InteractionWidget || !InteractionWidget->IsChatExpanded()) && (!InteractionWidget || !InteractionWidget->IsHandRackOpen()))
		{
			bMouseSelectionActive = true;
			FHitResult Hit;
			const bool bHitTarget = TraceCenterPointer(Hit);
			AActor* HitActor = bHitTarget ? Hit.GetActor() : nullptr;
			if (HitActor && HitActor->ActorHasTag(TEXT("OreDealMerchant")))
			{
				ToggleSalesMenu();
				return true;
			}
			if (AuctionComponent)
				if (APawn* TargetPawn = Cast<APawn>(HitActor))
					if (APlayerState* TargetPlayer = TargetPawn->GetPlayerState())
						if (AuctionComponent->HandlePlayerClick(TargetPlayer)) return true;
			if (HitActor && HitActor->ActorHasTag(TEXT("OreDealBottle")))
			{
				ServerPickupBottle(HitActor);
				return true;
			}
			if (HitActor == FindCardDeck(GetWorld()))
			{
				int32 Phase = INDEX_NONE;
				if (IsPlayersTurn(HitActor, PlayerState, Phase) && Phase == 3) ServerRequestSupplyDraw();
				return true;
			}
			// Do not reset the viewport's input mode on an ordinary world click. Forward
			// it so the viewport retains mouse capture and MouseX/MouseY continue driving
			// the camera.
			return Super::InputKey(Params);
		}
		if (InteractionWidget && InteractionWidget->IsHandRackOpen())
		{
			if (Params.Key == EKeys::Escape || Params.Key == EKeys::E || Params.Key == EKeys::Enter)
			{
				ToggleHandRack();
				return true;
			}
			if (Params.Key == EKeys::Left || Params.Key == EKeys::A) { InteractionWidget->MoveHandSelection(-1); return true; }
			if (Params.Key == EKeys::Right || Params.Key == EKeys::D) { InteractionWidget->MoveHandSelection(1); return true; }
		}
		if (InteractionWidget && InteractionWidget->IsSalesOpen() &&
			(Params.Key == EKeys::E || Params.Key == EKeys::Escape))
		{
			// The sales panel is modal. E and Escape both close it and restore camera input.
			ToggleSalesMenu();
			return true;
		}
		if (Params.Key == EKeys::Enter)
		{
			if (bChatEnterConsumed)
			{
				bChatEnterConsumed = false;
				return true;
			}
			if (!InteractionWidget || !InteractionWidget->IsChatExpanded()) ToggleChat();
			return true;
		}
		if (Params.Key == EKeys::E)
		{
			// BP_PC_Table previously mapped E straight to ServerEndTurn. Consume it here
			// and advance the validated phase instead; phase two is owned by the market timer.
			if (InteractionWidget && (InteractionWidget->IsSalesOpen() || InteractionWidget->IsChatExpanded() || InteractionWidget->IsMarketOpen())) return true;
			AActor* Deck = FindCardDeck(GetWorld());
			int32 Phase = INDEX_NONE;
			if (IsPlayersTurn(Deck, PlayerState, Phase) && (Phase == 0 || Phase == 2 || Phase == 3)) ServerCompleteTurnStage();
			return true;
		}
		if (Params.Key == EKeys::W)
		{
			// Keep the legacy draw shortcut inside phase four; deck clicks use this same
			// server-validated animated draw request.
			AActor* Deck = FindCardDeck(GetWorld());
			int32 Phase = INDEX_NONE;
			if ((!InteractionWidget || (!InteractionWidget->IsSalesOpen() && !InteractionWidget->IsChatExpanded() && !InteractionWidget->IsHandRackOpen())) &&
				IsPlayersTurn(Deck, PlayerState, Phase) && Phase == 3) ServerRequestSupplyDraw();
			return true;
		}
	}

	if (IsLocalController() && (Params.Event == IE_Pressed || Params.Event == IE_Repeat))
	{
		const bool bIsMouseInput = Params.Key.IsMouseButton() || Params.Key.GetFName().ToString().StartsWith(TEXT("Mouse"));
		if (bIsMouseInput)
		{
			bMouseSelectionActive = true;
		}
		else if (!Params.Key.IsGamepadKey())
		{
			bMouseSelectionActive = false;
			PointerFocusIndex = INDEX_NONE;
			if (bHasKeyboardFocusSnapshot)
			{
				SetBlueprintInt(TEXT("FocusIndex"), KeyboardFocusIndex);
				SetBlueprintInt(TEXT("SelectedReveal"), KeyboardSelectedReveal);
			}
			RefreshBlueprintGlow();
		}
	}

	return Super::InputKey(Params);
}

void AOreDealTablePlayerController::SetAuctionModalInput(bool bOpen)
{
	if (!IsLocalController()) return;
	if (bOpen)
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		bShowMouseCursor = true;
		bEnableClickEvents = true;
	}
	else
	{
		FInputModeGameOnly Mode;
		Mode.SetConsumeCaptureMouseDown(false);
		SetInputMode(Mode);
		bShowMouseCursor = false;
	}
}

void AOreDealTablePlayerController::HandleChatEnter()
{
	if (!IsLocalController() || !InteractionWidget || !InteractionWidget->IsChatExpanded()) return;
	ToggleChat();
	bChatEnterConsumed = true;
	GetWorldTimerManager().SetTimerForNextTick(this, &AOreDealTablePlayerController::ClearChatEnterGuard);
}

void AOreDealTablePlayerController::ClearChatEnterGuard()
{
	bChatEnterConsumed = false;
}

void AOreDealTablePlayerController::ToggleSalesMenu()
{
	// The old sales panel was replaced by the store window (F).
	if (StoreComponent && (!InteractionWidget || !InteractionWidget->IsChatExpanded())) StoreComponent->ToggleStore();
}

bool AOreDealTablePlayerController::IsStoreGameStarted() const
{
	const AActor* Deck = AOreDealAuctionBroker::FindDeck(GetWorld());
	const FBoolProperty* Started = Deck ? FindFProperty<FBoolProperty>(Deck->GetClass(), TEXT("bGameStarted")) : nullptr;
	return Started && Started->GetPropertyValue_InContainer(Deck);
}

int32 AOreDealTablePlayerController::GetStorePlayerGold() const
{
	const APlayerState* StorePlayerState = PlayerState;
	const FIntProperty* Gold = StorePlayerState ? FindFProperty<FIntProperty>(StorePlayerState->GetClass(), TEXT("Gold")) : nullptr;
	return Gold ? Gold->GetPropertyValue_InContainer(StorePlayerState) : 0;
}

int32 AOreDealTablePlayerController::GetStorePlayerWood() const
{
	const APlayerState* StorePlayerState = PlayerState;
	const FIntProperty* Wood = StorePlayerState ? FindFProperty<FIntProperty>(StorePlayerState->GetClass(), TEXT("Wood")) : nullptr;
	return Wood ? Wood->GetPropertyValue_InContainer(StorePlayerState) : 0;
}

void AOreDealTablePlayerController::OpenStoreInventory()
{
	if (StoreComponent) StoreComponent->OpenInventoryTab();
}

void AOreDealTablePlayerController::GetStoreInventoryCounts(TArray<int32>& OutCounts) const
{
	OutCounts.Reset();
	OutCounts.SetNumZeroed(StoreItems.Num());
	for (int32 Index = 0; Index < StoreItems.Num(); ++Index)
	{
		const FName Id = StoreItems[Index].Id;
		if (Id == TEXT("axe")) OutCounts[Index] = bHasWoodAxe ? 1 : 0;
		else if (Id == TEXT("bottle"))
		{
			for (TActorIterator<AOreDealThrowableBottle> It(GetWorld()); It; ++It)
				if (It->GetOwner() == this) ++OutCounts[Index];
		}
		else if (Id == TEXT("WagonSlotUpgrade"))
		{
			TArray<FOreDealStoreWagonView> Wagons;
			GetStoreWagons(Wagons);
			OutCounts[Index] = Wagons.Num() > 2 ? 1 : 0;
		}
	}
}

bool AOreDealTablePlayerController::HasEnoughStoreGold(int32 Cost) const
{
	return Cost >= 0 && GetStorePlayerGold() >= Cost;
}

bool AOreDealTablePlayerController::CanGrantStoreItem(FName GrantProperty, int32 Amount) const
{
	if (GrantProperty == TEXT("WagonSlotUpgrade"))
	{
		if (Amount != 1) return false;
		const AActor* Board = GetPlayerBoard();
		const FArrayProperty* TypesProp = Board ? FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonTypes")) : nullptr;
		const FArrayProperty* CountsProp = Board ? FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonCounts")) : nullptr;
		if (!TypesProp || !CountsProp) return false;
		const FScriptArrayHelper Types(TypesProp, TypesProp->ContainerPtrToValuePtr<void>(Board));
		const FScriptArrayHelper Counts(CountsProp, CountsProp->ContainerPtrToValuePtr<void>(Board));
		return Types.Num() == 2 && Counts.Num() == 2;
	}

	const APlayerState* StorePlayerState = PlayerState;
	const FIntProperty* Property = StorePlayerState ? FindFProperty<FIntProperty>(StorePlayerState->GetClass(), GrantProperty) : nullptr;
	if (!Property || Amount <= 0) return false;
	const int64 NewValue = static_cast<int64>(Property->GetPropertyValue_InContainer(StorePlayerState)) + Amount;
	return NewValue <= MAX_int32;
}

void AOreDealTablePlayerController::GetStoreCatalog(TArray<FOreDealStoreItem>& OutItems) const
{
	OutItems = StoreItems;
}

const FOreDealStoreItem* AOreDealTablePlayerController::FindStoreItem(FName ItemId) const
{
	if (ItemId.IsNone()) return nullptr;
	const FOreDealStoreItem* FoundItem = nullptr;
	for (int32 Index = 0; Index < FMath::Min(StoreItems.Num(), OreDealStore::SlotCount); ++Index)
	{
		const FOreDealStoreItem& Item = StoreItems[Index];
		if (Item.Id != ItemId) continue;
		if (FoundItem) return nullptr;
		FoundItem = &Item;
	}
	return FoundItem;
}

TSubclassOf<UOreDealStoreWidget> AOreDealTablePlayerController::GetStoreWidgetClass() const
{
	if (StoreWidgetClass) return StoreWidgetClass;
	return UOreDealStoreWidget::StaticClass();
}

bool AOreDealTablePlayerController::ValidateStorePurchase_Implementation(FName ItemId, int32 Cost, int32 Amount, FName GrantProperty, FString& OutMessage)
{
	if (!HasAuthority())
	{
		OutMessage = TEXT("서버에서만 구매를 확인할 수 있습니다");
		return false;
	}
	if (!IsStoreGameStarted())
	{
		OutMessage = TEXT("게임이 시작된 뒤에 구매할 수 있습니다");
		return false;
	}
	if (ItemId.IsNone() || Cost < 0 || !CanGrantStoreItem(GrantProperty, Amount))
	{
		OutMessage = TEXT("구매 항목이 올바르지 않습니다");
		return false;
	}
	if (!HasEnoughStoreGold(Cost))
	{
		OutMessage.Reset();
		return false;
	}
	OutMessage = TEXT("");
	return true;
}

void AOreDealTablePlayerController::OnStoreOpenChanged(bool bOpen)
{
	if (!IsLocalController()) return;
	if (bOpen)
	{
		if (HandViewWidget) HandViewWidget->SetOpen(false);
		if (InteractionWidget)
		{
			InteractionWidget->SetSalesOpen(false);
			InteractionWidget->SetHandRackOpen(false);
		}
	}
	const bool bChat = InteractionWidget && InteractionWidget->IsChatExpanded();
	ApplyInteractionInputMode(bOpen || bChat, false);
	// Keep the camera still while the cursor is used on the store (ApplyInteractionInputMode re-enables look).
	if (bOpen) SetIgnoreLookInput(true);
}

void AOreDealTablePlayerController::GetStoreWagons(TArray<FOreDealStoreWagonView>& OutWagons) const
{
	OutWagons.Reset();
	AActor* Board = GetPlayerBoard();
	if (!Board) return;
	const FArrayProperty* TypesProp = FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonTypes"));
	const FArrayProperty* CountsProp = FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonCounts"));
	const FIntProperty* TypesInner = TypesProp ? CastField<FIntProperty>(TypesProp->Inner) : nullptr;
	const FIntProperty* CountsInner = CountsProp ? CastField<FIntProperty>(CountsProp->Inner) : nullptr;
	if (!TypesProp || !CountsProp || !TypesInner || !CountsInner) return;
	FScriptArrayHelper Types(TypesProp, TypesProp->ContainerPtrToValuePtr<void>(Board));
	FScriptArrayHelper Counts(CountsProp, CountsProp->ContainerPtrToValuePtr<void>(Board));

	// Same rules as ServerSellWagon: only on my turn in phases 1~3, and a lone card only when every loaded wagon holds one.
	int32 Phase = INDEX_NONE;
	const bool bSalePhase = IsPlayersTurn(FindCardDeck(GetWorld()), PlayerState, Phase) && Phase >= 0 && Phase <= 2;
	UOreDealWagonStateComponent* WagonState = UOreDealWagonStateComponent::FindOrAdd(Board, Types.Num());
	const int32 WagonCount = FMath::Min3(Types.Num(), Counts.Num(), 3);
	for (int32 Index = 0; Index < WagonCount; ++Index)
	{
		FOreDealStoreWagonView& View = OutWagons.AddDefaulted_GetRef();
		const int32 Resource = TypesInner->GetPropertyValue(Types.GetRawPtr(Index));
		const int32 Count = CountsInner->GetPropertyValue(Counts.GetRawPtr(Index));
		View.Resource = Resource;
		View.Count = Count;
		View.Reward = Resource >= 0 && Resource < UE_ARRAY_COUNT(ControllerResourceNames) && Count > 0 ? SaleReward(Resource, Count, FindCardInfoAsset(GetWorld())) : 0;
		View.bBroken = WagonState && WagonState->IsBroken(Index);
		const bool bEnoughCards = Count >= 2 || CanSellSingleCardSlot(Counts, CountsInner, Index);
		View.bCanSell = !View.bBroken && Count > 0 && bSalePhase && bEnoughCards;
		if (View.bBroken) View.Note = TEXT("고장: 나무 2개로 수리");
		else if (Count <= 0) View.Note = TEXT("비어 있음");
		else if (!bSalePhase) View.Note = TEXT("내 턴 1~3단계에만 판매할 수 있습니다");
		else if (!bEnoughCards) View.Note = TEXT("1장은 다른 마차도 1장일 때만 판매");
		else if (View.Reward <= 0) View.Note = TEXT("개수가 모자라 0골드로 판매됩니다");
	}
}

void AOreDealTablePlayerController::ToggleHandRack(AActor* RackActor)
{
	if (!InteractionWidget || InteractionWidget->IsChatExpanded() || InteractionWidget->IsSalesOpen()) return;
	const bool bOpen = !InteractionWidget->IsHandRackOpen();
	InteractionWidget->SetHandRackOpen(bOpen);
	if (bOpen)
	{
		TArray<int32> Cards;
		GetLocalHandCards(Cards);
		InteractionWidget->RefreshHandCards(Cards);
		AActor* PositionActor = RackActor ? RackActor : GetPlayerBoard();
		int32 ViewportWidth = 0;
		int32 ViewportHeight = 0;
		GetViewportSize(ViewportWidth, ViewportHeight);
		FVector2D RackScreenPosition;
		if (PositionActor && ViewportWidth > 0 && ViewportHeight > 0 && ProjectWorldLocationToScreen(PositionActor->GetActorLocation(), RackScreenPosition, true))
			InteractionWidget->SetHandRackScreenPosition(RackScreenPosition, FVector2D(ViewportWidth, ViewportHeight));
	}
	ApplyInteractionInputMode(bOpen, false);
}

void AOreDealTablePlayerController::ToggleHandView()
{
	if (!HandViewWidget) return;
	if (InteractionWidget && (InteractionWidget->IsMarketOpen() || InteractionWidget->IsMarketRevealActive())) return;
	HandViewWidget->ToggleOpen();
}

void AOreDealTablePlayerController::GetLocalHandCards(TArray<int32>& OutCards) const
{
	OutCards.Reset();
	const APawn* MyPawn = GetPawn();
	const FArrayProperty* HandProperty = MyPawn ? FindFProperty<FArrayProperty>(MyPawn->GetClass(), TEXT("HandCards")) : nullptr;
	const FIntProperty* HandElement = HandProperty ? CastField<FIntProperty>(HandProperty->Inner) : nullptr;
	if (!HandProperty || !HandElement) return;
	FScriptArrayHelper Hand(HandProperty, HandProperty->ContainerPtrToValuePtr<void>(MyPawn));
	for (int32 I = 0; I < Hand.Num(); ++I) OutCards.Add(HandElement->GetPropertyValue(Hand.GetRawPtr(I)));
}

void AOreDealTablePlayerController::ToggleChat()
{
	if (!InteractionWidget) return;
	if (InteractionWidget->IsChatExpanded())
	{
		const FString Message = InteractionWidget->GetChatDraft().TrimStartAndEnd();
		if (!Message.IsEmpty()) ServerSendChat(Message.Left(256));
		InteractionWidget->SetChatExpanded(false);
		ApplyInteractionInputMode(InteractionWidget->IsSalesOpen(), false);
	}
	else
	{
		InteractionWidget->SetChatExpanded(true);
		ApplyInteractionInputMode(true, true);
	}
}

void AOreDealTablePlayerController::ApplyInteractionInputMode(bool bUIActive, bool bFocusChatInput)
{
	// Let the visible menu receive mouse clicks while retaining viewport lock and keeping
	// look input enabled. Chat additionally focuses its editable text field.
	const bool bChatInputActive = bFocusChatInput || (InteractionWidget && InteractionWidget->IsChatExpanded());
	SetIgnoreMoveInput(bChatInputActive);
	SetIgnoreLookInput(false);
	bShowMouseCursor = bUIActive;
	if (!bUIActive)
	{
		FInputModeGameOnly Mode;
		Mode.SetConsumeCaptureMouseDown(false);
		SetInputMode(Mode);
		int32 Width = 0;
		int32 Height = 0;
		GetViewportSize(Width, Height);
		if (Width > 0 && Height > 0) SetMouseLocation(Width / 2, Height / 2);
		bPointerCentered = Width > 0 && Height > 0;
		if (!bUIActive)
		{
			bMouseSelectionActive = false;
			PointerFocusIndex = INDEX_NONE;
		}
		return;
	}
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	const bool bStoreOpen = StoreComponent && StoreComponent->IsStoreOpen();
	Mode.SetLockMouseToViewportBehavior(bStoreOpen ? EMouseLockMode::DoNotLock : EMouseLockMode::LockAlways);
	if (bFocusChatInput && InteractionWidget && InteractionWidget->GetChatInputWidget())
		Mode.SetWidgetToFocus(InteractionWidget->GetChatInputWidget()->TakeWidget());
	else if (bStoreOpen && StoreComponent && StoreComponent->GetStoreWidget())
		Mode.SetWidgetToFocus(StoreComponent->GetStoreWidget()->TakeWidget());
	SetInputMode(Mode);
	// A modal store needs an uncaptured pointer so Slate can route clicks to its tabs
	// and item hit areas. Other UI keeps the game's existing relative-look capture.
	if (UWorld* World = GetWorld())
		if (UGameViewportClient* Viewport = World->GetGameViewport())
		{
			Viewport->SetMouseCaptureMode(bStoreOpen ? EMouseCaptureMode::NoCapture : EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
			if (bStoreOpen && FSlateApplication::IsInitialized())
				FSlateApplication::Get().ReleaseAllPointerCapture();
		}
	if (bFocusChatInput && InteractionWidget && InteractionWidget->GetChatInputWidget()) InteractionWidget->GetChatInputWidget()->SetKeyboardFocus();
}

void AOreDealTablePlayerController::GetSaleOptionText(int32 WagonIndex, FString& OutText, bool& bOutCanSell) const
{
	OutText.Reset();
	bOutCanSell = false;
	APlayerState* PS = PlayerState;
	AActor* Board = GetPlayerBoard();
	AActor* Deck = FindCardDeck(GetWorld());
	int32 Phase = INDEX_NONE;
	if (!Board || !IsPlayersTurn(Deck, PS, Phase) || Phase < 0 || Phase > 2)
	{
		OutText = FString::Printf(TEXT("마차 %d: 내 턴 1~3단계에서 판매 가능"), WagonIndex + 1);
		return;
	}
	const FArrayProperty* TypesProp = FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonTypes"));
	const FArrayProperty* CountsProp = FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonCounts"));
	const FIntProperty* TypesInner = TypesProp ? CastField<FIntProperty>(TypesProp->Inner) : nullptr;
	const FIntProperty* CountsInner = CountsProp ? CastField<FIntProperty>(CountsProp->Inner) : nullptr;
	if (!TypesProp || !CountsProp || !TypesInner || !CountsInner) return;
	FScriptArrayHelper Types(TypesProp, TypesProp->ContainerPtrToValuePtr<void>(Board));
	FScriptArrayHelper Counts(CountsProp, CountsProp->ContainerPtrToValuePtr<void>(Board));
	if (!Types.IsValidIndex(WagonIndex) || !Counts.IsValidIndex(WagonIndex)) return;
	const int32 Resource = TypesInner->GetPropertyValue(Types.GetRawPtr(WagonIndex));
	const int32 Count = CountsInner->GetPropertyValue(Counts.GetRawPtr(WagonIndex));
	if (Resource < 0 || Resource >= UE_ARRAY_COUNT(ControllerResourceNames) || Count <= 0)
	{
		OutText = FString::Printf(TEXT("마차 %d: 비어 있음"), WagonIndex + 1);
		return;
	}
	const int32 Reward = SaleReward(Resource, Count, FindCardInfoAsset(GetWorld()));
	bOutCanSell = Count >= 2 || CanSellSingleCardSlot(Counts, CountsInner, WagonIndex);
	OutText = FString::Printf(TEXT("마차 %d · %s X %d개 · 보상 %dG%s"), WagonIndex + 1, ControllerResourceNames[Resource], Count, Reward, Reward == 0 ? TEXT(" (0G 정산 가능)") : TEXT(""));
}

void AOreDealTablePlayerController::RequestSellWagon(int32 WagonIndex)
{
	if (WagonIndex >= 0 && WagonIndex < 3) ServerSellWagon(WagonIndex);
}

void AOreDealTablePlayerController::RequestMarketSelectTarget(APlayerState* Target)
{
	if (IsLocalController()) ServerMarketSelectTarget(Target);
}

void AOreDealTablePlayerController::RequestMarketSetOffer(const TArray<int32>& HandIndices)
{
	if (IsLocalController()) ServerMarketSetOffer(HandIndices);
}

void AOreDealTablePlayerController::RequestMarketBeginDeal(APlayerState* Target, const TArray<int32>& MarketIndices)
{
	if (IsLocalController()) ServerMarketBeginDeal(Target, MarketIndices);
}

void AOreDealTablePlayerController::RequestMarketConsent(bool bAccept)
{
	if (IsLocalController()) ServerMarketConsent(bAccept);
}

void AOreDealTablePlayerController::ServerMarketSelectTarget_Implementation(APlayerState* Target)
{
	if (MarketBroker) MarketBroker->SelectTarget(PlayerState, Target);
}

void AOreDealTablePlayerController::ServerMarketSetOffer_Implementation(const TArray<int32>& HandIndices)
{
	if (MarketBroker) MarketBroker->SetPlayerOffer(PlayerState, HandIndices);
}

void AOreDealTablePlayerController::ServerMarketBeginDeal_Implementation(APlayerState* Target, const TArray<int32>& MarketIndices)
{
	if (MarketBroker) MarketBroker->BeginDeal(PlayerState, Target, MarketIndices);
}

void AOreDealTablePlayerController::ServerMarketConsent_Implementation(bool bAccept)
{
	if (MarketBroker) MarketBroker->CastConsent(PlayerState, bAccept);
}

void AOreDealTablePlayerController::ServerSellWagon_Implementation(int32 WagonIndex)
{
	APlayerState* PS = PlayerState;
	AActor* Deck = FindCardDeck(GetWorld());
	int32 Phase = INDEX_NONE;
	if (!PS || WagonIndex < 0 || WagonIndex >= 3 || !IsPlayersTurn(Deck, PS, Phase) || Phase < 0 || Phase > 2) return;
	AActor* Board = GetPlayerBoard();
	if (!Board) return;
	if (UOreDealWagonStateComponent* WagonState = Board->FindComponentByClass<UOreDealWagonStateComponent>())
		if (WagonState->IsBroken(WagonIndex)) return;
	const FArrayProperty* TypesProp = FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonTypes"));
	const FArrayProperty* CountsProp = FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonCounts"));
	const FIntProperty* TypesInner = TypesProp ? CastField<FIntProperty>(TypesProp->Inner) : nullptr;
	const FIntProperty* CountsInner = CountsProp ? CastField<FIntProperty>(CountsProp->Inner) : nullptr;
	FIntProperty* GoldProp = FindFProperty<FIntProperty>(PS->GetClass(), TEXT("gold"));
	if (!TypesProp || !CountsProp || !TypesInner || !CountsInner || !GoldProp) return;
	FScriptArrayHelper Types(TypesProp, TypesProp->ContainerPtrToValuePtr<void>(Board));
	FScriptArrayHelper Counts(CountsProp, CountsProp->ContainerPtrToValuePtr<void>(Board));
	if (!Types.IsValidIndex(WagonIndex) || !Counts.IsValidIndex(WagonIndex)) return;
	const int32 Resource = TypesInner->GetPropertyValue(Types.GetRawPtr(WagonIndex));
	const int32 Count = CountsInner->GetPropertyValue(Counts.GetRawPtr(WagonIndex));
	if (Resource < 0 || Resource >= UE_ARRAY_COUNT(ControllerResourceNames) || Count <= 0) return;
	if (Count < 2 && !CanSellSingleCardSlot(Counts, CountsInner, WagonIndex)) return;
	const int32 Reward = SaleReward(Resource, Count, FindCardInfoAsset(this));
	TypesInner->SetPropertyValue(Types.GetRawPtr(WagonIndex), INDEX_NONE);
	CountsInner->SetPropertyValue(Counts.GetRawPtr(WagonIndex), 0);
	GoldProp->SetPropertyValue_InContainer(PS, GoldProp->GetPropertyValue_InContainer(PS) + Reward);

	if (AOreDealAuctionBroker* Broker = AOreDealAuctionBroker::Find(this))
	{
		Broker->AddToDiscardPile(Resource, Count);
	}

	const EOreDealSlotEffect EffectType = (Reward > 0) ? EOreDealSlotEffect::CoinBurst : EOreDealSlotEffect::MineralShatter;
	BroadcastWagonEffect(GetWorld(), Board, WagonIndex, EffectType);

	Board->ForceNetUpdate();
	PS->ForceNetUpdate();
	if (UFunction* UpdateVisual = Board->FindFunction(TEXT("UpdateVisual"))) Board->ProcessEvent(UpdateVisual, nullptr);
	if (UWorld* World = GetWorld())
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			if (AOreDealTablePlayerController* OtherPC = Cast<AOreDealTablePlayerController>(It->Get()))
				OtherPC->ClientShowSaleToast(ControllerResourceNames[Resource], Count);
}

int32 AOreDealTablePlayerController::CalculateSaleReward(int32 Resource, int32 Count, const UObject* WorldContext)
{
	return SaleReward(Resource, Count, FindCardInfoAsset(WorldContext));
}

void AOreDealTablePlayerController::ClientSetWagonFocus_Implementation(int32 WagonIndex)
{
	SetWagonFocus(WagonIndex);
}

void AOreDealTablePlayerController::ServerPlantHandCard_Implementation(int32 DesiredWagonIndex)
{
	APlayerState* PS = PlayerState;
	AActor* Deck = FindCardDeck(GetWorld());
	int32 Phase = INDEX_NONE;
	if (!PS) return;
	if (AOreDealAuctionBroker* Broker = AOreDealAuctionBroker::Find(this))
		if (Broker->GetStage() == EOreDealAuctionStage::PickReveal || Broker->GetStage() == EOreDealAuctionStage::RequestReview || Broker->GetStage() == EOreDealAuctionStage::ConfirmTrade)
		{
			Broker->PlantHandCard(PS, DesiredWagonIndex);
			return;
		}
	Phase = GetGlobalTurnPhase(Deck);
	if (Phase != 0) return;

	APawn* PlayerPawn = GetPawn();
	if (!IsValid(PlayerPawn)) return;

	const FArrayProperty* HandProp = FindFProperty<FArrayProperty>(PlayerPawn->GetClass(), TEXT("HandCards"));
	const FIntProperty* HandInner = HandProp ? CastField<FIntProperty>(HandProp->Inner) : nullptr;
	if (!HandProp || !HandInner) return;

	FScriptArrayHelper HandHelper(HandProp, HandProp->ContainerPtrToValuePtr<void>(PlayerPawn));
	if (HandHelper.Num() <= 0) return;

	const int32 Resource = HandInner->GetPropertyValue(HandHelper.GetRawPtr(0));

	AActor* Board = GetPlayerBoard();
	if (!IsValid(Board)) return;

	int32 PlacedWagonIndex = INDEX_NONE;
	if (!UOreDealTurnBridge::ExecutePlantCard(Board, PS, Resource, DesiredWagonIndex, PlacedWagonIndex))
	{
		return;
	}

	HandHelper.RemoveValues(0, 1);
	if (FIntProperty* CountProp = FindFProperty<FIntProperty>(PlayerPawn->GetClass(), TEXT("HandCount")))
	{
		CountProp->SetPropertyValue_InContainer(PlayerPawn, HandHelper.Num());
	}

	PlayerPawn->ForceNetUpdate();
	if (UFunction* OnRepHandCards = PlayerPawn->FindFunction(TEXT("OnRep_HandCards"))) PlayerPawn->ProcessEvent(OnRepHandCards, nullptr);
	if (UFunction* OnRepHandCount = PlayerPawn->FindFunction(TEXT("OnRep_HandCount"))) PlayerPawn->ProcessEvent(OnRepHandCount, nullptr);

	UOreDealTurnBridge::RecordMandatoryHandPlacement(Deck, PS);

	if (PlacedWagonIndex != INDEX_NONE)
	{
		ClientSetWagonFocus(PlacedWagonIndex);
	}
}

void AOreDealTablePlayerController::ServerRecordMandatoryHandPlacement_Implementation(AActor* CardDeck, APlayerState* PlacingPlayer)
{
	if (!IsValid(CardDeck) || !CardDeck->HasAuthority() || !IsValid(PlacingPlayer) || PlacingPlayer != PlayerState) return;
	UOreDealTurnBridge::RecordMandatoryHandPlacement(CardDeck, PlacingPlayer);
}

void AOreDealTablePlayerController::ServerRequestSupplyDraw_Implementation()
{
	AActor* Deck = FindCardDeck(GetWorld());
	int32 Phase = INDEX_NONE;
	APawn* PlayerPawn = GetPawn();
	if (!PlayerPawn || !IsPlayersTurn(Deck, PlayerState, Phase) || Phase != 3 || bSupplyDrawRequested) return;

	TArray<int32> Hand;
	GetLocalHandCards(Hand);
	SupplyDrawInitialHandCount = Hand.Num();
	SupplyDrawRequestedCount = FMath::Clamp(10 - Hand.Num(), 0, 3);
	bSupplyDrawRequested = true;
	if (SupplyDrawRequestedCount == 0) return;

	// Pre-draw cards from central deck
	SupplyDrawPendingCards.Reset();
	if (AOreDealAuctionBroker* Broker = AOreDealAuctionBroker::Find(this))
	{
		for (int32 I = 0; I < SupplyDrawRequestedCount; ++I)
		{
			const int32 Card = Broker->DrawCard();
			if (Card != INDEX_NONE)
			{
				SupplyDrawPendingCards.Add(Card);
				if (PlayerState)
					if (FIntProperty* Wood = FindFProperty<FIntProperty>(PlayerState->GetClass(), TEXT("Wood")))
						Wood->SetPropertyValue_InContainer(PlayerState, Wood->GetPropertyValue_InContainer(PlayerState) + (bHasWoodAxe ? 2 : 1));
			}
		}
	}
	if (!SupplyDrawPendingCards.IsEmpty())
	{
		SupplyDrawRequestedCount = SupplyDrawPendingCards.Num();
	}

	FIntProperty* DrawCountProperty = FindFProperty<FIntProperty>(PlayerPawn->GetClass(), TEXT("NextDrawCount"));
	UFunction* StartDraw = PlayerPawn->FindFunction(TEXT("ServerStartDraw"));
	if (!DrawCountProperty || !StartDraw)
	{
		bSupplyDrawRequested = false;
		SupplyDrawRequestedCount = 0;
		return;
	}
	DrawCountProperty->SetPropertyValue_InContainer(PlayerPawn, SupplyDrawRequestedCount);
	PlayerPawn->ProcessEvent(StartDraw, nullptr);
}

void AOreDealTablePlayerController::ServerCompleteTurnStage_Implementation()
{
	AActor* Deck = FindCardDeck(GetWorld());
	int32 Phase = INDEX_NONE;
	if (!IsPlayersTurn(Deck, PlayerState, Phase) || Phase == 1) return;
	// Phase 3/4 (placing taken cards) ends on its own once every player has placed them.
	if (Phase == 2)
		if (const AOreDealAuctionBroker* Auction = AOreDealAuctionBroker::Find(this))
			if (Auction->GetStage() != EOreDealAuctionStage::Idle) return;

	if (Phase == 3)
	{
		if (!bSupplyDrawRequested) return;
		TArray<int32> Hand;
		GetLocalHandCards(Hand);
		if (Hand.Num() < SupplyDrawInitialHandCount + SupplyDrawRequestedCount) return;
	}

	const bool bTurnComplete = UOreDealTurnBridge::CompleteCurrentTurnStage(Deck);
	const FIntProperty* PhaseProperty = FindFProperty<FIntProperty>(Deck->GetClass(), TEXT("TurnPhaseIndex"));
	const int32 NewPhase = PhaseProperty ? PhaseProperty->GetPropertyValue_InContainer(Deck) : INDEX_NONE;
	if (Phase == 0 && NewPhase == 1)
	{
		// Only the explicit E confirmation after mandatory placement reveals the two
		// shared cards. The placement function no longer reveals them as a side effect.
		if (UFunction* RevealTwo = Deck->FindFunction(TEXT("RevealTwo"))) Deck->ProcessEvent(RevealTwo, nullptr);
		if (UFunction* RevealIfEmpty = Deck->FindFunction(TEXT("RevealIfEmpty"))) Deck->ProcessEvent(RevealIfEmpty, nullptr);
		Deck->ForceNetUpdate();
	}
	else if (Phase == 2 && NewPhase == 3)
	{
		TArray<int32> Hand;
		GetLocalHandCards(Hand);
		SupplyDrawInitialHandCount = Hand.Num();
		SupplyDrawRequestedCount = 0;
		bSupplyDrawRequested = false;
	}
	else if (Phase == 3 && bTurnComplete)
	{
		bSupplyDrawRequested = false;
		SupplyDrawRequestedCount = 0;
		// BP_PC_Table owns the existing authoritative GameMode hand-off event.
		if (UFunction* EndTurn = FindFunction(TEXT("ServerEndTurn"))) ProcessEvent(EndTurn, nullptr);
	}
}

void AOreDealTablePlayerController::ServerSendChat_Implementation(const FString& Message)
{
	const FString Clean = Message.TrimStartAndEnd();
	if (Clean.IsEmpty()) return;
	const FString Sender = PlayerState ? PlayerState->GetPlayerName() : TEXT("Player");
	if (UWorld* World = GetWorld())
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(It->Get()))
				PC->ClientReceiveChat(Sender, Clean.Left(256));
}

void AOreDealTablePlayerController::ClientReceiveChat_Implementation(const FString& Sender, const FString& Message)
{
	if (InteractionWidget) InteractionWidget->AddChatMessage(Sender, Message);
}

void AOreDealTablePlayerController::ClientPlayProtectedCardFeedback_Implementation(int32 WagonIndex)
{
	AActor* Board = GetPlayerBoard();
	if (!Board) return;
	const FName CardName(*FString::Printf(TEXT("WagonCard%d"), WagonIndex));
	const FName GlowName(*FString::Printf(TEXT("WagonGlow%d"), WagonIndex));
	ProtectedCardComponent = nullptr;
	ProtectedGlowComponent = nullptr;
	for (UActorComponent* Component : Board->GetComponents())
	{
		if (!Component) continue;
		if (Component->GetFName() == CardName) ProtectedCardComponent = Cast<UPrimitiveComponent>(Component);
		if (Component->GetFName() == GlowName) ProtectedGlowComponent = Cast<UPrimitiveComponent>(Component);
	}
	if (UPrimitiveComponent* Card = ProtectedCardComponent.Get()) ProtectedCardStartLocation = Card->GetComponentLocation();
	ProtectedGlowOriginalMaterials.Reset();
	if (UPrimitiveComponent* Glow = ProtectedGlowComponent.Get())
	{
		for (int32 Index = 0; Index < Glow->GetNumMaterials(); ++Index)
		{
			UMaterialInterface* Original = Glow->GetMaterial(Index);
			ProtectedGlowOriginalMaterials.Add(Original);
			if (UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Original, this))
			{
				const FLinearColor Red(1.0f, 0.025f, 0.01f, 1.0f);
				Dynamic->SetVectorParameterValue(TEXT("Color"), Red);
				Dynamic->SetVectorParameterValue(TEXT("Tint"), Red);
				Dynamic->SetVectorParameterValue(TEXT("GlowColor"), Red);
				Dynamic->SetVectorParameterValue(TEXT("GlowTint"), Red);
				Dynamic->SetVectorParameterValue(TEXT("BaseColor"), Red);
				Dynamic->SetVectorParameterValue(TEXT("EmissiveColor"), Red);
				Glow->SetMaterial(Index, Dynamic);
			}
		}
	}
	ProtectedFeedbackWagonIndex = WagonIndex;
	ProtectedFeedbackElapsed = 0.5f;
}

void AOreDealTablePlayerController::ClientShowSaleToast_Implementation(const FString& Resource, int32 Count)
{
	if (InteractionWidget) InteractionWidget->ShowSaleToast(Resource, Count);
}

void AOreDealTablePlayerController::UpdatePointerSelection()
{
	FHitResult Hit;
	const bool bHitCard = TraceCenterPointer(Hit);
	ApplyPointerFocus(bHitCard ? GetCardFocusIndex(Hit) : INDEX_NONE);
}

bool AOreDealTablePlayerController::TraceCenterPointer(FHitResult& OutHit) const
{
	int32 Width = 0;
	int32 Height = 0;
	GetViewportSize(Width, Height);
	UWorld* World = GetWorld();
	if (!World || Width <= 0 || Height <= 0) return false;

	FVector RayOrigin;
	FVector RayDirection;
	if (!DeprojectScreenPositionToWorld(Width * 0.5f, Height * 0.5f, RayOrigin, RayDirection))
	{
		FRotator ViewRotation;
		GetPlayerViewPoint(RayOrigin, ViewRotation);
		RayDirection = ViewRotation.Vector();
	}
	if (RayDirection.IsNearlyZero()) return false;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(OreDealCenterPointer), false);
	QueryParams.AddIgnoredActor(GetPawn());
	const FVector RayEnd = RayOrigin + RayDirection.GetSafeNormal() * 10000.0f;
	// The draw deck and merchants are the world objects the centre pointer interacts with.
	AActor* Deck = FindCardDeck(World);
	const auto IsInteractive = [Deck](AActor* Actor)
	{
		if (!IsValid(Actor)) return false;
		if (Actor->ActorHasTag(TEXT("OreDealMerchant")) || Actor->ActorHasTag(TEXT("OreDealBottle"))) return true;
		if (const AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Actor))
			if (const UStaticMesh* Mesh = MeshActor->GetStaticMeshComponent()->GetStaticMesh())
				if (Mesh->GetFName() == TEXT("Table_Bell")) return true;
		return Actor == Deck;
	};
	if (World->LineTraceSingleByChannel(OutHit, RayOrigin, RayEnd, ECC_Visibility, QueryParams) && IsInteractive(OutHit.GetActor())) return true;
	if (World->LineTraceSingleByChannel(OutHit, RayOrigin, RayEnd, ECC_Camera, QueryParams) && IsInteractive(OutHit.GetActor())) return true;

	// Fall back to actor bounds so the centre pointer still activates actors without Visibility/Camera response.
	TArray<AActor*> Candidates;
	UGameplayStatics::GetAllActorsWithTag(World, TEXT("OreDealMerchant"), Candidates);
	TArray<AActor*> Bottles;
	UGameplayStatics::GetAllActorsWithTag(World, TEXT("OreDealBottle"), Bottles);
	Candidates.Append(Bottles);
	TArray<AActor*> MeshActors;
	UGameplayStatics::GetAllActorsOfClass(World, AStaticMeshActor::StaticClass(), MeshActors);
	for (AActor* MeshActor : MeshActors)
		if (IsInteractive(MeshActor)) Candidates.AddUnique(MeshActor);
	if (Deck) Candidates.AddUnique(Deck);

	const FVector Direction = RayDirection.GetSafeNormal();
	float NearestDistance = TNumericLimits<float>::Max();
	AActor* NearestActor = nullptr;

	for (AActor* Candidate : Candidates)
	{
		if (!IsInteractive(Candidate)) continue;
		const FBox Bounds = Candidate->GetComponentsBoundingBox(true);
		const float AlongRay = FVector::DotProduct(Bounds.GetCenter() - RayOrigin, Direction);
		if (AlongRay < 0.0f || AlongRay > 10000.0f || AlongRay >= NearestDistance) continue;
		if (FMath::PointDistToLine(Bounds.GetCenter(), Direction, RayOrigin) > Bounds.GetExtent().Size()) continue;
		NearestDistance = AlongRay;
		NearestActor = Candidate;
	}

	if (NearestActor)
	{
		OutHit = FHitResult(NearestActor, Cast<UPrimitiveComponent>(NearestActor->GetRootComponent()), RayOrigin + Direction * NearestDistance, -Direction);
		OutHit.Distance = NearestDistance;
		return true;
	}
	return false;
}

int32 AOreDealTablePlayerController::GetCardFocusIndex(const FHitResult& Hit) const
{
	AActor* HitActor = Hit.GetActor();
	if (!HitActor) return INDEX_NONE;
	int32 Phase = INDEX_NONE;
	AActor* Deck = FindCardDeck(GetWorld());
	if (!IsPlayersTurn(Deck, PlayerState, Phase)) return INDEX_NONE;
	AActor* LocalBoard = GetPlayerBoard();
	const bool bOwnBoard = HitActor == LocalBoard;
	const bool bPublicDeck = HitActor == Deck;
	if (!bOwnBoard && !bPublicDeck) return INDEX_NONE;
	const UPrimitiveComponent* Component = Hit.GetComponent();
	if (!Component)
	{
		return INDEX_NONE;
	}

	const FString Name = Component->GetName();
	// 덱 위 카드 are no longer focus targets here; the auction picks them with A/D.
	if (bOwnBoard && (Name.Contains(TEXT("WagonCard0")) || Name.Contains(TEXT("WagonGlow0")))) return 2;
	if (bOwnBoard && (Name.Contains(TEXT("WagonCard1")) || Name.Contains(TEXT("WagonGlow1")))) return 3;
	if (bOwnBoard && (Name.Contains(TEXT("WagonCard2")) || Name.Contains(TEXT("WagonGlow2")))) return 4;
	if (bPublicDeck && (Name.Contains(TEXT("WagonCard")) || Name.Contains(TEXT("WagonGlow")))) return INDEX_NONE;
	return INDEX_NONE;
}

void AOreDealTablePlayerController::SetBlueprintInt(const FName PropertyName, int32 Value)
{
	if (FIntProperty* Property = FindFProperty<FIntProperty>(GetClass(), PropertyName))
	{
		Property->SetPropertyValue_InContainer(this, Value);
	}
}

int32 AOreDealTablePlayerController::GetBlueprintInt(const FName PropertyName, int32 FallbackValue) const
{
	if (const FIntProperty* Property = FindFProperty<FIntProperty>(GetClass(), PropertyName))
	{
		return Property->GetPropertyValue_InContainer(this);
	}
	return FallbackValue;
}

void AOreDealTablePlayerController::RefreshBlueprintGlow()
{
	if (UFunction* Function = FindFunction(TEXT("RefreshGlow")))
	{
		ProcessEvent(Function, nullptr);
	}
}

void AOreDealTablePlayerController::ApplyPointerFocus(int32 FocusIndex)
{
	if (PointerFocusIndex == FocusIndex)
	{
		return;
	}

	PointerFocusIndex = FocusIndex;
	SetBlueprintInt(TEXT("FocusIndex"), FocusIndex);
	SetBlueprintInt(TEXT("SelectedReveal"), FocusIndex >= 0 && FocusIndex <= 1 ? FocusIndex : INDEX_NONE);
	RefreshBlueprintGlow();
}

void AOreDealTablePlayerController::CleanUpBoardWagonTexts()
{
	UWorld* World = GetWorld();
	if (!World) return;
	UClass* BoardClass = StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/Game/MaterialTraders/Blueprints/BP_CardBoard.BP_CardBoard_C"));
	if (!BoardClass) return;
	TArray<AActor*> Boards;
	UGameplayStatics::GetAllActorsOfClass(World, BoardClass, Boards);
	for (AActor* Board : Boards)
	{
		TArray<UTextRenderComponent*> TextComps;
		Board->GetComponents<UTextRenderComponent>(TextComps);
		for (UTextRenderComponent* TextComp : TextComps)
		{
			TextComp->SetText(FText::GetEmpty());
			TextComp->SetVisibility(false, true);
			TextComp->SetHiddenInGame(true);
			TextComp->SetWorldScale3D(FVector::ZeroVector);
		}
	}
}

void AOreDealTablePlayerController::EnsureSeatMerchantBells()
{
	UWorld* World = GetWorld();
	if (!World) return;
	UClass* BoardClass = StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/Game/MaterialTraders/Blueprints/BP_CardBoard.BP_CardBoard_C"));
	UStaticMesh* BellMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Asset/TableBell/StaticMeshes/Table_Bell.Table_Bell"));
	if (!BoardClass || !BellMesh) return;
	TArray<AActor*> Boards;
	UGameplayStatics::GetAllActorsOfClass(World, BoardClass, Boards);
	TArray<AActor*> ExistingBells;
	UGameplayStatics::GetAllActorsWithTag(World, TEXT("OreDealMerchant"), ExistingBells);
	for (AActor* Board : Boards)
	{
		if (!IsValid(Board)) continue;
		USceneComponent* SlotPivot = nullptr;
		for (UActorComponent* Component : Board->GetComponents())
			if (Component && Component->GetFName() == TEXT("WagonPivot0")) { SlotPivot = Cast<USceneComponent>(Component); break; }
		if (!SlotPivot) continue;
		const FVector PivotLocation = SlotPivot->GetComponentLocation();
		bool bAlreadyServed = false;
		for (AActor* Bell : ExistingBells)
			if (IsValid(Bell) && FVector::DistSquared(Bell->GetActorLocation(), PivotLocation) < FMath::Square(70.0f)) { bAlreadyServed = true; break; }
		if (bAlreadyServed) continue;
		const FVector Location = PivotLocation - SlotPivot->GetForwardVector() * 28.0f + SlotPivot->GetRightVector() * 36.0f + FVector(0, 0, 2.0f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Bell = World->SpawnActor<AStaticMeshActor>(Location, SlotPivot->GetComponentRotation(), Params);
		if (!Bell) continue;
		Bell->GetStaticMeshComponent()->SetStaticMesh(BellMesh);
		Bell->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Bell->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Bell->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
		Bell->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Bell->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
		Bell->Tags.AddUnique(TEXT("OreDealMerchant"));
		// Each client creates its local interactive copy so the static mesh and merchant tag are
		// available immediately without relying on runtime component-property replication.
		Bell->SetReplicates(false);
		ExistingBells.Add(Bell);
	}
}

void AOreDealTablePlayerController::UpdateWagonStackLabels()
{
	UWorld* World = GetWorld();
	if (!World) return;
	UClass* BoardClass = StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/Game/MaterialTraders/Blueprints/BP_CardBoard.BP_CardBoard_C"));
	if (!BoardClass) return;
	TArray<AActor*> Boards;
	UGameplayStatics::GetAllActorsOfClass(World, BoardClass, Boards);
	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	GetPlayerViewPoint(CameraLocation, CameraRotation);
	for (AActor* Board : Boards)
	{
		if (!IsValid(Board)) continue;
		const FArrayProperty* CountsProperty = FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("wagonCounts"));
		const FIntProperty* CountsInner = CountsProperty ? CastField<FIntProperty>(CountsProperty->Inner) : nullptr;
		if (!CountsProperty || !CountsInner) continue;
		FScriptArrayHelper Counts(CountsProperty, CountsProperty->ContainerPtrToValuePtr<void>(Board));
		for (int32 Slot = 0; Slot < Counts.Num(); ++Slot)
		{
			const FName PivotName(*FString::Printf(TEXT("WagonPivot%d"), Slot));
			const FName LabelName(*FString::Printf(TEXT("OreDealStackCount%d"), Slot));
			USceneComponent* Pivot = nullptr;
			UTextRenderComponent* Label = nullptr;
			for (UActorComponent* Component : Board->GetComponents())
			{
				if (!Component) continue;
				if (Component->GetFName() == PivotName) Pivot = Cast<USceneComponent>(Component);
				if (Component->GetFName() == LabelName) Label = Cast<UTextRenderComponent>(Component);
			}
			if (!Pivot) continue;
			if (!Label)
			{
				Label = NewObject<UTextRenderComponent>(Board, LabelName);
				Label->SetupAttachment(Board->GetRootComponent());
				Label->SetHorizontalAlignment(EHTA_Center);
				Label->SetVerticalAlignment(EVRTA_TextCenter);
				Label->SetWorldSize(11.0f);
				Label->SetTextRenderColor(FColor(58, 37, 24));
				Label->SetCastShadow(false);
				Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Label->RegisterComponent();
			}
			const int32 Count = CountsInner->GetPropertyValue(Counts.GetRawPtr(Slot));
			Label->SetText(Count > 0 ? FText::FromString(FString::Printf(TEXT("x%d"), Count)) : FText::GetEmpty());
			Label->SetVisibility(Count > 0);
			Label->SetWorldLocation(Pivot->GetComponentLocation() + Pivot->GetRightVector() * 10.0f + Pivot->GetUpVector() * 8.0f);
			Label->SetWorldRotation((CameraLocation - Label->GetComponentLocation()).Rotation());
		}
	}
}

void AOreDealTablePlayerController::UpdateProtectedCardFeedback(float DeltaTime)
{
	if (ProtectedFeedbackElapsed <= 0.0f) return;
	ProtectedFeedbackElapsed = FMath::Max(0.0f, ProtectedFeedbackElapsed - DeltaTime);
	if (UPrimitiveComponent* Card = ProtectedCardComponent.Get())
	{
		const float Time = 0.5f - ProtectedFeedbackElapsed;
		const float Envelope = FMath::Sin(PI * FMath::Clamp(Time / 0.5f, 0.0f, 1.0f));
		const float Shake = FMath::Sin(Time * 70.0f) * 8.0f * Envelope;
		Card->SetWorldLocation(ProtectedCardStartLocation + Card->GetRightVector() * Shake);
	}
	if (ProtectedFeedbackElapsed <= 0.0f)
	{
		if (UPrimitiveComponent* Card = ProtectedCardComponent.Get()) Card->SetWorldLocation(ProtectedCardStartLocation);
		if (UPrimitiveComponent* Glow = ProtectedGlowComponent.Get())
			for (int32 Index = 0; Index < ProtectedGlowOriginalMaterials.Num(); ++Index)
				Glow->SetMaterial(Index, ProtectedGlowOriginalMaterials[Index]);
		ProtectedFeedbackWagonIndex = INDEX_NONE;
		ProtectedGlowOriginalMaterials.Reset();
	}
}

void AOreDealTablePlayerController::BroadcastWagonEffect(UWorld* World, AActor* Board, int32 WagonIndex, EOreDealSlotEffect EffectType)
{
	if (!World) return;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (AOreDealTablePlayerController* PC = Cast<AOreDealTablePlayerController>(It->Get()))
		{
			PC->ClientPlayWagonEffect(Board, WagonIndex, EffectType);
		}
	}
}

void AOreDealTablePlayerController::ClientPlayWagonEffect_Implementation(AActor* Board, int32 WagonIndex, EOreDealSlotEffect EffectType)
{
	if (!Board) return;

	FVector SlotLocation = Board->GetActorLocation();
	FRotator SlotRotation = Board->GetActorRotation();
	const FName CardName(*FString::Printf(TEXT("WagonCard%d"), WagonIndex));
	UPrimitiveComponent* WagonCard = nullptr;
	for (UActorComponent* Comp : Board->GetComponents())
		if (Comp && Comp->GetFName() == CardName) { WagonCard = Cast<UPrimitiveComponent>(Comp); break; }
	if (WagonCard)
	{
		const FBox CardBounds = WagonCard->Bounds.GetBox();
		SlotLocation = CardBounds.GetCenter();
		SlotLocation.Z = CardBounds.Max.Z + 1.0f;
		SlotRotation = WagonCard->GetComponentRotation();
	}
	const FName PivotNames[] = { TEXT("WagonPivot0"), TEXT("WagonPivot1"), TEXT("WagonPivot2") };
	else if (WagonIndex >= 0 && WagonIndex < UE_ARRAY_COUNT(PivotNames))
	{
		for (UActorComponent* Comp : Board->GetComponents())
		{
			if (Comp && Comp->GetFName() == PivotNames[WagonIndex])
			{
				if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
				{
					SlotLocation = SceneComp->GetComponentLocation();
					break;
				}
			}
		}
	}
	if (SlotLocation.Equals(Board->GetActorLocation())) SlotLocation.Z += 1.0f;

	const TCHAR* SystemPath = (EffectType == EOreDealSlotEffect::CoinBurst)
		? TEXT("/Game/MaterialTraders/Effects/NS_CoinBurst.NS_CoinBurst")
		: TEXT("/Game/MaterialTraders/Effects/NS_MineralShatter.NS_MineralShatter");

	UNiagaraSystem* EffectSystem = LoadObject<UNiagaraSystem>(nullptr, SystemPath);
	if (!EffectSystem)
	{
		const TCHAR* FallbackPath = (EffectType == EOreDealSlotEffect::CoinBurst)
			? TEXT("/Game/Asset/NS_CoinBurst.NS_CoinBurst")
			: TEXT("/Game/Asset/NS_MineralShatter.NS_MineralShatter");
		EffectSystem = LoadObject<UNiagaraSystem>(nullptr, FallbackPath);
	}

	if (EffectSystem && GetWorld())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), EffectSystem, SlotLocation, SlotRotation, FVector(1.0f));
	}

	// Trigger Blueprint event on Board if defined (BP_PlaySlotEffect)
	if (UFunction* BpFn = Board->FindFunction(TEXT("BP_PlaySlotEffect")))
	{
		struct FSlotEffectParams
		{
			int32 InWagonIndex;
			bool bInIsSale;
			FVector InLocation;
		} Params{ WagonIndex, EffectType == EOreDealSlotEffect::CoinBurst, SlotLocation };
		Board->ProcessEvent(BpFn, &Params);
	}
}

// ---------------------------------------------------------------------------
// 게임 종료 세레머니 & 카메라 연출 구현
// ---------------------------------------------------------------------------

void AOreDealTablePlayerController::ClientStartGameOverCeremony_Implementation(APlayerState* InWinner)
{
	bInGameOverCeremony = true;
	bWinnerCameraPhase = false;
	bRandomCameraPhase = false;
	bResultWidgetOpen = false;
	bCanPressEnterForMainMenu = false;
	bResultPromptActive = false;
	CeremonyStage = 0;
	CeremonyTimeElapsed = 0.0f;
	CurrentCameraTimer = 0.0f;
	WinnerState = InWinner;
	CeremonyPlayers.Reset();
	SpotlightIndex = 0;

	UWorld* World = GetWorld();
	if (!World) return;

	// Keep only the HUD/status layer. The ceremony title is shown in its own hit-test-invisible overlay.
	if (InteractionWidget) InteractionWidget->SetVisibility(ESlateVisibility::Hidden);
	if (HandViewWidget) HandViewWidget->SetVisibility(ESlateVisibility::Hidden);
	if (InventoryWidget) InventoryWidget->SetVisibility(ESlateVisibility::Hidden);
	bShowMouseCursor = false;
	FInputModeGameOnly GameInputMode;
	SetInputMode(GameInputMode);

	if (!CeremonyWidget)
		CeremonyWidget = CreateWidget<UOreDealCeremonyWidget>(this, UOreDealCeremonyWidget::StaticClass());
	if (CeremonyWidget)
	{
		if (!CeremonyWidget->IsInViewport()) CeremonyWidget->AddToViewport(34000);
		CeremonyWidget->SetPrompt(FText::FromString(TEXT("게임 종료!")), false);
	}

	// Locate the fixed Winner_Camera and the level's spot light.
	AActor* WinnerCamera = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		const FString ObjectName = Actor->GetName();
		if (ObjectName.Contains(TEXT("Winner_Camera"), ESearchCase::IgnoreCase))
		{
			WinnerCamera = Actor;
		}
		if (ASpotLight* Light = Cast<ASpotLight>(Actor))
		{
			CeremonySpotlight = Light;
		}
	}

	// Center the full player line behind the bar in the Winner_Camera composition.
	TArray<APlayerState*> AllPlayers;
	if (AGameStateBase* GS = World->GetGameState())
	{
		AllPlayers = GS->PlayerArray;
	}
	int32 ValidPawnCount = 0;
	for (APlayerState* PS : AllPlayers)
	{
		if (PS && PS->GetPawn()) ++ValidPawnCount;
	}
	const float CenterY = 96.0f;
	const float FirstY = CenterY - FMath::Max(ValidPawnCount - 1, 0) * 40.0f;
	int32 PlayerIndex = 0;
	for (APlayerState* PS : AllPlayers)
	{
		APawn* PlayerPawn = PS ? PS->GetPawn() : nullptr;
		if (!PlayerPawn) continue;
		const FVector LineLocation(-527.0f, FirstY + PlayerIndex * 80.0f, 95.0f);
		PlayerPawn->SetActorLocationAndRotation(LineLocation, FRotator(0.0f, 0.0f, 0.0f));
		if (FStructProperty* LookRotProp = FindFProperty<FStructProperty>(PlayerPawn->GetClass(), TEXT("lookRot")))
		{
			const FRotator NeutralLook(0.0f, 0.0f, 0.0f);
			LookRotProp->SetValue_InContainer(PlayerPawn, &NeutralLook);
		}
		CeremonyPlayers.Add(PlayerPawn);
		++PlayerIndex;
	}

	if (ASpotLight* Light = CeremonySpotlight.Get(); Light && Light->GetLightComponent())
	{
		Light->GetLightComponent()->SetVisibility(false);
		Light->GetLightComponent()->SetIntensity(32000.0f);
	}

	if (WinnerCamera) SetViewTargetWithBlend(WinnerCamera, 0.5f);
	WinnerCoinSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/MaterialTraders/Effects/NS_CoinBurst.NS_CoinBurst"));
	if (!WinnerCoinSystem) WinnerCoinSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Asset/NS_CoinBurst.NS_CoinBurst"));
}

void AOreDealTablePlayerController::UpdateGameOverCeremony(float DeltaTime)
{
	if (!bInGameOverCeremony || bResultWidgetOpen) return;
	CeremonyTimeElapsed += DeltaTime;
	if (CeremonyStage == 0 && CeremonyTimeElapsed >= 5.0f)
	{
		CeremonyStage = 1;
		CeremonyTimeElapsed = 0.0f;
		bWinnerCameraPhase = true;
		if (CeremonyWidget) CeremonyWidget->SetPrompt(FText::FromString(TEXT("결과는?")), true);
		if (ASpotLight* Light = CeremonySpotlight.Get()) Light->GetLightComponent()->SetVisibility(true);
	}
	else if (CeremonyStage == 1 && CeremonyTimeElapsed >= 10.0f)
	{
		CeremonyStage = 2;
		CeremonyTimeElapsed = 0.0f;
		bWinnerCameraPhase = false;
	}
	UpdateCeremonySpotlight(DeltaTime);
	if (CeremonyStage == 2)
	{
		ASpotLight* Light = CeremonySpotlight.Get();
		APawn* WinnerPawn = WinnerState.IsValid() ? WinnerState->GetPawn() : nullptr;
		if (!Light || !WinnerPawn)
		{
			CeremonyStage = 3;
			bRandomCameraPhase = true;
			bResultPromptActive = true;
			const FString WinnerName = WinnerState.IsValid() ? WinnerState->GetPlayerName() : TEXT("플레이어");
			if (CeremonyWidget) CeremonyWidget->SetPrompt(FText::FromString(FString::Printf(TEXT("%s 우승!"), *WinnerName)), true);
			if (UWorld* World = GetWorld(); World && WinnerCoinSystem)
			{
				SpawnWinnerCoinBurst();
				World->GetTimerManager().SetTimer(WinnerCoinTimerHandle, this, &AOreDealTablePlayerController::SpawnWinnerCoinBurst, 0.75f, true, 0.75f);
			}
		}
		else
		{
			const FBox Bounds = WinnerPawn->GetComponentsBoundingBox(true);
			const FVector SpotlightStop(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Max.Z + 264.0f);
			if (FVector::DistSquared(Light->GetActorLocation(), SpotlightStop) <= FMath::Square(20.0f))
			{
				Light->SetActorLocation(SpotlightStop);
				Light->SetActorRotation((FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Max.Z + 4.0f) - SpotlightStop).Rotation());
				CeremonyStage = 3;
				bRandomCameraPhase = true;
				bResultPromptActive = true;
				const FString WinnerName = WinnerState->GetPlayerName();
				if (CeremonyWidget) CeremonyWidget->SetPrompt(FText::FromString(FString::Printf(TEXT("%s 우승!"), *WinnerName)), true);
				if (UWorld* World = GetWorld(); World && WinnerCoinSystem)
				{
					SpawnWinnerCoinBurst();
					World->GetTimerManager().SetTimer(WinnerCoinTimerHandle, this, &AOreDealTablePlayerController::SpawnWinnerCoinBurst, 0.75f, true, 0.75f);
				}
			}
		}
	}
}

void AOreDealTablePlayerController::UpdateCeremonySpotlight(float DeltaTime)
{
	ASpotLight* Light = CeremonySpotlight.Get();
	if (!Light || CeremonyPlayers.IsEmpty()) return;
	int32 TargetIndex = 0;
	if (CeremonyStage >= 2)
	{
		for (int32 Index = 0; Index < CeremonyPlayers.Num(); ++Index)
			if (CeremonyPlayers[Index].IsValid() && CeremonyPlayers[Index]->GetPlayerState() == WinnerState.Get()) { TargetIndex = Index; break; }
	}
	else
	{
		const int32 PlayerCount = CeremonyPlayers.Num();
		const int32 CycleSize = FMath::Max(2 * PlayerCount - 2, 1);
		const float Progress = CeremonyTimeElapsed / 1.15f;
		const int32 Segment = FMath::FloorToInt(Progress);
		const int32 CycleIndex = Segment % CycleSize;
		TargetIndex = CycleIndex < PlayerCount ? CycleIndex : CycleSize - CycleIndex;
	}
	APawn* TargetPawn = CeremonyPlayers.IsValidIndex(TargetIndex) ? CeremonyPlayers[TargetIndex].Get() : nullptr;
	if (!TargetPawn) return;
	const FBox Bounds = TargetPawn->GetComponentsBoundingBox(true);
	const FVector HeadPoint(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Max.Z + 4.0f);
	const FVector DesiredLocation = HeadPoint + FVector(0.0f, 0.0f, 260.0f);
	const FRotator DesiredRotation = (HeadPoint - DesiredLocation).Rotation();
	Light->SetActorLocation(FMath::VInterpTo(Light->GetActorLocation(), DesiredLocation, DeltaTime, 8.0f));
	Light->SetActorRotation(FMath::RInterpTo(Light->GetActorRotation(), DesiredRotation, DeltaTime, 8.0f));
}

void AOreDealTablePlayerController::SpawnWinnerCoinBurst()
{
	UWorld* World = GetWorld();
	APawn* WinnerPawn = WinnerState.IsValid() ? WinnerState->GetPawn() : nullptr;
	if (!World || !WinnerPawn || !WinnerCoinSystem) return;
	const FBox Bounds = WinnerPawn->GetComponentsBoundingBox(true);
	const FVector CoinSpawnLocation(Bounds.GetCenter().X + FMath::FRandRange(-35.0f, 35.0f), Bounds.GetCenter().Y + FMath::FRandRange(-35.0f, 35.0f), Bounds.Max.Z + 220.0f);
	if (UNiagaraComponent* CoinBurst = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, WinnerCoinSystem, CoinSpawnLocation, FRotator::ZeroRotator, FVector(0.55f)))
		CoinBurst->SetAutoDestroy(true);
}

void AOreDealTablePlayerController::SwitchToNextRandomCamera()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// 이전 카메라 위치 초기화
	if (CurrentActiveCamera.IsValid() && CameraInitialLocations.Contains(CurrentCameraIndex))
	{
		CurrentActiveCamera->SetActorLocation(CameraInitialLocations[CurrentCameraIndex]);
	}

	// 다음 카메라 랜덤 선택 (2, 3, 4 중 이전과 다른 것)
	TArray<int32> Candidates = { 2, 3, 4 };
	if (Candidates.Contains(CurrentCameraIndex) && Candidates.Num() > 1)
	{
		Candidates.Remove(CurrentCameraIndex);
	}
	const int32 PickIndex = FMath::RandRange(0, Candidates.Num() - 1);
	CurrentCameraIndex = Candidates[PickIndex];
	CurrentCameraTimer = 0.0f;

	const FString TargetLabel = FString::Printf(TEXT("CineCameraActor%d"), CurrentCameraIndex);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName().StartsWith(TargetLabel, ESearchCase::IgnoreCase))
		{
			CurrentActiveCamera = *It;
			// 현재 초기 위치 저장 (맵에 없을 시)
			if (!CameraInitialLocations.Contains(CurrentCameraIndex))
			{
				CameraInitialLocations.Add(CurrentCameraIndex, It->GetActorLocation());
			}
			SetViewTargetWithBlend(*It, 1.0f);
			break;
		}
	}
}

void AOreDealTablePlayerController::ShowResultScreen()
{
	if (bResultWidgetOpen) return;
	bResultWidgetOpen = true;

	UWorld* World = GetWorld();
	if (!World) return;
	World->GetTimerManager().ClearTimer(WinnerCoinTimerHandle);
	if (CeremonyWidget) CeremonyWidget->HidePrompt();

	UClass* ResultWidgetClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/MaterialTraders/UI/WBP_Result.WBP_Result_C"));
	if (ResultWidgetClass)
	{
		ResultWidgetInstance = CreateWidget<UUserWidget>(this, ResultWidgetClass);
		if (ResultWidgetInstance)
		{
			ResultWidgetInstance->SetVisibility(ESlateVisibility::Visible);
			ResultWidgetInstance->SetIsFocusable(true);
			ResultWidgetInstance->AddToViewport(33000);
			BindResultButtons();
			World->GetTimerManager().SetTimerForNextTick(this, &AOreDealTablePlayerController::BindResultButtons);
			FInputModeGameAndUI ResultInputMode;
			ResultInputMode.SetWidgetToFocus(ResultWidgetInstance->TakeWidget());
			ResultInputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			SetInputMode(ResultInputMode);
			bShowMouseCursor = true;
			ResultWidgetInstance->SetKeyboardFocus();
		}
	}

	bCanPressEnterForMainMenu = true;
}

void AOreDealTablePlayerController::BindResultButtons()
{
	UUserWidget* ResultWidget = ResultWidgetInstance;
	if (!ResultWidget || !ResultWidget->WidgetTree) return;
	TArray<UWidget*> Widgets;
	ResultWidget->WidgetTree->GetAllWidgets(Widgets);
	TArray<UButton*> UnassignedButtons;
	bool bHasRematch = false;
	bool bHasMainMenu = false;
	TFunction<void(UWidget*, FString&)> CollectText = [&CollectText](UWidget* Root, FString& OutText)
	{
		if (!Root) return;
		if (const UTextBlock* TextBlock = Cast<UTextBlock>(Root)) OutText += TextBlock->GetText().ToString();
		if (const URichTextBlock* RichText = Cast<URichTextBlock>(Root)) OutText += RichText->GetText().ToString();
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Root))
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index) CollectText(Panel->GetChildAt(Index), OutText);
		else if (const UContentWidget* Content = Cast<UContentWidget>(Root)) CollectText(Content->GetContent(), OutText);
	};
	for (UWidget* Widget : Widgets)
	{
		UButton* Button = Cast<UButton>(Widget);
		if (!Button) continue;
		FString ButtonText;
		CollectText(Button, ButtonText);
		const FString ButtonName = Button->GetName();
		const bool bRematch = ButtonText.Contains(TEXT("한 판 더")) || ButtonName.Contains(TEXT("Rematch"), ESearchCase::IgnoreCase) || ButtonName.Contains(TEXT("Retry"), ESearchCase::IgnoreCase);
		const bool bMainMenu = ButtonText.Contains(TEXT("메인 메뉴")) || ButtonName.Contains(TEXT("MainMenu"), ESearchCase::IgnoreCase);
		if (bRematch)
		{
			Button->SetIsEnabled(true);
			Button->OnClicked.Clear();
			Button->OnClicked.AddUniqueDynamic(this, &AOreDealTablePlayerController::HandleResultRematchClicked);
			bHasRematch = true;
		}
		else if (bMainMenu)
		{
			Button->SetIsEnabled(true);
			Button->OnClicked.Clear();
			Button->OnClicked.AddUniqueDynamic(this, &AOreDealTablePlayerController::HandleResultMainMenuClicked);
			bHasMainMenu = true;
		}
		else UnassignedButtons.Add(Button);
	}

	// Some versions of WBP_Result have text set by a binding after construction. If its text/name
	// cannot identify the two visible buttons yet, use their on-screen left-to-right positions.
	if ((!bHasRematch || !bHasMainMenu) && !UnassignedButtons.IsEmpty())
	{
		int32 ViewportWidth = 0, ViewportHeight = 0;
		GetViewportSize(ViewportWidth, ViewportHeight);
		UnassignedButtons.Sort([](const UButton& A, const UButton& B)
		{
			return A.GetCachedGeometry().GetAbsolutePosition().X < B.GetCachedGeometry().GetAbsolutePosition().X;
		});
		for (UButton* Button : UnassignedButtons)
		{
			if (bHasRematch && bHasMainMenu) break;
			const float X = Button->GetCachedGeometry().GetAbsolutePosition().X;
			const bool bLeftSide = ViewportWidth <= 0 ? !bHasRematch : X < ViewportWidth * 0.5f;
			const bool bBindRematch = bLeftSide ? !bHasRematch : bHasMainMenu;
			Button->SetIsEnabled(true);
			Button->OnClicked.Clear();
			if (bBindRematch)
			{
				Button->OnClicked.AddUniqueDynamic(this, &AOreDealTablePlayerController::HandleResultRematchClicked);
				bHasRematch = true;
			}
			else
			{
				Button->OnClicked.AddUniqueDynamic(this, &AOreDealTablePlayerController::HandleResultMainMenuClicked);
				bHasMainMenu = true;
			}
		}
	}
}

void AOreDealTablePlayerController::ReturnToMainMenu()
{
	UWorld* World = GetWorld();
	if (!World) return;

	UGameplayStatics::OpenLevel(World, FName(TEXT("Lvl_MainMenu")));
}

void AOreDealTablePlayerController::RestartCurrentMatch()
{
	UWorld* World = GetWorld();
	if (!World) return;
	const FString CurrentMap = UGameplayStatics::GetCurrentLevelName(World, true);
	if (!CurrentMap.IsEmpty()) UGameplayStatics::OpenLevel(World, FName(*CurrentMap));
}

void AOreDealTablePlayerController::HandleResultRematchClicked()
{
	RestartCurrentMatch();
}

void AOreDealTablePlayerController::HandleResultMainMenuClicked()
{
	ReturnToMainMenu();
}
