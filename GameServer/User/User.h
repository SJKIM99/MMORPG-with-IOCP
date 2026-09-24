#pragma once
#include <array>
#include <vector>

#include "Subject.h"
#include "Item/Inventory.h"
#include "Data/LevelTable.h"
#include "Data/CombatData.h"
#include "Data/ItemRules.h"

class User : public Subject
{
public:
	using SharedPtr = shared_ptr<User>;
	using WeakPtr = weak_ptr<User>;

public:
	uint32_t m_lastMoveTime      = 0;
	uint32_t m_lastAttackTime    = 0;
	uint32_t m_lastSkillTime     = 0;
	uint32_t m_lastPotionUseTime = 0;

	// ── 리니지풍 컨텐츠 확장 ──────────────────────────────────────────────────
	// 아래 값들은 전부 소유 Zone 스레드에서만 읽고 쓴다(Inventory와 같은 전제).
	uint32_t m_lastScrollUseTime = 0;   // 주문서/초록 물약 쿨타임(물약과 별개)
	uint32_t m_lastShopSeq       = 0;   // 상점/텔레포트 요청 멱등성 — 마지막으로 처리한 seq
	uint32_t m_purpleUntil       = 0;   // GetNowTime() 기준. 이 시각 전까지 보라돌이
	uint32_t m_hasteUntil        = 0;   // GetNowTime() 기준. 이 시각 전까지 가속
	uint32_t m_deathSeq          = 0;   // 사망할 때마다 증가 — 늦게 도착한 부활 타이머를 걸러낸다
	uint32_t m_lastWorldChatTime = 0;   // 전체 채팅 도배 방지
	// 마지막으로 DB에 저장한 성향치. 사냥할 때마다 1~20씩 오르므로 매번 저장하지 않고
	// 일정 폭 이상 변했을 때/PK/로그아웃 때만 저장한다(ContentHelper::SaveAlignment).
	int16_t  m_savedAlignment    = 0;

	// ── V2 이동(토큰 버킷, Data/MoveRule.h) ───────────────────────────────────
	uint32_t m_moveClock         = 0;   // 마지막으로 허용한 걸음이 "끝나는" 가상 시각

	// ── V2 전투(직업·능력치·스킬) — Data/CombatData.h, Combat/* ────────────────
	ClassId      m_classId       = ClassId::Knight;
	BaseStats    m_alloc{};                 // 내가 찍은 능력치 포인트
	GearBonus    m_gear{};                  // 장비 보너스(RecomputeStats가 채움)
	DerivedStats m_derived{};               // 전투 능력치(RecomputeStats가 채움)
	uint16_t     m_mp            = 0;
	std::array<uint32_t, 6> m_skillReadyAt{};   // 슬롯별 다시 쓸 수 있는 시각(GetNowTime)
	uint32_t     m_lastCounterAt = 0;           // 철벽 태세 반격 간격
	uint32_t     m_lastCombatAt  = 0;           // 마지막 전투 시각(비전투 회복)
	struct PendingImpact { uint32_t seq; uint16_t skillId; ObjID target; short x, y; };
	std::vector<PendingImpact> m_pendingImpacts; // 예고 뒤 판정할 스킬(혜성·처형·룬 폭발)
	uint32_t     m_impactSeq     = 0;

	// ── V2 아이템 성장(Data/ItemRules.h) ───────────────────────────────────────
	GearExtras   m_gearEx{};                // 착용 장비의 특수 효과·기술 강화·획득 보너스(RecomputeStats가 채움)
	uint16_t     m_pityRare      = 0;       // "희귀 이상"을 못 본 장비 드롭 수(천장)
	uint16_t     m_pityHeroic    = 0;       // "영웅 이상"을 못 본 장비 드롭 수
	uint32_t     m_lastSortAt    = 0;       // 소지품 정리 도배 방지
	uint32_t     m_guardianReadyAt = 0;     // 수호의 맹세(특수 효과) 재발동 시각
	uint32_t     m_lifestealFxAt = 0;       // 피의 갈증 연출 간격

	// ── V2 마을 NPC(Npc/NpcHelper) ─────────────────────────────────────────────
	uint32_t     m_npcSeenTick   = 0;   // 마지막으로 NPC 틱이 이 사용자를 마을에서 본 번호(새로 오면 목록을 보낸다)
	uint32_t     m_npcHealReadyAt = 0;  // 사제 기도 재사용
	uint32_t     m_npcSoupReadyAt = 0;  // 여관 수프 재사용

private:
	shared_ptr<GameSession> m_session;
	// Monster는 인벤토리가 없으므로 Subject가 아닌 User에 둔다 (m_session과 같은 이유).
	Inventory::SharedPtr m_inventory;
	// Players.playerId(DB 발급 정수 PK). 로그인/계정생성 시 DB 스레드가 한 번 조회해
	// UserHelper::HandleGetUserInfo/HandleAddUserInfo에서 곧바로 세팅해준다 —
	// 이후 Inventory 저장 경로는 매번 name으로 재조회하지 않고 이 값을 그대로 쓴다.
	int m_playerId = 0;

	int16_t  m_alignment = 0;   // 성향치(라우풀 +, 카오틱 -)
	uint16_t m_pkCount   = 0;

public:
	User() = default;
	~User() = default;

	virtual void InitInstance() override;

	[[nodiscard]] shared_ptr<GameSession> GetGameSession() const;
	void SetGameSession(const shared_ptr<GameSession>& session);

	// GetStat()과 동일하게 shared_ptr을 복사해서 반환한다.
	[[nodiscard]] Inventory::SharedPtr GetInventory() noexcept { return m_inventory; }

	[[nodiscard]] int GetPlayerId() const noexcept { return m_playerId; }
	void SetPlayerId(int playerId) noexcept { m_playerId = playerId; }

	// 이 유저의 세션이 확장 프로토콜(새 클라이언트)인가
	[[nodiscard]] bool IsExtended() const noexcept;

	[[nodiscard]] int16_t  GetAlignment() const noexcept { return m_alignment; }
	[[nodiscard]] uint16_t GetPkCount() const noexcept { return m_pkCount; }
	void SetAlignment(int16_t alignment) noexcept { m_alignment = alignment; }
	void SetPkCount(uint16_t pkCount) noexcept { m_pkCount = pkCount; }

	[[nodiscard]] bool IsPurple(uint32_t now) const noexcept { return now < m_purpleUntil; }
	[[nodiscard]] bool IsHasted(uint32_t now) const noexcept { return now < m_hasteUntil; }
	[[nodiscard]] uint32_t GetMoveCooldown(uint32_t now) const noexcept
	{
		return IsHasted(now) ? CombatRule::kHasteMoveCooldownMs : CombatRule::kMoveCooldownMs;
	}
	// 확장 세션 이동속도(%). 100 = 기본. 가속·(이후) 스탯/장비/상태이상이 더해진다.
	[[nodiscard]] uint32_t GetMoveSpeedPercent(uint32_t now) const noexcept;

	// 현재 레벨에 맞춰 최대 HP/기본 공격력을 다시 계산한다(레벨업·로그인 복원 시).
	// fullHeal이면 HP를 최대치로 채운다.
	void ApplyLevelStats(bool fullHeal);

	// V2: 직업·레벨·포인트·장비로 전투 능력치를 다시 계산한다. 확장 세션이 아니면
	// 예전 공식(LevelTable)을 그대로 쓴다 — 레거시 봇의 수치는 바뀌지 않는다.
	// 바뀌었으면 true.
	bool RecomputeStats(bool fullHeal);
	[[nodiscard]] uint16_t FreePoints() const noexcept;
	// V2 진행 데이터 직렬화(DB PlayerProgress). 알 수 없는 버전/깨진 데이터는 기본값으로 둔다.
	[[nodiscard]] std::vector<uint8_t> SerializeProgress() const;
	bool DeserializeProgress(const std::vector<uint8_t>& data);   // false = V2 기록 없음(기존 캐릭터의 첫 V2 접속)
	[[nodiscard]] BaseStats TotalBase() const noexcept;
};
