#include "pch.h"
#include "Inventory.h"

#include <algorithm>
#include <vector>

#include "ConsumableItem.h"
#include "EquipmentItem.h"
#include "ItemTable.h"
#include "ItemTableData.h"
#include "Thread/DBThread.h"
#include "User.h"
#include "UserHelper.h"
#include "Zone/ZoneManager.h"
#include "Data/LevelTable.h"
#include "Data/ItemRules.h"

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
		NotifySlotChanged(slotIndex);
		return;
	}

	const Item::SharedPtr& item = it->second;
	const auto equipment = std::dynamic_pointer_cast<EquipmentItem>(item);
	const bool equipped = (equipment != nullptr) && equipment->IsEquipped();
	const uint8_t enchant = (equipment != nullptr) ? equipment->GetEnchant() : 0;
	const uint64_t extra = (equipment != nullptr) ? equipment->GetExtra() : 0;

	GDBThread->RequestSaveItem(owner->GetPlayerId(), slotIndex, item->GetItemTableId(), item->GetCount(), equipped, enchant, extra);
	NotifySlotChanged(slotIndex);
}

DB_ITEM_SLOT_SAVE Inventory::DescribeSlot(uint16_t slotIndex) const
{
	DB_ITEM_SLOT_SAVE save{};
	save.slotIndex = slotIndex;

	const auto it = _slots.find(slotIndex);
	if (it == _slots.end())
	{
		save.hasItem = false;
		return save;
	}

	const Item::SharedPtr& item = it->second;
	const auto equipment = std::dynamic_pointer_cast<EquipmentItem>(item);

	save.hasItem  = true;
	save.itemId   = item->GetItemTableId();
	save.count    = item->GetCount();
	save.equipped = (equipment != nullptr) && equipment->IsEquipped();
	save.enchant  = (equipment != nullptr) ? equipment->GetEnchant() : 0;
	save.extra    = (equipment != nullptr) ? equipment->GetExtra() : 0;
	return save;
}

void Inventory::SaveTwoSlotsToDB(uint16_t slotIndexA, uint16_t slotIndexB) const
{
	auto owner = _owner.lock();
	ASSERT_CRASH(owner != nullptr);

	GDBThread->RequestSaveTwoItems(owner->GetPlayerId(), DescribeSlot(slotIndexA), DescribeSlot(slotIndexB));
	NotifySlotChanged(slotIndexA);
	NotifySlotChanged(slotIndexB);
}

void Inventory::NotifySlotChanged(uint16_t slotIndex) const
{
	auto owner = _owner.lock();
	if (owner == nullptr || !owner->IsExtended())
		return;
	UserHelper::SendITEM_SLOT_EX_INF(owner, slotIndex);
	// V2: 희귀도·옵션은 바로 뒤에 따로 보낸다(슬롯 포맷은 레거시 크기 그대로 둔다).
	UserHelper::SendITEM_DETAIL_INF(owner, slotIndex);
}

uint64_t Inventory::GetSlotExtra(uint16_t slotIndex) const noexcept
{
	const auto it = _slots.find(slotIndex);
	if (it == _slots.end())
		return 0;
	const auto equipment = std::dynamic_pointer_cast<EquipmentItem>(it->second);
	return equipment != nullptr ? equipment->GetExtra() : 0;
}

uint16_t Inventory::FindEquipped(EquipSlot slot) const noexcept
{
	for (const auto& [slotIndex, item] : _slots)
	{
		const auto equipment = std::dynamic_pointer_cast<EquipmentItem>(item);
		if (equipment == nullptr || !equipment->IsEquipped())
			continue;
		if (ItemTableData::SlotOf(item->GetItemTableId()) == slot)
			return slotIndex;
	}
	return MAX_INVENTORY_SLOTS;
}

void Inventory::FillSlotEx(uint16_t slotIndex, ITEM_SLOT_EX_DATA& out) const noexcept
{
	out = {};
	out.slotIndex = slotIndex;
	const auto it = _slots.find(slotIndex);
	if (it == _slots.end())
		return;

	const Item::SharedPtr& item = it->second;
	const auto equipment = std::dynamic_pointer_cast<EquipmentItem>(item);
	out.itemId   = item->GetItemTableId();
	out.count    = item->GetCount();
	out.equipped = (equipment != nullptr && equipment->IsEquipped()) ? 1 : 0;
	out.enchant  = (equipment != nullptr) ? equipment->GetEnchant() : 0;
}

void Inventory::CollectGearBonus(GearBonus& out, GearExtras* outExtras) const
{
	// 착용 중인 장비 전부(무기 · 방어구 · 장신구) → 공용 규칙(Data/ItemRules.h)으로 합산한다.
	// 클라이언트의 비교 툴팁도 같은 함수를 부르므로 "보이는 숫자 = 서버 숫자"가 된다.
	EquippedPiece pieces[static_cast<size_t>(EquipSlot::Count)];
	int count = 0;
	for (const auto& [slotIndex, item] : _slots)
	{
		const auto equipment = std::dynamic_pointer_cast<EquipmentItem>(item);
		if (equipment == nullptr || !equipment->IsEquipped())
			continue;
		const ItemTableRow* row = ItemTable::Find(item->GetItemTableId());
		if (row == nullptr || count >= static_cast<int>(EquipSlot::Count))
			continue;
		pieces[count++] = EquippedPiece{ row, equipment->GetExtra(), equipment->GetEnchant() };
	}
	GearExtras extras{};
	ItemRules::Accumulate(pieces, count, out, extras);
	if (outExtras != nullptr)
		*outExtras = extras;
}

void Inventory::RecomputeOffensive() const
{
	auto owner = _owner.lock();
	ASSERT_CRASH(owner != nullptr);

	// 확장 세션(V2)은 직업·능력치·장비를 모두 반영한 계산으로 넘긴다.
	if (owner->IsExtended())
	{
		owner->RecomputeStats(false);
		return;
	}

	uint16_t attackBonus = 0;
	if (const ItemTableId equippedId = GetEquippedItemId(); equippedId != ITEM_TABLE_ID_NONE)
	{
		if (const ItemTableRow* row = ItemTable::Find(equippedId); row != nullptr)
			attackBonus = row->attackBonus;
		attackBonus = static_cast<uint16_t>(attackBonus + EnchantRule::AttackBonus(GetEquippedEnchant()));
	}

	// 기본 공격력은 레벨에 따라 오른다(1레벨 = 기존 PLAYER_OFFENSIVE 10과 같다).
	owner->GetStat()->SetOffensive(static_cast<uint16_t>(LevelTable::BaseAttack(owner->GetStat()->GetLevel()) + attackBonus));
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
		// 범위 밖 슬롯이 DB에 남아있는 경우(과거 버전이 저장해둔 잘못된 행 등)도
		// 방어적으로 무시한다 — 메모리에 올려봐야 클라이언트에 보이지도 않고
		// 꺼낼 수도 없는 슬롯이라, 들고 있어봐야 혼란만 만든다.
		if (!IsValidSlotIndex(info._slotIndex))
			continue;

		Item::SharedPtr item = MakeNewItem(info._itemId, info._count);
		if (item == nullptr)
			continue;  // ItemTable에서 사라진 id — 게임 데이터가 DB보다 최신인 경우, 방어적으로 무시

		item->SetOwnerID(owner->GetObjID());

		if (auto equipment = std::dynamic_pointer_cast<EquipmentItem>(item))
		{
			if (info._equipped)
				equipment->Equip();
			// 테이블 상한보다 큰 값이 DB에 있어도(수동 수정 등) 방어적으로 잘라낸다.
			equipment->SetEnchant(std::min<uint8_t>(info._enchant, EnchantRule::kMaxLevel));
			equipment->SetExtra(info._extra);
		}

		_slots[info._slotIndex] = std::move(item);
	}

	// 로그인 시 이미 장비를 장착한 채로 복원됐을 수 있으므로, User::InitInstance()가
	// 세팅한 기본 공격력을 여기서 곧바로 실제 상태로 덮어쓴다.
	RecomputeOffensive();
}

bool Inventory::TryAddItem(ItemTableId itemId, uint16_t count, std::vector<uint16_t>* outTouchedSlots, uint8_t enchant, uint64_t extra) noexcept
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
	// 아데나는 슬롯이 아니라 지갑으로 간다(AddAdena) — 여기로 오면 호출자 실수다.
	if (row->type == ItemType::eCurrency)
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
		if (auto equipment = std::dynamic_pointer_cast<EquipmentItem>(newItem))
		{
			equipment->SetEnchant(std::min<uint8_t>(enchant, EnchantRule::kMaxLevel));
			equipment->SetExtra(extra);
		}
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

	if (count == 0 || !IsValidSlotIndex(slotIndex))
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

	if (count == 0 || !IsValidSlotIndex(slotIndex))
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
		{
			const bool wasEquipped = equipment->IsEquipped();
			equipment->Unequip();
			// 장착 중이던 장비를 버린 경우에만 공격력을 다시 계산한다 — 아니면
			// 불필요한 재계산(결과는 항상 같지만)을 매번 할 필요가 없다.
			if (wasEquipped)
				RecomputeOffensive();
		}
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
	// "장착 무기" — V2부터 방어구·장신구도 장착되므로 무기 부위만 본다
	// (레거시 공격력 계산 · 외형 패킷이 이 값을 쓴다).
	const uint16_t slotIndex = FindEquipped(EquipSlot::Weapon);
	if (slotIndex >= MAX_INVENTORY_SLOTS)
		return ITEM_TABLE_ID_NONE;
	return _slots.at(slotIndex)->GetItemTableId();
}

uint8_t Inventory::GetEquippedEnchant() const noexcept
{
	const uint16_t slotIndex = FindEquipped(EquipSlot::Weapon);
	if (slotIndex >= MAX_INVENTORY_SLOTS)
		return 0;
	const auto equipment = std::dynamic_pointer_cast<EquipmentItem>(_slots.at(slotIndex));
	return equipment != nullptr ? equipment->GetEnchant() : 0;
}

bool Inventory::TryEquip(uint16_t slotIndex, uint16_t* outPreviousSlot) noexcept
{
	AssertOwnedByCurrentZone();

	if (!IsValidSlotIndex(slotIndex))
		return false;

	const auto it = _slots.find(slotIndex);
	if (it == _slots.end())
		return false;

	auto equipment = std::dynamic_pointer_cast<EquipmentItem>(it->second);
	if (equipment == nullptr || equipment->IsEquipped())
		return false;

	const ItemTableRow* row = ItemTable::Find(equipment->GetItemTableId());
	if (row == nullptr)
		return false;
	const EquipSlot part = ItemTableData::SlotOf(*row);

	// V2(확장 세션): 직업 전용 무기 · 착용 레벨. 레거시 세션은 예전처럼 제한 없이 입는다.
	if (const auto owner = _owner.lock(); owner != nullptr && owner->IsExtended())
	{
		if (!ItemTableData::ClassCanUse(*row, static_cast<uint8_t>(owner->m_classId)))
			return false;
		if (row->reqLevel > owner->GetStat()->GetLevel())
			return false;
	}

	// 이 시점 이후로는 실패할 수 없다(단순 플래그 플립 + 비동기 DB enqueue뿐) —
	// 그래서 TryAddItem처럼 별도 드라이런 단계 없이도 "확인 -> 반영"이 이미 원자적이다.
	// 같은 부위에 이미 입고 있던 장비만 벗는다(부위마다 하나 — 불변조건).
	uint16_t previousSlot = 0;
	bool hadPrevious = false;
	for (auto& [otherSlotIndex, otherItem] : _slots)
	{
		if (otherSlotIndex == slotIndex)
			continue;

		if (auto otherEquipment = std::dynamic_pointer_cast<EquipmentItem>(otherItem);
			otherEquipment != nullptr && otherEquipment->IsEquipped()
			&& ItemTableData::SlotOf(otherEquipment->GetItemTableId()) == part)
		{
			otherEquipment->Unequip();
			previousSlot = otherSlotIndex;
			hadPrevious = true;
			break;
		}
	}

	equipment->Equip();
	RecomputeOffensive();

	// 메모리 상태는 이미 위에서 원자적으로(한 번의 함수 호출 안에서, 다른 스레드가
	// 끼어들 수 없이) 바뀌었다. DB 반영도 이전 장비가 있었다면 SaveTwoSlotsToDB로
	// 한 트랜잭션에 묶어서, 서버가 두 저장 사이에 죽어도 "DB에 두 개 다 장착"
	// 같은 불변조건 위반 상태가 남지 않게 한다.
	if (hadPrevious)
		SaveTwoSlotsToDB(previousSlot, slotIndex);
	else
		SaveSlotToDB(slotIndex);

	if (hadPrevious && outPreviousSlot != nullptr)
		*outPreviousSlot = previousSlot;

	return true;
}

bool Inventory::TryUnequip(uint16_t slotIndex) noexcept
{
	AssertOwnedByCurrentZone();

	if (!IsValidSlotIndex(slotIndex))
		return false;

	const auto it = _slots.find(slotIndex);
	if (it == _slots.end())
		return false;

	auto equipment = std::dynamic_pointer_cast<EquipmentItem>(it->second);
	if (equipment == nullptr || !equipment->IsEquipped())
		return false;

	equipment->Unequip();
	RecomputeOffensive();
	SaveSlotToDB(slotIndex);
	return true;
}

bool Inventory::TryUseItem(uint16_t slotIndex) noexcept
{
	AssertOwnedByCurrentZone();

	if (!IsValidSlotIndex(slotIndex))
		return false;

	const auto it = _slots.find(slotIndex);
	if (it == _slots.end())
		return false;

	const auto consumable = std::dynamic_pointer_cast<ConsumableItem>(it->second);
	if (consumable == nullptr)
		return false;

	const ItemTableRow* row = ItemTable::Find(consumable->GetItemTableId());
	// 슬롯에 실제로 들어있는 아이템이므로 ItemTable에 없을 수 없다 — 없다면
	// 그 자체가 불변조건 위반이다.
	ASSERT_CRASH(row != nullptr);

	// 이 경로는 회복 물약 전용이다. 주문서/초록 물약처럼 효과가 인벤토리 밖에서
	// 일어나는 아이템은 TryConsumeOne + UserHelper 쪽 효과 처리로 간다 — 여기서
	// 받아버리면 효과 없이 소모만 되는 버그가 된다.
	if (row->effect != ItemEffect::eHeal || row->healAmount == 0)
		return false;

	const auto owner = _owner.lock();
	// 레벨업으로 최대 HP가 오르므로 상한은 고정 상수(PLAYER_MAX_HP)가 아니라 현재 최대 HP다.
	owner->GetStat()->HealHp(row->healAmount, owner->GetStat()->GetMaxHp());

	consumable->RemoveCount(1);
	if (consumable->GetCount() == 0)
		_slots.erase(it);

	SaveSlotToDB(slotIndex);
	return true;
}

bool Inventory::TrySwapSlots(uint16_t slotIndexA, uint16_t slotIndexB) noexcept
{
	AssertOwnedByCurrentZone();

	// 아래에서 _slots[...]로 없던 키를 만들 수 있으므로, 두 인덱스 모두 반드시
	// 먼저 검증한다 — 하나라도 범위 밖이면 아이템이 접근 불가능한 슬롯으로
	// 넘어가 영구히 사라진다(Inventory.h의 TrySwapSlots 주석 참고).
	if (!IsValidSlotIndex(slotIndexA) || !IsValidSlotIndex(slotIndexB))
		return false;

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

	// 두 슬롯을 한 트랜잭션으로 묶는다 — 따로 저장하면 그 사이에 서버가 죽었을
	// 때 DB에 아이템이 두 슬롯 모두에 있거나(중복) 어디에도 없는(유실) 상태가
	// 남을 수 있다.
	SaveTwoSlotsToDB(slotIndexA, slotIndexB);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 리니지풍 컨텐츠 확장
// ─────────────────────────────────────────────────────────────────────────────

uint32_t Inventory::AddAdena(uint32_t amount, AdenaReason reason) noexcept
{
	AssertOwnedByCurrentZone();

	// DB 컬럼이 INT(부호 있음)라 그 범위를 넘지 않게 자른다.
	constexpr uint32_t kMaxAdena = 2'000'000'000u;
	const uint32_t room  = (_adena < kMaxAdena) ? (kMaxAdena - _adena) : 0;
	const uint32_t added = (std::min)(amount, room);
	if (added == 0)
		return 0;

	_adena += added;

	auto owner = _owner.lock();
	GDBThread->RequestSaveAdena(owner->GetPlayerId(), _adena);
	if (owner->IsExtended())
		UserHelper::SendADENA_INF(owner, static_cast<int32_t>(added), reason);
	return added;
}

bool Inventory::TrySpendAdena(uint32_t amount, AdenaReason reason) noexcept
{
	AssertOwnedByCurrentZone();

	if (_adena < amount)
		return false;
	if (amount == 0)
		return true;

	_adena -= amount;

	auto owner = _owner.lock();
	GDBThread->RequestSaveAdena(owner->GetPlayerId(), _adena);
	if (owner->IsExtended())
		UserHelper::SendADENA_INF(owner, -static_cast<int32_t>(amount), reason);
	return true;
}

bool Inventory::TryConsumeOne(uint16_t slotIndex, ItemEffect expected, ItemTableId* outItemId) noexcept
{
	AssertOwnedByCurrentZone();

	if (!IsValidSlotIndex(slotIndex))
		return false;

	const auto it = _slots.find(slotIndex);
	if (it == _slots.end())
		return false;

	const auto consumable = std::dynamic_pointer_cast<ConsumableItem>(it->second);
	if (consumable == nullptr)
		return false;

	const ItemTableRow* row = ItemTable::Find(consumable->GetItemTableId());
	ASSERT_CRASH(row != nullptr);
	if (row->effect != expected)
		return false;

	if (outItemId != nullptr)
		*outItemId = row->id;

	consumable->RemoveCount(1);
	if (consumable->GetCount() == 0)
		_slots.erase(it);

	SaveSlotToDB(slotIndex);
	return true;
}

Inventory::EnchantOutcome Inventory::TryEnchant(uint16_t scrollSlot, uint16_t targetSlot, uint32_t roll100, uint32_t bonusRoll) noexcept
{
	AssertOwnedByCurrentZone();

	EnchantOutcome outcome{};
	if (!IsValidSlotIndex(scrollSlot) || !IsValidSlotIndex(targetSlot) || scrollSlot == targetSlot)
		return outcome;

	const auto scrollIt = _slots.find(scrollSlot);
	const auto targetIt = _slots.find(targetSlot);
	if (scrollIt == _slots.end() || targetIt == _slots.end())
		return outcome;

	const ItemTableRow* scrollRow = ItemTable::Find(scrollIt->second->GetItemTableId());
	ASSERT_CRASH(scrollRow != nullptr);
	if (scrollRow->effect != ItemEffect::eEnchantWeapon)
		return outcome;

	const auto weapon = std::dynamic_pointer_cast<EquipmentItem>(targetIt->second);
	if (weapon == nullptr || !ItemTableData::IsWeapon(weapon->GetItemTableId()))
		return outcome;   // 무기 강화 주문서는 무기에만(방어구·장신구는 대상이 아니다)

	const uint8_t before = weapon->GetEnchant();
	if (before >= EnchantRule::kMaxLevel)
		return outcome;  // 최대 강화 — 주문서를 소모하지 않는다

	// ── 여기부터는 실패할 수 없다: 판정 → 메모리 반영 → DB 한 트랜잭션 ──
	outcome.itemId      = weapon->GetItemTableId();
	outcome.before      = before;
	outcome.wasEquipped = weapon->IsEquipped();

	const bool blessed = scrollRow->effectValue != 0;
	const bool success = roll100 < EnchantRule::SuccessPercent(before);

	// 주문서 1장 소모
	scrollIt->second->RemoveCount(1);
	if (scrollIt->second->GetCount() == 0)
		_slots.erase(scrollIt);

	if (success)
	{
		// 축복받은 주문서: 안전 구간에서는 +1~3, 안전 구간을 넘는 단계는 +1만(리니지와 같음)
		uint8_t step = 1;
		if (blessed && before < EnchantRule::kSafeLevel)
			step = static_cast<uint8_t>(1 + bonusRoll % 3);
		uint8_t after = static_cast<uint8_t>(before + step);
		if (before < EnchantRule::kSafeLevel && after > EnchantRule::kSafeLevel)
			after = EnchantRule::kSafeLevel;  // 축복으로 안전 구간을 뛰어넘지는 않는다
		after = std::min<uint8_t>(after, EnchantRule::kMaxLevel);

		weapon->SetEnchant(after);
		outcome.result = EnchantResult::Success;
		outcome.after  = after;
	}
	else
	{
		// 실패 = 증발. 장착 중이었다면 인벤토리 밖으로 사라지는 순간 장착도 풀린다.
		weapon->Unequip();
		_slots.erase(targetIt);
		outcome.result = EnchantResult::Evaporated;
		outcome.after  = 0;
	}

	if (outcome.wasEquipped)
		RecomputeOffensive();

	// 주문서 슬롯과 무기 슬롯을 한 트랜잭션으로 저장한다.
	SaveTwoSlotsToDB(scrollSlot, targetSlot);
	return outcome;
}

uint16_t Inventory::FindSingleSlotFor(const ItemTableRow& row, uint16_t count) const noexcept
{
	if (count == 0 || count > row.maxStack)
		return MAX_INVENTORY_SLOTS;

	// 같은 아이템 스택에 통째로 들어갈 자리가 있으면 거기 합친다(장비는 maxStack=1이라 해당 없음).
	if (row.maxStack > 1)
	{
		for (const auto& [slotIndex, item] : _slots)
		{
			if (item->GetItemTableId() == row.id && item->GetCount() + count <= row.maxStack)
				return slotIndex;
		}
	}

	for (uint16_t i = 0; i < MAX_INVENTORY_SLOTS; ++i)
	{
		if (_slots.find(i) == _slots.end())
			return i;
	}
	return MAX_INVENTORY_SLOTS;
}

ShopResult Inventory::TryBuy(ItemTableId itemId, uint16_t count, uint32_t totalPrice, uint16_t* outSlot) noexcept
{
	AssertOwnedByCurrentZone();

	const ItemTableRow* row = ItemTable::Find(itemId);
	if (row == nullptr || row->type == ItemType::eCurrency)
		return ShopResult::InvalidItem;
	if (count == 0 || count > row->maxStack)
		return ShopResult::InvalidCount;
	if (_adena < totalPrice)
		return ShopResult::NotEnoughAdena;

	const uint16_t slotIndex = FindSingleSlotFor(*row, count);
	if (slotIndex >= MAX_INVENTORY_SLOTS)
		return ShopResult::InventoryFull;

	// ── 확인이 전부 끝났다. 여기부터 실패할 수 없다(메모리 반영 → DB 한 트랜잭션). ──
	const auto owner = _owner.lock();
	const auto it = _slots.find(slotIndex);
	if (it != _slots.end())
	{
		it->second->AddCount(count);
	}
	else
	{
		Item::SharedPtr newItem = MakeNewItem(itemId, count);
		ASSERT_CRASH(newItem != nullptr);
		newItem->SetOwnerID(owner->GetObjID());
		_slots.emplace(slotIndex, std::move(newItem));
	}
	_adena -= totalPrice;

	GDBThread->RequestShopTransaction(owner->GetPlayerId(), _adena, DescribeSlot(slotIndex));
	NotifySlotChanged(slotIndex);
	if (owner->IsExtended())
		UserHelper::SendADENA_INF(owner, -static_cast<int32_t>(totalPrice), AdenaReason::ShopBuy);

	if (outSlot != nullptr)
		*outSlot = slotIndex;
	return ShopResult::Success;
}

ShopResult Inventory::TrySell(uint16_t slotIndex, uint16_t count, uint32_t* outGain) noexcept
{
	AssertOwnedByCurrentZone();

	if (!IsValidSlotIndex(slotIndex))
		return ShopResult::InvalidItem;

	const auto it = _slots.find(slotIndex);
	if (it == _slots.end())
		return ShopResult::InvalidItem;

	const ItemTableRow* row = ItemTable::Find(it->second->GetItemTableId());
	ASSERT_CRASH(row != nullptr);
	if (row->sellPrice == 0)
		return ShopResult::InvalidItem;
	if (count == 0 || count > it->second->GetCount())
		return ShopResult::InvalidCount;

	if (const auto equipment = std::dynamic_pointer_cast<EquipmentItem>(it->second); equipment != nullptr && equipment->IsEquipped())
		return ShopResult::EquippedItem;

	// 강화된 무기(+1당 10%)와 희귀도 높은 장비는 더 받는다 — 표시는 클라이언트가
	// 같은 함수(ItemRules::SellPrice)로 계산한다.
	uint64_t unit = row->sellPrice;
	if (const auto equipment = std::dynamic_pointer_cast<EquipmentItem>(it->second))
		unit = ItemRules::SellPrice(*row, equipment->GetExtra(), equipment->GetEnchant());
	const uint64_t gain64 = unit * count;
	constexpr uint32_t kMaxAdena = 2'000'000'000u;
	if (gain64 > kMaxAdena - (std::min)(_adena, kMaxAdena))
		return ShopResult::InvalidCount;  // 지갑 상한 초과 — 거래를 거절한다(버리지 않는다)
	const uint32_t gain = static_cast<uint32_t>(gain64);

	// ── 여기부터 실패할 수 없다 ──
	it->second->RemoveCount(count);
	if (it->second->GetCount() == 0)
		_slots.erase(it);
	_adena += gain;

	const auto owner = _owner.lock();
	GDBThread->RequestShopTransaction(owner->GetPlayerId(), _adena, DescribeSlot(slotIndex));
	NotifySlotChanged(slotIndex);
	if (owner->IsExtended())
		UserHelper::SendADENA_INF(owner, static_cast<int32_t>(gain), AdenaReason::ShopSell);

	if (outGain != nullptr)
		*outGain = gain;
	return ShopResult::Success;
}

int Inventory::TrySort() noexcept
{
	AssertOwnedByCurrentZone();

	int changes = 0;

	// 1) 같은 물건 스택 합치기(앞 칸으로 모은다). 두 칸이 함께 바뀌므로 한 트랜잭션.
	std::vector<uint16_t> order;
	order.reserve(_slots.size());
	for (const auto& [slotIndex, item] : _slots)
		order.push_back(slotIndex);
	std::sort(order.begin(), order.end());
	for (size_t a = 0; a < order.size(); ++a)
	{
		auto ita = _slots.find(order[a]);
		if (ita == _slots.end())
			continue;
		const ItemTableRow* row = ItemTable::Find(ita->second->GetItemTableId());
		if (row == nullptr || row->maxStack <= 1)
			continue;
		for (size_t b = a + 1; b < order.size(); ++b)
		{
			ita = _slots.find(order[a]);
			if (ita == _slots.end() || ita->second->GetCount() >= row->maxStack)
				break;
			const auto itb = _slots.find(order[b]);
			if (itb == _slots.end() || itb->second->GetItemTableId() != row->id)
				continue;
			const uint16_t move = (std::min)(static_cast<uint16_t>(row->maxStack - ita->second->GetCount()), itb->second->GetCount());
			if (move == 0)
				continue;
			ita->second->AddCount(move);
			itb->second->RemoveCount(move);
			if (itb->second->GetCount() == 0)
				_slots.erase(itb);
			SaveTwoSlotsToDB(order[a], order[b]);
			++changes;
		}
	}

	// 2) 원하는 순서
	struct Entry { Item::SharedPtr item; int group, part, rarity, tier, effect, id, enchant, count; };
	std::vector<Entry> entries;
	entries.reserve(_slots.size());
	for (const auto& [slotIndex, item] : _slots)
	{
		const ItemTableRow* row = ItemTable::Find(item->GetItemTableId());
		if (row == nullptr)
			continue;
		const auto eq = std::dynamic_pointer_cast<EquipmentItem>(item);
		Entry e{};
		e.item = item;
		e.group = (eq != nullptr && eq->IsEquipped()) ? 0 : (row->type == ItemType::eEquipment ? 1 : (row->type == ItemType::eConsumable ? 2 : 3));
		e.part = static_cast<int>(ItemTableData::SlotOf(*row));
		e.rarity = eq != nullptr ? static_cast<int>(ItemRules::EffectiveRarity(*row, eq->GetExtra())) : 0;
		e.tier = row->tier;
		e.effect = static_cast<int>(row->effect);
		e.id = row->id;
		e.enchant = eq != nullptr ? eq->GetEnchant() : 0;
		e.count = item->GetCount();
		entries.push_back(std::move(e));
	}
	std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b)
	{
		if (a.group != b.group) return a.group < b.group;
		if (a.part != b.part) return a.part < b.part;
		if (a.rarity != b.rarity) return a.rarity > b.rarity;
		if (a.tier != b.tier) return a.tier > b.tier;
		if (a.effect != b.effect) return a.effect < b.effect;
		if (a.id != b.id) return a.id < b.id;
		if (a.enchant != b.enchant) return a.enchant > b.enchant;
		return a.count > b.count;
	});

	// 3) 앞 칸부터 제자리로(각 교환이 한 트랜잭션)
	for (size_t t = 0; t < entries.size() && t < MAX_INVENTORY_SLOTS; ++t)
	{
		const uint16_t target = static_cast<uint16_t>(t);
		const auto here = _slots.find(target);
		if (here != _slots.end() && here->second == entries[t].item)
			continue;
		uint16_t from = MAX_INVENTORY_SLOTS;
		for (const auto& [slotIndex, item] : _slots)
			if (item == entries[t].item) { from = slotIndex; break; }
		if (from >= MAX_INVENTORY_SLOTS)
			continue;
		if (TrySwapSlots(from, target))
			++changes;
	}
	return changes;
}

Item::SharedPtr Inventory::TryTakeRandomItem(uint32_t roll) noexcept
{
	AssertOwnedByCurrentZone();

	if (_slots.empty())
		return nullptr;

	// unordered_map 순회 순서는 의미가 없으므로 슬롯 번호로 정렬된 후보에서 고른다.
	std::vector<uint16_t> candidates;
	candidates.reserve(_slots.size());
	for (const auto& [slotIndex, item] : _slots)
		candidates.push_back(slotIndex);
	std::sort(candidates.begin(), candidates.end());

	const uint16_t slotIndex = candidates[roll % candidates.size()];
	const uint16_t count = _slots[slotIndex]->GetCount();
	// TryExtractItem이 장착 해제/공격력 재계산/DB 저장/알림을 전부 처리한다.
	return TryExtractItem(slotIndex, count);
}
