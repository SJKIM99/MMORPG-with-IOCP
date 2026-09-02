#pragma once

#include "ItemTableRow.h"

// 몬스터 처치 시 적용되는 전역 드롭 테이블. 지금은 몬스터 종류(AGGRO/PASSIVE)
// 구분 없이 모든 처치에 같은 테이블을 적용한다 — 몬스터별로 다르게 하고 싶어지면
// Roll()에 몬스터 정보를 받는 식으로 확장하면 된다(지금은 그럴 근거가 없어 미리
// 만들지 않는다).
//
// 한 번의 처치에서 결과는 항상 "아무것도 없음" 또는 "아이템 하나"뿐이다.
// 여러 아이템이 동시에 나오는 규칙은 없다 — 필요해지면 Roll()을 확장한다.
namespace DropTable
{
	struct RollResult
	{
		bool        hasItem = false;
		ItemTableId itemId  = ITEM_TABLE_ID_NONE;
		uint16_t    count   = 0;
	};

	// 스레드 안전: 스레드마다 독립된 상태(LRng, 내부 캐시된 분포)만 사용하므로
	// 여러 Zone 스레드에서 동시에 호출해도 안전하다.
	[[nodiscard]] RollResult Roll() noexcept;
}
