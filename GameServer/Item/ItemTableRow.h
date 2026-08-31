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
};
