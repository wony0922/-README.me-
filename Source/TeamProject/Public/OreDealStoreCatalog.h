#pragma once

#include "CoreMinimal.h"
#include "OreDealStoreCatalog.generated.h"

/**
 * 상점(F 키) 구매 탭에 올라가는 아이템 목록.
 *
 * BP_PC_Table의 StoreItems 배열이 서버와 상점 화면에서 공유하는 상품 원본입니다.
 * 배열 순서가 곧 상점 칸 순서입니다(왼쪽 위부터 오른쪽으로, 최대 8개).
 */

/** 구매한 아이템을 플레이어에게 주는 방식. 새 방식이 필요하면 여기와 UOreDealStoreComponent::GrantItem 에 추가합니다. */
UENUM(BlueprintType)
enum class EOreDealStoreGrant : uint8
{
	/** PlayerState(BP_PS_Table)의 정수 변수에 Amount 만큼 더합니다. 예: Wood */
	PlayerStateInt,
	/** Adds one permanent wagon slot to the purchasing player's board. */
	WagonSlotUpgrade,
	/** AI 안경: 상대방들의 패 정보를 조회하여 구매자에게 채팅으로 안내 */
	AIGlasses,
	/** 도끼 (더 많은 나무!): 카드 뽑기 시 나무 획득량 2배 부여 */
	WoodAxe,
	/** 술병: 테이블 좌측에 던질 수 있는 술병 3개 생성 */
	SpawnBottles,
	/** 혼돈: 모든 플레이어의 패와 버린 패, 덱을 모두 모아 섞은 뒤 균등 재분배 */
	ChaosCard,
};

/** Currency charged by the store. Wood is allowed to go negative after a caught theft. */
UENUM(BlueprintType)
enum class EOreDealStoreCurrency : uint8
{
	Gold,
	Wood,
};

USTRUCT(BlueprintType)
struct FOreDealStoreItem
{
	GENERATED_BODY()

	/** 서버에 보내는 고유 이름. 다른 아이템과 겹치면 안 됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Store")
	FName Id;
	/** 칸 아래에 보이는 이름. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Store")
	FString Name;
	/** 한 번 살 때 드는 골드. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Store", meta=(ClampMin="0"))
	int32 Cost = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Store")
	EOreDealStoreCurrency Currency = EOreDealStoreCurrency::Gold;
	/** 한 번 살 때 받는 개수(한 세트). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Store", meta=(ClampMin="1"))
	int32 Amount = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Store")
	EOreDealStoreGrant Grant = EOreDealStoreGrant::PlayerStateInt;
	/** Grant 가 PlayerStateInt 일 때 더할 변수 이름. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Store")
	FName GrantProperty;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Store")
	TSoftObjectPtr<class UTexture2D> Icon;
	/** Optional hover description shown by the Blueprint store UI. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Store")
	FText Description;
};

/** 판매 탭에 보이는 마차 한 칸의 상태. */
USTRUCT(BlueprintType)
struct FOreDealStoreWagonView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Store")
	int32 Resource = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category="Store")
	int32 Count = 0;
	/** 지금 팔면 받는 골드(정산표에서 조건을 만족하는 가장 큰 값, 남는 카드는 버림). */
	UPROPERTY(BlueprintReadOnly, Category="Store")
	int32 Reward = 0;
	UPROPERTY(BlueprintReadOnly, Category="Store")
	bool bCanSell = false;
	UPROPERTY(BlueprintReadOnly, Category="Store")
	bool bBroken = false;
	/** 팔 수 없는 이유 등 버튼 아래 안내 문구. */
	UPROPERTY(BlueprintReadOnly, Category="Store")
	FString Note;

	bool operator==(const FOreDealStoreWagonView& Other) const
	{
		return Resource == Other.Resource && Count == Other.Count && Reward == Other.Reward && bCanSell == Other.bCanSell && bBroken == Other.bBroken && Note == Other.Note;
	}
};

namespace OreDealStore
{
	constexpr int32 SlotColumns = 4;
	constexpr int32 SlotRows = 2;
	constexpr int32 SlotCount = SlotColumns * SlotRows;
}
