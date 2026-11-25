#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OreDealCardInfo.generated.h"

/**
 * 판매 가격 구간 한 단계.
 * 예) MinCount=4, Reward=1 → "자원 4개 이상이면 1코인"
 */
USTRUCT(BlueprintType)
struct FOreDealSaleTier
{
	GENERATED_BODY()

	/** 이 가격을 받으려면 필요한 최소 자원 개수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sale", meta = (ClampMin = "1", DisplayName = "최소 개수"))
	int32 MinCount = 1;

	/** 위 조건을 만족할 때 받는 코인 수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sale", meta = (ClampMin = "1", DisplayName = "코인 보상"))
	int32 Reward = 1;
};

/**
 * 자원 카드 한 종류에 대한 모든 설정.
 * [나무 | 총 20장 | [4개:1코인, 7개:2코인, 9개:3코인, 10개:4코인]]
 */
USTRUCT(BlueprintType)
struct FOreDealCardInfo
{
	GENERATED_BODY()

	/** 자원 ID (0-based). ResourceId 가 곧 카드 종류 인덱스. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Card", meta = (DisplayName = "자원 ID"))
	int32 ResourceId = INDEX_NONE;

	/** UI 등에 표시될 이름 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Card", meta = (DisplayName = "자원 이름"))
	FText DisplayName = FText::GetEmpty();

	/** 덱에 넣을 카드 총 장 수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Card", meta = (ClampMin = "0", DisplayName = "덱 카드 수 (총 장 수)"))
	int32 DeckCopies = 0;

	/**
	 * 판매 가격 구간 목록. 오름차순으로 정렬하면 된다.
	 * 예) [{MinCount=4, Reward=1}, {MinCount=7, Reward=2}, {MinCount=9, Reward=3}, {MinCount=10, Reward=4}]
	 * → 자원 4개 이상이면 1코인, 7개 이상이면 2코인, 9개 이상이면 3코인, 10개 이상이면 4코인
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Card", meta = (DisplayName = "판매 가격 구간"))
	TArray<FOreDealSaleTier> SaleTiers;

	/** 한 슬롯에 놓일 수 있는 최대 카드 수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Card", meta = (ClampMin = "1", DisplayName = "최대 스택"))
	int32 MaxStack = 1;
};

/**
 * 여러 카드 정보를 배열로 담는 Data Asset.
 * 콘텐츠 브라우저에서 우클릭 → Miscellaneous → Data Asset → OreDealCardInfoAsset 으로 생성한다.
 */
UCLASS(BlueprintType)
class UOreDealCardInfoAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	/** 모든 자원 카드 정의를 여기서 관리한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Card")
	TArray<FOreDealCardInfo> CardInfos;
};
