#include "pch.h"
#include "Inventory.h"

#include <algorithm>

#include "ItemTable.h"
#include "User.h"
#include "Zone/ZoneManager.h"

void Inventory::Attach(const std::shared_ptr<User>& owner) noexcept
{
	_owner = owner;
}

void Inventory::AssertOwnedByCurrentZone() const noexcept
{
	auto owner = _owner.lock();
	// 소유자가 이미 사라졌다면(User 소멸 이후에도 이 Inventory를 들고 있었다는 뜻)
	// 그 자체로 호출자 쪽 수명 관리가 잘못된 것이므로, Release에서도 항상 크래시한다.
	ASSERT_CRASH(owner != nullptr);
	ASSERT_OWNED_BY_CURRENT_ZONE(owner->GetObjID());
}

bool Inventory::TryAddItem(ItemTableId itemId, uint16_t count) noexcept
{
	AssertOwnedByCurrentZone();

	if (count == 0)
		return false;

	const ItemTableRow* row = ItemTable::Find(itemId);
	if (row == nullptr)
		return false;

	// 1단계(드라이런): 맵을 하나도 건드리지 않고, count 전부를 수용할 수 있는지만
	// 확인한다. 여기서 부족하면 아무 것도 바꾸지 않고 바로 false —
	// "일부만 추가된 채로 실패" 상태가 생기지 않도록 반영 전에 반드시 먼저 확인한다.
	uint16_t remaining = count;
	if (row->maxStack > 1)
	{
		for (const auto& [slotIndex, slot] : _slots)
		{
			if (remaining == 0)
				break;
			if (slot.id == itemId && slot.count < row->maxStack)
				remaining -= std::min<uint16_t>(row->maxStack - slot.count, remaining);
		}
	}
	for (uint16_t i = 0; i < MAX_INVENTORY_SLOTS && remaining > 0; ++i)
	{
		if (_slots.find(i) == _slots.end())
			remaining -= std::min<uint16_t>(row->maxStack, remaining);
	}

	if (remaining > 0)
		return false;

	// 2단계(반영): 1단계와 완전히 같은 순회 순서를 사용하므로, 이미 자리가
	// 충분함을 확인했기에 여기서는 실패할 수 없다.
	remaining = count;
	if (row->maxStack > 1)
	{
		for (auto& [slotIndex, slot] : _slots)
		{
			if (remaining == 0)
				break;
			if (slot.id == itemId && slot.count < row->maxStack)
			{
				const uint16_t added = std::min<uint16_t>(row->maxStack - slot.count, remaining);
				slot.count += added;
				remaining -= added;
			}
		}
	}
	for (uint16_t i = 0; i < MAX_INVENTORY_SLOTS && remaining > 0; ++i)
	{
		if (_slots.find(i) != _slots.end())
			continue;

		const uint16_t added = std::min<uint16_t>(row->maxStack, remaining);
		_slots.emplace(i, ItemSlot{ itemId, added });
		remaining -= added;
	}

	return true;
}

bool Inventory::TryRemoveItem(uint16_t slotIndex, uint16_t count) noexcept
{
	AssertOwnedByCurrentZone();

	if (count == 0)
		return false;

	const auto it = _slots.find(slotIndex);
	if (it == _slots.end() || it->second.count < count)
		return false;

	it->second.count -= count;
	if (it->second.count == 0)
		_slots.erase(it);

	return true;
}
