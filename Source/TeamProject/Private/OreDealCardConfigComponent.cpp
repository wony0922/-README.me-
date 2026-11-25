#include "OreDealCardConfigComponent.h"

UOreDealCardConfigComponent::UOreDealCardConfigComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UOreDealCardConfigComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!CardInfoAsset)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CardConfig] CardInfoAsset 가 지정되지 않았습니다."));
		return;
	}

	// 배열 → Map 변환 (O(1) 조회)
	CardInfoMap.Empty();
	for (const FOreDealCardInfo& Info : CardInfoAsset->CardInfos)
	{
		CardInfoMap.Add(Info.ResourceId, Info);
	}
}

bool UOreDealCardConfigComponent::TryGetCardInfo(int32 ResourceId, FOreDealCardInfo& OutInfo) const
{
	if (const FOreDealCardInfo* Found = CardInfoMap.Find(ResourceId))
	{
		OutInfo = *Found;
		return true;
	}
	return false;
}

const FOreDealCardInfo* UOreDealCardConfigComponent::GetCardInfo(int32 ResourceId) const
{
	return CardInfoMap.Find(ResourceId);
}
