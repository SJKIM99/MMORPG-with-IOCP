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
	// enchant/extra는 장비에만 의미가 있다(강화 수치 · V2 희귀도·옵션 — 바닥에서 다시 주울 때 유지).
	[[nodiscard]] bool TryAddItem(ItemTableId itemId, uint16_t count, std::vector<uint16_t>* outTouchedSlots = nullptr, uint8_t enchant = 0, uint64_t extra = 0) noexcept;

	// slotIndex에 아이템이 없거나, count보다 적게 들어있으면 false.
	[[nodiscard]] bool TryRemoveItem(uint16_t slotIndex, uint16_t count) noexcept;

	// slotIndex에서 count만큼 꺼내 별도 Item으로 분리한다(필드에 버리는 용도).
	// 실패(슬롯 없음/count 부족/count=0)하면 nullptr — 인벤토리는 전혀 바뀌지
	// 않는다. 반환된 Item은 이제 어떤 Inventory에도 속하지 않는다(OwnerID가
	// 비어 있음, Item::IsOnGround() 참고) — 호출자가 필드에 놓을지 결정한다.
	[[nodiscard]] Item::SharedPtr TryExtractItem(uint16_t slotIndex, uint16_t count) noexcept;

	// slotIndex가 비어있거나, 장비 아이템이 아니거나, 이미 장착 중이면 false.
	// V2: 장비는 부위(EquipSlot)마다 하나씩 장착된다 — 아래의 "이전 장비"는 같은 부위의 장비다.
	// 확장 세션은 직업 전용 · 착용 레벨도 검사한다(레거시 세션은 예전처럼 무기만 다룬다).
	// 이 인벤토리에 이미 장착 중이던 "다른" 장비가 있었다면, 그 장비를 탈착하는
	// 것과 새 장비를 장착하는 것이 하나의 원자적 동작으로 함께 일어난다(둘 다
	// 장착된 상태나 둘 다 미장착인 상태가 잠깐이라도 관측되지 않는다 — 메모리
	// 상태는 이 함수 안에서 한 번에 바뀌고, DB 반영도 SaveTwoSlotsToDB로 한
	// 트랜잭션에 묶는다). outPreviousSlot이 non-null이고 그런 이전 장비가
	// 있었다면, 그 슬롯 인덱스를 채워준다(호출자가 클라이언트에 별도로 알릴 수
	// 있도록) — 없었다면 건드리지 않으므로, 호출 전에 MAX_INVENTORY_SLOTS 같은
	// "슬롯일 수 없는 값"으로 초기화해두고 바뀌었는지 확인해야 한다.
	[[nodiscard]] bool TryEquip(uint16_t slotIndex, uint16_t* outPreviousSlot = nullptr) noexcept;

	// slotIndex가 비어있거나, 장비 아이템이 아니거나, 장착 중이 아니면 false.
	[[nodiscard]] bool TryUnequip(uint16_t slotIndex) noexcept;

	// slotIndex가 비어있거나 소비 아이템(ConsumableItem)이 아니면 false. 성공하면
	// 1개를 소모하고(마지막 1개였다면 슬롯이 비워짐) ItemTableRow::healAmount만큼
	// 소유자 HP를 회복시킨다(HealHp가 PLAYER_MAX_HP로 알아서 clamp함). 쿨타임
	// 검사는 여기서 하지 않는다 — 다른 액션들(공격/스킬)과 같은 이유로 Route.cpp가
	// User::m_lastPotionUseTime을 보고 이미 걸러낸 뒤에만 이 함수를 부른다.
	[[nodiscard]] bool TryUseItem(uint16_t slotIndex) noexcept;

	// 두 슬롯의 내용을 맞바꾼다(한쪽이 비어있으면 이동). 둘 다 비어있거나
	// 같은 인덱스면 false.
	//
	// 다른 메서드들은 "없는 슬롯을 가리키면 조회에 실패해서 알아서 false"가 되지만,
	// 이 메서드만은 _slots[slotIndexB] 형태로 없던 키를 새로 만들 수 있다 —
	// 그래서 범위 밖 인덱스를 그대로 받으면 아이템이 MAX_INVENTORY_SLOTS 밖의
	// "화면에도 안 보이고 다시 꺼낼 수도 없는" 슬롯으로 사라진다(DB에도 그대로
	// 저장되어 재접속해도 복구되지 않는다). IsValidSlotIndex()로 반드시 막는다.
	[[nodiscard]] bool TrySwapSlots(uint16_t slotIndexA, uint16_t slotIndexB) noexcept;

	// 슬롯 인덱스 -> Item. 빈 슬롯은 키 자체가 존재하지 않는다(부재 = 빈 슬롯).
	[[nodiscard]] const std::unordered_map<uint16_t, Item::SharedPtr>& GetSlots() const noexcept { return _slots; }

	// 지금 장착 중인 "무기"의 ItemTableId(없으면 ITEM_TABLE_ID_NONE). TryEquip이
	// "새로 장착하면 같은 부위의 이전 장비는 자동으로 탈착"을 원자적으로 보장하므로
	// 부위마다 0개 또는 1개만 장착 상태다.
	[[nodiscard]] ItemTableId GetEquippedItemId() const noexcept;
	// 그 부위에 장착 중인 슬롯(없으면 MAX_INVENTORY_SLOTS)
	[[nodiscard]] uint16_t FindEquipped(EquipSlot slot) const noexcept;
	// 슬롯의 V2 인스턴스 값(희귀도·옵션). 비었거나 장비가 아니면 0.
	[[nodiscard]] uint64_t GetSlotExtra(uint16_t slotIndex) const noexcept;
	// 장착 중인 무기의 강화 수치(장착한 게 없으면 0)
	[[nodiscard]] uint8_t GetEquippedEnchant() const noexcept;

	// 소유자의 Stat::SetOffensive()를 "레벨 기본 공격력 + 장착 무기의 attackBonus +
	// 강화 보너스"로 다시 계산해 반영한다. 장착 상태/강화 수치/레벨이 바뀌는 모든
	// 경로 끝에서 호출한다 — "언제 호출을 빠뜨렸는지" 따지는 대신, 항상 최종 상태를
	// 그대로 다시 계산하므로 항상 정확하다. (레벨업 경로에서도 불러야 해서 public)
	void RecomputeOffensive() const;
	// V2: 장착 장비(전 부위)의 능력치 보너스 · 특수 효과를 모은다(User::RecomputeStats가 부른다).
	void CollectGearBonus(struct GearBonus& out, struct GearExtras* outExtras = nullptr) const;

	// V2: 소지품 정리 — 같은 물건 스택을 합치고, 착용 중 → 장비(부위·희귀도 순) → 소모품 순으로
	// 슬롯을 다시 놓는다. 모든 이동은 두 칸짜리 교환(한 DB 트랜잭션)의 연속이라 도중에 서버가
	// 죽어도 아이템이 사라지거나 복제되지 않는다. 바뀐 칸 수를 돌려준다.
	int TrySort() noexcept;

	// ── 리니지풍 컨텐츠 확장 ────────────────────────────────────────────────
	// 지갑(그로시). 리니지에서는 그로시도 인벤토리 안의 물건이라, 슬롯과 같은 곳에서
	// 관리해야 "그로시 차감 + 슬롯 지급"을 한 번의 원자적 동작으로 묶을 수 있다.
	[[nodiscard]] uint32_t GetAdena() const noexcept { return _adena; }

	// 로그인 시 DB 값 복원(LoadFromDB와 같은 전제 — 아직 공개 전이라 검증/저장 없음).
	void LoadAdena(uint32_t adena) noexcept { _adena = adena; }

	// 줍기 등으로 그로시 증가. 상한(DB INT 범위)을 넘는 만큼은 버려진다.
	// 실제로 늘어난 양을 반환하고, 즉시 DB 저장 + 확장 세션에 ADENA_INF를 보낸다.
	uint32_t AddAdena(uint32_t amount, AdenaReason reason) noexcept;

	// 그로시를 쓴다(텔레포트 요금 등 물건을 받지 않는 지출). 모자라면 false — 아무것도 바뀌지 않는다.
	[[nodiscard]] bool TrySpendAdena(uint32_t amount, AdenaReason reason) noexcept;

	// 소비 아이템 하나를 소모한다(두루마리/바람걸음 물약처럼 효과가 인벤토리 밖에서
	// 일어나는 아이템용). slotIndex가 비었거나, 소비 아이템이 아니거나, 효과가
	// expected와 다르면 false — 아무것도 바뀌지 않는다.
	[[nodiscard]] bool TryConsumeOne(uint16_t slotIndex, ItemEffect expected, ItemTableId* outItemId = nullptr) noexcept;

	struct EnchantOutcome
	{
		EnchantResult result        = EnchantResult::Invalid;
		ItemTableId   itemId        = ITEM_TABLE_ID_NONE;
		uint8_t       before        = 0;
		uint8_t       after         = 0;
		bool          wasEquipped   = false;  // 증발/변화한 무기가 장착 중이었나(외형 브로드캐스트 필요)
	};

	// 무기 강화. 두루마리 1장 소모 + 무기 수치 변경(또는 증발)을 한 번에 처리하고,
	// DB에도 SaveTwoSlotsToDB로 한 트랜잭션에 묶는다(두루마리만 사라지고 무기는 그대로,
	// 또는 그 반대인 상태가 남지 않게). 난수는 호출자가 넘긴다(roll100: 0~99, bonusRoll: 0~2).
	[[nodiscard]] EnchantOutcome TryEnchant(uint16_t scrollSlot, uint16_t targetSlot, uint32_t roll100, uint32_t bonusRoll) noexcept;

	// 상점 구매: 그로시 차감 + 슬롯 하나에 지급. 수량 전체가 "한 슬롯"에 들어갈 때만
	// 성공한다(기존 스택에 합치거나 빈 슬롯 하나) — 그래야 DB 반영이 "그로시 + 슬롯 1개"
	// 한 트랜잭션(ShopTransaction 프로시저)으로 끝난다. outSlot에 지급된 슬롯을 담아준다.
	[[nodiscard]] ShopResult TryBuy(ItemTableId itemId, uint16_t count, uint32_t totalPrice, uint16_t* outSlot) noexcept;

	// 상점 판매: 슬롯에서 count만큼 빼고 그로시를 준다. 장착 중인 장비는 팔 수 없다.
	[[nodiscard]] ShopResult TrySell(uint16_t slotIndex, uint16_t count, uint32_t* outGain) noexcept;

	// 악명 사망 시 떨어뜨릴 아이템을 하나 골라 꺼낸다(roll은 호출자가 넘기는 난수).
	// 비어 있으면 nullptr. 꺼낸 아이템은 어느 인벤토리에도 속하지 않는다(TryExtractItem과 같음).
	[[nodiscard]] Item::SharedPtr TryTakeRandomItem(uint32_t roll) noexcept;

	// slotIndex의 현재 상태를 확장 슬롯 포맷으로 채운다(빈 슬롯이면 itemId=0).
	void FillSlotEx(uint16_t slotIndex, ITEM_SLOT_EX_DATA& out) const noexcept;

private:
	// 슬롯 인덱스는 전부 클라이언트 패킷에서 그대로 넘어온 값이므로 절대 신뢰하지
	// 않는다. 검증을 Route.cpp가 아니라 여기 두는 이유: Route에 두면 핸들러마다
	// 같은 검사를 반복해야 하고 하나만 빠뜨려도 그대로 구멍이 되지만, Inventory는
	// 모든 경로가 반드시 지나가는 길목이라 여기서 한 번 막으면 전부 덮인다.
	[[nodiscard]] static constexpr bool IsValidSlotIndex(uint16_t slotIndex) noexcept
	{
		return slotIndex < MAX_INVENTORY_SLOTS;
	}

	// 소유자가 아직 살아있는지(Release 포함 항상 확인) + 지금 이 스레드가 그 소유자의
	// Zone 스레드가 맞는지(Debug 한정)를 한 곳에서 검사한다. mutating 메서드마다
	// 이 두 조건을 따로 챙기다 실수로 하나를 빠뜨리는 일이 없도록 한 곳에 모았다.
	void AssertOwnedByCurrentZone() const noexcept;

	// slotIndex의 현재 상태(있으면 내용, 없으면 빈 슬롯)를 그대로 DB에 반영한다.
	// "무엇이 바뀌었는지"를 따로 추적하지 않고 최종 상태를 그대로 저장/삭제하므로,
	// 호출 시점에 그 슬롯이 어떤 이유로 바뀌었는지는 몰라도 항상 정확하다.
	void SaveSlotToDB(uint16_t slotIndex) const;

	// 두 슬롯의 현재 상태를 하나의 DB 트랜잭션으로 함께 반영한다(SaveSlotToDB를
	// 두 번 따로 부르면 그 사이에 서버가 죽었을 때 한쪽만 반영된 상태가 영구히
	// 남을 수 있는 동작들 — 장착 시 이전 장비 자동 탈착, 슬롯 교체 — 에서 쓴다).
	void SaveTwoSlotsToDB(uint16_t slotIndexA, uint16_t slotIndexB) const;

	// 슬롯의 현재 상태를 DB 저장 단위로 바꾼다(SaveTwoSlotsToDB/상점 거래 공용).
	[[nodiscard]] DB_ITEM_SLOT_SAVE DescribeSlot(uint16_t slotIndex) const;

	// 슬롯이 바뀌었다고 소유자(확장 세션)에게 ITEM_SLOT_EX_INF로 알린다. 모든 변경
	// 경로가 이미 SaveSlotToDB/SaveTwoSlotsToDB를 거치므로 그 두 곳에서 함께 부른다 —
	// "저장한 것 = 알린 것"이 항상 같아진다.
	void NotifySlotChanged(uint16_t slotIndex) const;

	// itemId/count를 "한 슬롯"에 넣을 수 있는 자리(기존 스택 우선, 없으면 빈 슬롯).
	// 없으면 MAX_INVENTORY_SLOTS. 맵은 건드리지 않는다.
	[[nodiscard]] uint16_t FindSingleSlotFor(const ItemTableRow& row, uint16_t count) const noexcept;

private:
	std::weak_ptr<User> _owner;
	std::unordered_map<uint16_t, Item::SharedPtr> _slots;
	uint32_t _adena = 0;
};
