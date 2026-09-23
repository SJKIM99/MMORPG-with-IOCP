#pragma once

#include "ItemTableRow.h"

// 몬스터 처치 시 적용되는 드롭 테이블들. 몬스터 종족(Data/MonsterTable.h)의
// dropTable 번호로 어느 표를 굴릴지 정한다 — 약한 몬스터는 물약/주문서 위주,
// 강한 몬스터와 보스는 고등급 무기·강화 주문서가 나온다.
//
// 한 번의 굴림 결과는 항상 "아무것도 없음" 또는 "아이템 하나"뿐이다. 여러 개를
// 떨어뜨리는 보스는 굴림 횟수(MonsterTableRow::dropRolls)로 표현한다.
// 아데나는 이 표가 아니라 종족의 adenaMin/adenaMax로 따로 굴린다.
namespace DropTable
{
	struct RollResult
	{
		bool        hasItem = false;
		ItemTableId itemId  = ITEM_TABLE_ID_NONE;
		uint16_t    count   = 0;
	};

	// 1 = 1단계 몬스터 ... 5 = 5단계, 6 = 보스 공용, 7 = 데스나이트, 8 = 화염 드레이크, 9 = 바포메트
	inline constexpr uint8_t kTableCount = 9;

	// 스레드 안전: 스레드마다 독립된 상태(LRng, 내부 캐시된 분포)만 사용하므로
	// 여러 Zone 스레드에서 동시에 호출해도 안전하다. 없는 표 번호면 1번 표를 쓴다.
	[[nodiscard]] RollResult Roll(uint8_t tableId) noexcept;
}
