#include "OreDealTablePlayerController.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "CollisionQueryParams.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/StaticMeshActor.h"
#include "InputMappingContext.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "Components/EditableTextBox.h"
#include "OreDealInteractionWidget.h"
#include "OreDealMarketBroker.h"
#include "OreDealTurnBridge.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* ResourceNames[] = { TEXT("Wood"), TEXT("Coal"), TEXT("Copper"), TEXT("Iron"), TEXT("Silver"), TEXT("Gold"), TEXT("Diamond"), TEXT("Adamantium") };

	int32 SaleReward(int32 Resource, int32 Count)
	{
		static const int32 Thresholds[8][4] = {
			{4, 6, 8, 10}, {3, 5, 7, 8}, {3, 4, 6, 7}, {2, 4, 5, 6},
			{2, 4, 6, 7}, {2, 4, 5, 6}, {2, 3, 4, 5}, {2, 3, 0, 0}
		};
		static const int32 Rewards[8][4] = {
			{1, 2, 3, 4}, {1, 2, 3, 4}, {1, 2, 3, 4}, {1, 2, 3, 4},
			{1, 2, 3, 4}, {1, 2, 3, 4}, {1, 2, 3, 4}, {2, 3, 0, 0}
		};
		if (Resource < 0 || Resource >= UE_ARRAY_COUNT(ResourceNames)) return 0;
		int32 Reward = 0;
		for (int32 I = 0; I < 4 && Thresholds[Resource][I] > 0; ++I)
			if (Count >= Thresholds[Resource][I]) Reward = Rewards[Resource][I];
		return Reward;
	}

	UClass* LoadBlueprintClass(const TCHAR* Path)
	{
		return StaticLoadClass(AActor::StaticClass(), nullptr, Path);
	}

	AActor* FindBoardForPawn(UWorld* World, APawn* Pawn)
	{
		if (!World || !Pawn) return nullptr;
		UClass* BoardClass = LoadBlueprintClass(TEXT("/Game/MaterialTraders/Blueprints/BP_CardBoard.BP_CardBoard_C"));
		if (!BoardClass) return nullptr;
		TArray<AActor*> Boards;
		UGameplayStatics::GetAllActorsOfClass(World, BoardClass, Boards);
		AActor* Best = nullptr;
		double BestDistance = TNumericLimits<double>::Max();
		for (AActor* Board : Boards)
		{
			const double Distance = FVector::DistSquared(Pawn->GetActorLocation(), Board->GetActorLocation());
			if (Distance < BestDistance) { BestDistance = Distance; Best = Board; }
		}
		return Best;
	}

	AActor* FindCardDeck(UWorld* World)
	{
		if (!World) return nullptr;
		UClass* DeckClass = LoadBlueprintClass(TEXT("/Game/MaterialTraders/Blueprints/BP_CardDeck.BP_CardDeck_C"));
		return DeckClass ? UGameplayStatics::GetActorOfClass(World, DeckClass) : nullptr;
	}

	bool IsRackAtLocalSeat(UWorld* World, APawn* Pawn, AActor* ClickedRack)
	{
		if (!World || !Pawn || !ClickedRack) return false;
		AActor* LocalBoard = FindBoardForPawn(World, Pawn);
		if (!LocalBoard || ClickedRack == LocalBoard) return false;
		UClass* BoardClass = LoadBlueprintClass(TEXT("/Game/MaterialTraders/Blueprints/BP_CardBoard.BP_CardBoard_C"));
		if (!BoardClass) return false;
		TArray<AActor*> Boards;
		UGameplayStatics::GetAllActorsOfClass(World, BoardClass, Boards);
		AActor* NearestBoard = nullptr;
		double NearestDistance = TNumericLimits<double>::Max();
		for (AActor* Board : Boards)
		{
			if (!IsValid(Board)) continue;
			const double Distance = FVector::DistSquared(ClickedRack->GetActorLocation(), Board->GetActorLocation());
			if (Distance < NearestDistance) { NearestDistance = Distance; NearestBoard = Board; }
		}
		// A copied rack can be a plain StaticMeshActor with no gameplay tag. Identify it
		// by the seat area as well as an optional tag; this also handles rack assemblies
		// whose separate mesh pieces do not all carry the tag.
		const bool bExplicitRack = ClickedRack->ActorHasTag(TEXT("OreDealCardRack"));
		const double SeatRadius = 800.0;
		return NearestBoard == LocalBoard && (bExplicitRack || FVector::DistSquared(ClickedRack->GetActorLocation(), LocalBoard->GetActorLocation()) <= FMath::Square(SeatRadius));
	}

	bool IsRackCandidateAtLocalSeat(UWorld* World, APawn* Pawn, AActor* Actor)
	{
		if (!IsValid(Actor) || !IsRackAtLocalSeat(World, Pawn, Actor)) return false;
		if (Actor->ActorHasTag(TEXT("OreDealCardRack")) || Actor->GetName().Contains(TEXT("Rack"), ESearchCase::IgnoreCase)) return true;
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (const UActorComponent* Component : Components)
		{
			if (!Component) continue;
			if (Component->ComponentHasTag(TEXT("OreDealCardRack")) || Component->GetName().Contains(TEXT("Rack"), ESearchCase::IgnoreCase)) return true;
		}
		// Hand-built racks are often pasted as plain mesh actors without tags or a custom
		// name. Limit that fallback to small mesh pieces close to the local seat.
		return Actor->IsA<AStaticMeshActor>() && Actor->GetComponentsBoundingBox(true).GetExtent().Size() < 500.0f;
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
}

void AOreDealTablePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && GetWorld())
	{
		MarketBroker = Cast<AOreDealMarketBroker>(UGameplayStatics::GetActorOfClass(GetWorld(), AOreDealMarketBroker::StaticClass()));
		if (!MarketBroker)
		{
			FActorSpawnParameters SpawnInfo;
			SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			MarketBroker = GetWorld()->SpawnActor<AOreDealMarketBroker>(AOreDealMarketBroker::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo);
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
		KeyboardFocusIndex = GetBlueprintInt(TEXT("FocusIndex"), 0);
		KeyboardSelectedReveal = GetBlueprintInt(TEXT("SelectedReveal"), INDEX_NONE);
		bHasKeyboardFocusSnapshot = true;
		InteractionWidget = CreateWidget<UOreDealInteractionWidget>(this, UOreDealInteractionWidget::StaticClass());
		if (InteractionWidget) InteractionWidget->AddToViewport(32000);
		CachedCardDeck = FindCardDeck(GetWorld());
	}
}

void AOreDealTablePlayerController::PlayerTick(float DeltaTime)
{
	// Some Blueprint controllers become locally owned after BeginPlay in PIE/network
	// startup. Retry creation here so a late local-player assignment cannot leave the
	// interaction HUD permanently missing.
	if (IsLocalController() && !InteractionWidget)
	{
		InteractionWidget = CreateWidget<UOreDealInteractionWidget>(this, UOreDealInteractionWidget::StaticClass());
		if (InteractionWidget) InteractionWidget->AddToViewport(32000);
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

	if (IsLocalController() && bMouseSelectionActive)
	{
		UpdatePointerSelection();
	}
	else if (IsLocalController())
	{
		KeyboardFocusIndex = GetBlueprintInt(TEXT("FocusIndex"), KeyboardFocusIndex);
		KeyboardSelectedReveal = GetBlueprintInt(TEXT("SelectedReveal"), KeyboardSelectedReveal);
		bHasKeyboardFocusSnapshot = true;
	}
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
		const float RelativeYaw = FMath::FindDeltaAngleDegrees(ReferenceYaw, Rotation.Yaw);
		Rotation.Yaw = ReferenceYaw + FMath::Clamp(RelativeYaw, -MaxRelativeYawDegrees, MaxRelativeYawDegrees);
		SetControlRotation(Rotation);
	}
}

bool AOreDealTablePlayerController::InputKey(const FInputKeyEventArgs& Params)
{
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
			if (HitActor && IsRackCandidateAtLocalSeat(GetWorld(), GetPawn(), HitActor))
			{
				if (IsRackAtLocalSeat(GetWorld(), GetPawn(), Hit.GetActor())) ToggleHandRack(HitActor);
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
		if (Params.Key == EKeys::M && InteractionWidget && !InteractionWidget->IsChatExpanded())
		{
			ToggleSalesMenu();
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
	if (!InteractionWidget || InteractionWidget->IsChatExpanded() || InteractionWidget->IsHandRackOpen()) return;
	const bool bOpen = !InteractionWidget->IsSalesOpen();
	InteractionWidget->SetSalesOpen(bOpen);
	if (bOpen)
	{
		if (!InteractionWidget->IsInViewport()) InteractionWidget->AddToViewport(32000);
		InteractionWidget->SetVisibility(ESlateVisibility::Visible);
		InteractionWidget->RefreshSales();
	}
	ApplyInteractionInputMode(bOpen, false);
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
		AActor* PositionActor = RackActor ? RackActor : FindBoardForPawn(GetWorld(), GetPawn());
		int32 ViewportWidth = 0;
		int32 ViewportHeight = 0;
		GetViewportSize(ViewportWidth, ViewportHeight);
		FVector2D RackScreenPosition;
		if (PositionActor && ViewportWidth > 0 && ViewportHeight > 0 && ProjectWorldLocationToScreen(PositionActor->GetActorLocation(), RackScreenPosition, true))
			InteractionWidget->SetHandRackScreenPosition(RackScreenPosition, FVector2D(ViewportWidth, ViewportHeight));
	}
	ApplyInteractionInputMode(bOpen, false);
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
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
	if (bFocusChatInput && InteractionWidget && InteractionWidget->GetChatInputWidget())
		Mode.SetWidgetToFocus(InteractionWidget->GetChatInputWidget()->TakeWidget());
	SetInputMode(Mode);
	// FInputModeGameAndUI defaults to CaptureDuringMouseDown. Override that default
	// after applying the mode so the camera keeps receiving relative mouse motion
	// after the left button is released.
	if (UWorld* World = GetWorld())
		if (UGameViewportClient* Viewport = World->GetGameViewport())
			Viewport->SetMouseCaptureMode(EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
	if (bFocusChatInput && InteractionWidget && InteractionWidget->GetChatInputWidget()) InteractionWidget->GetChatInputWidget()->SetKeyboardFocus();
}

void AOreDealTablePlayerController::GetSaleOptionText(int32 WagonIndex, FString& OutText, bool& bOutCanSell) const
{
	OutText.Reset();
	bOutCanSell = false;
	APlayerState* PS = PlayerState;
	APawn* MyPawn = GetPawn();
	AActor* Board = FindBoardForPawn(GetWorld(), MyPawn);
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
	if (Resource < 0 || Resource >= UE_ARRAY_COUNT(ResourceNames) || Count <= 0)
	{
		OutText = FString::Printf(TEXT("마차 %d: 비어 있음"), WagonIndex + 1);
		return;
	}
	const int32 Reward = SaleReward(Resource, Count);
	bool bAnyWagonHasTwoOrMore = false;
	for (int32 I = 0; I < Counts.Num(); ++I)
		if (CountsInner->GetPropertyValue(Counts.GetRawPtr(I)) >= 2) { bAnyWagonHasTwoOrMore = true; break; }
	bOutCanSell = Count >= 2 || !bAnyWagonHasTwoOrMore;
	OutText = FString::Printf(TEXT("마차 %d · %s X %d개 · 보상 %dG%s"), WagonIndex + 1, ResourceNames[Resource], Count, Reward, Reward == 0 ? TEXT(" (0G 정산 가능)") : TEXT(""));
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
	AActor* Board = FindBoardForPawn(GetWorld(), GetPawn());
	if (!Board) return;
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
	if (Resource < 0 || Resource >= UE_ARRAY_COUNT(ResourceNames) || Count <= 0) return;
	bool bAnyWagonHasTwoOrMore = false;
	for (int32 I = 0; I < Counts.Num(); ++I)
		if (CountsInner->GetPropertyValue(Counts.GetRawPtr(I)) >= 2) { bAnyWagonHasTwoOrMore = true; break; }
	if (Count < 2 && bAnyWagonHasTwoOrMore) return;
	const int32 Reward = SaleReward(Resource, Count);
	TypesInner->SetPropertyValue(Types.GetRawPtr(WagonIndex), INDEX_NONE);
	CountsInner->SetPropertyValue(Counts.GetRawPtr(WagonIndex), 0);
	GoldProp->SetPropertyValue_InContainer(PS, GoldProp->GetPropertyValue_InContainer(PS) + Reward);
	Board->ForceNetUpdate();
	PS->ForceNetUpdate();
	if (UFunction* UpdateVisual = Board->FindFunction(TEXT("UpdateVisual"))) Board->ProcessEvent(UpdateVisual, nullptr);
	if (UWorld* World = GetWorld())
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			if (AOreDealTablePlayerController* OtherPC = Cast<AOreDealTablePlayerController>(It->Get()))
				OtherPC->ClientShowSaleToast(ResourceNames[Resource], Count);
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
	const auto IsInteractive = [this](AActor* Actor)
	{
		if (!IsValid(Actor)) return false;
		if (Actor->ActorHasTag(TEXT("OreDealMerchant")) || Actor->ActorHasTag(TEXT("OreDealCardRack"))) return true;
		if (IsRackCandidateAtLocalSeat(GetWorld(), GetPawn(), Actor)) return true;
		return Actor == FindCardDeck(GetWorld());
	};
	if (World->LineTraceSingleByChannel(OutHit, RayOrigin, RayEnd, ECC_Visibility, QueryParams) && IsInteractive(OutHit.GetActor())) return true;
	if (World->LineTraceSingleByChannel(OutHit, RayOrigin, RayEnd, ECC_Camera, QueryParams) && IsInteractive(OutHit.GetActor())) return true;

	// Some table meshes have no Visibility/Camera response. Fall back to their bounds so the
	// center pointer still activates tagged merchant/rack actors and the draw deck.
	TArray<AActor*> Candidates;
	UGameplayStatics::GetAllActorsWithTag(World, TEXT("OreDealMerchant"), Candidates);
	TArray<AActor*> RackActors;
	UGameplayStatics::GetAllActorsWithTag(World, TEXT("OreDealCardRack"), RackActors);
	for (AActor* Rack : RackActors) Candidates.AddUnique(Rack);
	TArray<AActor*> NearbyActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), NearbyActors);
	for (AActor* Candidate : NearbyActors)
	{
		if (IsRackCandidateAtLocalSeat(World, GetPawn(), Candidate)) Candidates.AddUnique(Candidate);
	}
	if (AActor* Deck = FindCardDeck(World)) Candidates.AddUnique(Deck);
	float NearestDistance = TNumericLimits<float>::Max();
	AActor* NearestActor = nullptr;
	for (AActor* Candidate : Candidates)
	{
		if (!IsInteractive(Candidate)) continue;
		const FBox Bounds = Candidate->GetComponentsBoundingBox(true);
		const FVector ToBounds = Bounds.GetCenter() - RayOrigin;
		const float AlongRay = FVector::DotProduct(ToBounds, RayDirection.GetSafeNormal());
		if (AlongRay < 0.0f || AlongRay > 10000.0f || AlongRay >= NearestDistance) continue;
		const float PerpendicularDistance = FMath::PointDistToLine(Bounds.GetCenter(), RayOrigin, RayDirection.GetSafeNormal());
		if (PerpendicularDistance > Bounds.GetExtent().Size()) continue;
		NearestDistance = AlongRay;
		NearestActor = Candidate;
	}
	if (!NearestActor) return false;
	OutHit = FHitResult(NearestActor, Cast<UPrimitiveComponent>(NearestActor->GetRootComponent()), RayOrigin + RayDirection.GetSafeNormal() * NearestDistance, -RayDirection.GetSafeNormal());
	OutHit.Distance = NearestDistance;
	return true;
}

int32 AOreDealTablePlayerController::GetCardFocusIndex(const FHitResult& Hit) const
{
	AActor* HitActor = Hit.GetActor();
	if (!HitActor) return INDEX_NONE;
	AActor* LocalBoard = FindBoardForPawn(GetWorld(), GetPawn());
	const bool bOwnBoard = HitActor == LocalBoard;
	const bool bPublicDeck = HitActor == FindCardDeck(GetWorld());
	if (!bOwnBoard && !bPublicDeck) return INDEX_NONE;
	const UPrimitiveComponent* Component = Hit.GetComponent();
	if (!Component)
	{
		return INDEX_NONE;
	}

	const FString Name = Component->GetName();
	if (bPublicDeck && (Name.Contains(TEXT("RevealCard0")) || Name.Contains(TEXT("RevealGlow0")))) return 0;
	if (bPublicDeck && (Name.Contains(TEXT("RevealCard1")) || Name.Contains(TEXT("RevealGlow1")))) return 1;
	if (bOwnBoard && (Name.Contains(TEXT("WagonCard0")) || Name.Contains(TEXT("WagonGlow0")))) return 2;
	if (bOwnBoard && (Name.Contains(TEXT("WagonCard1")) || Name.Contains(TEXT("WagonGlow1")))) return 3;
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
