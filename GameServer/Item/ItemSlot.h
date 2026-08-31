#pragma once

#include "ItemTableRow.h"

// 인벤토리 한 칸의 런타임 상태. ItemTableRow(정적 데이터)와 달리 유저마다,
// 순간마다 값이 달라진다. 슬롯 위치는 배열 인덱스로 표현하므로 여기엔 담지 않는다.
struct ItemSlot
{
	ItemTableId id    = ITEM_TABLE_ID_NONE;
	uint16_t    count = 0;

	[[nodiscard]] bool IsEmpty() const noexcept { return id == ITEM_TABLE_ID_NONE; }
};
