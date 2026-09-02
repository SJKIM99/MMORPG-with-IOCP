#pragma once

#include "Subject.h"
#include "ItemTableRow.h"

// 인벤토리 슬롯에 담기는 아이템 인스턴스. 필드(바닥)에 떨어진 아이템도 같은
// Item 인스턴스가 그대로 옮겨가서 표현한다 — 별도의 "필드 아이템" 클래스는
// 만들지 않는다(ItemHelper.cpp 참고).
//
// Subject를 상속해 이름/ObjID/위치 등 기존 필드를 재사용한다. 기본적으로는
// GameObjectManager에 등록하지 않는다 — Inventory가 shared_ptr로 직접 들고
// 있는 것 말고는 어디서도 전역으로 조회되지 않는다. (장착/탈착/교체/소비처럼
// 인벤토리 안에서만 일어나는 동작은 최대 4만 유저 x 30슬롯 규모라 전역 맵
// insert/erase 락을 타면 비용이 크다.)
//
// 예외: GetOwnerID()가 비어있으면(ObjID::npos) 그 아이템은 지금 어느
// Inventory에도 속하지 않은 "필드에 떨어진 상태"다. 이 경우에 한해
// ItemHelper::SpawnFieldItem이 GameObjectManager에 등록하고 Sector에 배치한다
// — 필드 아이템은 동시에 존재하는 개수가 훨씬 적고(전체 인벤토리 아이템 수와
// 무관), 다른 플레이어에게 "보여야" 하므로 전역 조회가 꼭 필요하다.
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

	// GetOwnerID()가 비어있는지(=필드에 떨어진 상태인지)를 매번 ObjID::npos와
	// 직접 비교하지 않도록 이름 붙인 것뿐 — 별도 필드를 두지 않는다.
	[[nodiscard]] bool IsOnGround() const noexcept { return GetOwnerID() == ObjID::npos; }

	// 몬스터 처치 드롭 전용: 일정 시간 동안 처치자만 주울 수 있게 하는 "루팅
	// 우선권" 보유자. GetOwnerID()(=인벤토리 소속 여부)와는 완전히 별개 개념이라
	// 섞어 쓰지 않는다 — 필드에 떨어진 상태(OwnerID 비어있음)에서도 루팅 우선권은
	// 따로 걸려 있을 수 있다. 비어있으면(ObjID::npos) 지금 누구나 주울 수 있는
	// 상태 — 플레이어가 직접 버린 아이템은 처음부터 이 값이 비어있고, 몬스터
	// 드롭만 ItemHelper::SpawnFieldItem이 잠시 채워뒀다가 타이머로 풀어준다.
	[[nodiscard]] const ObjID& GetLootPriorityOwner() const noexcept { return m_lootPriorityOwner; }
	void SetLootPriorityOwner(const ObjID& owner) noexcept { m_lootPriorityOwner = owner; }

private:
	ItemTableId m_itemTableId = ITEM_TABLE_ID_NONE;
	uint16_t    m_count       = 0;
	ObjID       m_lootPriorityOwner = ObjID::npos;
};

// itemId가 ItemTable에 없으면 nullptr. ItemTableRow::type을 보고 EquipmentItem/
// ConsumableItem 중 알맞은 파생 클래스를 만든다 — Subject.cpp의 MakeNewSubject()와
// 같은 패턴(카테고리를 보고 구체 타입을 결정하는 팩토리).
[[nodiscard]] Item::SharedPtr MakeNewItem(ItemTableId itemId, uint16_t count);
