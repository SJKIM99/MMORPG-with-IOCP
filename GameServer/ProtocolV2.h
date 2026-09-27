#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 프로토콜 V2 — Dark Fantasy Storybook 고도화(이동·직업·스킬·아이템·퀘스트·길드).
//
// Protocol.h 끝에서 include된다(PacketType 값은 Protocol.h의 enum 끝에 이어 붙인다).
// V2 패킷은 확장 세션(USER_LOGIN_EX_REQ로 접속)에만 오간다. 레거시 봇/클라이언트는
// 이 헤더의 어떤 패킷도 보내거나 받지 않는다.
//
// 레거시 필드 재사용: SUBJECT_MOVE_NFY.move_time(예전엔 항상 0)에 **그 걸음에 걸린
// 시간(ms)**을 싣는다. 0이면 순간이동/재배치라 보간 없이 바로 옮기면 된다. 봇은 이
// 필드를 읽지 않는다.
// ─────────────────────────────────────────────────────────────────────────────

#include "Data/CombatData.h"

#pragma pack (push, 1)

// ── 이동 ─────────────────────────────────────────────────────────────────────
// dir: USER_MOVE_REQ와 같은 8방향(0:UP 1:DOWN 2:LEFT 3:RIGHT 4:UP-LEFT 5:UP-RIGHT
// 6:DOWN-LEFT 7:DOWN-RIGHT). seq: 클라이언트가 이동마다 1씩 올리는 번호(16비트 순환).
struct USER_MOVE_EX_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        dir;
	uint16_t       seq;
};

enum class MoveResult : uint8_t
{
	Ok,          // 이동함(벽에 막혀 제자리여도 규칙대로 처리됐으면 Ok)
	TooFast,     // 토큰 버킷 초과 — 서버 좌표로 되돌려야 한다
	Busy,        // 사망/존 이동 중 등 지금은 움직일 수 없음
	Stunned,     // 기절·빙결 등 이동 불가 상태
};

// 확장 세션의 모든 USER_MOVE_EX_REQ에 정확히 하나씩, 받은 순서대로 응답한다.
// x/y는 처리 직후의 서버 좌표(거절이면 현재 좌표) — 클라이언트는 이 값으로 예측을 맞춘다.
struct USER_MOVE_EX_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       seq;
	short          x, y;
	uint8_t        result;    // MoveResult
	uint16_t       stepMs;    // 이 걸음의 비용(ms). 클라 예측 속도 보정용
};

// 로그인 직후 한 번. 클라이언트가 추측하지 말고 서버 값을 쓰도록.
struct SERVER_CONFIG_INF_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       protocolVersion;   // PROTOCOL_EX_VERSION
	uint8_t        viewRange;         // 시야(체비셰프 칸)
	uint16_t       baseStepMs;        // 이동속도 100%일 때 직선 한 칸 시간
	uint16_t       diagPermille;      // 대각선 비용 배율(‰)
	uint16_t       moveSpeedPercent;  // 지금 내 이동속도(%)
};

// ── 전투 ─────────────────────────────────────────────────────────────────────
// 스킬/기본 공격 시전. 대상 규칙은 SkillTable(Data/CombatData.h)의 target:
//   Enemy → targetId, Ground → x/y(지점), Direction → x/y(바라보는 칸), Self → 무시
struct SKILL_CAST_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       skillId;
	ObjID          targetId;
	short          x, y;
	uint16_t       seq;
};

enum class SkillResult : uint8_t
{
	Ok,
	Cooldown,        // param = 남은 ms
	NoMana,
	OutOfRange,
	InvalidTarget,
	Locked,          // 레벨이 모자람(param = 필요 레벨)
	Busy,            // 사망·존 이동 중
	SafeZone,        // 마을 안
	Stunned,         // 기절 중
	Blocked,         // 이동/도약할 칸이 없음
	WrongClass,
	// Phase 2-B(끝에만 추가)
	NoGauge,         // 고유 기술 게이지가 모자람
};

struct SKILL_RESULT_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       seq;
	uint16_t       skillId;
	uint8_t        result;       // SkillResult
	uint32_t       cooldownMs;   // 성공이면 이번 쿨다운, 실패(Cooldown)면 남은 시간
	uint16_t       mp;           // 현재 MP
};

// 주변에 보내는 시전 알림 — 모션·예고·투사체·돌진 연출용. 판정 결과(피해)는
// SUBJECT_DAMAGE_NFY로 따로 온다.
struct SKILL_CAST_NFY_PACKET
{
	unsigned short size;
	char           type;
	ObjID          casterId;
	uint16_t       skillId;
	ObjID          targetId;
	short          fromX, fromY;  // 시전 위치
	short          x, y;          // 목표 지점(돌진·도약이면 도착 칸)
	uint16_t       castMs;        // 예고 시간(이만큼 뒤에 판정)
	uint8_t        flags;         // 예약
};

struct STATUS_NFY_PACKET
{
	unsigned short size;
	char           type;
	ObjID          subjectId;
	uint8_t        status;       // StatusId
	uint8_t        stacks;       // 0 = 풀림
	uint32_t       durationMs;   // 남은 시간
	ObjID          sourceId;
};

// 내 직업·능력치·전투 능력치(로그인·레벨업·장비 변경·포인트 분배 때)
struct USER_STATS_INF_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        classId;
	uint8_t        level;
	BaseStats      base;          // 직업 성장분
	BaseStats      alloc;         // 내가 찍은 포인트
	BaseStats      gear;          // 장비 보너스
	uint16_t       freePoints;
	DerivedStats   stats;
	uint16_t       mp;
};

struct USER_RESOURCE_INF_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       hp, maxhp;
	uint16_t       mp, maxmp;
};

struct STAT_ALLOC_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        stat;        // StatKind
	uint8_t        amount;      // 1~10
};

// 몬스터 강공격 예고 — 클라이언트는 duration 동안 모양을 채워 가며 그린다.
enum class TelegraphShape : uint8_t { Circle, Line, Cone };
struct MONSTER_TELEGRAPH_NFY_PACKET
{
	unsigned short size;
	char           type;
	ObjID          monsterId;
	uint8_t        shape;        // TelegraphShape
	short          x, y;         // 중심(원) 또는 끝점(선)
	short          fromX, fromY; // 시작점(선/부채꼴)
	uint8_t        radius;       // 칸
	uint16_t       durationMs;
	uint8_t        pattern;      // MonsterPattern
};

// ── 살아 있는 마을(NPC) ───────────────────────────────────────────────────────
// 마을 NPC는 서버(마을 Zone 스레드)가 배회시킨다. Sector 오브젝트가 아니라서 레거시
// 세션에는 아무것도 가지 않고, 확장 세션에만 아래 패킷이 간다.
enum class NpcActState : uint8_t { Idle, Walk, Talk };

struct NPC_ENTRY
{
	uint16_t npcId;
	short    x, y;
	uint8_t  state;        // NpcActState
	int8_t   faceX, faceY; // 바라보는 방향(타일, -1~1)
};

inline constexpr int kNpcListMax = 24;
struct NPC_LIST_NFY_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        count;
	NPC_ENTRY      npcs[kNpcListMax];
};

struct NPC_STATE_NFY_PACKET
{
	unsigned short size;
	char           type;
	NPC_ENTRY      npc;
	uint16_t       stepMs;    // 걸음이면 그 걸음 시간(보간), 아니면 0
	ObjID          talkTo;    // 대화 중이면 상대
};

// line: Data/NpcTalk.h barks 번호. 0xFF = 인사(NpcRow.greeting)
inline constexpr uint8_t kNpcGreetLine = 0xFF;
struct NPC_BARK_NFY_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       npcId;
	uint8_t        line;
	ObjID          toUser;    // 인사/대답 상대(없으면 빈 ObjID)
};

struct NPC_TALK_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       npcId;
};

enum class NpcTalkResult : uint8_t { Ok, TooFar, Unknown, Busy };
struct NPC_TALK_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       npcId;
	uint8_t        result;    // NpcTalkResult
};

struct NPC_SERVICE_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       npcId;
	uint8_t        service;   // WorldData::NpcService
};

enum class NpcServiceResult : uint8_t { Ok, TooFar, Cooldown, NoAdena, Full, Invalid };
struct NPC_SERVICE_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       npcId;
	uint8_t        service;
	uint8_t        result;      // NpcServiceResult
	uint32_t       cooldownMs;  // 다시 받을 수 있을 때까지(성공/쿨다운)
};

// ── 아이템 성장 ─────────────────────────────────────────────────────────────
// 슬롯 하나의 V2 인스턴스 값(Data/ItemRules.h ItemInstance::Pack). 슬롯이 바뀔 때마다
// ITEM_SLOT_EX_INF 바로 뒤에 보낸다(빈 슬롯·소모품이면 extra = 0). 로그인 스냅샷
// (ITEM_LIST_EX_ACK) 뒤에는 extra가 0이 아닌 슬롯만 이어서 보낸다.
struct ITEM_DETAIL_INF_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       slotIndex;
	uint16_t       itemId;
	uint64_t       extra;
};

// 주변 플레이어의 착용 외형. items/rarity는 EquipSlot - 1(무기 · 투구 · 갑옷 · 장갑 ·
// 신발 · 망토 · 목걸이 · 반지) 순서. 시야에 들어올 때와 장착이 바뀔 때 보낸다.
inline constexpr int kLookSlots = 8;
struct PLAYER_LOOK_NFY_PACKET
{
	unsigned short size;
	char           type;
	ObjID          id;
	uint16_t       items[kLookSlots];
	uint8_t        rarity[kLookSlots];
	uint8_t        enchant;       // 무기 강화 수치
};

enum class SortMode : uint8_t { Default };
struct ITEM_SORT_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        mode;          // SortMode
};

// ── 퀘스트 · 메인 이야기(Data/QuestData.h) ───────────────────────────────────
// 진행도는 목표마다 하나(QuestData::kMaxObjs). "목표를 다 이뤘나"는 표와 진행도로 양쪽이
// 같은 식(QuestData::ObjectivesDone)으로 계산한다 — 따로 상태를 싣지 않는다.
struct QUEST_ENTRY
{
	uint16_t questId;
	uint16_t progress[3];
};

struct QUEST_COOLDOWN
{
	uint16_t questId;
	uint32_t remainSec;       // 반복 퀘스트를 다시 받을 수 있을 때까지(초)
};

inline constexpr int kQuestListActiveMax = 8;     // = QuestData::kMaxActive
inline constexpr int kQuestListCooldownMax = 8;
struct QUEST_LIST_INF_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        activeCount;
	QUEST_ENTRY    active[kQuestListActiveMax];
	uint8_t        done[32];                         // 끝낸 퀘스트(한 번뿐인 것) id 비트 1~255
	uint8_t        cooldownCount;
	QUEST_COOLDOWN cooldowns[kQuestListCooldownMax];
};

enum class QuestEvent : uint8_t
{
	Accepted,    // 받음(진행도는 받는 순간의 값 — 이미 입고 있는 장비 등)
	Progress,    // 목표 하나가 늘었다(objIndex, x/y = 계기가 된 곳 — 쓰러진 몬스터 자리)
	Completed,   // 끝(보상은 아래 필드). 반복 퀘스트면 cooldownSec
	Abandoned,
};

struct QUEST_UPDATE_INF_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       questId;
	uint8_t        event;          // QuestEvent
	uint8_t        objIndex;       // Progress: 바뀐 목표(0~2), 그 밖은 0xFF
	uint16_t       progress[3];
	short          x, y;           // Progress: 계기가 된 칸(없으면 -1)
	// Completed: 받은 보상
	uint32_t       exp;
	uint32_t       adena;
	uint16_t       itemId;
	uint16_t       itemCount;
	uint8_t        rarity;         // 장비 보상의 희귀도(Rarity)
	uint32_t       cooldownSec;    // 반복 퀘스트: 다시 받을 수 있을 때까지
};

struct QUEST_ACCEPT_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       questId;
};

struct QUEST_COMPLETE_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       questId;
};

struct QUEST_ABANDON_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       questId;
};

enum class QuestAction : uint8_t { Accept, Complete, Abandon };
enum class QuestResult : uint8_t
{
	Ok,
	Unknown,        // 없는 퀘스트 · 진행 중이 아닌 퀘스트
	TooFar,         // 주는/보고할 NPC 곁이 아니다
	NotAvailable,   // 이미 받았거나 끝냈다 · 선행 퀘스트를 안 끝냈다
	LevelTooLow,    // param = 필요 레벨
	WrongClass,
	LogFull,        // 진행 중인 퀘스트가 가득(QuestData::kMaxActive)
	NotReady,       // 목표를 아직 다 이루지 못했다
	InventoryFull,  // 보상을 받을 자리가 없다(아무것도 바뀌지 않는다)
	Cooldown,       // param = 남은 초
	Busy,           // 사망 · 존 이동 중
};

struct QUEST_RESULT_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       questId;
	uint8_t        action;         // QuestAction
	uint8_t        result;         // QuestResult
	uint32_t       param;
};

// ── Phase 2-B: 기술 구조 V3 ────────────────────────────────────────────────
enum class SkillPhase : uint8_t { Press, Release, Cancel };
enum SkillReqFlag : uint8_t { SKILLREQ_FORCE = 1 << 0 };   // 강제 공격(Ctrl) — 다른 플레이어도 맞힌다

// C→S. 칸 · 유형 · 타격 시각은 SkillDefTable(Data/SkillData.h) — 서버가 판정한다.
struct SKILL_EX_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       skillId;
	uint8_t        phase;        // SkillPhase
	uint8_t        flags;        // SkillReqFlag
	ObjID          targetId;     // 커서에 가장 가까운 적(대상형 · 조준 보조, 없으면 비움)
	short          aimX, aimY;   // 커서 칸(서버가 사거리로 자른다)
	uint8_t        dir;          // 화면 각 0~255(표시용 — 판정 방향은 서버가 aim에서 다시 계산)
	uint16_t       seq;
};

enum class SkillExPhase : uint8_t
{
	Start,        // 일반 · 캐스팅 · 각성기 · 이동기 · 콤보 한 타(stage = 몇 타)
	ChargeBegin,  // 차지 시작(모으는 자세)
	ChargeRelease,// 차지 놓음(stage = 단계, flags 완벽)
	HoldBegin,    // 홀딩 시작
	HoldEnd,      // 홀딩 끝(놓음 · 최대 시간)
	ChainStep,    // 체인 단(stage = 몇 단)
	ToggleOn,
	ToggleOff,
	Cancel,       // 끊김(이동기 · 기절)
	Identity,     // 고유 기술 발동
};
// 비트 1~2 = 켜진 갈래 Ⅰ(0 없음 · 1 · 2), 3~4 = 갈래 Ⅱ — SkillGrowth::NfyBits · FromNfy(Phase 2-D)
enum SkillExFlag : uint8_t { SKILLEX_PERFECT = 1 << 0, SKILLEX_VARIANT_MASK = 0x1E };

// S→C 주변 시전 알림 — 같은 타격 시각표로 연출을 재생한다
struct SKILL_EX_NFY_PACKET
{
	unsigned short size;
	char           type;
	ObjID          casterId;
	uint16_t       skillId;
	uint8_t        phase;        // SkillExPhase
	uint8_t        stage;
	uint8_t        flags;        // SkillExFlag
	uint8_t        dir;          // 서버가 정한 방향(화면 각 0~255)
	short          fromX, fromY; // 시전 위치
	short          aimX, aimY;   // 조준 칸(돌진 · 도약이면 도착 칸)
	ObjID          targetId;
	uint16_t       castMs;
	uint32_t       serverTime;   // GetNowTime(ms) — 연출 시각표 기준
};

enum class ForcedMoveKind : uint8_t { Dash, Blink, Knockback, Pull, Roll, Launch };
struct SUBJECT_FORCED_MOVE_NFY_PACKET
{
	unsigned short size;
	char           type;
	ObjID          id;
	short          fromX, fromY;
	short          toX, toY;
	uint16_t       durationMs;
	uint8_t        kind;         // ForcedMoveKind
};

enum CombatGaugeFlag : uint8_t { GAUGE_FULL = 1 << 0, GAUGE_ACTIVE = 1 << 1 };
struct COMBAT_GAUGE_INF_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       identity;     // 0~1000
	uint8_t        flags;        // CombatGaugeFlag
	uint32_t       activeMs;     // 고유 기술이 켜져 있으면 남은 시간
};

struct MONSTER_GAUGE_NFY_PACKET
{
	unsigned short size;
	char           type;
	ObjID          monsterId;
	uint16_t       stagger;      // 남은 무력화 게이지 ‰(1000 = 가득)
	uint8_t        state;        // 0 보통 · 1 무력화 중
	uint16_t       brokenMs;     // 무력화 남은 시간
};

// ── Phase 2-D: 기술 성장(Data/SkillGrowth.h) ─────────────────────────────────
// 기술 하나 = id + 상태 한 바이트(SkillGrowth::Pack — 레벨-1 · 갈래 Ⅰ · 갈래 Ⅱ)
struct SkillGrowEntry
{
	uint16_t skillId;
	uint8_t  state;
};

// S→C 본인. 직업의 자라는 기술 여덟 개 전부(QWERASDF 기본 배치 순서) — 로그인 · 레벨 업 · 초기화 때
struct SKILL_TREE_INF_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       points;       // 남은 기술 점수
	uint16_t       total;        // 캐릭터 레벨로 받은 점수 전체
	uint8_t        count;
	SkillGrowEntry skills[8];
};

enum class SkillGrowOp : uint8_t { LevelUp, LevelDown, Pick, ResetAll };
enum class SkillGrowResult : uint8_t
{
	Ok,
	Unknown,       // 이 직업의 자라는 기술이 아니다
	Locked,        // 아직 배우지 않은 기술(param = 필요 레벨)
	MaxLevel,
	MinLevel,
	NoPoints,      // param = 필요한 점수
	TierLocked,    // 갈래를 고를 레벨이 아니다(param = 필요한 기술 레벨)
	InCombat,      // 싸우는 중(마지막 공격 · 피격 5초 안) · 시전 중에는 바꿀 수 없다
	Busy,          // 사망 · 존 이동 중
};

// C→S. seq가 직전과 같으면 다시 처리하지 않고 결과만 다시 보낸다(재전송 멱등)
struct SKILL_GROW_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        op;           // SkillGrowOp
	uint16_t       skillId;
	uint8_t        tier;         // Pick: 0 = 갈래 Ⅰ(레벨 4) · 1 = 갈래 Ⅱ(레벨 7)
	uint8_t        choice;       // Pick: 0 = 끄기 · 1 · 2
	uint16_t       seq;
};

struct SKILL_GROW_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       seq;
	uint8_t        op;           // SkillGrowOp
	uint8_t        result;       // SkillGrowResult
	uint16_t       param;
	SkillGrowEntry skill;        // 바뀐 뒤 상태(초기화면 skillId 0 — SKILL_TREE_INF가 뒤따른다)
	uint16_t       points;       // 남은 점수
};

// ── Phase 3-B: 채널 채팅(확장 세션 전용) ──────────────────────────────────────
// 받는 쪽은 기존 SC_CHAT_EX(168B) 그대로 — channel에 Trade · Guild · WhisperIn · WhisperOut 값만 끝에 추가했다.
// 서버: 소유 Zone 스레드에서 검사(토큰 버킷 5줄/5초 · 같은 말 되풀이 · UTF-8 · 채널별 재사용 대기) →
//   일반 = 같은 Zone(지역) 전체 확장 세션 + 시야 안 레거시 세션(SC_CHAT) / 전체 · 거래 = 서버 전체 확장 세션 /
//   귓속말 = 이름 색인(GameSessionManager) / 결사 = Phase 5.
struct CS_CHAT_EX_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        channel;                 // ChatChannel(Normal · World · Trade · Guild · WhisperOut=귓속말 보내기)
	char           targetName[NAME_SIZE];   // 귓속말 받는 사람(그 밖에는 비움)
	char           mess[CHAT_SIZE];         // UTF-8, 널 종료(128바이트 ≈ 한글 42자)
};

static_assert(sizeof(SKILL_TREE_INF_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(SKILL_GROW_REQ_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(SKILL_GROW_ACK_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(static_cast<int>(PacketType::SKILL_GROW_ACK) == 87, "Phase 2-D 뒤 PacketType은 88개(0~87)");
static_assert(static_cast<int>(PacketType::SKILL_GROW_ACK) < 127, "패킷 헤더 type은 char — 127 안에서");
static_assert(sizeof(CS_CHAT_EX_PACKET) == 152 && sizeof(CS_CHAT_EX_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(static_cast<int>(PacketType::CS_CHAT_EX) == 88, "Phase 3-B 뒤 PacketType은 89개(0~88) — 127까지 38개 남음");

static_assert(sizeof(SKILL_EX_REQ_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(SKILL_EX_NFY_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(SUBJECT_FORCED_MOVE_NFY_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(COMBAT_GAUGE_INF_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(MONSTER_GAUGE_NFY_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(static_cast<int>(PacketType::MONSTER_GAUGE_NFY) < 127, "패킷 헤더 type은 char — 127 안에서");

static_assert(sizeof(USER_STATS_INF_PACKET) <= 255, "STRESS_TEST 수신 버퍼(255)를 넘으면 안 된다");
static_assert(sizeof(QUEST_LIST_INF_PACKET) <= 255, "클라 수신 한 패킷(255) 안에 들어가야 한다");
static_assert(sizeof(PLAYER_LOOK_NFY_PACKET) <= 255, "클라 수신 한 패킷(255) 안에 들어가야 한다");
static_assert(sizeof(NPC_LIST_NFY_PACKET) <= 255, "클라 수신 한 패킷(255) 안에 들어가야 한다");

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5: 결사 · 경매장(docs/adr/0001-guild-auction-services.md) — 확장 세션 전용 묶음 패킷.
// 요청은 op 하나로 가르고, 필드는 op마다 쓰는 것만 채운다(나머지 0). 모든 요청에 seq(멱등 — 같은 순번은 한 번만).
// 규칙 · 한도는 Data/GuildAuction.h. 서버는 받은 Zone(소유 스레드)에서 검사한 뒤 서비스(GuildService · AuctionService) 큐로 넘긴다.
// ─────────────────────────────────────────────────────────────────────────────
enum class GuildOp : uint8_t
{
	Info,          // 내 결사 정보(GUILD_INF Info)
	Members,       // 구성원 페이지 arg8 = 페이지, arg32 = 정렬(0 접속·직위 · 1 레벨 · 2 이름 · 3 최근 접속)
	Create,        // 세우기 text = 이름, arg8 = 문장 — 결사 회관지기 곁에서
	Invite,        // 초대 target = 이름
	InviteReply,   // 초대에 답 arg8 = 1 수락 / 0 거절, arg32 = 결사 번호
	Leave,         // 탈퇴(결사장은 넘긴 뒤에)
	Kick,          // 추방 target
	SetRank,       // 직위 target, arg8 = GuildRule::Rank
	Transfer,      // 결사장 넘기기 target
	Notice,        // 공지 text
	Disband,       // 해산(결사장 혼자 남았을 때)
	Name,          // 결사 이름 묻기 arg32 = 결사 번호(머리 위 표시 캐시)
	Emblem,        // 문장 바꾸기 arg8 — 결사 회관지기 곁에서
};
enum class GuildResult : uint8_t
{
	Ok, NotInGuild, AlreadyInGuild, NoPermission, NameTaken, NameInvalid, NoAdena, LevelTooLow, Full,
	TargetNotFound, TargetInGuild, InviteExpired, NotAtNpc, Busy, Invalid, Duplicate, LeaderMustTransfer, NotEmpty,
};
struct GUILD_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        op;                      // GuildOp
	uint32_t       seq;
	uint8_t        arg8;
	uint32_t       arg32;
	char           target[NAME_SIZE];       // 대상 사람 이름(ASCII)
	char           text[92];                // 결사 이름(UTF-8, 한글 8자) · 공지(한글 30자) — 널 종료
};
struct GUILD_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        op;                      // GuildOp
	uint8_t        result;                  // GuildResult
	uint32_t       seq;
	uint32_t       param;                   // 세우기: 결사 번호 · 그로시 부족: 필요한 값
};

enum class GuildInfKind : uint8_t { Info, Members, Name };
struct GUILD_INFO_INF_PACKET                // GUILD_INF, kind = Info — 결사가 없으면 guildId 0
{
	unsigned short size;
	char           type;
	uint8_t        kind;
	uint32_t       guildId;
	uint8_t        emblem;
	uint8_t        myRank;                  // GuildRule::Rank
	uint8_t        members;
	uint8_t        online;
	char           name[25];
	char           notice[92];
	char           leader[NAME_SIZE];
};
struct GuildMemberRow
{
	char     name[NAME_SIZE];
	uint8_t  rank;
	uint8_t  level;
	uint8_t  classId;
	uint8_t  online;
	uint32_t lastSeenMin;                   // 마지막 접속(서버 시작 기준이 아니라 유닉스 분 — 접속 중이면 0)
};
struct GUILD_MEMBERS_INF_PACKET             // GUILD_INF, kind = Members — 8명씩
{
	unsigned short size;
	char           type;
	uint8_t        kind;
	uint8_t        page;
	uint8_t        pages;
	uint8_t        count;
	GuildMemberRow rows[8];
};
struct GUILD_NAME_INF_PACKET                // GUILD_INF, kind = Name — 머리 위 표시 캐시
{
	unsigned short size;
	char           type;
	uint8_t        kind;
	uint32_t       guildId;
	uint8_t        emblem;
	char           name[25];
};

enum class GuildEvent : uint8_t
{
	InviteFrom,    // who = 초대한 사람, name = 결사 이름, guildId · emblem
	Joined, Left, Kicked,                   // who(, by)
	RankChanged,   // who · rank · by
	LeaderChanged, // who = 새 결사장
	Online, Offline,
	Disbanded,     // name
	Created,       // 월드 공지: name · who(결사장) · emblem — 모두에게
	Tag,           // 머리 위 표시: obj · guildId · emblem(0 번호 = 결사 없음) — 주변 확장 세션에
	NoticeChanged, // by
	InviteDeclined, // who = 거절한 사람(초대한 사람에게)
	EmblemChanged,
};
struct GUILD_NFY_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        event;                   // GuildEvent
	uint8_t        emblem;
	uint8_t        rank;
	uint32_t       guildId;
	ObjID          obj;
	char           who[NAME_SIZE];
	char           by[NAME_SIZE];
	char           name[25];
};

enum class AuctionOp : uint8_t
{
	Search,        // cat · sub · minRarity · minEnchant · minLevel · maxLevel · option · sort · page · query
	Register,      // slot · count · price · duration(0~2) — 경매인 곁에서
	Buy,           // listingId · price(보이던 값 — 그 사이 바뀌면 거절)
	Cancel,        // listingId
	Mine,          // 내 경매 page
	Settlements,   // 정산함 page
	Claim,         // settlementId(0 = 맨 앞 하나) — "모두 받기"는 하나씩 이어서
	PriceHint,     // itemId · minRarity — 현재 최저가 · 최근 거래가
};
enum class AuctionResult : uint8_t
{
	Ok, NotFound, Sold, NoAdena, TooMany, BadPrice, BadItem, Equipped, InventoryFull, NotYours, NotAtNpc, Busy, Invalid, Duplicate,
};
struct AUCTION_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        op;                      // AuctionOp
	uint32_t       seq;
	// 검색
	uint8_t        cat, sub, minRarity, minEnchant, minLevel, maxLevel, option, sort;
	uint16_t       page;
	char           query[26];
	// 등록 · 구매 · 취소 · 받기 · 시세
	uint16_t       slot;
	uint16_t       count;
	uint32_t       price;
	uint8_t        duration;
	uint32_t       id;                      // listingId · settlementId
	uint16_t       itemId;
};
struct AUCTION_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        op;                      // AuctionOp
	uint8_t        result;                  // AuctionResult
	uint32_t       seq;
	uint32_t       id;                      // 목록 · 정산함 번호
	uint32_t       adena;                   // 요청 뒤 내 그로시(바뀌었으면)
	int32_t        delta;
	uint16_t       itemId;
	uint16_t       count;
};
enum class AuctionListKind : uint8_t { Search, Mine, Settlements };
struct AuctionRow
{
	uint32_t id;                            // 목록 번호 · 정산함 번호
	uint16_t itemId;                        // 정산함 대금이면 0
	uint16_t count;
	uint8_t  enchant;
	uint64_t extra;                         // ItemInstance(희귀도 · 옵션)
	uint32_t price;                         // 값 · 정산함 대금
	uint16_t minutesLeft;                   // 남은 시간(분) · 정산함: 사유(AuctionSettleReason)
	uint8_t  flags;                         // 1 = 내 목록 · 2 = 거래 중
};
enum class AuctionSettleReason : uint8_t { Sold, Bought, Cancelled, Expired };
struct AUCTION_LIST_INF_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        kind;                    // AuctionListKind
	uint16_t       page;
	uint16_t       pages;
	uint32_t       total;
	uint8_t        count;
	AuctionRow     rows[6];
};
enum class AuctionEvent : uint8_t { Sold, Expired, SettleCount, PriceHint, Listed };
struct AUCTION_NFY_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        event;                   // AuctionEvent
	uint32_t       id;
	uint16_t       itemId;
	uint32_t       price;                   // 팔린 값 · 정산 받을 대금
	uint16_t       settleCount;             // 받지 않은 정산함 행 수
	uint32_t       lowest;                  // 시세: 지금 최저가(없으면 0)
	uint32_t       lastSold;                // 시세: 마지막 거래가
	uint32_t       avgSold;                 // 시세: 최근 평균
	uint16_t       soldCount;               // 시세: 최근 거래 수
};

static_assert(sizeof(GUILD_REQ_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(GUILD_ACK_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(GUILD_INFO_INF_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(GUILD_MEMBERS_INF_PACKET) <= 255, "모든 패킷 255바이트 이하 — 구성원 8명씩");
static_assert(sizeof(GUILD_NAME_INF_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(GUILD_NFY_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(AUCTION_REQ_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(AUCTION_ACK_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(AUCTION_LIST_INF_PACKET) <= 255, "모든 패킷 255바이트 이하 — 6줄씩");
static_assert(sizeof(AUCTION_NFY_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(AuctionRow) == 24, "경매 한 줄 24바이트(6줄 = 144B)");
static_assert(static_cast<int>(PacketType::AUCTION_NFY) == 96, "Phase 5 뒤 PacketType은 97개(0~96) — 127까지 30개 남음");
static_assert(static_cast<int>(PacketType::AUCTION_NFY) < 127, "패킷 헤더 type은 char — 127 안에서");

#pragma pack (pop)

// ── Client → Server 최대 크기(수신 버퍼) ─────────────────────────────────────
namespace ClientPacketSizeDetailV2
{
	constexpr size_t v01 = ProtocolConstMaxSize(MAX_CLIENT_PACKET_SIZE_V1, sizeof(USER_MOVE_EX_REQ_PACKET));
	constexpr size_t v02 = ProtocolConstMaxSize(v01, sizeof(SKILL_CAST_REQ_PACKET));
	constexpr size_t v03 = ProtocolConstMaxSize(v02, sizeof(STAT_ALLOC_REQ_PACKET));
	constexpr size_t v04 = ProtocolConstMaxSize(v03, sizeof(NPC_TALK_REQ_PACKET));
	constexpr size_t v05 = ProtocolConstMaxSize(v04, sizeof(NPC_SERVICE_REQ_PACKET));
	constexpr size_t v06 = ProtocolConstMaxSize(v05, sizeof(ITEM_SORT_REQ_PACKET));
	constexpr size_t v07 = ProtocolConstMaxSize(v06, sizeof(QUEST_ACCEPT_REQ_PACKET));
	constexpr size_t v08 = ProtocolConstMaxSize(v07, sizeof(QUEST_COMPLETE_REQ_PACKET));
	constexpr size_t v09 = ProtocolConstMaxSize(v08, sizeof(QUEST_ABANDON_REQ_PACKET));
	constexpr size_t v10 = ProtocolConstMaxSize(v09, sizeof(SKILL_EX_REQ_PACKET));
	constexpr size_t v11 = ProtocolConstMaxSize(v10, sizeof(SKILL_GROW_REQ_PACKET));
	constexpr size_t v12 = ProtocolConstMaxSize(v11, sizeof(CS_CHAT_EX_PACKET));
	constexpr size_t v13 = ProtocolConstMaxSize(v12, sizeof(GUILD_REQ_PACKET));
	constexpr size_t v14 = ProtocolConstMaxSize(v13, sizeof(AUCTION_REQ_PACKET));
}
constexpr size_t MAX_CLIENT_PACKET_SIZE = ClientPacketSizeDetailV2::v14;
