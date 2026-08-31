#pragma once

#include <memory>
#include <unordered_map>

#include "ItemSlot.h"

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
class Inventory
{
public:
	using SharedPtr = std::shared_ptr<Inventory>;
	using WeakPtr   = std::weak_ptr<Inventory>;

public:
	// User::InitInstance()에서 1회 호출해 소유자를 등록한다.
	void Attach(const std::shared_ptr<User>& owner) noexcept;

	// itemId가 ItemTable에 없거나, count가 0이거나, 자리가 부족하면 아무것도
	// 바꾸지 않고 false를 반환한다 — 부분 반영 없이 전부 성공하거나 전부 실패한다.
	[[nodiscard]] bool TryAddItem(ItemTableId itemId, uint16_t count) noexcept;

	// slotIndex에 아이템이 없거나, count보다 적게 들어있으면 false.
	[[nodiscard]] bool TryRemoveItem(uint16_t slotIndex, uint16_t count) noexcept;

	// 슬롯 인덱스 -> 슬롯 내용. 빈 슬롯은 키 자체가 존재하지 않는다(부재 = 빈 슬롯).
	[[nodiscard]] const std::unordered_map<uint16_t, ItemSlot>& GetSlots() const noexcept { return _slots; }

private:
	// 소유자가 아직 살아있는지(Release 포함 항상 확인) + 지금 이 스레드가 그 소유자의
	// Zone 스레드가 맞는지(Debug 한정)를 한 곳에서 검사한다. mutating 메서드마다
	// 이 두 조건을 따로 챙기다 실수로 하나를 빠뜨리는 일이 없도록 한 곳에 모았다.
	void AssertOwnedByCurrentZone() const noexcept;

private:
	std::weak_ptr<User> _owner;
	std::unordered_map<uint16_t, ItemSlot> _slots;
};
