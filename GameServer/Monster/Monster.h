#pragma once

#include "AStar.h"
#include "Subject.h"
#include "Data/MonsterTable.h"

class Monster : public Subject
{
	MONSTER_TYPE m_Type;
	vector<NODE> m_AstarPath;
	std::atomic<bool> m_Active;
	std::atomic<bool> m_Attack;

	short m_spawnX = 0;
	short m_spawnY = 0;
	short m_pathGoalX = -1;
	short m_pathGoalY = -1;

	// 마지막 이동 시점에 이 몬스터를 보고 있던 유저 목록.
	// 다음 이동 시 oldList로 재사용해 CollectUsers 호출을 절반으로 줄인다.
	// GameLogicThread 단일 소비자이므로 동기화 불필요.
	vector<ObjID> m_viewList;

	// ── 리니지풍 컨텐츠 확장 ──
	// 종족 정보(Data/MonsterTable.h). ObjID의 KIND에 실린 종족 id로 InitInstance에서
	// 한 번 찾아 둔다 — 테이블은 불변 constexpr 데이터라 포인터를 들고 있어도 안전하다.
	const MonsterTableRow* m_species = nullptr;
	// 선공하지 않는 몬스터도 맞으면 때린 사람을 쫓는다(리니지의 반격). 쫓을 대상.
	ObjID m_provokedBy = ObjID::npos;
	bool  m_provoked   = false;
	// 보스 광역기 루프가 돌고 있는가(중복 예약 방지 — TryActivate와 같은 패턴)
	std::atomic<bool> m_bossSkillLoop{ false };
	short m_skillX = 0;
	short m_skillY = 0;
public:
	// ── V2 강공격 패턴(예고 → 판정). 소유 Zone 스레드 전용 ──
	struct PatternState
	{
		MonsterPattern type = MonsterPattern::None;
		short cx = 0, cy = 0;          // 판정 중심(원) / 끝점(선)
		short fromX = 0, fromY = 0;
		int8_t dx = 0, dy = 0;         // 돌진 방향
		uint8_t radius = 0;
		bool pending = false;
	};
	PatternState m_pattern;
	// Phase 2-B 보스 무력화(게이지가 비면 3초 무력화 + 받는 피해 +30%) · 반격
	uint16_t m_stagger = 0;           // 남은 무력화 게이지(0이면 무력화 중 · 회복 대기)
	uint16_t m_staggerMax = 0;
	uint32_t m_staggerRefillAt = 0;   // 이 시각 이후 첫 타격 때 게이지를 다시 채운다
	uint32_t m_staggerSentAt = 0;
	bool     m_countered = false;     // 예고 중 반격 타격을 맞았다(판정 때 끊긴다)
	uint32_t m_patternReadyAt = 0;   // 다음 패턴 가능 시각(GetNowTime)
	uint32_t m_busyUntil = 0;        // 예고·후딜 동안 이동/평타를 멈춘다

public:
	using SharedPtr = shared_ptr<Monster>;
	using WeakPtr = weak_ptr<Monster>;

public:
	Monster() = default;
	Monster(const ObjID& objID) : Subject(objID) {};

	virtual void InitInstance() override;

	MONSTER_TYPE GetType() const { return m_Type; }
	vector<NODE>& GetPath() { return m_AstarPath; }
	bool IsPathTarget(short x, short y) const noexcept { return m_pathGoalX == x && m_pathGoalY == y; }
	bool HasCachedPathTo(short x, short y) const noexcept { return IsPathTarget(x, y) && !m_AstarPath.empty(); }

	bool IsActive() const { return m_Active.load(); }
	bool IsAttacking() const { return m_Attack.load(); }

	// TryActivate: false → already active, true → first to activate
	bool TryActivate() { return !m_Active.exchange(true); }

	void SetType(MONSTER_TYPE t) { m_Type = t; }
	void CachePathTarget(short x, short y) noexcept { m_pathGoalX = x; m_pathGoalY = y; }
	void ClearPath()             { m_AstarPath.clear(); m_pathGoalX = -1; m_pathGoalY = -1; }

	void SetActive(bool active) { m_Active.store(active); }
	void SetAttack(bool attack) { m_Attack.store(attack); }

	const vector<ObjID>& GetViewList() const noexcept { return m_viewList; }
	void SetViewList(vector<ObjID> list) noexcept { m_viewList = std::move(list); }
	void ClearViewList() noexcept { m_viewList.clear(); }

	void  SetSpawn(short x, short y) { m_spawnX = x; m_spawnY = y; }
	short GetSpawnX() const { return m_spawnX; }
	short GetSpawnY() const { return m_spawnY; }

	[[nodiscard]] const MonsterTableRow* GetSpecies() const noexcept { return m_species; }
	[[nodiscard]] bool IsBoss() const noexcept { return m_species != nullptr && m_species->boss; }
	// Phase 2-E 훈련장 허수아비: 깨어나지 않고(움직임 · 공격 없음) 죽지 않는다
	[[nodiscard]] bool IsDummy() const noexcept { return m_species != nullptr && m_species->dummy; }
	[[nodiscard]] uint16_t GetMoveIntervalMs() const noexcept { return m_species != nullptr ? m_species->moveIntervalMs : 500; }
	[[nodiscard]] uint16_t GetAttackIntervalMs() const noexcept { return m_species != nullptr ? m_species->attackIntervalMs : 1000; }
	[[nodiscard]] int GetLeash() const noexcept { return m_species != nullptr ? m_species->leash : 7; }

	[[nodiscard]] bool IsProvoked() const noexcept { return m_provoked; }
	[[nodiscard]] const ObjID& GetProvokedBy() const noexcept { return m_provokedBy; }
	void SetProvoked(const ObjID& attackerId) noexcept { m_provoked = true; m_provokedBy = attackerId; }
	void ClearProvoke() noexcept { m_provoked = false; m_provokedBy = ObjID::npos; }

	// 보스 광역기 중심(예고 시점의 위치 — 적중 판정은 이 좌표 기준이라 피할 수 있다)
	void SetSkillCenter(short x, short y) noexcept { m_skillX = x; m_skillY = y; }
	[[nodiscard]] short GetSkillX() const noexcept { return m_skillX; }
	[[nodiscard]] short GetSkillY() const noexcept { return m_skillY; }

	// 보스 광역기 루프 시작 권한(처음 한 번만 true)
	bool TryStartBossSkillLoop() { return !m_bossSkillLoop.exchange(true); }
	void StopBossSkillLoop() { m_bossSkillLoop.store(false); }

};
