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
constexpr int MONSTER_MAX_HP = 50;

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

enum class PacketType : uint16_t
{
	// Client → Server (REQ)
	USER_LOGIN_REQ,
	USER_MOVE_REQ,
	USER_ATTACK_REQ,
	USER_SKILL_REQ,
	USER_TELEPORT_REQ,

	// Server → Client, unicast ACK (response to requester only)
	USER_LOGIN_ACK,
	USER_LOGIN_FAIL_ACK,
	USER_ATTACK_ACK,

	// Server → Client(s), NFY (server-initiated notification / broadcast)
	SUBJECT_ADD_NFY,
	SUBJECT_MOVE_NFY,
	SUBJECT_REMOVE_NFY,
	SUBJECT_DIE_NFY,
	SUBJECT_RESPAWN_NFY,
	SUBJECT_ATTACK_NFY,
	PLAYER_ATTACK_NFY,

	// Server → Client(s), INF (Server-only notifications)
	USER_HEAL_INF,
	USER_STAT_CHANGE_INF,

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

struct USER_ATTACK_REQ_PACKET
{
	unsigned short	size;
	char			type;
	uint32_t		attack_time;
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

constexpr size_t MAX_CLIENT_PACKET_SIZE =
	ProtocolConstMaxSize(
		sizeof(USER_LOGIN_REQ_PACKET),
		ProtocolConstMaxSize(
			sizeof(USER_MOVE_REQ_PACKET),
			ProtocolConstMaxSize(
				sizeof(USER_ATTACK_REQ_PACKET),
				ProtocolConstMaxSize(sizeof(USER_SKILL_REQ_PACKET),
					ProtocolConstMaxSize(sizeof(USER_TELEPORT_REQ_PACKET),
						ProtocolConstMaxSize(sizeof(USER_LOGOUT_REQ_PACKET), sizeof(CS_CHAT_PACKET))))
			)
		)
	);

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

struct SUBJECT_ADD_NFY_PACKET
{
	unsigned short size;
	char	type;
	char	monster_type;
	ObjID	id;
	short	x, y;
	char	name[NAME_SIZE];
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
