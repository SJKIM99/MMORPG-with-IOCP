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
	// outTouchedSlots가 non-null이면 성공 시 실제로 채워지거나 수량이 늘어난
	// 슬롯 인덱스들을 담아준다(스택이 여러 슬롯에 걸쳐 나뉘어 채워지면 2개 이상일
	// 수 있다) — 호출자가 어떤 슬롯이 바뀌었는지 알아야(예: 클라이언트에 알림)
	// 할 때 GetSlots() 전체를 다시 비교하지 않도록 하기 위함.
	[[nodiscard]] bool TryAddItem(ItemTableId itemId, uint16_t count, std::vector<uint16_t>* outTouchedSlots = nullptr) noexcept;

	// slotIndex에 아이템이 없거나, count보다 적게 들어있으면 false.
	[[nodiscard]] bool TryRemoveItem(uint16_t slotIndex, uint16_t count) noexcept;

	// slotIndex에서 count만큼 꺼내 별도 Item으로 분리한다(필드에 버리는 용도).
	// 실패(슬롯 없음/count 부족/count=0)하면 nullptr — 인벤토리는 전혀 바뀌지
	// 않는다. 반환된 Item은 이제 어떤 Inventory에도 속하지 않는다(OwnerID가
	// 비어 있음, Item::IsOnGround() 참고) — 호출자가 필드에 놓을지 결정한다.
	[[nodiscard]] Item::SharedPtr TryExtractItem(uint16_t slotIndex, uint16_t count) noexcept;

	// slotIndex가 비어있거나, 장비 아이템이 아니거나, 이미 장착 중이면 false.
	[[nodiscard]] bool TryEquip(uint16_t slotIndex) noexcept;

	// slotIndex가 비어있거나, 장비 아이템이 아니거나, 장착 중이 아니면 false.
	[[nodiscard]] bool TryUnequip(uint16_t slotIndex) noexcept;

	// 두 슬롯의 내용을 맞바꾼다(한쪽이 비어있으면 이동). 둘 다 비어있거나
	// 같은 인덱스면 false.
	[[nodiscard]] bool TrySwapSlots(uint16_t slotIndexA, uint16_t slotIndexB) noexcept;

	// 슬롯 인덱스 -> Item. 빈 슬롯은 키 자체가 존재하지 않는다(부재 = 빈 슬롯).
	[[nodiscard]] const std::unordered_map<uint16_t, Item::SharedPtr>& GetSlots() const noexcept { return _slots; }

	// 지금 장착 중인 장비의 ItemTableId(없으면 ITEM_TABLE_ID_NONE). 여러 개를
	// 동시에 장착하는 규칙은 아직 없지만, 만약 그런 상태가 생기더라도 이 함수는
	// 그중 하나만(순회 중 처음 발견한 것) 돌려준다 — 다른 플레이어에게 "지금 이
	// 무기를 들고 있다"고 보여주는 화면 표시용이므로 여러 개를 동시에 표시할
	// 필요가 없다.
	[[nodiscard]] ItemTableId GetEquippedItemId() const noexcept;

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
