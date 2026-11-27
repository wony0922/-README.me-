#include "OreDealAuctionDisplay.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"

namespace
{
	// Eight market-card faces in BP_TradeManager order.
	const TCHAR* AuctionCardTexturePaths[] = {
		TEXT("/Game/MaterialTraders/Cards/rock.rock"),
		TEXT("/Game/MaterialTraders/Cards/coal.coal"),
		TEXT("/Game/MaterialTraders/Cards/copper.copper"),
		TEXT("/Game/MaterialTraders/Cards/iron.iron"),
		TEXT("/Game/MaterialTraders/Cards/silver.silver"),
		TEXT("/Game/MaterialTraders/Cards/gold.gold"),
		TEXT("/Game/MaterialTraders/Cards/diamond.diamond"),
		TEXT("/Game/MaterialTraders/Cards/adamantium.adamantium"),
	};
	const TCHAR* FloatBackPath = TEXT("/Game/MaterialTraders/Cards/MI_CardFloat_Back.MI_CardFloat_Back");
	const TCHAR* GlowPath = TEXT("/Game/MaterialTraders/Cards/M_CardGlow.M_CardGlow");
	const TCHAR* PlanePath = TEXT("/Engine/BasicShapes/Plane.Plane");

	// Matches BP_CardBoard's WagonCard/WagonGlow setup: the pivot's X axis looks at the camera and
	// the plane is rotated (P0, Y-90, R90) inside it.
	const FRotator CardRelativeRotation(0.0f, -90.0f, 90.0f);
	const FVector CardBaseScale(0.1f, 0.14f, 1.0f);
	const FVector GlowBaseScale(0.118f, 0.158f, 1.0f);
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
	CardFaceMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MaterialTraders/Cards/M_CardFloat.M_CardFloat"));
	for (const TCHAR* Path : AuctionCardTexturePaths) FaceTextures.Add(LoadObject<UTexture2D>(nullptr, Path));
	FaceMaterials.SetNum(UE_ARRAY_COUNT(AuctionCardTexturePaths));
	BackMaterial = LoadObject<UMaterialInterface>(nullptr, FloatBackPath);
	GlowMaterial = LoadObject<UMaterialInterface>(nullptr, GlowPath);
}

UMaterialInterface* AOreDealAuctionDisplay::MaterialFor(int32 Resource)
{
	if (!FaceTextures.IsValidIndex(Resource) || !FaceTextures[Resource]) return BackMaterial.Get();
	if (!FaceMaterials.IsValidIndex(Resource)) FaceMaterials.SetNum(Resource + 1);
	if (!FaceMaterials[Resource] && CardFaceMaterial)
	{
		UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(CardFaceMaterial, this);
		if (Dynamic)
		{
			TArray<FMaterialParameterInfo> Parameters;
			TArray<FGuid> ParameterIds;
			CardFaceMaterial->GetAllTextureParameterInfo(Parameters, ParameterIds);
			for (const FMaterialParameterInfo& Parameter : Parameters)
				Dynamic->SetTextureParameterValueByInfo(Parameter, FaceTextures[Resource]);
			FaceMaterials[Resource] = Dynamic;
		}
	}
	if (FaceMaterials.IsValidIndex(Resource) && FaceMaterials[Resource]) return FaceMaterials[Resource].Get();
	return BackMaterial.Get();
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
		UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
		Label->SetupAttachment(Pivot);
		Label->SetRelativeRotation(CardRelativeRotation);
		Label->SetRelativeScale3D(CardBaseScale);
		Label->SetRelativeLocation(FVector(0.0f, 0.0f, 0.25f));
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetVerticalAlignment(EVRTA_TextCenter);
		Label->SetWorldSize(11.0f);
		Label->SetTextRenderColor(FColor::White);
		Label->SetCastShadow(false);
		Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Label->RegisterComponent();

		Pivots.Add(Pivot);
		Faces.Add(Face);
		Glows.Add(Glow);
		ResourceLabels.Add(Label);
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
		UTextRenderComponent* Label = ResourceLabels.IsValidIndex(Index) ? ResourceLabels[Index].Get() : nullptr;
		if (!Pivot || !Face || !Glow || !Label) continue;
		if (!Cards.IsValidIndex(Index))
		{
			Face->SetVisibility(false);
			Glow->SetVisibility(false);
			Label->SetVisibility(false);
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
		static const TCHAR* ResourceNames[] = { TEXT("Rock"), TEXT("Coal"), TEXT("Copper"), TEXT("Iron"), TEXT("Silver"), TEXT("Gold"), TEXT("Diamond"), TEXT("Adamantium") };
		Label->SetText(FText::FromString(Card.Resource >= 0 && Card.Resource < UE_ARRAY_COUNT(ResourceNames) ? ResourceNames[Card.Resource] : TEXT("?")));
		Label->SetVisibility(true);
	}
}
