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

static_assert(sizeof(USER_STATS_INF_PACKET) <= 255, "STRESS_TEST 수신 버퍼(255)를 넘으면 안 된다");
static_assert(sizeof(QUEST_LIST_INF_PACKET) <= 255, "클라 수신 한 패킷(255) 안에 들어가야 한다");
static_assert(sizeof(PLAYER_LOOK_NFY_PACKET) <= 255, "클라 수신 한 패킷(255) 안에 들어가야 한다");
static_assert(sizeof(NPC_LIST_NFY_PACKET) <= 255, "클라 수신 한 패킷(255) 안에 들어가야 한다");

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
}
constexpr size_t MAX_CLIENT_PACKET_SIZE = ClientPacketSizeDetailV2::v09;
