#include "OreDealThrowableBottle.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

AOreDealThrowableBottle::AOreDealThrowableBottle()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	BottleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BottleMesh"));
	SetRootComponent(BottleMesh);
	BottleMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	BottleMesh->SetNotifyRigidBodyCollision(true);
	BottleMesh->SetSimulatePhysics(true);
}

void AOreDealThrowableBottle::InitializeBottle(UStaticMesh* Mesh, UMaterialInterface* Material)
{
	BottleMesh->SetStaticMesh(Mesh);
	if (Material) BottleMesh->SetMaterial(0, Material);
	BottleMesh->SetWorldScale3D(FVector(0.35f, 0.35f, 0.75f));
	Tags.AddUnique(TEXT("OreDealBottle"));
}

void AOreDealThrowableBottle::SetHeld(bool bInHeld)
{
	bHeld = bInHeld;
	BottleMesh->SetSimulatePhysics(!bHeld);
	BottleMesh->SetCollisionEnabled(bHeld ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
}

void AOreDealThrowableBottle::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
	bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	if (HasAuthority() && !bHeld && Other && Other != GetOwner()) Destroy();
}
