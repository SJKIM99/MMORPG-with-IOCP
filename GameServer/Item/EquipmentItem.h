#pragma once

#include "Item.h"

// 장착 가능한 아이템. Item 대비 추가 상태는 "지금 장착 중인가" 플래그와 강화 수치다.
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

	// 무기 강화 수치(+0 ~ EnchantRule::kMaxLevel). 인스턴스별 상태라 테이블이 아니라
	// 아이템 자신이 들고 다닌다 — 바닥에 버렸다가 다시 주워도 유지된다.
	[[nodiscard]] uint8_t GetEnchant() const noexcept { return m_enchant; }
	void SetEnchant(uint8_t enchant) noexcept { m_enchant = enchant; }

private:
	bool    m_equipped = false;
	uint8_t m_enchant  = 0;
};
