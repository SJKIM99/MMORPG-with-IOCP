#include "pch.h"
#include "Inventory.h"

#include <algorithm>
#include <vector>

#include "EquipmentItem.h"
#include "ItemTable.h"
#include "Thread/DBThread.h"
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

void Inventory::SaveSlotToDB(uint16_t slotIndex) const
{
	auto owner = _owner.lock();
	ASSERT_CRASH(owner != nullptr);

	const auto it = _slots.find(slotIndex);
	if (it == _slots.end())
	{
		GDBThread->RequestDeleteItem(owner->GetPlayerId(), slotIndex);
		return;
	}

	const Item::SharedPtr& item = it->second;
	const auto equipment = std::dynamic_pointer_cast<EquipmentItem>(item);
	const bool equipped = (equipment != nullptr) && equipment->IsEquipped();

	GDBThread->RequestSaveItem(owner->GetPlayerId(), slotIndex, item->GetItemTableId(), item->GetCount(), equipped);
}

void Inventory::LoadFromDB(const std::vector<DB_ITEM_INFO>& items) noexcept
{
	for (const auto& info : items)
	{
		Item::SharedPtr item = MakeNewItem(info._itemId, info._count);
		if (item == nullptr)
			continue;  // ItemTable에서 사라진 id — 게임 데이터가 DB보다 최신인 경우, 방어적으로 무시

		if (info._equipped)
		{
			if (auto equipment = std::dynamic_pointer_cast<EquipmentItem>(item))
				equipment->Equip();
		}

		_slots[info._slotIndex] = std::move(item);
	}
}

bool Inventory::TryAddItem(ItemTableId itemId, uint16_t count, std::vector<uint16_t>* outTouchedSlots) noexcept
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
		for (const auto& [slotIndex, item] : _slots)
		{
			if (remaining == 0)
				break;
			if (item->GetItemTableId() == itemId && item->GetCount() < row->maxStack)
				remaining -= std::min<uint16_t>(row->maxStack - item->GetCount(), remaining);
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
	// 충분함을 확인했기에 여기서는 실패할 수 없다. 건드린 슬롯은 모아뒀다가
	// 전부 반영이 끝난 뒤 한 번에 DB로 즉시 저장한다.
	remaining = count;
	std::vector<uint16_t> touchedSlots;
	if (row->maxStack > 1)
	{
		for (auto& [slotIndex, item] : _slots)
		{
			if (remaining == 0)
				break;
			if (item->GetItemTableId() == itemId && item->GetCount() < row->maxStack)
			{
				const uint16_t added = std::min<uint16_t>(row->maxStack - item->GetCount(), remaining);
				item->AddCount(added);
				remaining -= added;
				touchedSlots.push_back(slotIndex);
			}
		}
	}
	for (uint16_t i = 0; i < MAX_INVENTORY_SLOTS && remaining > 0; ++i)
	{
		if (_slots.find(i) != _slots.end())
			continue;

		const uint16_t added = std::min<uint16_t>(row->maxStack, remaining);
		Item::SharedPtr newItem = MakeNewItem(itemId, added);
		// ItemTable::Find(itemId)가 이미 위에서 성공했으므로 MakeNewItem은 실패할
		// 수 없다 — nullptr이 나온다면 그 자체가 불변 조건 위반이라 여기서 멈춘다.
		// (여기서 조용히 false를 반환하면 앞 슬롯들은 이미 채워진 채로 남아
		// "일부만 반영된 실패"가 되어버리므로, 절대 그렇게 하지 않는다.)
		ASSERT_CRASH(newItem != nullptr);
		_slots.emplace(i, std::move(newItem));
		touchedSlots.push_back(i);
		remaining -= added;
	}

	for (uint16_t slotIndex : touchedSlots)
		SaveSlotToDB(slotIndex);

	if (outTouchedSlots != nullptr)
		*outTouchedSlots = std::move(touchedSlots);

	return true;
}

bool Inventory::TryRemoveItem(uint16_t slotIndex, uint16_t count) noexcept
{
	AssertOwnedByCurrentZone();

	if (count == 0)
		return false;

	const auto it = _slots.find(slotIndex);
	if (it == _slots.end() || it->second->GetCount() < count)
		return false;

	it->second->RemoveCount(count);
	if (it->second->GetCount() == 0)
		_slots.erase(it);

	SaveSlotToDB(slotIndex);
	return true;
}

bool Inventory::TryEquip(uint16_t slotIndex) noexcept
{
	AssertOwnedByCurrentZone();

	const auto it = _slots.find(slotIndex);
	if (it == _slots.end())
		return false;

	auto equipment = std::dynamic_pointer_cast<EquipmentItem>(it->second);
	if (equipment == nullptr || equipment->IsEquipped())
		return false;

	equipment->Equip();
	SaveSlotToDB(slotIndex);
	return true;
}

bool Inventory::TryUnequip(uint16_t slotIndex) noexcept
{
	AssertOwnedByCurrentZone();

	const auto it = _slots.find(slotIndex);
	if (it == _slots.end())
		return false;

	auto equipment = std::dynamic_pointer_cast<EquipmentItem>(it->second);
	if (equipment == nullptr || !equipment->IsEquipped())
		return false;

	equipment->Unequip();
	SaveSlotToDB(slotIndex);
	return true;
}

bool Inventory::TrySwapSlots(uint16_t slotIndexA, uint16_t slotIndexB) noexcept
{
	AssertOwnedByCurrentZone();

	if (slotIndexA == slotIndexB)
		return false;

	const auto itA = _slots.find(slotIndexA);
	const auto itB = _slots.find(slotIndexB);
	if (itA == _slots.end() && itB == _slots.end())
		return false;

	// 지운 뒤 다시 넣는 과정에서 이터레이터가 무효화될 수 있으므로, 먼저
	// 값을 복사해두고 그 다음에 맵을 수정한다.
	const Item::SharedPtr a = (itA != _slots.end()) ? itA->second : nullptr;
	const Item::SharedPtr b = (itB != _slots.end()) ? itB->second : nullptr;

	if (a != nullptr)
		_slots[slotIndexB] = a;
	else
		_slots.erase(slotIndexB);

	if (b != nullptr)
		_slots[slotIndexA] = b;
	else
		_slots.erase(slotIndexA);

	SaveSlotToDB(slotIndexA);
	SaveSlotToDB(slotIndexB);
	return true;
}
