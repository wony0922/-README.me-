#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OreDealCardInfo.h"
#include "OreDealCardConfigComponent.generated.h"

/**
 * 카드 정보(Data Asset)를 로드하고 런타임에 조회할 수 있게 해주는 컴포넌트.
 * GameMode 혹은 다른 액터에 부착해서 사용한다.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEAMPROJECT_API UOreDealCardConfigComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UOreDealCardConfigComponent();

	/** 에디터에서 지정하는 카드 정보 애셋 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Config")
	TObjectPtr<UOreDealCardInfoAsset> CardInfoAsset = nullptr;

	/** 런타임에 빠르게 조회하기 위한 캐시 (C++ 전용) */
	TMap<int32, FOreDealCardInfo> CardInfoMap;

	virtual void BeginPlay() override;

	/**
	 * 블루프린트에서 카드 정보를 조회할 때 사용. 
	 * 성공하면 true를 반환하고 OutInfo에 복사본을 넣어준다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Card|Config")
	bool TryGetCardInfo(int32 ResourceId, FOreDealCardInfo& OutInfo) const;

	/** C++ 에서 직접 포인터로 접근할 때 사용 (UFUNCTION 아님) */
	const FOreDealCardInfo* GetCardInfo(int32 ResourceId) const;
};
