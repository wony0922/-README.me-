#include "OreDealAuctionComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Math/RotationMatrix.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "OreDealAuctionBroker.h"
#include "OreDealAuctionDisplay.h"
#include "OreDealAuctionHudWidget.h"
#include "OreDealTablePlayerController.h"

namespace
{
	// 덱 위 카드 layout, in BP_CardDeck local space. Matches the existing RevealPivot0/1 (0, -/+9, 20)
	// and RevealCard size, so two cards look exactly like before.
	constexpr float DeckCardHeight = 20.0f;
	constexpr float DeckTwoCardOffset = 9.0f;     // 2 cards: Y = -9 / +9
	constexpr float DeckRowStep = 15.0f;          // 3+ cards: a little tighter than the 18 of the 2-card layout
	constexpr float DeckRowHeight = 14.0f;        // card height 11.9 + gap
	constexpr int32 DeckPerRow = 3;
	const TCHAR* BlueprintRevealParts[] = { TEXT("RevealCard0"), TEXT("RevealGlow0"), TEXT("RevealCard1"), TEXT("RevealGlow1") };
	constexpr float OfferCardScale = 1.1f;
	constexpr float OfferColumnStep = 12.0f;
	constexpr float OfferRowStep = 16.5f;
	constexpr int32 OfferPerRow = 5;
	constexpr float PendingCardScale = 0.8f;
	constexpr float PendingStep = 3.5f;

	/** 1 card: centre. 2 cards: the existing left/right pair. 3+: rows of 3, extra rows stack upwards. */
	FVector DeckSlotLocal(int32 Index, int32 Count)
	{
		if (Count <= 1) return FVector(0.0f, 0.0f, DeckCardHeight);
		if (Count == 2) return FVector(0.0f, Index == 0 ? -DeckTwoCardOffset : DeckTwoCardOffset, DeckCardHeight);
		const int32 Row = Index / DeckPerRow;
		const int32 Column = Index % DeckPerRow;
		const int32 InRow = FMath::Min(DeckPerRow, Count - Row * DeckPerRow);
		return FVector(0.0f, (Column - (InRow - 1) * 0.5f) * DeckRowStep, DeckCardHeight + Row * DeckRowHeight);
	}

	/** Top of the character's head (falls back to a fixed height above the pawn). */
	float HeadTopZ(const APawn* Pawn)
	{
		if (!Pawn) return 0.0f;
		TArray<UPrimitiveComponent*> Primitives;
		Pawn->GetComponents<UPrimitiveComponent>(Primitives);
		for (const UPrimitiveComponent* Primitive : Primitives)
			if (Primitive && Primitive->IsVisible() && Primitive->GetName().Contains(TEXT("Head")))
				return Primitive->Bounds.GetBox().Max.Z;
		return Pawn->GetActorLocation().Z + 60.0f;
	}

	/** Centre-aligned rows of PerRow cards growing upwards from Base, laid out along Right. */
	FVector GridLocation(int32 Index, int32 Count, int32 PerRow, const FVector& Base, const FVector& Right, float ColumnStep, float RowStep)
	{
		const int32 Row = Index / PerRow;
		const int32 Column = Index % PerRow;
		const int32 InRow = FMath::Min(PerRow, Count - Row * PerRow);
		return Base + Right * ((Column - (InRow - 1) * 0.5f) * ColumnStep) + FVector(0.0f, 0.0f, Row * RowStep);
	}
}

UOreDealAuctionComponent::UOreDealAuctionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetIsReplicatedByDefault(true);
}

// ---------------------------------------------------------------------------------------------
// Server RPCs: forward to the broker, which validates everything.

void UOreDealAuctionComponent::ServerPickReveal_Implementation(const TArray<int32>& RevealIndices)
{
	if (AOreDealAuctionBroker* B = GetBroker()) B->PickReveal(GetOwnerPlayerState(), RevealIndices);
}

void UOreDealAuctionComponent::ServerSubmitOffer_Implementation(const TArray<int32>& HandIndices)
{
	if (AOreDealAuctionBroker* B = GetBroker()) B->SubmitOffer(GetOwnerPlayerState(), HandIndices);
}

void UOreDealAuctionComponent::ServerSelectLot_Implementation(const TArray<int32>& PoolIndices)
{
	if (AOreDealAuctionBroker* B = GetBroker()) B->SelectLot(GetOwnerPlayerState(), PoolIndices);
}

void UOreDealAuctionComponent::ServerFocusPartner_Implementation(APlayerState* Target)
{
	if (AOreDealAuctionBroker* B = GetBroker()) B->FocusPartner(GetOwnerPlayerState(), Target);
}

void UOreDealAuctionComponent::ServerChoosePartner_Implementation(APlayerState* Target)
{
	if (AOreDealAuctionBroker* B = GetBroker()) B->ChoosePartner(GetOwnerPlayerState(), Target);
}

void UOreDealAuctionComponent::ServerPlacePending_Implementation(int32 WagonIndex)
{
	if (AOreDealAuctionBroker* B = GetBroker()) B->PlacePending(GetOwnerPlayerState(), WagonIndex);
}

// ---------------------------------------------------------------------------------------------
// Helpers

APlayerController* UOreDealAuctionComponent::GetOwnerController() const
{
	return Cast<APlayerController>(GetOwner());
}

APlayerState* UOreDealAuctionComponent::GetOwnerPlayerState() const
{
	const APlayerController* PC = GetOwnerController();
	return PC ? PC->PlayerState.Get() : nullptr;
}

AOreDealAuctionBroker* UOreDealAuctionComponent::GetBroker()
{
	if (!Broker.IsValid()) Broker = AOreDealAuctionBroker::Find(this);
	return Broker.Get();
}

bool UOreDealAuctionComponent::IsLocallyOwned() const
{
	const APlayerController* PC = GetOwnerController();
	return PC && PC->IsLocalController();
}

bool UOreDealAuctionComponent::IsAuctionRunning() const
{
	return Broker.IsValid() && Broker->GetStage() != EOreDealAuctionStage::Idle;
}

bool UOreDealAuctionComponent::IsActivePlayer() const
{
	return Broker.IsValid() && GetOwnerPlayerState() && Broker->GetActivePlayer() == GetOwnerPlayerState();
}

void UOreDealAuctionComponent::GetLocalHand(TArray<int32>& OutCards) const
{
	OutCards.Reset();
	const APlayerController* PC = GetOwnerController();
	if (const APawn* Pawn = PC ? PC->GetPawn() : nullptr) AOreDealAuctionBroker::ReadIntArray(Pawn, TEXT("HandCards"), OutCards);
}

void UOreDealAuctionComponent::GetMyPending(TArray<int32>& OutCards) const
{
	OutCards.Reset();
	if (!Broker.IsValid()) return;
	if (const FOreDealAuctionSeat* Seat = Broker->FindSeat(GetOwnerPlayerState())) OutCards = Seat->PendingCards;
}

int32 UOreDealAuctionComponent::GetMyWagonCount() const
{
	const APlayerController* PC = GetOwnerController();
	TArray<int32> Types;
	if (AActor* Board = AOreDealAuctionBroker::FindBoardFor(GetWorld(), PC ? PC->GetPawn() : nullptr))
		if (AOreDealAuctionBroker::ReadIntArray(Board, TEXT("WagonTypes"), Types) && Types.Num() > 0) return Types.Num();
	return 2;
}

void UOreDealAuctionComponent::GetOfferers(TArray<APlayerState*>& OutPlayers) const
{
	OutPlayers.Reset();
	if (Broker.IsValid()) Broker->GetOfferers(OutPlayers);
}

EOreDealAuctionLocalMode UOreDealAuctionComponent::ComputeMode() const
{
	if (!Broker.IsValid()) return EOreDealAuctionLocalMode::None;
	const bool bActive = IsActivePlayer();
	switch (Broker->GetStage())
	{
	case EOreDealAuctionStage::Idle: return EOreDealAuctionLocalMode::None;
	case EOreDealAuctionStage::PickReveal: return bActive ? EOreDealAuctionLocalMode::PickReveal : EOreDealAuctionLocalMode::Waiting;
	case EOreDealAuctionStage::ActiveOffer: return bActive ? EOreDealAuctionLocalMode::HandPick : EOreDealAuctionLocalMode::Waiting;
	case EOreDealAuctionStage::SelectLot: return bActive ? EOreDealAuctionLocalMode::LotPick : EOreDealAuctionLocalMode::Waiting;
	case EOreDealAuctionStage::Bidding:
	{
		if (bActive) return EOreDealAuctionLocalMode::Waiting;
		const FOreDealAuctionSeat* Seat = Broker->FindSeat(GetOwnerPlayerState());
		return Seat && !Seat->bConfirmed ? EOreDealAuctionLocalMode::HandPick : EOreDealAuctionLocalMode::Waiting;
	}
	case EOreDealAuctionStage::ChoosePartner: return bActive ? EOreDealAuctionLocalMode::PartnerPick : EOreDealAuctionLocalMode::Waiting;
	case EOreDealAuctionStage::Consume: return EOreDealAuctionLocalMode::Waiting;
	default: return EOreDealAuctionLocalMode::None;
	}
}

int32 UOreDealAuctionComponent::GetModeItemCount() const
{
	if (!Broker.IsValid()) return 0;
	switch (LocalMode)
	{
	case EOreDealAuctionLocalMode::PickReveal:
	case EOreDealAuctionLocalMode::LotPick:
	{
		TArray<int32> DeckCards;
		AOreDealAuctionBroker::GetDeckCards(GetWorld(), DeckCards);
		return DeckCards.Num();
	}
	case EOreDealAuctionLocalMode::HandPick:
	{
		TArray<int32> Hand;
		GetLocalHand(Hand);
		return Hand.Num();
	}
	case EOreDealAuctionLocalMode::PartnerPick:
	{
		TArray<APlayerState*> Offerers;
		GetOfferers(Offerers);
		return Offerers.Num();
	}
	default: return 0;
	}
}

void UOreDealAuctionComponent::GetDeckVisualOrder(TArray<int32>& OutOrder) const
{
	OutOrder.Reset();
	TArray<int32> DeckCards;
	AOreDealAuctionBroker::GetDeckCards(GetWorld(), DeckCards);
	for (int32 I = 0; I < DeckCards.Num(); ++I) OutOrder.Add(I);
	const APlayerController* PC = GetOwnerController();
	const AActor* Deck = AOreDealAuctionBroker::FindDeck(GetWorld());
	if (!PC || !Deck) return;
	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const FVector Right = FRotationMatrix(FRotator(0.0f, CameraRotation.Yaw, 0.0f)).GetUnitAxis(EAxis::Y);
	const FTransform DeckTransform = Deck->GetActorTransform();
	const int32 Count = DeckCards.Num();
	OutOrder.Sort([&](int32 A, int32 B)
	{
		const int32 RowA = Count >= 3 ? A / DeckPerRow : 0;
		const int32 RowB = Count >= 3 ? B / DeckPerRow : 0;
		if (RowA != RowB) return RowA < RowB;
		const double SideA = FVector::DotProduct(DeckTransform.TransformPosition(DeckSlotLocal(A, Count)), Right);
		const double SideB = FVector::DotProduct(DeckTransform.TransformPosition(DeckSlotLocal(B, Count)), Right);
		return SideA < SideB;
	});
}

void UOreDealAuctionComponent::Flash(const FString& Message)
{
	if (Hud) Hud->Flash(Message);
}

void UOreDealAuctionComponent::BeginAwait()
{
	// Record before the RPC: on a listen server the RPC runs immediately and bumps Revision.
	bAwaitingServer = true;
	AwaitRevision = Broker.IsValid() ? Broker->GetRevision() : INDEX_NONE;
	AwaitStartTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
}

// ---------------------------------------------------------------------------------------------
// Input

void UOreDealAuctionComponent::ToggleSelection(int32 Index)
{
	if (Index < 0 || Index >= GetModeItemCount()) return;
	if (Selection.Contains(Index)) Selection.Remove(Index);
	else Selection.Add(Index);
}

void UOreDealAuctionComponent::SubmitCurrent()
{
	if (bAwaitingServer) return;
	TArray<int32> Sorted = Selection;
	Sorted.Sort();
	switch (LocalMode)
	{
	case EOreDealAuctionLocalMode::PickReveal:
		if (Sorted.IsEmpty()) { Flash(TEXT("가져갈 카드를 1장 이상 고르세요 (C : 선택)")); break; }
		BeginAwait();
		ServerPickReveal(Sorted);
		break;
	case EOreDealAuctionLocalMode::HandPick:
		BeginAwait();
		ServerSubmitOffer(Sorted);
		break;
	case EOreDealAuctionLocalMode::LotPick:
		if (Sorted.IsEmpty()) { Flash(TEXT("거래할 카드를 1장 이상 고르세요 (C : 선택)")); break; }
		BeginAwait();
		ServerSelectLot(Sorted);
		break;
	case EOreDealAuctionLocalMode::PartnerPick:
	{
		TArray<APlayerState*> Offerers;
		GetOfferers(Offerers);
		if (Offerers.IsValidIndex(PartnerCursor)) { BeginAwait(); ServerChoosePartner(Offerers[PartnerCursor]); }
		break;
	}
	default: break;
	}
}

bool UOreDealAuctionComponent::HandleInputKey(const FKey& Key, EInputEvent Event)
{
	if (!IsLocallyOwned() || !GetBroker() || Broker->GetStage() == EOreDealAuctionStage::Idle) return false;
	UpdateLocalState();
	const bool bPressed = Event == IE_Pressed || Event == IE_Repeat;

	// E: the turn cannot move on while trading or while anyone still holds taken cards.
	if (Key == EKeys::E)
	{
		if (!bPressed || !IsActivePlayer()) return bPressed && IsActivePlayer();
		if (Broker->GetStage() == EOreDealAuctionStage::Consume)
		{
			const FString Names = Broker->GetUnfinishedPlayerNames();
			Flash(Names.IsEmpty() ? FString(TEXT("자원 배치를 확인하는 중입니다")) : FString::Printf(TEXT("%s의 자원 배치가 끝나지 않았습니다"), *Names));
		}
		else Flash(TEXT("거래 단계가 진행 중입니다"));
		return true;
	}

	const int32 Count = GetModeItemCount();
	switch (LocalMode)
	{
	case EOreDealAuctionLocalMode::PickReveal:
	case EOreDealAuctionLocalMode::HandPick:
	case EOreDealAuctionLocalMode::LotPick:
	{
		// Control rule: cards in the world (덱 위 카드) move with A/D, cards shown only on my screen
		// (손패 picker) move with Z/X. C toggles a multi-selection, Space decides.
		const bool bScreenCards = LocalMode == EOreDealAuctionLocalMode::HandPick;
		const FKey LeftKey = bScreenCards ? EKeys::Z : EKeys::A;
		const FKey RightKey = bScreenCards ? EKeys::X : EKeys::D;
		if (Key == EKeys::Q && bScreenCards) return true; // the picker replaces the Q hand viewer
		const bool bHandled = Key == LeftKey || Key == RightKey || Key == EKeys::SpaceBar || Key == EKeys::C;
		if (!bHandled) return false;
		if (!bPressed) return true;
		if ((Key == LeftKey || Key == RightKey) && Count > 0)
		{
			const int32 Step = Key == RightKey ? 1 : -1;
			if (bScreenCards) Cursor = FMath::Clamp(Cursor + Step, 0, Count - 1);
			else
			{
				// 덱 위 카드 sit in deck space, so follow this player's own screen left/right.
				TArray<int32> Order;
				GetDeckVisualOrder(Order);
				const int32 Position = FMath::Max(Order.IndexOfByKey(Cursor), 0);
				Cursor = Order[FMath::Clamp(Position + Step, 0, Order.Num() - 1)];
			}
		}
		else if (Key == EKeys::C) ToggleSelection(Cursor);
		else if (Key == EKeys::SpaceBar) SubmitCurrent();
		RefreshHud();
		return true;
	}

	case EOreDealAuctionLocalMode::PartnerPick:
		if (Key != EKeys::A && Key != EKeys::D && Key != EKeys::SpaceBar) return false;
		if (!bPressed) return true;
		if ((Key == EKeys::A || Key == EKeys::D) && Count > 0)
		{
			PartnerCursor = (PartnerCursor + (Key == EKeys::D ? 1 : Count - 1)) % Count;
			TArray<APlayerState*> Offerers;
			GetOfferers(Offerers);
			if (Offerers.IsValidIndex(PartnerCursor)) ServerFocusPartner(Offerers[PartnerCursor]);
		}
		else if (Key == EKeys::SpaceBar) SubmitCurrent();
		RefreshHud();
		return true;

	case EOreDealAuctionLocalMode::Waiting:
	default:
	{
		// Taken cards: A/D picks the wagon, Space places the front card. Hand placement is blocked meanwhile.
		TArray<int32> Pending;
		GetMyPending(Pending);
		if (Pending.IsEmpty()) return false;
		if (Key != EKeys::A && Key != EKeys::D && Key != EKeys::SpaceBar) return false;
		if (!bPressed) return true;
		const int32 Wagons = GetMyWagonCount();
		AOreDealTablePlayerController* TablePC = Cast<AOreDealTablePlayerController>(GetOwnerController());
		if (TablePC && WagonFocus < 2) WagonFocus = TablePC->GetWagonFocus();
		if (Key == EKeys::A) WagonFocus = FMath::Max(WagonFocus - 1, 0);
		else if (Key == EKeys::D) WagonFocus = FMath::Min(WagonFocus + 1, Wagons - 1);
		else if (!bAwaitingServer) { BeginAwait(); ServerPlacePending(FMath::Clamp(WagonFocus, 0, Wagons - 1)); }
		// Mirror the choice onto the board's own wagon glow (BP FocusIndex) so the wagon lights up.
		if (TablePC && Key != EKeys::SpaceBar) TablePC->SetWagonFocus(WagonFocus);
		RefreshHud();
		return true;
	}
	}
}

bool UOreDealAuctionComponent::GetCameraOverride(FRotator& OutRotation) const
{
	if (LocalMode != EOreDealAuctionLocalMode::PartnerPick || !IsLocallyOwned()) return false;
	TArray<APlayerState*> Offerers;
	GetOfferers(Offerers);
	const APlayerState* Target = Offerers.IsValidIndex(PartnerCursor) ? Offerers[PartnerCursor] : nullptr;
	const APawn* TargetPawn = Target ? Target->GetPawn() : nullptr;
	const APlayerController* PC = GetOwnerController();
	if (!TargetPawn || !PC || !PC->PlayerCameraManager) return false;
	const FVector From = PC->PlayerCameraManager->GetCameraLocation();
	const FVector To(TargetPawn->GetActorLocation().X, TargetPawn->GetActorLocation().Y, HeadTopZ(TargetPawn) - 10.0f);
	FRotator Look = (To - From).Rotation();
	Look.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Look.Pitch), -35.0f, 25.0f);
	Look.Roll = 0.0f;
	OutRotation = Look;
	return true;
}

// ---------------------------------------------------------------------------------------------
// Per-frame local update

void UOreDealAuctionComponent::UpdateLocalState()
{
	if (!Broker.IsValid()) { LocalMode = EOreDealAuctionLocalMode::None; return; }
	const EOreDealAuctionLocalMode NewMode = ComputeMode();
	const uint8 Stage = static_cast<uint8>(Broker->GetStage());
	// A rejected request (e.g. invalid wagon) changes nothing, so stop waiting after a short timeout.
	const bool bAwaitTimedOut = bAwaitingServer && GetWorld() && GetWorld()->GetRealTimeSeconds() - AwaitStartTime > 1.5;
	const bool bServerAnswered = bAwaitingServer && (Broker->GetRevision() != AwaitRevision || bAwaitTimedOut);
	if (NewMode != LocalMode || Stage != LastStage || bServerAnswered)
	{
		Cursor = 0;
		Selection.Reset();
		bAwaitingServer = false;
		if (NewMode == EOreDealAuctionLocalMode::PickReveal || NewMode == EOreDealAuctionLocalMode::LotPick)
		{
			// Start on the card at the bottom-left of this player's view.
			TArray<int32> Order;
			GetDeckVisualOrder(Order);
			if (!Order.IsEmpty()) Cursor = Order[0];
		}
		if (NewMode == EOreDealAuctionLocalMode::PartnerPick)
		{
			TArray<APlayerState*> Offerers;
			GetOfferers(Offerers);
			PartnerCursor = FMath::Max(0, Offerers.IndexOfByKey(Broker->GetFocusedPartner()));
		}
	}
	LocalMode = NewMode;
	LastStage = Stage;
	const int32 Count = GetModeItemCount();
	if (LocalMode == EOreDealAuctionLocalMode::PartnerPick) PartnerCursor = Count > 0 ? FMath::Clamp(PartnerCursor, 0, Count - 1) : 0;
	else Cursor = Count > 0 ? FMath::Clamp(Cursor, 0, Count - 1) : 0;
	Selection.RemoveAll([Count](int32 Index) { return Index >= Count; });
	WagonFocus = FMath::Clamp(WagonFocus, 0, FMath::Max(GetMyWagonCount() - 1, 0));
}

void UOreDealAuctionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsLocallyOwned() || !GetWorld()) return;
	GetBroker();

	if (!Hud)
	{
		Hud = CreateWidget<UOreDealAuctionHudWidget>(GetOwnerController(), UOreDealAuctionHudWidget::StaticClass());
		if (Hud) Hud->AddToViewport(30500);
	}
	if (!Display.IsValid())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		Display = GetWorld()->SpawnActor<AOreDealAuctionDisplay>(AOreDealAuctionDisplay::StaticClass(), FTransform::Identity, Params);
	}

	UpdateLocalState();
	RefreshHud();
	RefreshDisplay();
}

void UOreDealAuctionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Hud) Hud->RemoveFromParent();
	Hud = nullptr;
	if (Display.IsValid()) Display->Destroy();
	Super::EndPlay(EndPlayReason);
}

void UOreDealAuctionComponent::RefreshHud()
{
	if (!Hud) return;
	FOreDealAuctionHudState State;
	if (!Broker.IsValid() || Broker->GetStage() == EOreDealAuctionStage::Idle)
	{
		Hud->ApplyState(State);
		return;
	}
	const bool bActive = IsActivePlayer();
	const FString ActiveName = AOreDealAuctionBroker::GetDisplayName(Broker->GetActivePlayer());
	TArray<APlayerState*> Offerers;
	GetOfferers(Offerers);

	switch (Broker->GetStage())
	{
	case EOreDealAuctionStage::PickReveal:
		State.Status = bActive ? FString(TEXT("▶ 덱 위 카드 중 가져갈 카드를 고르세요 (여러 장 가능)   ·   A / D 이동   ·   C 선택   ·   Space 결정"))
		                       : FString::Printf(TEXT("%s 님이 가져갈 카드를 고르는 중..."), *ActiveName);
		break;
	case EOreDealAuctionStage::ActiveOffer:
		State.Status = bActive ? FString(TEXT("▶ 거래에 올릴 손패를 고르세요"))
		                       : FString::Printf(TEXT("%s 님이 거래에 올릴 카드를 고르는 중..."), *ActiveName);
		State.PickerHint = TEXT("Z / X 이동   ·   C 선택   ·   Space 결정   (안 올리려면 그냥 Space)");
		break;
	case EOreDealAuctionStage::SelectLot:
		State.Status = bActive ? FString(TEXT("▶ 덱 위 카드 중 이번에 거래할 카드를 고르세요 (여러 장 가능)   ·   A / D 이동   ·   C 선택   ·   Space 결정"))
		                       : FString::Printf(TEXT("%s 님이 거래할 카드를 고르는 중..."), *ActiveName);
		break;
	case EOreDealAuctionStage::Bidding:
	{
		int32 Confirmed = 0, Total = 0;
		Broker->GetBiddingProgress(Confirmed, Total);
		if (LocalMode == EOreDealAuctionLocalMode::HandPick)
			State.Status = TEXT("▶ 강조된 거래 카드와 바꿀 손패(제안 카드)를 고르세요");
		else if (bActive)
			State.Status = FString::Printf(TEXT("다른 플레이어들이 내밀 카드를 고르는 중... (%d / %d 결정)"), Confirmed, Total);
		else
			State.Status = FString::Printf(TEXT("결정 완료   ·   다른 플레이어를 기다리는 중... (%d / %d)"), Confirmed, Total);
		State.PickerHint = TEXT("Z / X 이동   ·   C 선택   ·   Space 결정   (거래하지 않으려면 그냥 Space)");
		break;
	}
	case EOreDealAuctionStage::ChoosePartner:
	{
		const APlayerState* Focus = bActive && Offerers.IsValidIndex(PartnerCursor) ? Offerers[PartnerCursor] : Broker->GetFocusedPartner();
		const FString FocusName = AOreDealAuctionBroker::GetDisplayName(Focus);
		State.Status = bActive ? FString::Printf(TEXT("▶ 거래할 상대를 고르세요   ·   A / D 이동   ·   Space 결정      현재 : %s"), *FocusName)
		                       : FString::Printf(TEXT("%s 님이 거래 상대를 고르는 중...   (현재 : %s)"), *ActiveName, *FocusName);
		break;
	}
	case EOreDealAuctionStage::Consume:
	{
		const FString Names = Broker->GetUnfinishedPlayerNames();
		State.Status = Names.IsEmpty() ? FString() : FString::Printf(TEXT("%s의 자원 배치가 끝나지 않았습니다"), *Names);
		break;
	}
	default: break;
	}

	if (LocalMode == EOreDealAuctionLocalMode::HandPick)
	{
		State.bPickerOpen = true;
		GetLocalHand(State.PickerCards);
		State.PickerCursor = Cursor;
		State.PickerSelected = Selection;
	}
	GetMyPending(State.PendingCards);
	State.WagonCount = GetMyWagonCount();
	State.WagonFocus = WagonFocus;
	State.bPendingInputEnabled = LocalMode == EOreDealAuctionLocalMode::Waiting;
	Hud->ApplyState(State);
}

void UOreDealAuctionComponent::RefreshDisplay()
{
	if (!Display.IsValid()) return;
	TArray<FOreDealCardDraw> Draws;
	APlayerController* PC = GetOwnerController();
	if (!PC || !Broker.IsValid())
	{
		Display->ShowCards(Draws, FVector::ZeroVector);
		return;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const FVector Right = FRotationMatrix(FRotator(0.0f, CameraRotation.Yaw, 0.0f)).GetUnitAxis(EAxis::Y);
	AActor* Deck = AOreDealAuctionBroker::FindDeck(GetWorld());
	const FVector TableCenter = Deck ? Deck->GetActorLocation() : FVector::ZeroVector;

	auto Add = [&Draws, &CameraLocation](FVector Location, float Scale, int32 Resource, bool bGlow, bool bLift)
	{
		if (bLift)
		{
			Location += (CameraLocation - Location).GetSafeNormal() * 3.0f;
			Scale *= 1.12f;
		}
		FOreDealCardDraw& Draw = Draws.AddDefaulted_GetRef();
		Draw.Location = Location;
		Draw.Scale = Scale;
		Draw.Resource = Resource;
		Draw.bGlow = bGlow;
	};

	// 덱 위 카드: one array (BP_CardDeck.RevealedCards), drawn with the same look as the BP's own
	// RevealCard pair, whose two fixed components are hidden so nothing is drawn twice.
	if (Deck)
	{
		TArray<UPrimitiveComponent*> DeckParts;
		Deck->GetComponents<UPrimitiveComponent>(DeckParts);
		for (UPrimitiveComponent* Part : DeckParts)
			for (const TCHAR* PartName : BlueprintRevealParts)
				if (Part && Part->GetName() == PartName && !Part->bHiddenInGame) Part->SetHiddenInGame(true);

		TArray<int32> DeckCards;
		AOreDealAuctionBroker::GetDeckCards(GetWorld(), DeckCards);
		const bool bLotShown = Broker->GetStage() == EOreDealAuctionStage::Bidding || Broker->GetStage() == EOreDealAuctionStage::ChoosePartner;
		const bool bLocalWorldPick = LocalMode == EOreDealAuctionLocalMode::PickReveal || LocalMode == EOreDealAuctionLocalMode::LotPick;
		for (int32 I = 0; I < DeckCards.Num(); ++I)
		{
			const bool bCursor = bLocalWorldPick && I == Cursor;
			const bool bGlow = bCursor || (bLotShown && Broker->GetLotIndices().Contains(I)) ||
				(bLocalWorldPick && Selection.Contains(I));
			Add(Deck->GetActorTransform().TransformPosition(DeckSlotLocal(I, DeckCards.Num())), 1.0f, DeckCards[I], bGlow, bCursor);
		}
	}

	const EOreDealAuctionStage Stage = Broker->GetStage();
	TArray<APlayerState*> Offerers;
	GetOfferers(Offerers);
	const APlayerState* FocusedPartner = (LocalMode == EOreDealAuctionLocalMode::PartnerPick && Offerers.IsValidIndex(PartnerCursor))
		? Offerers[PartnerCursor] : Broker->GetFocusedPartner();

	for (const FOreDealAuctionSeat& Seat : Broker->GetSeats())
	{
		const APawn* Pawn = IsValid(Seat.Player) ? Seat.Player->GetPawn() : nullptr;
		if (!Pawn) continue;

		// Offered cards float above the player's head, slightly towards the table, for everyone to see.
		if ((Stage == EOreDealAuctionStage::Bidding || Stage == EOreDealAuctionStage::ChoosePartner) && Seat.bConfirmed && !Seat.OfferCards.IsEmpty())
		{
			const FVector ToCenter = (TableCenter - Pawn->GetActorLocation()).GetSafeNormal2D();
			const FVector Base(Pawn->GetActorLocation().X + ToCenter.X * 18.0f, Pawn->GetActorLocation().Y + ToCenter.Y * 18.0f, HeadTopZ(Pawn) + 12.0f);
			const bool bGlow = Stage == EOreDealAuctionStage::ChoosePartner && Seat.Player == FocusedPartner;
			for (int32 I = 0; I < Seat.OfferCards.Num(); ++I)
				Add(GridLocation(I, Seat.OfferCards.Num(), OfferPerRow, Base, Right, OfferColumnStep, OfferRowStep), OfferCardScale, Seat.OfferCards[I], bGlow, false);
		}

		// Taken cards wait beside the player's wagon board; the next card to place is in front.
		if (!Seat.PendingCards.IsEmpty())
			if (AActor* Board = AOreDealAuctionBroker::FindBoardFor(GetWorld(), Pawn))
			{
				const FVector Base = Board->GetActorLocation() + FVector(0.0f, 0.0f, 14.0f);
				const FVector ToCamera = (CameraLocation - Base).GetSafeNormal();
				for (int32 I = 0; I < Seat.PendingCards.Num(); ++I)
					Add(Base + Right * (I * PendingStep) - ToCamera * (I * 0.4f), PendingCardScale, Seat.PendingCards[I], I == 0, false);
			}
	}

	Display->ShowCards(Draws, CameraLocation);
}
