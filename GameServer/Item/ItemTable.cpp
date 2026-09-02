#include "pch.h"
#include "ItemTable.h"

namespace
{
	// 새 아이템은 이 배열 끝에만 추가한다.
	// id는 인벤토리 DB 저장 데이터가 참조하는 영구 식별자이므로,
	// 이미 등록된 항목의 id는 절대 바꾸거나 다른 아이템에 재사용하지 않는다.
	constexpr ItemTableRow kRows[] =
	{
		// { id, type, maxStack, name, grade }
		{ 1, ItemType::eConsumable, 99, "Health Potion", ItemGrade::eNone },
		{ 2, ItemType::eEquipment,   1, "Wooden Sword",  ItemGrade::eNone },
		{ 3, ItemType::eEquipment,   1, "Iron Sword",    ItemGrade::eRare },
		{ 4, ItemType::eEquipment,   1, "Steel Sword",   ItemGrade::eEpic },
		{ 5, ItemType::eEquipment,   1, "Flame Sword",   ItemGrade::eUnique },
		{ 6, ItemType::eEquipment,   1, "Dragon Slayer", ItemGrade::eLegendary },
	};

	// id == 0(예약값) 또는 중복 id가 섞여 들어가면 Find()가 엉뚱한 행을
	// 반환하거나 두 아이템이 같은 id를 공유하는 채로 굳어버릴 수 있다.
	// 그런 실수를 런타임이 아니라 컴파일 타임에 막는다.
	constexpr bool IsItemTableValid(const ItemTableRow* rows, size_t count) noexcept
	{
		for (size_t i = 0; i < count; ++i)
		{
			if (rows[i].id == ITEM_TABLE_ID_NONE)
				return false;

			for (size_t j = i + 1; j < count; ++j)
			{
				if (rows[i].id == rows[j].id)
					return false;
			}
		}
		return true;
	}

	static_assert(IsItemTableValid(kRows, sizeof(kRows) / sizeof(kRows[0])),
		"ItemTable: id는 0일 수 없고, 서로 겹치지 않아야 합니다.");
}

const ItemTableRow* ItemTable::Find(ItemTableId id) noexcept
{
	for (const auto& row : kRows)
	{
		if (row.id == id)
			return &row;
	}

	return nullptr;
}
