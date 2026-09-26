#pragma once
#include <array>
#include <vector>

#include "Subject.h"
#include "Item/Inventory.h"
#include "Data/LevelTable.h"
#include "Data/CombatData.h"
#include "Data/ItemRules.h"
#include "Data/QuestData.h"
#include "Data/SkillGrowth.h"

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
	uint32_t m_lastScrollUseTime = 0;   // 두루마리/바람걸음 물약 쿨타임(물약과 별개)
	uint32_t m_lastShopSeq       = 0;   // 상점/텔레포트 요청 멱등성 — 마지막으로 처리한 seq
	uint32_t m_purpleUntil       = 0;   // GetNowTime() 기준. 이 시각 전까지 적대
	uint32_t m_hasteUntil        = 0;   // GetNowTime() 기준. 이 시각 전까지 가속
	uint32_t m_deathSeq          = 0;   // 사망할 때마다 증가 — 늦게 도착한 부활 타이머를 걸러낸다
	uint32_t m_lastWorldChatTime = 0;   // 전체 채팅 도배 방지
	// Phase 3-B 채팅(Content/ChatHelper) — 소유 Zone 스레드만
	uint32_t m_lastTradeChatTime = 0;   // 거래 채팅 재사용 대기
	uint32_t m_chatRefillAt      = 0;   // 토큰 버킷: 마지막으로 채운 시각
	uint32_t m_chatLastHash      = 0;   // 되풀이: 마지막 문장의 해시 · 시각 · 연속 횟수
	uint32_t m_chatLastAt        = 0;
	uint8_t  m_chatTokens        = 5;
	uint8_t  m_chatRepeat        = 0;
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
	// 예약된 기술 사건. kind 0 = 예전 판정(낙성·처형·각인 폭발), 나머지는 Phase 2-B 확장(SkillEx — ExEvent)
	struct PendingImpact
	{
		uint32_t seq; uint16_t skillId; ObjID target; short x, y;
		uint8_t  kind = 0;       // SkillEx::ExEvent
		uint8_t  hit = 0;        // 타격표 번호
		uint8_t  stage = 0;      // 콤보 타 · 차지 단계 · 체인 단
		uint8_t  flags = 0;      // 완벽 · 강제 공격
		uint32_t castSeq = 0;    // 이 시전의 번호(끊기면 버린다)
		short    ox = 0, oy = 0; // 시전 위치
		float    dx = 1.f, dy = 0.f;   // 조준 방향(칸 공간 단위 벡터)
		uint32_t extra = 0;
		uint8_t  grow = 0;       // 시전 때의 기술 성장 상태(SkillGrowth::Pack) — 갈래를 도중에 바꿔도 이 시전은 그대로
	};
	std::vector<PendingImpact> m_pendingImpacts;
	uint32_t     m_impactSeq     = 0;

	// ── Phase 2-B 기술 구조 V3(Data/SkillData.h, Combat/SkillEx) — 소유 Zone 스레드 ──
	std::array<uint32_t, 12> m_keyReadyAt{};   // 칸별(SkillKey) 재사용 대기(확장 요청)
	struct SkillExState
	{
		uint32_t castSeq = 0;                       // 시전마다 증가 — 끊긴 시전의 예약은 버린다
		uint16_t chargeSkill = 0; uint32_t chargeStart = 0; uint32_t chargeCast = 0;
		uint16_t holdSkill = 0;   uint32_t holdStart = 0;   uint32_t holdCast = 0;
		uint16_t toggleSkill = 0; uint32_t toggleCast = 0;
		uint16_t chainSkill = 0;  uint8_t chainStage = 0;   uint32_t chainUntil = 0;
		uint8_t  comboStage = 0;  uint32_t comboAt = 0;     // 기본 공격 콤보
		uint32_t castingUntil = 0; uint32_t castingSeq = 0; // 캐스팅(이동하면 끊긴다)
		uint32_t lastCloneCast = 0;                         // 먹물 분신: 시전마다 한 번만 줄인다
		uint32_t dodgeAt = 0;                               // 잔상 회피 시각(완벽 회피 판정)
		std::vector<ObjID> marks;                           // 방패 투척 · 그믐 베기 · 달 가르기가 기억한 대상
	} m_skx;
	uint16_t     m_identity      = 0;           // 고유 기술 게이지 0~1000(서약 · 새김 · 그늘)
	uint16_t     m_identitySent  = 0xFFFF;
	uint32_t     m_superArmorUntil = 0;         // 경직 면역(몬스터의 기절 · 빙결을 받지 않는다)
	uint32_t     m_invulnUntil   = 0;           // 무적(이동기 · 각성기)

	// ── Phase 2-D 기술 성장(Data/SkillGrowth.h, SkillEx::HandleGrow) — DB PlayerSkills ──
	std::array<uint8_t, SkillGrowth::kIdSpan> m_skillGrow{};   // 기술 id → SkillGrowth::Pack(0 = 레벨 1 · 갈래 없음)
	uint16_t     m_lastGrowSeq   = 0;
	bool         m_growSeqSeen   = false;       // 첫 요청 전에는 seq 비교를 하지 않는다
	[[nodiscard]] SkillGrowth::State GrowState(uint16_t skillId) const noexcept
	{
		return skillId < SkillGrowth::kIdSpan ? SkillGrowth::Unpack(m_skillGrow[skillId]) : SkillGrowth::State{};
	}

	// ── V2 아이템 성장(Data/ItemRules.h) ───────────────────────────────────────
	GearExtras   m_gearEx{};                // 착용 장비의 특수 효과·기술 강화·획득 보너스(RecomputeStats가 채움)
	uint16_t     m_pityRare      = 0;       // "희귀 이상"을 못 본 장비 드롭 수(천장)
	uint16_t     m_pityHeroic    = 0;       // "영웅 이상"을 못 본 장비 드롭 수
	uint32_t     m_lastSortAt    = 0;       // 소지품 정리 도배 방지
	uint32_t     m_guardianReadyAt = 0;     // 서약의 방패(특수 효과) 재발동 시각
	uint32_t     m_lifestealFxAt = 0;       // 생명 흡수 연출 간격

	// ── V2 마을 NPC(Npc/NpcHelper) ─────────────────────────────────────────────
	uint32_t     m_npcSeenTick   = 0;   // 마지막으로 NPC 틱이 이 사용자를 마을에서 본 번호(새로 오면 목록을 보낸다)
	uint32_t     m_npcHealReadyAt = 0;  // 사제 기도 재사용
	uint32_t     m_npcSoupReadyAt = 0;  // 여관 수프 재사용

	// ── V2 퀘스트(Data/QuestData.h, Quest/QuestHelper) — 진행 데이터 v3에 저장 ────────
	struct QuestSlot
	{
		uint16_t id = 0;                                   // 0 = 빈 칸
		std::array<uint16_t, QuestData::kMaxObjs> progress{};
		std::array<uint8_t, QuestData::kMaxObjs> misses{}; // 수집 실패 횟수(운 보정, 저장하지 않는다)
	};
	std::array<QuestSlot, QuestData::kMaxActive> m_quests{};
	std::array<uint8_t, 32> m_questDone{};                  // 끝낸 퀘스트(한 번뿐인 것) id 비트
	struct QuestCooldown { uint16_t id; uint32_t readyAtUnix; };
	std::vector<QuestCooldown> m_questCooldowns;            // 반복 퀘스트: 다시 받을 수 있는 시각(벽시계)

private:
	shared_ptr<GameSession> m_session;
	// Monster는 인벤토리가 없으므로 Subject가 아닌 User에 둔다 (m_session과 같은 이유).
	Inventory::SharedPtr m_inventory;
	// Players.playerId(DB 발급 정수 PK). 로그인/계정생성 시 DB 스레드가 한 번 조회해
	// UserHelper::HandleGetUserInfo/HandleAddUserInfo에서 곧바로 세팅해준다 —
	// 이후 Inventory 저장 경로는 매번 name으로 재조회하지 않고 이 값을 그대로 쓴다.
	int m_playerId = 0;

	int16_t  m_alignment = 0;   // 성향치(명예 +, 악명 -)
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
