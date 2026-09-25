#pragma once

#include "raylib.h"
#include "net/Shared.h"
#include "game/Motion.h"

#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 화면에 보이는 오브젝트 하나(플레이어/몬스터/필드 아이템). 서버가 알려 준 칸(tx,ty)과
// 화면에 그리는 연속 위치(pos)를 따로 들고, pos가 tx,ty를 부드럽게 따라간다.
// ─────────────────────────────────────────────────────────────────────────────
enum class EntityKind : uint8_t { Player, Monster, Item };

struct Entity
{
	ObjID       id;
	EntityKind  kind = EntityKind::Monster;
	std::string name;

	// 위치 — tx,ty(서버 칸) → 논리 위치(logPos: 재생 시간표대로 칸 사이를 직선 이동)
	//        → 표시 위치(pos: 화면 추종자 — 명시 속도 · 앞보기). 자세한 건 game/Motion.h
	int     tx = 0, ty = 0;          // 서버 칸
	Vector2 pos{};                   // 그리는 위치(타일 좌표, 실수)
	Vector2 logPos{};
	motion::Track track;             // 원격 개체의 재생 시간표 · 화면 추종자
	motion::Body  body;              // 방향(연속 각 · 8구역) · 걸음 위상 · 자세
	float   walkPhase = 0.f;         // ↓ body를 비춘 값(그리기가 읽는다)
	float   moving = 0.f;            // 0~1(idle↔walk 섞기, 속도 기반)
	float   run = 0.f;               // 0~1(walk↔run)
	float   speed = 0.f;             // 표시 속도(칸/초)
	bool    faceRight = true;
	bool    faceBack = false;

	// 상태
	int      hp = 0, maxhp = 0;
	int      level = 0;
	uint16_t species = 0;
	const MonsterTableRow* row = nullptr;
	bool     aggressive = false;
	uint16_t itemId = 0;             // 플레이어: 장착 무기 / 아이템: 아이템 종류
	uint8_t  enchant = 0;
	int16_t  alignment = 0;
	uint8_t  flags = 0;              // SubjectExFlag
	uint32_t nameHash = 0;
	uint8_t  classId = 0;            // 플레이어 직업(ClassId)
	uint8_t  weaponGrade = 0;        // 플레이어: 무기 희귀도 / 아이템: 그 장비의 희귀도(Rarity)
	// V2 착용 외형(PLAYER_LOOK_NFY) — EquipSlot - 1 순서(무기·투구·갑옷·장갑·신발·망토·목걸이·반지)
	std::array<uint16_t, 8> gear{};
	std::array<uint8_t, 8>  gearRarity{};
	bool     gearKnown = false;

	// 상태이상(표시용) — STATUS_NFY로 갱신, 만료는 클라 시계로
	struct StatusFx { uint8_t id = 0; uint8_t stacks = 0; float until = 0.f; };
	std::array<StatusFx, 6> statuses{};
	[[nodiscard]] const StatusFx* Status(uint8_t id, float now) const
	{
		for (const auto& s : statuses) if (s.id == id && s.stacks > 0 && now < s.until) return &s;
		return nullptr;
	}

	// 스킬 모션(시전자) — 0이면 기본 동작
	uint16_t animSkill = 0;
	float    animT = -1.f;           // 경과(초), <0 없음
	float    animDur = 0.5f;
	float    animCast = 0.f;         // 예고 시간(처형·낙성)

	// 몬스터 강공격 예고
	struct Telegraph { bool active = false; uint8_t shape = 0, pattern = 0; Vector2 c{}, from{}; float radius = 0.f, t = 0.f, dur = 1.f; };
	Telegraph tele;
	float    windup = 0.f;           // 0~1 예고 자세
	float    knockT = 0.f;           // 1→0 넉백 흔들림
	float    hopT = -1.f;            // 도약 포물선 진행(0→1), <0 없음
	float    hopH = 46.f;
	double   projArriveAt = 0.0;     // 이 대상에게 날아가는 투사체가 닿는 시각(표시 지연)
	float    stunShake = 0.f;        // 기절/빙결 걸린 순간 흔들림

	// 연출
	bool  dead = false;
	float deadT = 0.f;
	bool  removing = false;
	float removeT = 0.f;
	float spawnT = 0.f;              // 나타나는 연출(0→1)
	float attackT = -1.f;            // 공격 동작 경과(초), <0 없음
	float castT = -1.f;
	float hurtT = 0.f;               // 1→0
	float flashT = 0.f;              // 1→0 흰색 번쩍
	float hpShown = 1.f;             // 체력바(부드럽게)
	float hpLag = 1.f;               // 늦게 따라오는 흰 바
	float lastHitAt = -100.f;        // 최근 맞은 시각(체력바 표시)
	std::string chat;
	float chatT = 0.f;

	// 필드 아이템 떨어지는 연출
	float   dropT = 1.f;
	Vector2 dropFrom{};
	float   flyT = -1.f;             // 주웠을 때 플레이어에게 날아감
	Vector2 flyTo{};

	[[nodiscard]] bool IsBoss() const { return row != nullptr && row->boss; }
	[[nodiscard]] float HpRatio() const { return maxhp > 0 ? static_cast<float>(hp) / maxhp : 1.f; }
};

struct InvSlot
{
	uint16_t itemId = 0;
	uint16_t count = 0;
	bool     equipped = false;
	uint8_t  enchant = 0;
	uint64_t extra = 0;          // V2 희귀도·옵션(ITEM_DETAIL_INF, Data/ItemRules.h ItemInstance)
	float    newAt = -100.f;     // 방금 들어온 좋은 물건(반짝임, 마우스를 올리면 꺼진다)
	[[nodiscard]] bool Empty() const { return itemId == 0 || count == 0; }
};

struct ClientInventory
{
	std::array<InvSlot, MAX_INVENTORY_SLOTS> slots{};
	uint32_t adena = 0;

	[[nodiscard]] int CountOf(uint16_t itemId) const
	{
		int n = 0;
		for (const auto& s : slots) if (s.itemId == itemId) n += s.count;
		return n;
	}
	[[nodiscard]] int FirstSlotOf(uint16_t itemId) const
	{
		for (int i = 0; i < MAX_INVENTORY_SLOTS; ++i) if (slots[i].itemId == itemId && slots[i].count > 0) return i;
		return -1;
	}
	// 무기 부위에 장착 중인 슬롯(V2부터 방어구도 장착되므로 부위를 본다)
	[[nodiscard]] int EquippedSlot() const { return EquippedIn(EquipSlot::Weapon); }
	[[nodiscard]] int EquippedIn(EquipSlot part) const
	{
		for (int i = 0; i < MAX_INVENTORY_SLOTS; ++i)
			if (slots[i].equipped && !slots[i].Empty() && ItemTableData::SlotOf(slots[i].itemId) == part) return i;
		return -1;
	}
	[[nodiscard]] int FreeSlots() const
	{
		int n = 0;
		for (const auto& s : slots) if (s.Empty()) ++n;
		return n;
	}
};
