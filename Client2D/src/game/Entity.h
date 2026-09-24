#pragma once

#include "raylib.h"
#include "net/Shared.h"

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

	// 위치 — tx,ty(서버 칸) → 논리 위치(logPos: 걸음 시간 동안 칸 사이를 직선 이동)
	//        → 표시 위치(pos: logPos를 지수 스무딩으로 따라감). 자세한 건 game/Motion.h
	int     tx = 0, ty = 0;          // 서버 칸
	Vector2 pos{};                   // 그리는 위치(타일 좌표, 실수)
	Vector2 logPos{};
	Vector2 from{}, to{};
	float   moveT = 1.f, moveDur = 0.5f;
	struct QueuedStep { Vector2 to; float dur; };
	std::array<QueuedStep, 4> stepQ{};   // 아직 재생하지 않은 걸음(원격 개체)
	int     stepQN = 0;
	float   walkPhase = 0.f;
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
	float lastStepDust = 0.f;
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
	[[nodiscard]] int EquippedSlot() const
	{
		for (int i = 0; i < MAX_INVENTORY_SLOTS; ++i) if (slots[i].equipped && !slots[i].Empty()) return i;
		return -1;
	}
	[[nodiscard]] int FreeSlots() const
	{
		int n = 0;
		for (const auto& s : slots) if (s.Empty()) ++n;
		return n;
	}
};
