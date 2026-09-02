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
	// Attach()가 InitInstance() 안에서 이미 호출된 뒤라 _owner는 항상 유효하다.
	// (이 시점엔 아직 Zone에 등록 전이라 AssertOwnedByCurrentZone은 쓸 수 없지만,
	// owner 자체의 생존은 별개 문제이므로 그냥 크래시로 불변조건을 지킨다.)
	const auto owner = _owner.lock();
	ASSERT_CRASH(owner != nullptr);

	for (const auto& info : items)
	{
		Item::SharedPtr item = MakeNewItem(info._itemId, info._count);
		if (item == nullptr)
			continue;  // ItemTable에서 사라진 id — 게임 데이터가 DB보다 최신인 경우, 방어적으로 무시

		item->SetOwnerID(owner->GetObjID());

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

	// AssertOwnedByCurrentZone()이 이미 owner != nullptr을 보장했으므로 여기서
	// 다시 null 체크할 필요는 없다 — 새로 만드는 Item에 OwnerID를 찍어주기 위해
	// 필요한 값만 미리 꺼내둔다.
	const auto owner = _owner.lock();

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
		newItem->SetOwnerID(owner->GetObjID());
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

Item::SharedPtr Inventory::TryExtractItem(uint16_t slotIndex, uint16_t count) noexcept
{
	AssertOwnedByCurrentZone();

	if (count == 0)
		return nullptr;

	const auto it = _slots.find(slotIndex);
	if (it == _slots.end() || it->second->GetCount() < count)
		return nullptr;

	Item::SharedPtr extracted;
	if (it->second->GetCount() == count)
	{
		// 슬롯 전량 추출 — 기존 Item 인스턴스를 그대로 꺼내 재사용한다(새로
		// 만들 필요 없음). 장착 중이었다면 여기서 탈착한다 — 인벤토리 밖으로
		// 나가는 순간 "장착 상태"는 의미가 없어진다.
		extracted = std::move(it->second);
		_slots.erase(it);

		if (auto equipment = std::dynamic_pointer_cast<EquipmentItem>(extracted))
			equipment->Unequip();
	}
	else
	{
		// 부분 추출 — 스택형 아이템에서만 일어난다(장비는 maxStack=1이라
		// count == 전체 수량인 위 분기로만 들어옴). 남은 수량은 슬롯에 그대로
		// 두고, 꺼내는 만큼만 새 Item으로 만든다.
		it->second->RemoveCount(count);
		extracted = MakeNewItem(it->second->GetItemTableId(), count);
		// 이미 존재하는(같은 슬롯에 들어있던) itemId를 그대로 쓰므로 ItemTable
		// 조회는 반드시 성공한다 — 실패한다면 그 자체가 불변조건 위반이다.
		ASSERT_CRASH(extracted != nullptr);
	}

	// 지금부터 이 Item은 어떤 Inventory에도 속하지 않는다 — 호출자(ItemHelper)가
	// 필드에 놓거나, 놓지 않고 버려서(참조 카운트가 0이 되어) 그대로 사라지게 한다.
	extracted->SetOwnerID(ObjID::npos);

	// 슬롯의 "남은" 상태를 그대로 반영한다 — 전량 추출이면 삭제, 부분 추출이면
	// 갱신. SaveSlotToDB는 이미 그 두 경우를 모두 올바르게 처리한다.
	SaveSlotToDB(slotIndex);

	return extracted;
}

ItemTableId Inventory::GetEquippedItemId() const noexcept
{
	for (const auto& [slotIndex, item] : _slots)
	{
		if (const auto equipment = std::dynamic_pointer_cast<EquipmentItem>(item); equipment != nullptr && equipment->IsEquipped())
			return item->GetItemTableId();
	}
	return ITEM_TABLE_ID_NONE;
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
