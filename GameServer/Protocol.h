#pragma once

#include "EnumCategory.h"
#include "ContentID.h"
#include "ObjID.h"

constexpr int PORT_NUM = 4000;

constexpr int NAME_SIZE     = 20;
constexpr int PASSWORD_SIZE = 20;
constexpr int CHAT_SIZE     = 128;
// 플레이어/몬스터 이름(NAME_SIZE)과 우연히 값이 같아도 별개 상수로 둔다 —
// 훗날 둘 중 하나만 길이를 바꿔야 할 때 서로 영향을 주지 않기 위함.
constexpr int ITEM_NAME_SIZE = 20;

constexpr int MAX_USER    = 40000;
constexpr int MAX_MONSTER = 200000;

// 유저 한 명이 가질 수 있는 인벤토리 슬롯 수. 다른 구조에 영향을 주지 않는
// 단일 상수이므로, 게임 디자인이 확정되면 이 값만 바꾸면 된다.
constexpr int MAX_INVENTORY_SLOTS = 30;

constexpr uint32_t PLAYER_ID_START   = 1;
constexpr uint32_t MONSTER_ID_START  = 1'000'000'000;
constexpr uint32_t AGGRO_MONSTER_BOUNDARY = MONSTER_ID_START + static_cast<uint32_t>(MAX_MONSTER / 4) - 1;  // First 25% of monsters are AGGRO

inline constexpr bool IsPlayerObjectId(uint32_t id) noexcept
{
	return id >= PLAYER_ID_START && id < MONSTER_ID_START;
}

inline constexpr bool IsMonsterObjectId(uint32_t id) noexcept
{
	return id >= MONSTER_ID_START;
}

constexpr int W_WIDTH  = 2000;
constexpr int W_HEIGHT = 2000;

constexpr int SECTOR_RANGE = 10;

constexpr int VIEW_RANGE            = 5;   // 레거시 기본값(참고용). 실제 시야는 런타임 값 GViewRange(ServerGlobal.h)
constexpr int ATTACK_RANGE          = 1;
constexpr int WAKE_RANGE            = 3;   // aggro monster wakes up when player is within this many tiles
constexpr int ZONE_BOUNDARY_MARGIN  = 3;   // aggro monster turns back when within this many tiles of zone boundary

constexpr int PLAYER_MAX_HP  = 100;
constexpr int MONSTER_MAX_HP = 60;

constexpr int PLAYER_OFFENSIVE  = 10;
constexpr int SKILL_DAMAGE      = 50;
constexpr int MONSTER_OFFENSIVE = 3;
constexpr int HEAL_SIZE         = 10;

constexpr int BUF_SIZE = 1024;

enum MONSTER_TYPE
{
	AGGRO,
	PASSIVE
};

// 상황별로 새 패킷을 계속 늘리는 대신, 서버가 클라이언트에 보내는 상황 알림을
// 코드값 하나로 표현하는 공용 포맷(SYSTEM_MESSAGE_INF_PACKET)에서 쓰는 코드다.
// 실제 문구는 클라이언트가 code 기준으로 자체 보유한 문자열 테이블에서 찾아
// 렌더링한다(서버가 언어별 문자열을 들고 있을 필요가 없다). 새 상황이 생기면
// 이 자리에 값만 추가하면 되고, 기존 값은 절대 재사용/변경하지 않는다.
enum class SystemMessageCode : uint16_t
{
	// 인벤토리가 가득 차 아이템을 얻지 못함. param1=놓친 아이템의 ItemTableId,
	// param2=놓친 수량.
	InventoryFull,

	// ── 이하 확장 프로토콜(USER_LOGIN_EX_REQ로 접속한 세션)에만 보낸다 ──────────
	// 레거시 클라이언트/STRESS_TEST 봇은 아래 코드를 받을 일이 없다.
	ExpLost,             // 사망으로 경험치 감소. param1=잃은 양
	NotEnoughAdena,      // param1=필요 그로시, param2=보유 그로시
	SafeZoneNoCombat,    // 안전지대에서는 공격할 수 없음
	ItemDroppedOnDeath,  // 악명 사망으로 아이템을 떨어뜨림. param1=ItemTableId, param2=강화 수치
	BecamePurple,        // 먼저 공격해 보라색 이름이 됨(정당방위 대상). param1=지속 ms
	BecameChaotic,       // PK로 성향치가 음수가 됨. param1=현재 성향치
	HasteStart,          // param1=지속 초
	HasteEnd,
	ReturnedToTown,
	Teleported,          // param1=도착 지역(zoneId)
	LevelUp,             // param1=새 레벨
	TargetInvalid,       // 공격/강화 대상이 없거나 잘못됨
	TargetTooFar,        // 대상이 너무 멀리 있음
	ItemCooldown,        // param1=남은 ms
	CannotUseHere,       // 여기서는 사용할 수 없음(예: 마을에서 옛길 두루마리)
	AlignmentRecovered,  // 성향치가 다시 0 이상이 됨. param1=현재 성향치
};

// 확장 프로토콜 버전. 새 클라이언트는 USER_LOGIN_EX_REQ에 이 값을 실어 보낸다.
constexpr uint16_t PROTOCOL_EX_VERSION = 2;

// EFFECT_NFY의 effectId. "서버는 무슨 일이 일어났는지(어디서 누가)만 알리고,
// 파티클/사운드/흔들림 같은 표현은 전부 클라이언트가 정한다" — 서버가 이펙트의
// 모양을 아는 순간 설계가 틀린 것이다.
enum class EffectId : uint16_t
{
	None,
	SkillCross,       // 십자베기. subject=시전자, x/y=시전 위치
	LevelUp,          // param=새 레벨
	PotionHeal,       // param=회복량
	TeleportOut,      // 사라지는 위치
	TeleportIn,       // 나타나는 위치
	HasteOn,
	EnchantSuccess,   // param=새 강화 수치
	EnchantFail,      // param=증발하기 전 강화 수치
	BossSlamCast,     // x/y=중심, param=반경(칸). 곧 내려찍는다는 경고
	BossSlamImpact,   // x/y=중심, param=반경
	PlayerKill,       // subject=PK한 플레이어
	Restart,          // 마을에서 다시 시작
	BossSpawn,        // subject=보스
	// V2
	DiscoverRegion,   // param=지역(zoneId) — 새 지역 발견(본인에게)
	RuneExplosion,    // x/y=중심 — 옛길 도약이 남긴 각인 폭발
	SteamBurst,       // x/y=중심 — 증발 폭발
	Counter,          // subject=반격한 기사
	WallSlam,         // x/y=부딪힌 칸
	PoisonBurst,      // subject=대상
	PriestHeal,       // subject=치유받은 플레이어, param=회복량
	// V2 아이템 특수 효과(Data/ItemRules.h ItemProc) — subject=장비 주인, x/y=효과 위치
	ProcThunder,      // 천둥의 일격(벼락이 떨어진 칸)
	ProcLifesteal,    // 생명 흡수(param=회복량)
	ProcFrost,        // 서리 손길(x/y=대상)
	ProcExecute,      // 처형자(x/y=대상)
	ProcEcho,         // 메아리(x/y=대상)
	ProcGuardian,     // 서약의 방패(주인)
	ProcEmber,        // 잔불(x/y=대상)
	ProcVenom,        // 맹독(x/y=대상)
	// Phase 2-B(끝에만 추가)
	StaggerBreak,     // subject=보스, x/y=보스 — 무력화(게이지가 비었다)
	OathRelease,      // subject=기사 — 서약 해방
	Resonance,        // subject=각인술사, x/y=폭발 칸, param=순서 — 새김 울림 연쇄 폭발
	PerfectDodge,     // subject=밤사냥꾼 — 완벽 회피(무적 중 공격을 피했다)
	CloneStrike,      // subject=밤사냥꾼, x/y=대상 — 분신이 따라 벤다
	Taunted,          // subject=기사, param=도발한 수
};

// WORLD_NOTICE_INF의 code — 전 서버(확장 세션)에 띄우는 공지.
enum class WorldNoticeCode : uint16_t
{
	BossSpawned,      // param1=보스 종족 id, param2=지역(zoneId)
	BossKilled,       // param1=보스 종족 id, param2=지역, name=막타 플레이어
	EnchantSuccess,   // param1=ItemTableId, param2=강화 수치, name=플레이어
	PlayerKilled,     // param1=지역, name=PK한 플레이어
	RareLoot,         // V2: param1=ItemTableId, param2=희귀도(Rarity), name=주운 플레이어 — 신화 획득
	StoryComplete,    // V2: param1=퀘스트 id(메인 이야기 마지막), name=플레이어 — 하르벤이 다시 뛴다
};

enum class PacketType : uint16_t
{
	// Client → Server (REQ)
	USER_LOGIN_REQ,
	USER_MOVE_REQ,
	USER_ATTACK_REQ,
	USER_SKILL_REQ,
	USER_TELEPORT_REQ,
	ITEM_EQUIP_REQ,
	ITEM_UNEQUIP_REQ,
	ITEM_SWAP_REQ,
	ITEM_DISCARD_REQ,
	ITEM_PICKUP_REQ,
	ITEM_USE_REQ,

	// Server → Client, unicast ACK (response to requester only)
	USER_LOGIN_ACK,
	USER_LOGIN_FAIL_ACK,
	USER_ATTACK_ACK,
	ITEM_LIST_ACK,
	ITEM_EQUIP_ACK,
	ITEM_UNEQUIP_ACK,
	ITEM_SWAP_ACK,
	ITEM_DISCARD_ACK,
	ITEM_PICKUP_ACK,
	ITEM_USE_ACK,

	// Server → Client(s), NFY (server-initiated notification / broadcast)
	SUBJECT_ADD_NFY,
	SUBJECT_MOVE_NFY,
	SUBJECT_REMOVE_NFY,
	SUBJECT_DIE_NFY,
	SUBJECT_RESPAWN_NFY,
	SUBJECT_ATTACK_NFY,
	PLAYER_ATTACK_NFY,
	SUBJECT_EQUIP_CHANGE_NFY,

	// Server → Client(s), INF (Server-only notifications)
	USER_HEAL_INF,
	USER_STAT_CHANGE_INF,
	ITEM_ACQUIRE_INF,
	SYSTEM_MESSAGE_INF,

	// Chat
	CS_CHAT,
	SC_CHAT,

	// ── 확장 프로토콜(리니지풍 컨텐츠) ──────────────────────────────────────────
	// 기존 값의 번호가 한 칸도 밀리지 않도록 반드시 끝에만 추가한다 — STRESS_TEST와
	// 레거시 클라이언트는 아래 타입을 보내지도, 받지도 않는다(모르는 타입은 무시).
	// Client → Server
	USER_LOGIN_EX_REQ,        // 확장 로그인: 이 세션은 확장 패킷을 받는다
	USER_RESTART_REQ,         // 사망 상태에서 마을로 리스타트
	USER_TARGET_ATTACK_REQ,   // 대상 지정 공격(몬스터/플레이어 공통, PK 포함)
	ITEM_ENCHANT_REQ,         // 각인 두루마리 사용
	SHOP_BUY_REQ,
	SHOP_SELL_REQ,
	NPC_TELEPORT_REQ,

	// Server → Client (확장 세션에만)
	USER_INFO_EX_INF,         // 내 확장 정보(그로시/성향치/공격력/다음 레벨 경험치 등)
	SUBJECT_INFO_EX_NFY,      // 주변 플레이어의 레벨/성향치/상태/무기 강화 수치
	ITEM_LIST_EX_ACK,         // 강화 수치를 포함한 인벤토리 전체 스냅샷
	ITEM_SLOT_EX_INF,         // 슬롯 하나의 최신 상태(모든 슬롯 변경에서 전송)
	ADENA_INF,
	ENCHANT_RESULT_ACK,
	SHOP_RESULT_ACK,          // 구매/판매/텔레포트 결과
	SUBJECT_DAMAGE_NFY,       // 누가 누구에게 얼마를(치명타/스킬 여부 포함)
	EFFECT_NFY,               // 이펙트 트리거(EffectId)
	WORLD_NOTICE_INF,         // 전 서버 공지(보스 출현/처치, 고강화 성공)
	SC_CHAT_EX,               // 채널 채팅(전체 채팅 등 — 보낸 사람 이름 포함)

	// ── V2: Dark Fantasy Storybook 고도화(ProtocolV2.h) ──────────────────────────
	// 여기도 끝에만 추가한다. 패킷 타입은 헤더의 1바이트(uint8_t로 읽는다)라 255까지 쓸 수 있다.
	USER_MOVE_EX_REQ,         // C→S 순번 달린 이동(토큰 버킷 검증)
	USER_MOVE_EX_ACK,         // S→C 이동 결과(순번·확정 좌표) — 거절도 즉시 응답
	SERVER_CONFIG_INF,        // S→C 시야 범위·기본 걸음 시간 등 서버 설정
	// 전투 V2(직업·스킬·상태이상)
	SKILL_CAST_REQ,           // C→S 스킬/기본 공격 시전
	SKILL_RESULT_ACK,         // S→C 시전 결과(쿨다운·MP) — 시전자에게
	SKILL_CAST_NFY,           // S→C 시전 알림(모션·예고·이펙트) — 주변에
	STATUS_NFY,               // S→C 상태이상 걸림/풀림 — 주변에
	USER_STATS_INF,           // S→C 내 직업·능력치·전투 능력치
	USER_RESOURCE_INF,        // S→C 내 HP/MP
	STAT_ALLOC_REQ,           // C→S 능력치 포인트 분배
	MONSTER_TELEGRAPH_NFY,    // S→C 몬스터 강공격 예고(모양·시간) — 피할 수 있다
	// 살아 있는 마을(서버가 배회시키는 NPC · 대화 · 서비스)
	NPC_LIST_NFY,             // S→C 마을 NPC 전체 위치/상태(마을에 들어올 때)
	NPC_STATE_NFY,            // S→C NPC 한 걸음 · 멈춤 · 대화 상태
	NPC_BARK_NFY,             // S→C NPC 혼잣말/인사(말풍선) — 모두가 같은 말을 본다
	NPC_TALK_REQ,             // C→S NPC에게 말 걸기(거리 확인 → NPC가 멈춰서 바라본다)
	NPC_TALK_ACK,             // S→C 말 걸기 결과
	NPC_SERVICE_REQ,          // C→S 사제의 치유 기도 · 여관 수프
	NPC_SERVICE_ACK,          // S→C 서비스 결과
	// 아이템 성장(희귀도 · 옵션 · 세트 · 외형)
	ITEM_DETAIL_INF,          // S→C 슬롯 하나의 인스턴스 값(희귀도·옵션) — ITEM_SLOT_EX_INF 바로 뒤에
	PLAYER_LOOK_NFY,          // S→C 주변 플레이어의 착용 외형(부위별 아이템·희귀도)
	ITEM_SORT_REQ,            // C→S 소지품 정리
	// 퀘스트 · 메인 이야기(Data/QuestData.h)
	QUEST_LIST_INF,           // S→C 진행 중 · 완료 · 반복 대기 전체(로그인 때 한 번)
	QUEST_UPDATE_INF,         // S→C 퀘스트 하나의 변화(받음 · 진행 · 완료 · 포기)
	QUEST_ACCEPT_REQ,         // C→S 퀘스트 받기(주는 NPC 곁에서)
	QUEST_COMPLETE_REQ,       // C→S 퀘스트 보고(보고할 NPC 곁에서)
	QUEST_ABANDON_REQ,        // C→S 퀘스트 포기
	QUEST_RESULT_ACK,         // S→C 받기 · 보고 · 포기 요청의 결과(요청마다 하나)
	// ── Phase 2-B: 기술 구조 V3(Data/SkillData.h) — 확장 세션 전용. 여기도 끝에만 추가한다 ──
	SKILL_EX_REQ,             // C→S 기술 누름 · 뗌(차지 · 홀딩) · 취소 — 칸 · 유형은 SkillDefTable
	SKILL_EX_NFY,             // S→C 시전 알림(단계 · 조준 · 서버 시각) — 주변 확장 세션에
	SUBJECT_FORCED_MOVE_NFY,  // S→C 서버가 옮긴 이동(돌진 · 순간이동 · 넉백 · 끌어당김 · 구르기) — 곡선으로 미끄러뜨린다
	COMBAT_GAUGE_INF,         // S→C 내 고유 기술 게이지(서약 · 새김 · 그늘)
	MONSTER_GAUGE_NFY,        // S→C 보스 무력화 게이지 · 무력화 상태 — 주변에
	// ── Phase 2-D: 기술 성장(Data/SkillGrowth.h) — 확장 세션 전용 ──
	SKILL_TREE_INF,           // S→C 내 기술 레벨 · 갈래 · 남은 점수(로그인 · 레벨 업 · 초기화 때)
	SKILL_GROW_REQ,           // C→S 기술 레벨 올림 · 내림 · 갈래 고르기 · 모두 초기화
	SKILL_GROW_ACK,           // S→C 요청 결과(요청마다 하나) + 바뀐 기술 하나
};

#pragma pack (push, 1)
struct USER_LOGIN_REQ_PACKET
{
	unsigned short size;
	char           type;
	char           name[NAME_SIZE];
	char           password[PASSWORD_SIZE];
};

struct USER_MOVE_REQ_PACKET
{
	unsigned short	size;
	char			type;
	char			direction;  // 0:UP 1:DOWN 2:LEFT 3:RIGHT 4:UP-LEFT 5:UP-RIGHT 6:DOWN-LEFT 7:DOWN-RIGHT
	uint32_t		move_time;
};

// 쿨타임 판정에 필요한 시간은 서버가 직접(GetNowTime()) 재므로, 클라이언트가
// 시간값을 보낼 필요도, 서버가 그 값을 믿을 이유도 없다 — 과거엔 attack_time을
// 클라이언트에서 받아 그대로 m_lastAttackTime에 저장했는데, 조작된 클라이언트가
// 항상 작은 값(0 등)을 보내면 "now > lastAttackTime + 500" 검사가 항상 참이 되어
// 공격 쿨타임이 사실상 무력화되는 문제가 있었다. USER_MOVE_REQ/USER_SKILL_REQ/
// ITEM_USE_REQ처럼 서버 자신의 GetNowTime()만 신뢰하도록 통일한다.
struct USER_ATTACK_REQ_PACKET
{
	unsigned short	size;
	char			type;
	uint8_t			facing;   // 0 = right, 1 = left
};

struct USER_SKILL_REQ_PACKET
{
	unsigned short	size;
	char			type;
};

struct USER_TELEPORT_REQ_PACKET
{
	unsigned short size;
	char	type;
	short	x, y;
};

struct USER_LOGOUT_REQ_PACKET
{
	unsigned short size;
	char	type;
};

// slotIndex는 클라이언트가 보는 인벤토리 그리드 좌표 그대로다. 범위를 벗어나거나
// 비어있는 슬롯을 가리켜도 서버는 크래시하지 않는다 — Inventory::TryEquip 등은
// 존재하지 않는 슬롯 조회를 그냥 실패(false)로 처리하므로, 유효성 검사는
// Inventory 쪽에 이미 있다(여기서 중복으로 검사하지 않는다).
struct ITEM_EQUIP_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       slotIndex;
};

struct ITEM_UNEQUIP_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       slotIndex;
};

struct ITEM_SWAP_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       slotIndexA;
	uint16_t       slotIndexB;
};

struct ITEM_DISCARD_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       slotIndex;
	uint16_t       count;
};

// 파라미터 없음 — 대상은 항상 "요청한 플레이어가 서버 기준으로 서 있는 칸"이다.
// 클라이언트가 좌표나 대상 id를 보내지 않으므로, 위조된 좌표로 먼 곳의 아이템을
// 원격으로 줍는 부정 사용 자체가 프로토콜 레벨에서 성립하지 않는다.
struct ITEM_PICKUP_REQ_PACKET
{
	unsigned short size;
	char           type;
};

// 소비 아이템 사용(포션 등). slotIndex가 장비/빈 슬롯을 가리키면 서버는
// Inventory::TryUseItem에서 그냥 실패로 처리한다(ITEM_EQUIP_REQ와 같은 관례).
struct ITEM_USE_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       slotIndex;
};

constexpr size_t ProtocolConstMaxSize(size_t lhs, size_t rhs)
{
	return (lhs > rhs) ? lhs : rhs;
}

struct CS_CHAT_PACKET
{
	unsigned short size;
	char type;
	char mess[CHAT_SIZE];
};

// ── 확장 프로토콜: Client → Server ──────────────────────────────────────────

// USER_LOGIN_REQ와 같지만, 이 패킷으로 로그인한 세션은 "확장 세션"으로 표시되어
// 리니지풍 컨텐츠 패킷(아래 *_EX, 상점/강화 등)을 주고받는다. 레거시 봇/클라이언트는
// 계속 USER_LOGIN_REQ를 쓰므로 받는 패킷 구성이 예전과 똑같다(부하 테스트 조건 보존).
struct USER_LOGIN_EX_REQ_PACKET
{
	unsigned short size;
	char           type;
	char           name[NAME_SIZE];
	char           password[PASSWORD_SIZE];
	uint16_t       version;   // PROTOCOL_EX_VERSION
	uint8_t        classId;   // 새 캐릭터를 만들 때의 직업(ClassId). 기존 캐릭터면 무시된다.
};

// 사망 상태일 때만 유효. 마을 부활 지점에서 HP를 채워 다시 시작한다.
struct USER_RESTART_REQ_PACKET
{
	unsigned short size;
	char           type;
};

// 대상을 지정한 근접 공격(체비셰프 거리 1). 몬스터면 일반 사냥, 플레이어면 PK 규칙을
// 따른다. 방향(facing)은 서버가 대상 위치로 직접 계산하므로 보내지 않는다.
struct USER_TARGET_ATTACK_REQ_PACKET
{
	unsigned short size;
	char           type;
	ObjID          targetId;
};

struct ITEM_ENCHANT_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       scrollSlot;
	uint16_t       targetSlot;
};

// seq: 클라이언트가 요청마다 1씩 올리는 번호. 서버는 플레이어별로 마지막으로 처리한
// 번호를 기억해 같은 요청의 재전송(더블클릭/재접속 직후 재시도 등)을 무시한다(멱등성).
struct SHOP_BUY_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       npcId;
	uint16_t       itemId;
	uint16_t       count;
	uint32_t       seq;
};

struct SHOP_SELL_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       npcId;
	uint16_t       slotIndex;
	uint16_t       count;
	uint32_t       seq;
};

// destIndex = 목적지 지역(zoneId). 요금은 WorldData::kRegions[].teleportFee
struct NPC_TELEPORT_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       npcId;
	uint16_t       destIndex;
	uint32_t       seq;
};

// 패킷이 하나 늘 때마다 깊게 중첩된 ProtocolConstMaxSize(...) 트리를 손으로
// 다시 짜는 대신, 누적 최댓값을 한 단계씩 이름 붙여 계산한다 — 각 줄은 항상
// "지금까지의 최댓값 vs 새 패킷 하나"만 비교하므로 실수할 여지가 없고, 새
// 패킷은 끝에 한 줄만 추가하면 된다.
namespace ClientPacketSizeDetail
{
	constexpr size_t s01 = sizeof(USER_LOGIN_REQ_PACKET);
	constexpr size_t s02 = ProtocolConstMaxSize(s01, sizeof(USER_MOVE_REQ_PACKET));
	constexpr size_t s03 = ProtocolConstMaxSize(s02, sizeof(USER_ATTACK_REQ_PACKET));
	constexpr size_t s04 = ProtocolConstMaxSize(s03, sizeof(USER_SKILL_REQ_PACKET));
	constexpr size_t s05 = ProtocolConstMaxSize(s04, sizeof(USER_TELEPORT_REQ_PACKET));
	constexpr size_t s06 = ProtocolConstMaxSize(s05, sizeof(USER_LOGOUT_REQ_PACKET));
	constexpr size_t s07 = ProtocolConstMaxSize(s06, sizeof(CS_CHAT_PACKET));
	constexpr size_t s08 = ProtocolConstMaxSize(s07, sizeof(ITEM_EQUIP_REQ_PACKET));
	constexpr size_t s09 = ProtocolConstMaxSize(s08, sizeof(ITEM_UNEQUIP_REQ_PACKET));
	constexpr size_t s10 = ProtocolConstMaxSize(s09, sizeof(ITEM_SWAP_REQ_PACKET));
	constexpr size_t s11 = ProtocolConstMaxSize(s10, sizeof(ITEM_DISCARD_REQ_PACKET));
	constexpr size_t s12 = ProtocolConstMaxSize(s11, sizeof(ITEM_PICKUP_REQ_PACKET));
	constexpr size_t s13 = ProtocolConstMaxSize(s12, sizeof(ITEM_USE_REQ_PACKET));
	constexpr size_t s14 = ProtocolConstMaxSize(s13, sizeof(USER_LOGIN_EX_REQ_PACKET));
	constexpr size_t s15 = ProtocolConstMaxSize(s14, sizeof(USER_RESTART_REQ_PACKET));
	constexpr size_t s16 = ProtocolConstMaxSize(s15, sizeof(USER_TARGET_ATTACK_REQ_PACKET));
	constexpr size_t s17 = ProtocolConstMaxSize(s16, sizeof(ITEM_ENCHANT_REQ_PACKET));
	constexpr size_t s18 = ProtocolConstMaxSize(s17, sizeof(SHOP_BUY_REQ_PACKET));
	constexpr size_t s19 = ProtocolConstMaxSize(s18, sizeof(SHOP_SELL_REQ_PACKET));
	constexpr size_t s20 = ProtocolConstMaxSize(s19, sizeof(NPC_TELEPORT_REQ_PACKET));
}
// V2 패킷까지 포함한 최종 최댓값(MAX_CLIENT_PACKET_SIZE)은 ProtocolV2.h에서 정한다.
constexpr size_t MAX_CLIENT_PACKET_SIZE_V1 = ClientPacketSizeDetail::s20;

struct USER_LOGIN_ACK_PACKET
{
	unsigned short size;
	char	type;
	ObjID	id;
	short	x, y;
	uint16_t	maxhp;
	uint16_t	hp;
	uint8_t		level;
	uint32_t	exp;
};

struct USER_LOGIN_FAIL_ACK_PACKET
{
	unsigned short size;
	char           type;
};

struct ITEM_SLOT_DATA
{
	uint16_t slotIndex;
	uint16_t itemId;
	uint16_t count;
	uint8_t  equipped;  // 0/1
};

// 로그인 시 1회 전송하는 인벤토리 스냅샷. slotCount만큼만(0 ~ MAX_INVENTORY_SLOTS)
// items[]의 앞부분이 유효하고, 나머지는 의미 없는 값이다. 이후 변경은 이 패킷을
// 다시 보내지 않고 슬롯 단위 알림(추후 추가)으로 처리한다.
struct ITEM_LIST_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        slotCount;
	ITEM_SLOT_DATA items[MAX_INVENTORY_SLOTS];
};

// 장착/탈착 요청 결과. success=0이면 slot은 의미 없는 값(0)이고, 실패해도
// slot.slotIndex만은 항상 요청받은 값 그대로 채워 보낸다 — 클라이언트가 여러
// 요청을 연달아 보낸 뒤에도 어떤 요청에 대한 응답인지 별도 상태 없이 알 수 있게.
struct ITEM_EQUIP_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        success;
	ITEM_SLOT_DATA slot;
};

struct ITEM_UNEQUIP_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        success;
	ITEM_SLOT_DATA slot;
};

// 두 슬롯의 최종 상태를 함께 보낸다. 스왑 결과 어느 한쪽이 비게 되면 그 슬롯은
// itemId=0, count=0으로 채워 보낸다 — 클라이언트는 그걸 "슬롯 비움"으로 해석한다.
// (slotA/slotB의 slotIndex는 실패 시에도 항상 요청받은 값 그대로다.)
struct ITEM_SWAP_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        success;
	ITEM_SLOT_DATA slotA;
	ITEM_SLOT_DATA slotB;
};

// success=0이어도 slot.slotIndex는 항상 요청받은 값 그대로다(EQUIP/UNEQUIP_ACK와
// 같은 관례). 성공 시 슬롯이 완전히 비워졌으면 itemId=0, count=0으로 온다.
struct ITEM_DISCARD_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        success;
	ITEM_SLOT_DATA slot;
};

// 성공 시 실제로 채워진 슬롯 내용은 뒤이어 오는 ITEM_ACQUIRE_INF로 전달된다 —
// 이 패킷은 "그 Z키 입력 자체가 뭔가를 주웠는지"만 알려준다. 인벤토리가 가득
// 차서 실패한 경우는 뒤이어 SYSTEM_MESSAGE_INF(InventoryFull)도 함께 온다.
struct ITEM_PICKUP_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        success;
};

// success=0이어도 slot.slotIndex는 항상 요청받은 값 그대로다(다른 ACK와 같은
// 관례). 성공 시 마지막 1개를 소비해 슬롯이 완전히 비워졌으면 itemId=0,
// count=0으로 온다. 실제로 회복된 HP는 이 패킷이 아니라 뒤이어 오는
// USER_HEAL_INF로 전달된다 — "이 요청이 뭘 했는지"(슬롯 변화)와 "이 플레이어의
// HP가 바뀌었다"(이미 있던 공용 알림)를 굳이 한 패킷에 합치지 않는다.
struct ITEM_USE_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        success;
	ITEM_SLOT_DATA slot;
};

// itemId 필드는 카테고리에 따라 의미가 다르다(둘 다 아니면 0/무의미):
//   eUser  : 지금 장착 중인 무기의 ItemTableId. 0이면 장착한 게 없음.
//            (Inventory::GetEquippedItemId() 참고 — 아직 "한 번에 하나만
//            장착" 규칙은 없지만, 화면 표시는 그중 하나만 보여준다.)
//   eItem  : 그 필드 아이템 자체의 ItemTableId(클라이언트가 어떤 아이콘을
//            그릴지 결정하는 데 쓴다).
struct SUBJECT_ADD_NFY_PACKET
{
	unsigned short size;
	char	type;
	char	monster_type;
	ObjID	id;
	short	x, y;
	char	name[NAME_SIZE];
	uint16_t itemId;
};

// 이미 화면에 보이는 플레이어의 장착 상태가 바뀌었을 때(장착/탈착/장착 중이던
// 아이템을 버림) 주변 뷰어에게 알린다. itemId=0이면 "지금은 아무 것도 장착
// 안 함" — SUBJECT_ADD_NFY의 itemId 필드와 동일한 의미다. 새로 시야에
// 들어올 때는 이 패킷이 아니라 SUBJECT_ADD_NFY의 itemId로 이미 전달된다.
struct SUBJECT_EQUIP_CHANGE_NFY_PACKET
{
	unsigned short size;
	char     type;
	ObjID    id;
	uint16_t itemId;
};

struct SUBJECT_REMOVE_NFY_PACKET
{
	unsigned short size;
	char	type;
	ObjID	id;
};

struct SUBJECT_MOVE_NFY_PACKET
{
	unsigned short size;
	char	type;
	ObjID	id;
	short	x, y;
	unsigned int move_time;
};


struct SUBJECT_DIE_NFY_PACKET
{
	unsigned short size;
	char type;
	ObjID id;
	uint16_t hp;
};

struct SUBJECT_RESPAWN_NFY_PACKET
{
	unsigned short size;
	char type;
	char monster_type;
	ObjID id;
	short	x, y;
	uint16_t hp;
	char	name[NAME_SIZE];
};

struct USER_ATTACK_ACK_PACKET
{
	unsigned short size;
	char type;
	ObjID id;
	int32_t hp;
	int32_t damage;
};

struct SUBJECT_ATTACK_NFY_PACKET
{
	unsigned short size;
	char  type;
	ObjID victim_id;
	ObjID attacker_id;
	int32_t hp;
};

struct USER_HEAL_INF_PACKET
{
	unsigned short size;
	char type;
	int32_t hp;
};

struct USER_STAT_CHANGE_INF_PACKET
{
	unsigned short size;
	char   type;
	uint8_t  level;
	uint16_t hp;
	uint16_t maxhp;
	uint32_t exp;
};

// 몬스터 처치 등으로 아이템을 새로 얻거나 기존 스택 수량이 늘었을 때 그 슬롯
// 하나의 최신 상태를 알린다. 한 번에 여러 슬롯이 바뀌면(스택이 여러 슬롯에
// 걸쳐 나뉘어 채워지는 경우) 이 패킷을 슬롯 개수만큼 나눠 보낸다 — 그런
// 경우가 실제로는 드물어서, 고정 배열을 항상 잡아두는 것보다 이 편이 낫다.
struct ITEM_ACQUIRE_INF_PACKET
{
	unsigned short size;
	char           type;
	ITEM_SLOT_DATA slot;
};

// SystemMessageCode 기준의 공용 상황 알림. param1/param2의 의미는 code마다
// 다르며, 각 code 정의 옆 주석에 명시한다.
struct SYSTEM_MESSAGE_INF_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       code;    // SystemMessageCode
	int32_t        param1;
	int32_t        param2;
};

struct PLAYER_ATTACK_NFY_PACKET
{
	unsigned short size;
	char    type;
	ObjID   attacker_id;
	uint8_t facing;  // 0 = right, 1 = left
};

struct SC_CHAT_PACKET
{
	unsigned short size;
	char  type;
	ObjID sender_id;
	char  mess[CHAT_SIZE];
};

// ── 확장 프로토콜: Server → Client (확장 세션에만 보낸다) ───────────────────────
// STRESS_TEST 봇의 수신 버퍼(MAX_PACKET_SIZE=255)를 넘는 패킷은 만들지 않는다 —
// 봇은 확장 패킷을 받지 않지만, 혹시라도 경로가 섞였을 때 봇 연결이 통째로 끊기는
// 사고를 막기 위해 아래 static_assert로 크기를 묶어 둔다.

// ITEM_SLOT_DATA + 강화 수치. 기존 ITEM_SLOT_DATA는 레거시 패킷 호환을 위해 그대로 둔다.
struct ITEM_SLOT_EX_DATA
{
	uint16_t slotIndex;
	uint16_t itemId;     // 0 = 빈 슬롯
	uint16_t count;
	uint8_t  equipped;
	uint8_t  enchant;
};

// SUBJECT_INFO_EX_NFY.flags / USER_INFO_EX_INF.flags
enum SubjectExFlag : uint8_t
{
	SUBJECT_EX_PURPLE  = 1 << 0,   // 적대(먼저 공격함)
	SUBJECT_EX_HASTE   = 1 << 1,   // 가속 중
	SUBJECT_EX_DEAD    = 1 << 2,
	SUBJECT_EX_SAFE    = 1 << 3,   // 안전지대 안
};

struct USER_INFO_EX_INF_PACKET
{
	unsigned short size;
	char     type;
	uint8_t  level;
	uint32_t exp;
	uint32_t expToNext;
	uint16_t hp;
	uint16_t maxhp;
	uint16_t attack;       // 레벨 기본 + 무기 + 강화 보너스
	uint32_t adena;
	int16_t  alignment;
	uint16_t pkCount;
	uint8_t  flags;        // SubjectExFlag
	uint32_t hasteRemainMs;
};

// 플레이어가 시야에 들어올 때(SUBJECT_ADD_NFY 직후)와 정보가 바뀔 때 보낸다.
// 필드 아이템이면 enchant만 의미가 있다(강화된 무기를 바닥에 버린 경우).
struct SUBJECT_INFO_EX_NFY_PACKET
{
	unsigned short size;
	char     type;
	ObjID    id;
	uint8_t  level;
	int16_t  alignment;
	uint8_t  flags;
	uint8_t  enchant;      // 장착 무기(또는 필드 아이템)의 강화 수치
	uint16_t hp;
	uint16_t maxhp;
	uint8_t  classId;      // ClassId (V2)
	uint8_t  weaponGrade;  // 장착 무기 인스턴스 등급(V2, 외형 발광)
};

struct ITEM_LIST_EX_ACK_PACKET
{
	unsigned short    size;
	char              type;
	uint8_t           slotCount;
	ITEM_SLOT_EX_DATA items[MAX_INVENTORY_SLOTS];
};

// 슬롯 내용이 바뀔 때마다(획득/사용/장착/교체/버림/강화/상점) 그 슬롯의 최신 상태.
// Inventory의 모든 변경 경로가 이미 한 곳(슬롯 저장)을 거치므로 거기서 함께 보낸다.
struct ITEM_SLOT_EX_INF_PACKET
{
	unsigned short    size;
	char              type;
	ITEM_SLOT_EX_DATA slot;
};

enum class AdenaReason : uint8_t { Pickup, ShopBuy, ShopSell, Teleport, Init, Service, Quest };

struct ADENA_INF_PACKET
{
	unsigned short size;
	char     type;
	uint32_t adena;
	int32_t  delta;
	uint8_t  reason;   // AdenaReason
};

enum class EnchantResult : uint8_t
{
	Invalid,       // 요청 자체가 잘못됨(두루마리/대상 없음, 최대 강화 등) — 아무 것도 소모되지 않음
	Success,
	Evaporated,    // 실패 — 무기가 증발했다
};

struct ENCHANT_RESULT_ACK_PACKET
{
	unsigned short size;
	char     type;
	uint8_t  result;          // EnchantResult
	uint16_t targetSlot;
	uint16_t itemId;
	uint8_t  enchantBefore;
	uint8_t  enchantAfter;
};

enum class ShopKind : uint8_t { Buy, Sell, Teleport };

enum class ShopResult : uint8_t
{
	Success,
	Duplicate,          // 이미 처리한 seq — 무시됨
	NotEnoughAdena,
	InventoryFull,
	TooFar,             // NPC와 너무 멀다
	InvalidItem,        // 이 상점에서 팔지 않는 물건 / 팔 수 없는 물건
	InvalidCount,
	EquippedItem,       // 장착 중인 장비는 팔 수 없다
	InvalidState,       // 사망/이동 중 등
	InvalidNpc,
};

struct SHOP_RESULT_ACK_PACKET
{
	unsigned short size;
	char     type;
	uint8_t  kind;     // ShopKind
	uint8_t  result;   // ShopResult
	uint32_t seq;
	uint16_t itemId;   // 텔레포트면 목적지 zoneId
	uint16_t count;
	int32_t  adenaDelta;
};

enum SubjectDamageFlag : uint8_t
{
	DAMAGE_CRIT       = 1 << 0,
	DAMAGE_SKILL      = 1 << 1,
	DAMAGE_KILLED     = 1 << 2,
	DAMAGE_PVP        = 1 << 3,
	DAMAGE_BOSS_SKILL = 1 << 4,
	DAMAGE_COMBO      = 1 << 5,   // V2: 연계 성공(comboId 참고)
	DAMAGE_DOT        = 1 << 6,   // V2: 지속 피해(화상/중독)
	DAMAGE_KNOCKBACK  = 1 << 7,   // V2: 넉백됨
};

// SUBJECT_DAMAGE_NFY.flags2 (V2)
enum SubjectDamageFlag2 : uint8_t
{
	DAMAGE2_EVADE     = 1 << 0,   // 피함(피해 0)
	DAMAGE2_GUARD     = 1 << 1,   // 철벽 태세로 경감
	DAMAGE2_COUNTER   = 1 << 2,   // 반격
	DAMAGE2_WALL      = 1 << 3,   // 넉백이 벽에 부딪힘
	DAMAGE2_BACKSTAB  = 1 << 4,   // 급소/기습
	DAMAGE2_HEAVY     = 1 << 5,   // 강타(큰 히트스톱)
	DAMAGE2_PROC      = 1 << 6,   // 장비 특수 효과로 생긴 피해(벼락·메아리)
	DAMAGE2_BACK      = 1 << 7,   // Phase 2-B: 후방 타격(기습 · 급소가 아닌 등 뒤 보너스)
};

// 레거시 USER_ATTACK_ACK(공격자에게만)/SUBJECT_ATTACK_NFY(피해 수치 없음)를 대신해
// 주변의 확장 세션 모두에게 "누가 누구에게 얼마를" 알린다 — 남이 때린 데미지 숫자와
// 몬스터 HP 바를 모두가 보게 하기 위함이다.
struct SUBJECT_DAMAGE_NFY_PACKET
{
	unsigned short size;
	char     type;
	ObjID    victimId;
	ObjID    attackerId;
	int32_t  hp;
	int32_t  maxhp;
	int32_t  damage;
	uint8_t  flags;    // SubjectDamageFlag
	// ── V2 ──
	uint16_t skillId;  // 0 = 기본/몬스터 평타
	uint8_t  comboId;  // ComboTable (DAMAGE_COMBO일 때)
	uint8_t  flags2;   // SubjectDamageFlag2
	uint8_t  hitIndex; // 연타 중 몇 번째(0부터)
};

struct EFFECT_NFY_PACKET
{
	unsigned short size;
	char     type;
	ObjID    subjectId;
	uint16_t effectId;   // EffectId
	short    x, y;
	int32_t  param;
};

struct WORLD_NOTICE_INF_PACKET
{
	unsigned short size;
	char     type;
	uint16_t code;       // WorldNoticeCode
	int32_t  param1;
	int32_t  param2;
	char     name[NAME_SIZE];
};

enum class ChatChannel : uint8_t { Normal, World, System };

// '&'로 시작하는 채팅은 전체 채팅이 된다. 받는 쪽이 보낸 사람을 시야에서 모를 수
// 있으므로 이름을 함께 싣는다(기존 SC_CHAT은 sender_id만 있다).
struct SC_CHAT_EX_PACKET
{
	unsigned short size;
	char     type;
	uint8_t  channel;    // ChatChannel
	ObjID    senderId;
	char     senderName[NAME_SIZE];
	char     mess[CHAT_SIZE];
};

static_assert(sizeof(ITEM_LIST_EX_ACK_PACKET) <= 255, "STRESS_TEST 수신 버퍼(255)를 넘으면 안 된다");
static_assert(sizeof(SC_CHAT_EX_PACKET) <= 255, "STRESS_TEST 수신 버퍼(255)를 넘으면 안 된다");
#pragma pack (pop)

#include "ProtocolV2.h"

