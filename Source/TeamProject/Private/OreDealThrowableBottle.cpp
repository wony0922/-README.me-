#include "OreDealThrowableBottle.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AOreDealThrowableBottle::AOreDealThrowableBottle()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	SetReplicateMovement(true);
	BottleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BottleMesh"));
	SetRootComponent(BottleMesh);
	BottleShoulder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BottleShoulder"));
	BottleShoulder->SetupAttachment(BottleMesh);
	BottleNeck = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BottleNeck"));
	BottleNeck->SetupAttachment(BottleMesh);
	BottleLip = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BottleLip"));
	BottleLip->SetupAttachment(BottleMesh);
	BottleLabel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BottleLabel"));
	BottleLabel->SetupAttachment(BottleMesh);
	BottleCork = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BottleCork"));
	BottleCork->SetupAttachment(BottleMesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BottleMaterial(TEXT("/Game/Asset/Saloon/SL_BottleAmber.SL_BottleAmber"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> LabelMaterial(TEXT("/Game/Asset/Saloon/SL_Label.SL_Label"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CorkMaterial(TEXT("/Game/Asset/Saloon/SL_Gilt.SL_Gilt"));
	if (CylinderMesh.Succeeded())
	{
		BottleMesh->SetStaticMesh(CylinderMesh.Object);
		BottleMesh->SetRelativeScale3D(FVector(0.22f, 0.22f, 0.30f));
		BottleNeck->SetStaticMesh(CylinderMesh.Object);
		BottleNeck->SetRelativeLocation(FVector(0.0f, 0.0f, 27.0f));
		BottleNeck->SetRelativeScale3D(FVector(0.085f, 0.085f, 0.16f));
		BottleLip->SetStaticMesh(CylinderMesh.Object);
		BottleLip->SetRelativeLocation(FVector(0.0f, 0.0f, 35.0f));
		BottleLip->SetRelativeScale3D(FVector(0.105f, 0.105f, 0.035f));
		BottleLabel->SetStaticMesh(CylinderMesh.Object);
		BottleLabel->SetRelativeScale3D(FVector(0.225f, 0.225f, 0.09f));
		BottleCork->SetStaticMesh(CylinderMesh.Object);
		BottleCork->SetRelativeLocation(FVector(0.0f, 0.0f, 40.0f));
		BottleCork->SetRelativeScale3D(FVector(0.06f, 0.06f, 0.055f));
	}
	if (SphereMesh.Succeeded())
	{
		BottleShoulder->SetStaticMesh(SphereMesh.Object);
		BottleShoulder->SetRelativeLocation(FVector(0.0f, 0.0f, 15.0f));
		BottleShoulder->SetRelativeScale3D(FVector(0.22f, 0.22f, 0.14f));
	}
	if (BottleMaterial.Succeeded())
	{
		BottleMesh->SetMaterial(0, BottleMaterial.Object);
		BottleShoulder->SetMaterial(0, BottleMaterial.Object);
		BottleNeck->SetMaterial(0, BottleMaterial.Object);
	}
	if (LabelMaterial.Succeeded()) BottleLabel->SetMaterial(0, LabelMaterial.Object);
	if (CorkMaterial.Succeeded())
	{
		BottleLip->SetMaterial(0, CorkMaterial.Object);
		BottleCork->SetMaterial(0, CorkMaterial.Object);
	}
	Tags.AddUnique(TEXT("OreDealBottle"));
	BottleMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	BottleMesh->SetNotifyRigidBodyCollision(true);
	BottleMesh->SetSimulatePhysics(true);
	for (UStaticMeshComponent* Part : { BottleShoulder.Get(), BottleNeck.Get(), BottleLip.Get(), BottleLabel.Get(), BottleCork.Get() })
		if (Part) Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AOreDealThrowableBottle::InitializeBottle()
{
	Tags.AddUnique(TEXT("OreDealBottle"));
}

void AOreDealThrowableBottle::SetHeld(bool bInHeld)
{
	bHeld = bInHeld;
	BottleMesh->SetSimulatePhysics(!bHeld);
	BottleMesh->SetCollisionEnabled(bHeld ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
}

void AOreDealThrowableBottle::BeginDrink()
{
	if (!HasAuthority() || !bHeld || bDrinking) return;
	bDrinking = true;
	DrinkElapsed = 0.0f;
	DrinkStartLocation = BottleMesh->GetRelativeLocation();
	DrinkStartRotation = BottleMesh->GetRelativeRotation();
	SetActorTickEnabled(true);
}

void AOreDealThrowableBottle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !bDrinking) return;
	DrinkElapsed += DeltaSeconds;
	constexpr float RaiseTime = 0.32f;
	constexpr float HoldTime = 0.62f;
	constexpr float LowerTime = 0.36f;
	const float TotalTime = RaiseTime + HoldTime + LowerTime;
	float Alpha = 1.0f;
	if (DrinkElapsed < RaiseTime)
		Alpha = FMath::InterpEaseInOut(0.0f, 1.0f, DrinkElapsed / RaiseTime, 2.0f);
	else if (DrinkElapsed > RaiseTime + HoldTime)
		Alpha = 1.0f - FMath::InterpEaseInOut(0.0f, 1.0f, (DrinkElapsed - RaiseTime - HoldTime) / LowerTime, 2.0f);
	BottleMesh->SetRelativeLocation(DrinkStartLocation + FVector(2.0f, 0.0f, 22.0f) * Alpha);
	BottleMesh->SetRelativeRotation(DrinkStartRotation + FRotator(-68.0f, 0.0f, 0.0f) * Alpha);
	if (DrinkElapsed >= TotalTime) Destroy();
}

void AOreDealThrowableBottle::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
	bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	if (HasAuthority() && !bHeld && Other && Other != GetOwner()) Destroy();
}
