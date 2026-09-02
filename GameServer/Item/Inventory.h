#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "Item.h"

class User;

// 유저 한 명의 인벤토리.
//
// 항상 소유자(User)를 담당하는 Zone 스레드 위에서만 mutate된다는 전제로
// 락/atomic 없이 설계했다. WorkerThread.cpp가 패킷을 처리하기 전에 이미
// GZoneManager->EnqueueBySession()으로 소유 Zone 스레드에 진입시키므로,
// 이 전제는 정상적인 호출 경로에서는 항상 성립한다.
// 그 전제가 깨지는 경우(다른 스레드에서 실수로 직접 호출)를 대비해,
// mutating 메서드들은 시작하자마자 AssertOwnedByCurrentZone()으로 위반을
// 즉시 크래시로 드러낸다 — 소유자 생존 여부는 Release에서도 항상 확인하고,
// Zone 스레드 일치 여부는 Debug 빌드에서만 확인한다(Release에서는 제거됨).
//
// User -> Inventory는 shared_ptr(강한 소유), Inventory -> User는 weak_ptr(약한 참조)다.
// 서로를 강하게 붙잡으면 순환 참조로 둘 다 해제되지 않으므로, 소유 방향만 강하게 둔다.
//
// 슬롯에는 ItemSlot 같은 POD가 아니라 실제 Item(EquipmentItem/ConsumableItem)
// 인스턴스를 담는다 — 장착 여부 같은 아이템별 상태를 Item 쪽에 캡슐화하기 위함.
//
// 모든 mutating 메서드는 성공하면 영향받은 슬롯을 그 자리에서 즉시(디바운스
// 없이) DB에 반영한다 — SaveSlotToDB() 참고.
class Inventory
{
public:
	using SharedPtr = std::shared_ptr<Inventory>;
	using WeakPtr   = std::weak_ptr<Inventory>;

public:
	// User::InitInstance()에서 1회 호출해 소유자를 등록한다.
	void Attach(const std::shared_ptr<User>& owner) noexcept;

	// 로그인 시 DB에서 읽어온 행을 그대로 슬롯에 복원한다. 이미 DB가 원본인
	// 데이터를 되돌려놓는 것뿐이므로, 다른 mutating 메서드와 달리 Zone 스레드
	// 검증도 DB 재저장(SaveSlotToDB)도 하지 않는다 — 그리고 호출 시점의 Inventory는
	// 아직 GameObjectManager에 공개되기 전(User::InitInstance() 직후)이라 다른
	// 스레드가 동시에 접근할 수 없다는 게 이 메서드를 쓸 수 있는 전제 조건이다.
	void LoadFromDB(const std::vector<DB_ITEM_INFO>& items) noexcept;

	// itemId가 ItemTable에 없거나, count가 0이거나, 자리가 부족하면 아무것도
	// 바꾸지 않고 false를 반환한다 — 부분 반영 없이 전부 성공하거나 전부 실패한다.
	[[nodiscard]] bool TryAddItem(ItemTableId itemId, uint16_t count) noexcept;

	// slotIndex에 아이템이 없거나, count보다 적게 들어있으면 false.
	[[nodiscard]] bool TryRemoveItem(uint16_t slotIndex, uint16_t count) noexcept;

	// slotIndex가 비어있거나, 장비 아이템이 아니거나, 이미 장착 중이면 false.
	[[nodiscard]] bool TryEquip(uint16_t slotIndex) noexcept;

	// slotIndex가 비어있거나, 장비 아이템이 아니거나, 장착 중이 아니면 false.
	[[nodiscard]] bool TryUnequip(uint16_t slotIndex) noexcept;

	// 두 슬롯의 내용을 맞바꾼다(한쪽이 비어있으면 이동). 둘 다 비어있거나
	// 같은 인덱스면 false.
	[[nodiscard]] bool TrySwapSlots(uint16_t slotIndexA, uint16_t slotIndexB) noexcept;

	// 슬롯 인덱스 -> Item. 빈 슬롯은 키 자체가 존재하지 않는다(부재 = 빈 슬롯).
	[[nodiscard]] const std::unordered_map<uint16_t, Item::SharedPtr>& GetSlots() const noexcept { return _slots; }

private:
	// 소유자가 아직 살아있는지(Release 포함 항상 확인) + 지금 이 스레드가 그 소유자의
	// Zone 스레드가 맞는지(Debug 한정)를 한 곳에서 검사한다. mutating 메서드마다
	// 이 두 조건을 따로 챙기다 실수로 하나를 빠뜨리는 일이 없도록 한 곳에 모았다.
	void AssertOwnedByCurrentZone() const noexcept;

	// slotIndex의 현재 상태(있으면 내용, 없으면 빈 슬롯)를 그대로 DB에 반영한다.
	// "무엇이 바뀌었는지"를 따로 추적하지 않고 최종 상태를 그대로 저장/삭제하므로,
	// 호출 시점에 그 슬롯이 어떤 이유로 바뀌었는지는 몰라도 항상 정확하다.
	void SaveSlotToDB(uint16_t slotIndex) const;

private:
	std::weak_ptr<User> _owner;
	std::unordered_map<uint16_t, Item::SharedPtr> _slots;
};
