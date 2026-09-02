#pragma once

#include "Subject.h"
#include "ItemTableRow.h"

// 인벤토리 슬롯에 담기는 아이템 인스턴스.
//
// Subject를 상속해 이름/ObjID 등 기존 필드를 재사용하지만, GameObjectManager에는
// 절대 등록하지 않는다 — Inventory가 shared_ptr로 직접 들고 있는 것 말고는
// 어디서도 전역으로 조회되지 않는다. (픽업/소비/장착/교체마다 최대 4만 유저 x
// 30슬롯 규모로 전역 맵 insert/erase 락을 타는 비용을 피하기 위한 의도적 선택.)
//
// InitInstance()는 Subject::InitInstance()를 그대로 쓰지 않고 GameObject의
// 것만 호출한다 — Subject::InitInstance()는 호출될 때마다 Stat을 새로 힙
// 할당하는데(Subject.cpp), 아이템에는 HP/레벨/경험치 개념이 없어 그 자체가
// 낭비다. 그래서 Item에는 유효한 Stat이 없다 — 앞으로도 Item에 GetStat()을
// 호출하는 코드가 생겨서는 안 되고, 만약 생긴다면 nullptr 역참조로 바로
// 드러나도록 의도적으로 방치했다.
class Item : public Subject
{
public:
	using SharedPtr = shared_ptr<Item>;
	using WeakPtr   = weak_ptr<Item>;

public:
	Item() = default;

	virtual void InitInstance() override;

	[[nodiscard]] ItemTableId GetItemTableId() const noexcept { return m_itemTableId; }
	void SetItemTableId(ItemTableId id) noexcept { m_itemTableId = id; }

	[[nodiscard]] uint16_t GetCount() const noexcept { return m_count; }
	void SetCount(uint16_t count) noexcept { m_count = count; }
	void AddCount(uint16_t amount) noexcept { m_count += amount; }
	// amount가 현재 수량보다 많으면 0으로 만든다 — 호출자(Inventory)가 이미
	// 수량을 검증한 뒤에만 부르는 걸 전제하지만, 언더플로 자체는 여기서 막는다.
	void RemoveCount(uint16_t amount) noexcept { m_count = (amount >= m_count) ? 0 : (m_count - amount); }

private:
	ItemTableId m_itemTableId = ITEM_TABLE_ID_NONE;
	uint16_t    m_count       = 0;
};

// itemId가 ItemTable에 없으면 nullptr. ItemTableRow::type을 보고 EquipmentItem/
// ConsumableItem 중 알맞은 파생 클래스를 만든다 — Subject.cpp의 MakeNewSubject()와
// 같은 패턴(카테고리를 보고 구체 타입을 결정하는 팩토리).
[[nodiscard]] Item::SharedPtr MakeNewItem(ItemTableId itemId, uint16_t count);
