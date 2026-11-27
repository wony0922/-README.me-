#include "OreDealAuctionComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Math/RotationMatrix.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "OreDealAuctionBroker.h"
#include "OreDealAuctionDisplay.h"
#include "OreDealAuctionHudWidget.h"
#include "OreDealTablePlayerController.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"

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
	constexpr float OfferCardScale = 1.18f;
	constexpr float OfferColumnStep = 12.0f;
	constexpr float OfferRowStep = 16.5f;
	constexpr int32 OfferPerRow = 5;
	constexpr float PendingCardScale = 0.92f;
	constexpr float PendingStep = 3.5f;
	const TCHAR* HandCardTexturePaths[] = {
		TEXT("/Game/MaterialTraders/Cards/rock.rock"), TEXT("/Game/MaterialTraders/Cards/coal.coal"),
		TEXT("/Game/MaterialTraders/Cards/copper.copper"), TEXT("/Game/MaterialTraders/Cards/iron.iron"),
		TEXT("/Game/MaterialTraders/Cards/silver.silver"), TEXT("/Game/MaterialTraders/Cards/gold.gold"),
		TEXT("/Game/MaterialTraders/Cards/diamond.diamond"), TEXT("/Game/MaterialTraders/Cards/adamantium.adamantium")
	};
	const TCHAR* HandCardNames[] = { TEXT("Rock"), TEXT("Coal"), TEXT("Copper"), TEXT("Iron"), TEXT("Silver"), TEXT("Gold"), TEXT("Diamond"), TEXT("Adamantium") };

	void EnsureHandCardFaces(APawn* Pawn)
	{
		if (!Pawn) return;
		static TWeakObjectPtr<UMaterialInterface> CardBaseMaterial;
		static TWeakObjectPtr<UTexture2D> HandCardTextures[UE_ARRAY_COUNT(HandCardTexturePaths)];
		if (!CardBaseMaterial.IsValid()) CardBaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MaterialTraders/Cards/M_Card.M_Card"));
		if (!CardBaseMaterial.IsValid()) return;

		FArrayProperty* MaterialsProperty = FindFProperty<FArrayProperty>(Pawn->GetClass(), TEXT("CardFrontMaterials"));
		FObjectPropertyBase* MaterialElement = MaterialsProperty ? CastField<FObjectPropertyBase>(MaterialsProperty->Inner) : nullptr;
		if (!MaterialsProperty || !MaterialElement) return;
		FScriptArrayHelper Materials(MaterialsProperty, MaterialsProperty->ContainerPtrToValuePtr<void>(Pawn));
		bool bRefreshBlueprintCards = Materials.Num() != UE_ARRAY_COUNT(HandCardTexturePaths);
		if (Materials.Num() != UE_ARRAY_COUNT(HandCardTexturePaths)) Materials.Resize(UE_ARRAY_COUNT(HandCardTexturePaths));

		TArray<FMaterialParameterInfo> TextureParameters;
		TArray<FGuid> TextureParameterIds;
		CardBaseMaterial->GetAllTextureParameterInfo(TextureParameters, TextureParameterIds);
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(HandCardTexturePaths); ++Index)
		{
			UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(MaterialElement->GetObjectPropertyValue(Materials.GetRawPtr(Index)));
			const bool bNeedsTextureSetup = Dynamic == nullptr;
			if (!Dynamic)
			{
				Dynamic = UMaterialInstanceDynamic::Create(CardBaseMaterial.Get(), Pawn);
				if (!Dynamic) continue;
				MaterialElement->SetObjectPropertyValue(Materials.GetRawPtr(Index), Dynamic);
				bRefreshBlueprintCards = true;
			}
			if (!HandCardTextures[Index].IsValid()) HandCardTextures[Index] = LoadObject<UTexture2D>(nullptr, HandCardTexturePaths[Index]);
			UTexture2D* Texture = HandCardTextures[Index].Get();
			if (!Texture) continue;
			if (bNeedsTextureSetup)
				for (const FMaterialParameterInfo& Parameter : TextureParameters)
					Dynamic->SetTextureParameterValueByInfo(Parameter, Texture);
		}

		FArrayProperty* NamesProperty = FindFProperty<FArrayProperty>(Pawn->GetClass(), TEXT("CardTypeNames"));
		FStrProperty* NameElement = NamesProperty ? CastField<FStrProperty>(NamesProperty->Inner) : nullptr;
		if (NameElement)
		{
			FScriptArrayHelper Names(NamesProperty, NamesProperty->ContainerPtrToValuePtr<void>(Pawn));
			if (Names.Num() != UE_ARRAY_COUNT(HandCardNames))
			{
				Names.Resize(UE_ARRAY_COUNT(HandCardNames));
				bRefreshBlueprintCards = true;
			}
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(HandCardNames); ++Index)
				if (NameElement->GetPropertyValue(Names.GetRawPtr(Index)) != HandCardNames[Index])
				{
					NameElement->SetPropertyValue(Names.GetRawPtr(Index), FString(HandCardNames[Index]));
					bRefreshBlueprintCards = true;
				}
		}

		if (bRefreshBlueprintCards)
			if (UFunction* Refresh = Pawn->FindFunction(TEXT("OnRep_HandCards"))) Pawn->ProcessEvent(Refresh, nullptr);
	}

	void EnsureBoardCardFaces(AActor* Board)
	{
		if (!Board) return;
		static TWeakObjectPtr<UMaterialInterface> FloatCardBase;
		static TWeakObjectPtr<UTexture2D> Textures[UE_ARRAY_COUNT(HandCardTexturePaths)];
		if (!FloatCardBase.IsValid()) FloatCardBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MaterialTraders/Cards/M_CardFloat.M_CardFloat"));
		if (!FloatCardBase.IsValid()) return;

		FArrayProperty* MaterialsProperty = FindFProperty<FArrayProperty>(Board->GetClass(), TEXT("floatMats"));
		FObjectPropertyBase* MaterialElement = MaterialsProperty ? CastField<FObjectPropertyBase>(MaterialsProperty->Inner) : nullptr;
		if (!MaterialsProperty || !MaterialElement) return;
		FScriptArrayHelper Materials(MaterialsProperty, MaterialsProperty->ContainerPtrToValuePtr<void>(Board));
		bool bRefreshBoard = Materials.Num() != UE_ARRAY_COUNT(HandCardTexturePaths);
		if (Materials.Num() != UE_ARRAY_COUNT(HandCardTexturePaths)) Materials.Resize(UE_ARRAY_COUNT(HandCardTexturePaths));

		TArray<FMaterialParameterInfo> Parameters;
		TArray<FGuid> ParameterIds;
		FloatCardBase->GetAllTextureParameterInfo(Parameters, ParameterIds);
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(HandCardTexturePaths); ++Index)
		{
			UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(MaterialElement->GetObjectPropertyValue(Materials.GetRawPtr(Index)));
			const bool bNeedsTextureSetup = Dynamic == nullptr;
			if (!Dynamic)
			{
				Dynamic = UMaterialInstanceDynamic::Create(FloatCardBase.Get(), Board);
				if (!Dynamic) continue;
				MaterialElement->SetObjectPropertyValue(Materials.GetRawPtr(Index), Dynamic);
				bRefreshBoard = true;
			}
			if (!Textures[Index].IsValid()) Textures[Index] = LoadObject<UTexture2D>(nullptr, HandCardTexturePaths[Index]);
			if (bNeedsTextureSetup)
			if (UTexture2D* Texture = Textures[Index].Get())
				for (const FMaterialParameterInfo& Parameter : Parameters)
					Dynamic->SetTextureParameterValueByInfo(Parameter, Texture);
		}
		if (bRefreshBoard)
			if (UFunction* Refresh = Board->FindFunction(TEXT("UpdateVisual"))) Board->ProcessEvent(Refresh, nullptr);
	}

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

	/**
	 * BP_CardDeck's own two reveal cards (and their glows). Looked up by name once per deck instead of
	 * scanning and string-comparing every component each frame.
	 */
	const TArray<TWeakObjectPtr<UPrimitiveComponent>>& GetBlueprintRevealParts(AActor* Deck)
	{
		struct FEntry
		{
			TWeakObjectPtr<AActor> Deck;
			TArray<TWeakObjectPtr<UPrimitiveComponent>> Parts;
		};
		static TArray<FEntry> Entries; // one per PIE world at most
		for (const FEntry& Entry : Entries)
			if (Entry.Deck.Get() == Deck) return Entry.Parts;
		Entries.RemoveAll([](const FEntry& Entry) { return !Entry.Deck.IsValid(); });
		FEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Deck = Deck;
		TArray<UPrimitiveComponent*> DeckParts;
		Deck->GetComponents<UPrimitiveComponent>(DeckParts);
		for (UPrimitiveComponent* Part : DeckParts)
			for (const TCHAR* PartName : BlueprintRevealParts)
				if (Part && Part->GetName() == PartName) Entry.Parts.Add(Part);
		return Entry.Parts;
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

void UOreDealAuctionComponent::ServerSubmitTradeRequest_Implementation(const TArray<int32>& RevealIndices, const TArray<int32>& HandIndices)
{
	if (AOreDealAuctionBroker* B = GetBroker()) B->SubmitRequest(GetOwnerPlayerState(), RevealIndices, HandIndices);
}

void UOreDealAuctionComponent::ServerCancelTradeRequest_Implementation()
{
	if (AOreDealAuctionBroker* B = GetBroker()) B->CancelRequest(GetOwnerPlayerState());
}

void UOreDealAuctionComponent::ServerResolveTradeRequest_Implementation(bool bAccept)
{
	if (AOreDealAuctionBroker* B = GetBroker()) B->ResolveRequest(GetOwnerPlayerState(), bAccept);
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

void UOreDealAuctionComponent::ServerReturnPending_Implementation()
{
	if (AOreDealAuctionBroker* B = GetBroker()) B->ReturnPending(GetOwnerPlayerState());
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

bool UOreDealAuctionComponent::HandlePlayerClick(APlayerState* TargetPlayer)
{
	if (!IsLocallyOwned() || !TargetPlayer || !Broker.IsValid() || !IsActivePlayer() || Broker->GetStage() != EOreDealAuctionStage::PickReveal) return false;
	const FOreDealAuctionSeat* Request = Broker->FindSeat(TargetPlayer);
	if (!Request || !Request->bConfirmed || Request->RequestedRevealIndices.IsEmpty()) return false;
	BeginAwait();
	ServerChoosePartner(TargetPlayer);
	return true;
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
	case EOreDealAuctionStage::PickReveal:
	{
		if (bPlantHandMode) return EOreDealAuctionLocalMode::HandPlant;
		if (bActive)
		{
			TArray<APlayerState*> Requests;
			GetOfferers(Requests);
			return bActiveRequestBrowser && !Requests.IsEmpty() ? EOreDealAuctionLocalMode::PartnerPick : EOreDealAuctionLocalMode::PickReveal;
		}
		if (!bRequestEditing) return EOreDealAuctionLocalMode::Waiting;
		return bRequestOfferPhase ? EOreDealAuctionLocalMode::RequestOfferPick : EOreDealAuctionLocalMode::RequestLotPick;
	}
	case EOreDealAuctionStage::RequestReview:
		if (bPlantHandMode) return EOreDealAuctionLocalMode::HandPlant;
		if (bActive) return EOreDealAuctionLocalMode::RequestReview;
		if (bRequestEditing) return bRequestOfferPhase ? EOreDealAuctionLocalMode::RequestOfferPick : EOreDealAuctionLocalMode::RequestLotPick;
		return bPlantHandMode ? EOreDealAuctionLocalMode::HandPlant : EOreDealAuctionLocalMode::Waiting;
	case EOreDealAuctionStage::ConfirmTrade:
		if (GetOwnerPlayerState() == Broker->GetFocusedPartner()) return bPlantHandMode ? EOreDealAuctionLocalMode::HandPlant : EOreDealAuctionLocalMode::ConfirmTrade;
		if (bPlantHandMode) return EOreDealAuctionLocalMode::HandPlant;
		if (bRequestEditing) return bRequestOfferPhase ? EOreDealAuctionLocalMode::RequestOfferPick : EOreDealAuctionLocalMode::RequestLotPick;
		return bPlantHandMode ? EOreDealAuctionLocalMode::HandPlant : EOreDealAuctionLocalMode::Waiting;
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
	case EOreDealAuctionLocalMode::RequestLotPick:
	{
		TArray<int32> DeckCards;
		AOreDealAuctionBroker::GetDeckCards(GetWorld(), DeckCards);
		return DeckCards.Num();
	}
	case EOreDealAuctionLocalMode::RequestOfferPick:
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
	case EOreDealAuctionLocalMode::HandPlant: return GetMyWagonCount();
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
	else
	{
		const bool bTwoCardLimit = LocalMode == EOreDealAuctionLocalMode::PickReveal || LocalMode == EOreDealAuctionLocalMode::RequestLotPick;
		if (bTwoCardLimit && Selection.Num() >= 2) return;
		Selection.Add(Index);
	}
}

void UOreDealAuctionComponent::SubmitCurrent()
{
	if (bAwaitingServer) return;
	TArray<int32> Sorted = Selection;
	Sorted.Sort();
	switch (LocalMode)
	{
	case EOreDealAuctionLocalMode::PickReveal:
		BeginAwait();
		ServerPickReveal(Sorted);
		break;
	case EOreDealAuctionLocalMode::RequestLotPick:
		if (Sorted.IsEmpty())
		{
			bRequestEditing = false;
			bRequestOfferPhase = false;
			BeginAwait();
			ServerCancelTradeRequest();
			break;
		}
		RequestRevealSelection = Sorted;
		Selection = RequestOfferSelection;
		bRequestOfferPhase = true;
		UpdateLocalState();
		RefreshHud();
		break;
	case EOreDealAuctionLocalMode::RequestOfferPick:
		RequestOfferSelection = Sorted;
		bRequestEditing = false;
		bRequestOfferPhase = false;
		BeginAwait();
		ServerSubmitTradeRequest(RequestRevealSelection, Sorted);
		break;
	case EOreDealAuctionLocalMode::PartnerPick:
	{
		TArray<APlayerState*> Offerers;
		GetOfferers(Offerers);
		if (Offerers.IsValidIndex(PartnerCursor)) { BeginAwait(); ServerChoosePartner(Offerers[PartnerCursor]); }
		break;
	}
	case EOreDealAuctionLocalMode::RequestReview:
		BeginAwait();
		ServerResolveTradeRequest(true);
		break;
	case EOreDealAuctionLocalMode::ConfirmTrade:
		BeginAwait();
		ServerResolveTradeRequest(true);
		break;
	default: break;
	}
}

void UOreDealAuctionComponent::PrepareRequestEditor()
{
	RequestRevealSelection.Reset();
	RequestOfferSelection.Reset();
	if (!Broker.IsValid()) return;
	const FOreDealAuctionSeat* Seat = Broker->FindSeat(GetOwnerPlayerState());
	if (!Seat || !Seat->bConfirmed) return;
	RequestRevealSelection = Seat->RequestedRevealIndices;
	RequestOfferSelection = Seat->OfferHandIndices;
}

bool UOreDealAuctionComponent::HandleInputKey(const FKey& Key, EInputEvent Event)
{
	if (!IsLocallyOwned() || !GetBroker() || Broker->GetStage() == EOreDealAuctionStage::Idle) return false;
	UpdateLocalState();
	const bool bPressed = Event == IE_Pressed || Event == IE_Repeat;
	const EOreDealAuctionStage Stage = Broker->GetStage();

	if (Key == EKeys::E && bPressed && (Stage == EOreDealAuctionStage::PickReveal || Stage == EOreDealAuctionStage::RequestReview || Stage == EOreDealAuctionStage::ConfirmTrade))
	{
		bPlantHandMode = !bPlantHandMode;
		UpdateLocalState();
		RefreshHud();
		return true;
	}
	if (Key == EKeys::Tab && bPressed && Stage == EOreDealAuctionStage::PickReveal && IsActivePlayer())
	{
		bActiveRequestBrowser = !bActiveRequestBrowser;
		UpdateLocalState();
		RefreshHud();
		return true;
	}

	if (LocalMode == EOreDealAuctionLocalMode::HandPlant)
	{
		if (Key != EKeys::A && Key != EKeys::D && Key != EKeys::SpaceBar && Key != EKeys::X) return false;
		if (!bPressed) return true;
		TArray<int32> Pending;
		GetMyPending(Pending);
		const int32 Wagons = GetMyWagonCount();
		AOreDealTablePlayerController* TablePC = Cast<AOreDealTablePlayerController>(GetOwnerController());
		if (TablePC && WagonFocus < 2) WagonFocus = TablePC->GetWagonFocus();
		if (Key == EKeys::A) WagonFocus = FMath::Max(WagonFocus - 1, 0);
		else if (Key == EKeys::D) WagonFocus = FMath::Min(WagonFocus + 1, Wagons - 1);
		else if (Key == EKeys::X && IsActivePlayer() && Stage == EOreDealAuctionStage::PickReveal && !Pending.IsEmpty() && !bAwaitingServer)
		{
			BeginAwait();
			ServerReturnPending();
		}
		else if (!bAwaitingServer)
		{
			BeginAwait();
			if (!Pending.IsEmpty()) ServerPlacePending(FMath::Clamp(WagonFocus, 0, Wagons - 1));
			else if (TablePC) TablePC->ServerPlantHandCard(FMath::Clamp(WagonFocus, 0, Wagons - 1));
		}
		if (TablePC && Key != EKeys::SpaceBar) TablePC->SetWagonFocus(WagonFocus);
		RefreshHud();
		return true;
	}

	const int32 Count = GetModeItemCount();
	switch (LocalMode)
	{
	case EOreDealAuctionLocalMode::PickReveal:
	case EOreDealAuctionLocalMode::RequestLotPick:
	case EOreDealAuctionLocalMode::RequestOfferPick:
	{
		const bool bScreenCards = LocalMode == EOreDealAuctionLocalMode::RequestOfferPick;
		const FKey LeftKey = bScreenCards ? EKeys::Z : EKeys::A;
		const FKey RightKey = bScreenCards ? EKeys::X : EKeys::D;
		if (Key == EKeys::Q && bScreenCards) return true; // the picker replaces the Q hand viewer
		const bool bReturn = LocalMode == EOreDealAuctionLocalMode::PickReveal && Key == EKeys::X && IsActivePlayer();
		const bool bHandled = Key == LeftKey || Key == RightKey || Key == EKeys::SpaceBar || Key == EKeys::C || bReturn;
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
		else if (bReturn)
		{
			TArray<int32> Pending;
			GetMyPending(Pending);
			if (!Pending.IsEmpty() && !bAwaitingServer) { BeginAwait(); ServerReturnPending(); }
		}
		else if (Key == EKeys::C) ToggleSelection(Cursor);
		else if (Key == EKeys::SpaceBar) SubmitCurrent();
		RefreshHud();
		return true;
	}

	case EOreDealAuctionLocalMode::PartnerPick:
		if (Key == EKeys::Tab && bPressed) { bActiveRequestBrowser = false; UpdateLocalState(); RefreshHud(); return true; }
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

	case EOreDealAuctionLocalMode::RequestReview:
		if (Key != EKeys::C && Key != EKeys::SpaceBar) return false;
		if (bPressed)
		{
			BeginAwait();
			ServerResolveTradeRequest(Key == EKeys::C);
		}
		return true;

	case EOreDealAuctionLocalMode::ConfirmTrade:
		if (Key != EKeys::C && Key != EKeys::SpaceBar) return false;
		if (bPressed)
		{
			BeginAwait();
			ServerResolveTradeRequest(Key == EKeys::C);
		}
		return true;

	case EOreDealAuctionLocalMode::Waiting:
	default:
	{
		// C reopens a live request; E switches to hand planting during the trade stage.
		const bool bCanRequest = Stage == EOreDealAuctionStage::PickReveal || Stage == EOreDealAuctionStage::RequestReview || Stage == EOreDealAuctionStage::ConfirmTrade;
		const bool bFocusedFinalDecision = Stage == EOreDealAuctionStage::ConfirmTrade && GetOwnerPlayerState() == Broker->GetFocusedPartner();
		if (bCanRequest && !bFocusedFinalDecision && Key == EKeys::C && bPressed && !IsActivePlayer())
		{
			bRequestEditing = true;
			bRequestOfferPhase = false;
			PrepareRequestEditor();
			UpdateLocalState();
			RefreshHud();
			return true;
		}
		if (bCanRequest && Key == EKeys::E && bPressed)
		{
			bPlantHandMode = true;
			UpdateLocalState();
			RefreshHud();
			return true;
		}
		TArray<int32> Pending;
		GetMyPending(Pending);
		if (Pending.IsEmpty() || (Stage != EOreDealAuctionStage::Consume && Stage != EOreDealAuctionStage::PickReveal)) return false;
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
	const bool bStageChanged = Stage != LastStage;
	const bool bModeChanged = NewMode != LocalMode;
	const bool bAwaitTimedOut = bAwaitingServer && GetWorld() && GetWorld()->GetRealTimeSeconds() - AwaitStartTime > 1.5;
	const bool bServerAnswered = bAwaitingServer && (Broker->GetRevision() != AwaitRevision || bAwaitTimedOut);
	if (bStageChanged)
	{
		bPlantHandMode = false;
		bActiveRequestBrowser = false;
		if (Broker->GetStage() == EOreDealAuctionStage::PickReveal)
		{
			bRequestEditing = !IsActivePlayer();
			bRequestOfferPhase = false;
			RequestRevealSelection.Reset();
			RequestOfferSelection.Reset();
		}
	}
	if (bModeChanged || bStageChanged || bServerAnswered)
	{
		if (bModeChanged || bStageChanged) { Cursor = 0; Selection.Reset(); }
		if (bModeChanged)
			if (AOreDealTablePlayerController* TablePC = Cast<AOreDealTablePlayerController>(GetOwnerController()))
				TablePC->SetAuctionModalInput(NewMode == EOreDealAuctionLocalMode::RequestReview || NewMode == EOreDealAuctionLocalMode::ConfirmTrade);
		bAwaitingServer = false;
		if (NewMode == EOreDealAuctionLocalMode::PickReveal || NewMode == EOreDealAuctionLocalMode::RequestLotPick)
		{
			TArray<int32> Order;
			GetDeckVisualOrder(Order);
			if (!Order.IsEmpty()) Cursor = Order[0];
			if (NewMode == EOreDealAuctionLocalMode::RequestLotPick) Selection = RequestRevealSelection;
		}
		else if (NewMode == EOreDealAuctionLocalMode::RequestOfferPick)
		{
			Selection = RequestOfferSelection;
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
	LastSeenBrokerRevision = Broker->GetRevision();
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
	HandCardMaterialRefreshElapsed += DeltaTime;
	if (HandCardMaterialRefreshElapsed >= 0.05f)
	{
		HandCardMaterialRefreshElapsed = 0.0f;
		for (TActorIterator<APawn> It(GetWorld()); It; ++It) EnsureHandCardFaces(*It);
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
			if (*It && (*It)->GetClass()->GetName() == TEXT("BP_CardBoard_C")) EnsureBoardCardFaces(*It);
	}
	GetBroker();

	if (!Hud)
	{
		Hud = CreateWidget<UOreDealAuctionHudWidget>(GetOwnerController(), UOreDealAuctionHudWidget::StaticClass());
		if (Hud)
		{
			Hud->SetTradeDecisionCallbacks(
				FSimpleDelegate::CreateLambda([this]() { if (!bAwaitingServer) { BeginAwait(); ServerResolveTradeRequest(true); } }),
				FSimpleDelegate::CreateLambda([this]() { if (!bAwaitingServer) { BeginAwait(); ServerResolveTradeRequest(false); } }));
			Hud->AddToViewport(30500);
		}
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
	const bool bRequestPick = LocalMode == EOreDealAuctionLocalMode::RequestOfferPick;
	switch (LocalMode)
	{
	case EOreDealAuctionLocalMode::PickReveal:
		State.PickerHint = TEXT("가져가기: A/D 이동 · C 선택 · Space 결정\n요청 확인: 플레이어 클릭 / Tab\n취소: X · 손패 심기: E"); break;
	case EOreDealAuctionLocalMode::RequestLotPick:
		State.PickerHint = TEXT("거래 요청: 중앙 카드 0~2장 선택 · C 선택 · Space 다음"); break;
	case EOreDealAuctionLocalMode::RequestOfferPick:
		State.PickerHint = TEXT("대가 손패 0장 이상 선택 · Z/X 이동 · C 선택 · Space 요청 확정"); break;
	case EOreDealAuctionLocalMode::PartnerPick:
		State.PickerHint = TEXT("요청 선택: A/D 이동 · Space 검토\n중앙 카드로 복귀: Tab"); break;
	case EOreDealAuctionLocalMode::RequestReview:
		State.PickerHint = TEXT("거래 요청 검토 · C 수락 · Space 거절"); break;
	case EOreDealAuctionLocalMode::ConfirmTrade:
		State.PickerHint = TEXT("거래 최종 결정 · C 수락 · Space 거절"); break;
	case EOreDealAuctionLocalMode::HandPlant:
		State.PickerHint = TEXT("마차 선택: A/D\n손패 심기: Space · 취소: X · 종료: E"); break;
	case EOreDealAuctionLocalMode::Waiting:
		State.PickerHint = TEXT("거래 요청 작성 / 수정: C · 손패 심기: E · 대기 중"); break;
	default: break;
	}
	if (bRequestPick)
	{
		State.bPickerOpen = true;
		GetLocalHand(State.PickerCards);
		State.PickerCursor = LocalMode == EOreDealAuctionLocalMode::RequestOfferPick ? Cursor : 0;
		State.PickerSelected = Selection;
	}
	GetMyPending(State.PendingCards);
	State.WagonCount = GetMyWagonCount();
	State.WagonFocus = WagonFocus;
	State.bPendingInputEnabled = LocalMode == EOreDealAuctionLocalMode::Waiting || LocalMode == EOreDealAuctionLocalMode::HandPlant;
	State.bTradeModalVisible = LocalMode == EOreDealAuctionLocalMode::RequestReview || LocalMode == EOreDealAuctionLocalMode::ConfirmTrade;
	if (State.bTradeModalVisible)
	{
		const APlayerState* Requester = Broker->GetFocusedPartner();
		const FOreDealAuctionSeat* Seat = Broker->FindSeat(Requester);
		if (Seat)
		{
			static const TCHAR* Names[] = { TEXT("돌"), TEXT("석탄"), TEXT("구리"), TEXT("철"), TEXT("은"), TEXT("금"), TEXT("다이아몬드"), TEXT("아다만티움") };
			TArray<int32> Central;
			AOreDealAuctionBroker::GetDeckCards(GetWorld(), Central);
			TArray<FString> Wanted, Payment;
			for (int32 Index : Seat->RequestedRevealIndices)
				if (Central.IsValidIndex(Index)) { Wanted.Add(Names[FMath::Clamp(Central[Index], 0, UE_ARRAY_COUNT(Names) - 1)]); State.TradeWantedCards.Add(Central[Index]); }
			for (int32 Card : Seat->OfferCards) { Payment.Add(Names[FMath::Clamp(Card, 0, UE_ARRAY_COUNT(Names) - 1)]); State.TradeOfferedCards.Add(Card); }
			State.TradeModalText = FString::Printf(TEXT("%s의 거래 요청\n가져갈 카드: %s\n대가 카드: %s"),
				*AOreDealAuctionBroker::GetDisplayName(Requester), *FString::Join(Wanted, TEXT(", ")),
				Payment.IsEmpty() ? TEXT("없음") : *FString::Join(Payment, TEXT(", ")));
			State.TradeAcceptLabel = TEXT("거래한다");
		}
	}
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
		for (const TWeakObjectPtr<UPrimitiveComponent>& WeakPart : GetBlueprintRevealParts(Deck))
			if (UPrimitiveComponent* Part = WeakPart.Get())
				if (!Part->bHiddenInGame) Part->SetHiddenInGame(true);

		TArray<int32> DeckCards;
		AOreDealAuctionBroker::GetDeckCards(GetWorld(), DeckCards);
		const bool bLotShown = Broker->GetStage() == EOreDealAuctionStage::RequestReview || Broker->GetStage() == EOreDealAuctionStage::ConfirmTrade;
		const bool bLocalWorldPick = LocalMode == EOreDealAuctionLocalMode::PickReveal || LocalMode == EOreDealAuctionLocalMode::RequestLotPick;
		const FOreDealAuctionSeat* FocusedSeat = Broker->FindSeat(Broker->GetFocusedPartner());
		for (int32 I = 0; I < DeckCards.Num(); ++I)
		{
			const bool bCursor = bLocalWorldPick && I == Cursor;
			const bool bRequestGlow = FocusedSeat && FocusedSeat->RequestedRevealIndices.Contains(I);
			const bool bGlow = bCursor || (bLotShown && bRequestGlow) ||
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
		const bool bShowRequestMarker = Stage != EOreDealAuctionStage::Idle && Seat.Player != Broker->GetActivePlayer() && Seat.bConfirmed && !Seat.RequestedRevealIndices.IsEmpty();
		TWeakObjectPtr<APawn> PawnKey(const_cast<APawn*>(Pawn));
		TWeakObjectPtr<UTextRenderComponent>& MarkerRef = RequestMarkers.FindOrAdd(PawnKey);
		if (bShowRequestMarker)
		{
			UTextRenderComponent* Marker = MarkerRef.Get();
			if (!Marker)
			{
				Marker = NewObject<UTextRenderComponent>(const_cast<APawn*>(Pawn), NAME_None, RF_Transient);
				if (Marker)
				{
					Marker->SetText(FText::FromString(TEXT("!")));
					Marker->SetTextRenderColor(FColor(255, 205, 30));
					Marker->SetWorldSize(72.0f);
					Marker->SetHorizontalAlignment(EHTA_Center);
					Marker->SetVerticalAlignment(EVRTA_TextCenter);
					Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Marker->RegisterComponent();
					MarkerRef = Marker;
				}
			}
			if (Marker)
			{
				const FVector MarkerLocation(Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y, HeadTopZ(Pawn) + 48.0f);
				Marker->SetWorldLocation(MarkerLocation);
				Marker->SetWorldRotation(FRotator(0.0f, CameraRotation.Yaw + 180.0f, 0.0f));
				Marker->SetVisibility(true);
			}
		}
		else if (UTextRenderComponent* Marker = MarkerRef.Get()) Marker->SetVisibility(false);

		// A large exclamation mark signals a completed request; offer cards appear in the review UI.
		if (!bShowRequestMarker && Stage != EOreDealAuctionStage::Idle && Seat.bConfirmed && !Seat.OfferCards.IsEmpty())
		{
			const FVector ToCenter = (TableCenter - Pawn->GetActorLocation()).GetSafeNormal2D();
			const FVector Base(Pawn->GetActorLocation().X + ToCenter.X * 18.0f, Pawn->GetActorLocation().Y + ToCenter.Y * 18.0f, HeadTopZ(Pawn) + 12.0f);
			const bool bGlow = (Stage == EOreDealAuctionStage::RequestReview || Stage == EOreDealAuctionStage::ConfirmTrade) && Seat.Player == FocusedPartner;
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
