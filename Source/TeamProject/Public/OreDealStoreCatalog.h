#pragma once

#include "CoreMinimal.h"

/**
 * 상점(F 키) 구매 탭에 올라가는 아이템 목록.
 *
 * 아이템을 추가/수정하려면 OreDealStoreCatalog.cpp 의 GetItems() 안에 한 줄만 고치면 됩니다.
 * 배열 순서가 곧 상점 칸 순서입니다(왼쪽 위부터 오른쪽으로, 4칸 x 2줄 = 최대 8개).
 */

/** 구매한 아이템을 플레이어에게 주는 방식. 새 방식이 필요하면 여기와 UOreDealStoreComponent::GrantItem 에 추가합니다. */
enum class EOreDealStoreGrant : uint8
{
	/** PlayerState(BP_PS_Table)의 정수 변수에 Amount 만큼 더합니다. 예: Wood */
	PlayerStateInt,
};

struct FOreDealStoreItem
{
	/** 서버에 보내는 고유 이름. 다른 아이템과 겹치면 안 됩니다. */
	FName Id;
	/** 칸 아래에 보이는 이름. */
	FString Name;
	/** 한 번 살 때 드는 골드. */
	int32 Cost = 0;
	/** 한 번 살 때 받는 개수(한 세트). */
	int32 Amount = 1;
	EOreDealStoreGrant Grant = EOreDealStoreGrant::PlayerStateInt;
	/** Grant 가 PlayerStateInt 일 때 더할 변수 이름. */
	FName GrantProperty;
	/** 칸 안에 보이는 아이콘 텍스처 경로. */
	FString IconPath;
};

/** 판매 탭에 보이는 마차 한 칸의 상태. */
struct FOreDealStoreWagonView
{
	int32 Resource = INDEX_NONE;
	int32 Count = 0;
	/** 지금 팔면 받는 골드(정산표에서 조건을 만족하는 가장 큰 값, 남는 카드는 버림). */
	int32 Reward = 0;
	bool bCanSell = false;
	/** 팔 수 없는 이유 등 버튼 아래 안내 문구. */
	FString Note;

	bool operator==(const FOreDealStoreWagonView& Other) const
	{
		return Resource == Other.Resource && Count == Other.Count && Reward == Other.Reward && bCanSell == Other.bCanSell && Note == Other.Note;
	}
};

namespace OreDealStore
{
	constexpr int32 SlotColumns = 4;
	constexpr int32 SlotRows = 2;
	constexpr int32 SlotCount = SlotColumns * SlotRows;

	TEAMPROJECT_API const TArray<FOreDealStoreItem>& GetItems();
	TEAMPROJECT_API const FOreDealStoreItem* FindItem(FName Id);
}
