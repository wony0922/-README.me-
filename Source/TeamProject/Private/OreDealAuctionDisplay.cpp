#include "OreDealAuctionDisplay.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

namespace
{
	// Same order as BP_CardBoard.FloatMats (unlit floating card faces).
	const TCHAR* FloatMaterialPaths[] = {
		TEXT("/Game/MaterialTraders/Cards/MI_CardFloat_Stone.MI_CardFloat_Stone"),
		TEXT("/Game/MaterialTraders/Cards/MI_CardFloat_Coal.MI_CardFloat_Coal"),
		TEXT("/Game/MaterialTraders/Cards/MI_CardFloat_Gold.MI_CardFloat_Gold"),
		TEXT("/Game/MaterialTraders/Cards/MI_CardFloat_Iron.MI_CardFloat_Iron"),
		TEXT("/Game/MaterialTraders/Cards/MI_CardFloat_Diamond.MI_CardFloat_Diamond"),
	};
	const TCHAR* FloatBackPath = TEXT("/Game/MaterialTraders/Cards/MI_CardFloat_Back.MI_CardFloat_Back");
	const TCHAR* GlowPath = TEXT("/Game/MaterialTraders/Cards/M_CardGlow.M_CardGlow");
	const TCHAR* PlanePath = TEXT("/Engine/BasicShapes/Plane.Plane");

	// Matches BP_CardBoard's WagonCard/WagonGlow setup: the pivot's X axis looks at the camera and
	// the plane is rotated (P0, Y-90, R90) inside it.
	const FRotator CardRelativeRotation(0.0f, -90.0f, 90.0f);
	const FVector CardBaseScale(0.085f, 0.119f, 1.0f);
	const FVector GlowBaseScale(0.1f, 0.134f, 1.0f);
	const FVector GlowOffset(-0.3f, 0.0f, 0.0f);
}

AOreDealAuctionDisplay::AOreDealAuctionDisplay()
{
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);
	DisplayRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DisplayRoot"));
	RootComponent = DisplayRoot;
}

void AOreDealAuctionDisplay::LoadAssets()
{
	if (PlaneMesh) return;
	PlaneMesh = LoadObject<UStaticMesh>(nullptr, PlanePath);
	for (const TCHAR* Path : FloatMaterialPaths) FaceMaterials.Add(LoadObject<UMaterialInterface>(nullptr, Path));
	BackMaterial = LoadObject<UMaterialInterface>(nullptr, FloatBackPath);
	GlowMaterial = LoadObject<UMaterialInterface>(nullptr, GlowPath);
}

UMaterialInterface* AOreDealAuctionDisplay::MaterialFor(int32 Resource) const
{
	UMaterialInterface* Material = FaceMaterials.IsValidIndex(Resource) ? FaceMaterials[Resource].Get() : nullptr;
	return Material ? Material : BackMaterial.Get();
}

void AOreDealAuctionDisplay::EnsureVisuals(int32 Count)
{
	LoadAssets();
	while (Pivots.Num() < Count)
	{
		USceneComponent* Pivot = NewObject<USceneComponent>(this);
		Pivot->SetupAttachment(DisplayRoot);
		Pivot->SetUsingAbsoluteLocation(true);
		Pivot->SetUsingAbsoluteRotation(true);
		Pivot->SetUsingAbsoluteScale(true);
		Pivot->RegisterComponent();

		auto MakePlane = [this, Pivot](UMaterialInterface* Material) -> UStaticMeshComponent*
		{
			UStaticMeshComponent* Plane = NewObject<UStaticMeshComponent>(this);
			Plane->SetupAttachment(Pivot);
			Plane->SetStaticMesh(PlaneMesh);
			Plane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Plane->SetCastShadow(false);
			Plane->SetRelativeRotation(CardRelativeRotation);
			if (Material) Plane->SetMaterial(0, Material);
			Plane->RegisterComponent();
			return Plane;
		};
		UStaticMeshComponent* Face = MakePlane(BackMaterial);
		Face->SetRelativeScale3D(CardBaseScale);
		UStaticMeshComponent* Glow = MakePlane(GlowMaterial);
		Glow->SetRelativeLocation(GlowOffset);
		Glow->SetRelativeScale3D(GlowBaseScale);

		Pivots.Add(Pivot);
		Faces.Add(Face);
		Glows.Add(Glow);
		ShownResources.Add(INDEX_NONE - 1);
	}
}

void AOreDealAuctionDisplay::ShowCards(const TArray<FOreDealCardDraw>& Cards, const FVector& CameraLocation)
{
	EnsureVisuals(Cards.Num());
	for (int32 Index = 0; Index < Pivots.Num(); ++Index)
	{
		USceneComponent* Pivot = Pivots[Index];
		UStaticMeshComponent* Face = Faces[Index];
		UStaticMeshComponent* Glow = Glows[Index];
		if (!Pivot || !Face || !Glow) continue;
		if (!Cards.IsValidIndex(Index))
		{
			Face->SetVisibility(false);
			Glow->SetVisibility(false);
			continue;
		}
		const FOreDealCardDraw& Card = Cards[Index];
		Pivot->SetWorldLocation(Card.Location);
		Pivot->SetWorldRotation((CameraLocation - Card.Location).Rotation());
		Pivot->SetWorldScale3D(FVector(Card.Scale));
		if (ShownResources[Index] != Card.Resource)
		{
			Face->SetMaterial(0, MaterialFor(Card.Resource));
			ShownResources[Index] = Card.Resource;
		}
		Face->SetVisibility(true);
		Glow->SetVisibility(Card.bGlow);
	}
}
