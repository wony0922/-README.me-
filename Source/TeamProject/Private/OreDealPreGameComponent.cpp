#include "OreDealPreGameComponent.h"

#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Widget.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UnrealType.h"

namespace
{
	const FName MerchantTag(TEXT("OreDealMerchant"));
	// HUD children hidden while waiting: resource bar (wood/gold) and the turn phase label.
	const FName WaitingHiddenHudWidgets[] = { FName(TEXT("ResBar")), FName(TEXT("TxtPhase")) };
	// Left hand resting on the table, mirrored from the right hand (RightHandRoot 38,12,79 / RightHand roll 90 yaw -90).
	const FVector RestingLeftHandLocation(38.0f, -12.0f, 79.0f);
	const FRotator RestingLeftHandRotation(0.0f, -90.0f, 90.0f); // pitch, yaw, roll
	const FVector RestingLeftHandScale(2.0f, 2.0f, 2.0f);
	const TCHAR* RestingLeftHandAnimPath = TEXT("/Game/Asset/Hands/SK_Hand_LRelax.SK_Hand_LRelax");

	bool IsClassNamed(const AActor* Actor, const TCHAR* ClassName)
	{
		return Actor && Actor->GetClass()->GetName() == ClassName;
	}

	template <typename T>
	T* FindComponentNamed(AActor* Actor, const TCHAR* Name)
	{
		TArray<T*> Components;
		Actor->GetComponents<T>(Components);
		for (T* Component : Components)
			if (Component && Component->GetName() == Name) return Component;
		return nullptr;
	}
}

UOreDealPreGameComponent::UOreDealPreGameComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

AActor* UOreDealPreGameComponent::FindDeck()
{
	if (CachedDeck.IsValid()) return CachedDeck.Get();
	if (UWorld* World = GetWorld())
		for (TActorIterator<AActor> It(World); It; ++It)
			if (IsClassNamed(*It, TEXT("BP_CardDeck_C"))) { CachedDeck = *It; break; }
	return CachedDeck.Get();
}

void UOreDealPreGameComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Deck = FindDeck();
	const FBoolProperty* StartedProperty = Deck ? FindFProperty<FBoolProperty>(Deck->GetClass(), TEXT("bGameStarted")) : nullptr;
	if (!StartedProperty) return; // Not on the table level (main menu etc.).
	const bool bWaiting = !StartedProperty->GetPropertyValue_InContainer(Deck);

	ApplyTableActors(bWaiting);
	ApplyHud(bWaiting);
	ApplyHands(bWaiting);
	LastAppliedState = bWaiting ? 1 : 0;
}

void UOreDealPreGameComponent::ApplyTableActors(bool bWaiting)
{
	UWorld* World = GetWorld();
	if (!World) return;
	TArray<AActor*> Merchants;
	TArray<AActor*> Targets;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (IsClassNamed(Actor, TEXT("BP_CardDeck_C")) || IsClassNamed(Actor, TEXT("BP_CardBoard_C"))) Targets.Add(Actor);
		else if (Actor->ActorHasTag(MerchantTag)) { Targets.Add(Actor); Merchants.Add(Actor); }
	}
	// Merchant name signs are untagged TextRenderActors standing on each merchant counter.
	for (TActorIterator<ATextRenderActor> It(World); It; ++It)
		for (const AActor* Merchant : Merchants)
			if (FVector::DistSquared2D(It->GetActorLocation(), Merchant->GetActorLocation()) < FMath::Square(60.0f)) { Targets.Add(*It); break; }

	for (AActor* Actor : Targets)
		if (Actor->IsHidden() != bWaiting) Actor->SetActorHiddenInGame(bWaiting);
}

void UOreDealPreGameComponent::ApplyHud(bool bWaiting)
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController()) return;
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PC, Widgets, UUserWidget::StaticClass(), false);
	for (UUserWidget* Hud : Widgets)
	{
		if (!Hud || Hud->GetOwningPlayer() != PC || !Hud->GetClass()->GetName().StartsWith(TEXT("WBP_HUD"))) continue;
		for (const FName& Name : WaitingHiddenHudWidgets)
		{
			UWidget* Child = Hud->GetWidgetFromName(Name);
			if (!Child) continue;
			if (bWaiting)
			{
				if (!SavedWidgetVisibility.Contains(Child)) SavedWidgetVisibility.Add(Child, Child->GetVisibility());
				if (Child->GetVisibility() != ESlateVisibility::Collapsed) Child->SetVisibility(ESlateVisibility::Collapsed);
			}
			else if (const ESlateVisibility* Saved = SavedWidgetVisibility.Find(Child))
			{
				Child->SetVisibility(*Saved);
				SavedWidgetVisibility.Remove(Child);
			}
		}
	}
}

void UOreDealPreGameComponent::ApplyHands(bool bWaiting)
{
	UWorld* World = GetWorld();
	if (!World) return;
	static TWeakObjectPtr<UAnimationAsset> RestAnim;
	if (bWaiting && !RestAnim.IsValid()) RestAnim = LoadObject<UAnimationAsset>(nullptr, RestingLeftHandAnimPath);

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Pawn = *It;
		if (!IsClassNamed(Pawn, TEXT("BP_SeatedPlayer_C"))) continue;
		USkeletalMeshComponent* LeftHand = FindComponentNamed<USkeletalMeshComponent>(Pawn, TEXT("LeftHand"));
		if (!LeftHand) continue;

		if (bWaiting)
		{
			if (!SavedLeftHands.Contains(LeftHand))
			{
				FSavedHand& Saved = SavedLeftHands.Add(LeftHand);
				Saved.RelativeTransform = LeftHand->GetRelativeTransform();
				if (UAnimSingleNodeInstance* Single = LeftHand->GetSingleNodeInstance()) Saved.Animation = Single->GetAnimationAsset();
				if (RestAnim.IsValid()) LeftHand->PlayAnimation(RestAnim.Get(), true);
			}
			const FTransform RestLocal(RestingLeftHandRotation, RestingLeftHandLocation, RestingLeftHandScale);
			LeftHand->SetWorldTransform(RestLocal * Pawn->GetActorTransform());
		}
		else if (const FSavedHand* Saved = SavedLeftHands.Find(LeftHand))
		{
			LeftHand->SetRelativeTransform(Saved->RelativeTransform);
			if (Saved->Animation.IsValid()) LeftHand->PlayAnimation(Saved->Animation.Get(), true);
			SavedLeftHands.Remove(LeftHand);
		}
	}
}
