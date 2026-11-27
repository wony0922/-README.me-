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
	void InitializeBottle();
	void SetHeld(bool bHeld);
	void BeginDrink();
	virtual void Tick(float DeltaSeconds) override;
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved,
		FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;
	UStaticMeshComponent* GetBottleMesh() const { return BottleMesh; }
private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BottleMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BottleShoulder;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BottleNeck;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BottleLip;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BottleLabel;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BottleCork;
	bool bHeld = false;
	bool bDrinking = false;
	float DrinkElapsed = 0.0f;
	FVector DrinkStartLocation = FVector::ZeroVector;
	FRotator DrinkStartRotation = FRotator::ZeroRotator;
};
