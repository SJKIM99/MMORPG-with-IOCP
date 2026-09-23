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
	// 아데나. 필드에 떨어진 상태로만 존재하고, 주우면 인벤토리 슬롯이 아니라
	// 지갑(Inventory::GetAdena)으로 들어간다 — 슬롯을 차지하지 않는다.
	eCurrency   = 4,
};

// 소비 아이템을 "사용"했을 때 일어나는 일. ItemType과 같은 이유로 이미 배정된 값은
// 절대 바꾸지 않는다. 회복 물약은 기존 경로(healAmount)를 그대로 쓰고, 나머지
// 효과는 인벤토리 밖의 시스템(텔레포트/버프/강화)이 필요해서 UserHelper가 처리한다.
enum class ItemEffect : uint8_t
{
	eNone           = 0,
	eHeal           = 1,  // healAmount만큼 HP 회복
	eReturnTown     = 2,  // 마을 귀환 주문서
	eTeleportRandom = 3,  // 순간이동 주문서 — 현재 지역 안의 무작위 위치
	eHaste          = 4,  // 초록 물약 — effectValue초 동안 이동 가속
	eEnchantWeapon  = 5,  // 무기 강화 주문서 — ITEM_ENCHANT_REQ로만 사용(effectValue: 0 일반 / 1 축복)
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

	// ── 아래는 리니지풍 컨텐츠 확장으로 추가된 필드 ─────────────────────────────
	// 기존 행 초기화 구문({ id, type, ..., healAmount })이 그대로 컴파일되도록 전부
	// 끝에 붙이고 기본값을 둔다.
	ItemEffect     effect               = ItemEffect::eNone;
	uint16_t       effectValue          = 0;   // 효과별 수치(가속 지속 초, 축복 여부 등)
	uint32_t       buyPrice             = 0;   // 상점 판매가(플레이어가 살 때). 0 = 상점에서 팔지 않음
	uint32_t       sellPrice            = 0;   // 상점 매입가(플레이어가 팔 때). 0 = 팔 수 없음
	// 클라이언트 표시 전용 한글 이름(UTF-8). 서버 로직/패킷은 name(ASCII)만 쓴다 —
	// name[ITEM_NAME_SIZE]에 한글을 넣으면 20바이트 제한에 걸리는 이름이 많아서다.
	const char*    displayName          = "";
	uint8_t        iconId               = 0;   // 클라이언트 아이콘 모양 번호
};
