#pragma once

#include "Item.h"

// 장착 가능한 아이템. Item 대비 유일한 추가 상태는 "지금 장착 중인가" 플래그다.
class EquipmentItem : public Item
{
public:
	using SharedPtr = shared_ptr<EquipmentItem>;
	using WeakPtr   = weak_ptr<EquipmentItem>;

public:
	EquipmentItem() = default;

	[[nodiscard]] bool IsEquipped() const noexcept { return m_equipped; }
	void Equip() noexcept   { m_equipped = true; }
	void Unequip() noexcept { m_equipped = false; }

private:
	bool m_equipped = false;
};
