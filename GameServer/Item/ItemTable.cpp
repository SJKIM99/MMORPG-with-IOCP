#include "pch.h"
#include "ItemTable.h"
#include "ItemTableData.h"

namespace
{
	// 원본 행 목록은 ItemTableData.h로 옮겼다(클라이언트가 같은 표를 읽도록).
	// 서버 쪽 조회 지점은 여전히 이 파일의 ItemTable::Find() 하나뿐이다.
	//
	// attackBonus는 레벨 기본 공격력(LevelTable::BaseAttack)에 더해진다 — 등급별 드롭
	// 확률(DropTable.cpp)이 크게 벌어지는 것과 같은 취지로, 공격력 보너스도
	// 등급이 오를수록 큰 폭으로 뛰게 잡았다.
	using ItemTableData::kRows;

	// id == 0(예약값) 또는 중복 id가 섞여 들어가면 Find()가 엉뚱한 행을
	// 반환하거나 두 아이템이 같은 id를 공유하는 채로 굳어버릴 수 있다.
	// 그런 실수를 런타임이 아니라 컴파일 타임에 막는다.
	constexpr bool IsItemTableValid(const ItemTableRow* rows, size_t count) noexcept
	{
		for (size_t i = 0; i < count; ++i)
		{
			if (rows[i].id == ITEM_TABLE_ID_NONE)
				return false;
			// 표시 이름은 클라이언트가 그대로 그리므로 비어 있으면 안 된다.
			if (rows[i].displayName == nullptr || rows[i].displayName[0] == '\0')
				return false;
			// 장비는 스택 불가, 그로시는 슬롯에 들어가지 않으므로 효과를 갖지 않는다.
			if (rows[i].type == ItemType::eEquipment && rows[i].maxStack != 1)
				return false;
			// 매입가가 판매가보다 높으면 "사서 되팔기"로 그로시를 무한 복사할 수 있다.
			if (rows[i].buyPrice != 0 && rows[i].sellPrice > rows[i].buyPrice)
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
		"ItemTable: id는 0일 수 없고 서로 겹치지 않아야 하며, 매입가는 판매가를 넘을 수 없습니다.");
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
