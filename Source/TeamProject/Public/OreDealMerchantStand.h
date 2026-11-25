#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OreDealMerchantStand.generated.h"

class UStaticMeshComponent;
class USceneComponent;

UCLASS(Blueprintable)
class TEAMPROJECT_API AOreDealMerchantStand : public AActor
{
	GENERATED_BODY()

public:
	AOreDealMerchantStand();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MerchantStand")
	TObjectPtr<USceneComponent> StandRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MerchantStand")
	TObjectPtr<UStaticMeshComponent> TableBase;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MerchantStand")
	TObjectPtr<UStaticMeshComponent> CounterTop;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MerchantStand")
	TObjectPtr<UStaticMeshComponent> RoofAwning;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MerchantStand")
	TObjectPtr<UStaticMeshComponent> PostFL;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MerchantStand")
	TObjectPtr<UStaticMeshComponent> PostFR;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MerchantStand")
	TObjectPtr<UStaticMeshComponent> PostBL;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MerchantStand")
	TObjectPtr<UStaticMeshComponent> PostBR;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MerchantStand")
	TObjectPtr<UStaticMeshComponent> WheelL;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MerchantStand")
	TObjectPtr<UStaticMeshComponent> WheelR;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MerchantStand")
	TObjectPtr<UStaticMeshComponent> MerchantBody;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MerchantStand")
	TObjectPtr<UStaticMeshComponent> MerchantHead;
};
