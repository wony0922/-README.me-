#include "OreDealStoreCatalog.h"

namespace
{
	FOreDealStoreItem MakePlayerStateItem(const TCHAR* Id, const TCHAR* Name, int32 Cost, int32 Amount, const TCHAR* PlayerStateVariable, const TCHAR* IconPath)
	{
		FOreDealStoreItem Item;
		Item.Id = FName(Id);
		Item.Name = Name;
		Item.Cost = Cost;
		Item.Amount = Amount;
		Item.Grant = EOreDealStoreGrant::PlayerStateInt;
		Item.GrantProperty = FName(PlayerStateVariable);
		Item.IconPath = IconPath;
		return Item;
	}
}

const TArray<FOreDealStoreItem>& OreDealStore::GetItems()
{
	// ===== 상점 아이템 등록 =====
	// 한 줄 = 한 칸. (고유 이름, 표시 이름, 골드 가격, 한 세트 개수, PlayerState 변수 이름, 아이콘)
	// 값은 임시이므로 여기 숫자만 바꾸면 됩니다.
	static const TArray<FOreDealStoreItem> Items = {
		MakePlayerStateItem(TEXT("Wood"), TEXT("나무 2개"), 1, 2, TEXT("Wood"), TEXT("/Game/MaterialTraders/UI/Textures/T_Icon_Wood.T_Icon_Wood")),
	};
	return Items;
}

const FOreDealStoreItem* OreDealStore::FindItem(FName Id)
{
	return GetItems().FindByPredicate([Id](const FOreDealStoreItem& Item) { return Item.Id == Id; });
}
