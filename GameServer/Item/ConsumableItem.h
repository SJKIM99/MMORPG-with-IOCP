#pragma once

#include "Item.h"

// 소비 아이템 — 현재는 Item 대비 추가 필드/동작이 없다. equipped 같은 장비
// 전용 상태를 갖지 않는다는 것 자체가 EquipmentItem과 타입으로 구분되는 지점.
// 실제 소비 효과(회복량 등)는 나중에 필요해지면 이 클래스에 추가한다.
class ConsumableItem : public Item
{
public:
	using SharedPtr = shared_ptr<ConsumableItem>;
	using WeakPtr   = weak_ptr<ConsumableItem>;

	ConsumableItem() = default;
};
