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

constexpr int VIEW_RANGE            = 5;
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
}
constexpr size_t MAX_CLIENT_PACKET_SIZE = ClientPacketSizeDetail::s13;

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
#pragma pack (pop)
