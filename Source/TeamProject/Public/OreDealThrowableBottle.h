#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OreDealThrowableBottle.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;

UCLASS()
class TEAMPROJECT_API AOreDealThrowableBottle : public AActor
{
	GENERATED_BODY()
public:
	AOreDealThrowableBottle();
	void InitializeBottle(UStaticMesh* Mesh, UMaterialInterface* Material);
	void SetHeld(bool bHeld);
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved,
		FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;
	UStaticMeshComponent* GetBottleMesh() const { return BottleMesh; }
private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BottleMesh;
	bool bHeld = false;
};
