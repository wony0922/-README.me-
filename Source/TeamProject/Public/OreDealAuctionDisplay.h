#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OreDealAuctionDisplay.generated.h"

class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** One floating card to draw this frame. */
struct FOreDealCardDraw
{
	FVector Location = FVector::ZeroVector;
	float Scale = 1.0f;
	int32 Resource = INDEX_NONE;
	bool bGlow = false;
};

/**
 * Local-only (never replicated) set of floating cards for the auction: trade goods above the deck,
 * offers above each player, and taken cards waiting beside each wagon board. Every card turns to face
 * the local camera, so each player sees all of them head-on.
 */
UCLASS(NotPlaceable, Transient)
class TEAMPROJECT_API AOreDealAuctionDisplay : public AActor
{
	GENERATED_BODY()

public:
	AOreDealAuctionDisplay();
	void ShowCards(const TArray<FOreDealCardDraw>& Cards, const FVector& CameraLocation);

private:
	UPROPERTY(Transient) TObjectPtr<USceneComponent> DisplayRoot;
	UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> Pivots;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Faces;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Glows;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> FaceMaterials;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BackMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> PlaneMesh;

	TArray<int32> ShownResources;

	void LoadAssets();
	void EnsureVisuals(int32 Count);
	UMaterialInterface* MaterialFor(int32 Resource) const;
};
