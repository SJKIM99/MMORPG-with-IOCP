#pragma once

#include "Protocol.h"

// 아이템의 취급 방식을 구분하는 최소 분류.
// 값이 늘어나는 건 괜찮지만, 이미 배정된 값은 절대 바꾸지 않는다 —
// 클라이언트도 같은 숫자로 분기하게 되므로, 값이 바뀌면 서버/클라이언트 데이터가
// 재빌드 없이 어긋난다.
enum ItemType : uint8_t
{
	eConsumable = 0,  // 사용 시 소모됨 (포션 등)
	eEquipment  = 1,  // 장착형. 기본적으로 스택 불가(maxStack = 1)
	eMaterial   = 2,  // 제작 재료 등, 사용 로직 없음
	eEtc        = 3,  // 분류 미정
};

// 장비 등급. eNone은 "등급이 없는 장비"(예: 시작 무기)와 "장비가 아니라서 등급
// 자체가 의미 없는 아이템"(소비/재료 등) 양쪽 모두를 가리킨다 — 후자의 경우
// grade 필드를 그냥 확인하지 않으면 되므로 별도 값을 두지 않는다.
// ItemType과 같은 이유로, 이미 배정된 값은 절대 바꾸지 않는다.
// ItemType과 달리 scoped enum(enum class)으로 둔다 — unscoped로 두면 eNone이
// EnumCategory::eNone과 같은 전역 스코프에서 충돌한다(둘 다 unscoped라면
// 재정의 오류). eRare/eEpic 등도 흔히 쓰일 이름이라 어차피 항상
// ItemGrade::eXxx로 명시하는 게 안전하다.
enum class ItemGrade : uint8_t
{
	eNone      = 0,
	eRare      = 1,
	eEpic      = 2,
	eUnique    = 3,
	eLegendary = 4,
};

// ContentID::KIND(uint16_t)에 그대로 실려서 아이템 인스턴스 ObjID, 인벤토리 슬롯,
// DB 저장 데이터를 오가는 영구 식별자. ItemTable(아이템 테이블) 안의 한 행(row)을 가리킨다.
// 0은 "빈 슬롯 / 없음"을 뜻하는 예약값이므로 실제 아이템에는 절대 배정하지 않는다.
using ItemTableId = uint16_t;
constexpr ItemTableId ITEM_TABLE_ID_NONE = 0;

// 서버가 들고 있는 아이템의 정적(불변) 정보 — 아이템 테이블의 행(row) 하나.
// 인스턴스별로 달라지는 값(수량, 슬롯 위치, 인스턴스 시리얼 등)은 여기 넣지 않는다 —
// 그건 나중에 만들 인벤토리 슬롯 쪽 데이터의 몫이다. 이 구조체는 오직
// ItemTable(ItemTable.cpp) 안의 상수 배열을 채우는 용도로만 쓴다.
struct ItemTableRow
{
	ItemTableId    id                   = ITEM_TABLE_ID_NONE;
	ItemType       type                 = ItemType::eEtc;
	uint16_t       maxStack             = 1;   // 1 = 스택 불가
	char           name[ITEM_NAME_SIZE] = {};
	ItemGrade      grade                = ItemGrade::eNone;  // 장비가 아니면 항상 eNone
	// 장착 중일 때 Stat::GetOffensive()에 더해지는 공격력 보너스. 장비가 아니면
	// 항상 0(의미 없음). 지금 등록된 장비가 전부 무기(검)라 공격력만 두고,
	// 방어구가 생기면 그때 defenseBonus 같은 필드를 같은 방식으로 추가하면 된다.
	uint16_t       attackBonus          = 0;
	// ConsumableItem 사용(ITEM_USE_REQ) 시 회복되는 HP. 소비 아이템이 아니면
	// 항상 0(의미 없음) — 장비 쪽 attackBonus와 같은 자리(무기가 아니면 0)를 따랐다.
	uint16_t       healAmount           = 0;
};
