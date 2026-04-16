#pragma once

#include "EnumCategory.h"
#include "ContentID.h"
#include "ObjID.h"

constexpr int PORT_NUM = 4000;

constexpr int NAME_SIZE     = 20;
constexpr int PASSWORD_SIZE = 20;
constexpr int CHAT_SIZE     = 20;

constexpr int MAX_USER    = 20000;
constexpr int MAX_MONSTER = 200000;

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

constexpr int VIEW_RANGE   = 5;
constexpr int ATTACK_RANGE = 1;
constexpr int WAKE_RANGE   = 3;  // aggro monster wakes up when player is within this many tiles

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
	//client to server
	CS_LOGIN,
	CS_MOVE,
	CS_ATTACK,
	CS_SKILL,

	//server to client
	SC_LOGIN_SUCCESS,
	SC_LOGIN_FAIL,
	SC_ADD_OBJECT,
	SC_MOVE_OBJECT,
	SC_REMOVE_OBJECT,
	SC_PLAYER_ATTACK_MONSTER,
	SC_MONSTER_DIE,
	SC_MONSTER_RESPAWN,
	SC_MONSTER_ATTACK_PLAYER,
	SC_HEAL,
	SC_PLAYER_DIE,
	SC_PLAYER_RESPAWN,
	SC_STAT_CHANGE
};

#pragma pack (push, 1)
struct CS_LOGIN_PACKET
{
	unsigned short size;
	char           type;
	char           name[NAME_SIZE];
	char           password[PASSWORD_SIZE];
};

struct CS_MOVE_PACKET
{
	unsigned short	size;
	char			type;
	char			direction;  // 0:UP 1:DOWN 2:LEFT 3:RIGHT 4:UP-LEFT 5:UP-RIGHT 6:DOWN-LEFT 7:DOWN-RIGHT
	uint32_t		move_time;
};

struct CS_ATTACK_PACKET
{
	unsigned short	size;
	char			type;
	uint32_t		attack_time;
	uint8_t			facing;   // 0 = right, 1 = left
};

struct CS_SKILL_PACKET
{
	unsigned short	size;
	char			type;
};

struct CS_TELEPORT_PACKET
{
	unsigned short size;
	char	type;
};

struct CS_LOGOUT_PACKET
{
	unsigned short size;
	char	type;
};

struct SC_LOGIN_SUCCESS_PACKET
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

struct SC_LOGIN_FAIL_PACKET
{
	unsigned short size;
	char           type;
};

struct SC_ADD_OBJECT_PACKET
{
	unsigned short size;
	char	type;
	char	monster_type;
	ObjID	id;
	short	x, y;
	char	name[NAME_SIZE];
};

struct SC_REMOVE_OBJECT_PACKET
{
	unsigned short size;
	char	type;
	ObjID	id;
};

struct SC_MOVE_OBJECT_PACKET
{
	unsigned short size;
	char	type;
	ObjID	id;
	short	x, y;
	unsigned int move_time;
};


struct SC_MONSTER_DIE_PACKET
{
	unsigned short size;
	char type;
	ObjID monster_id;
};

struct SC_MONSTER_RESPAWN_PACKET
{
	unsigned short size;
	char type;
	ObjID monster_id;
	short	x, y;
};

struct SC_PLAYER_ATTACK_MONSTER_PACKET
{
	unsigned short size;
	char type;
	ObjID id;
	int32_t hp;
	int32_t damage;
};

struct SC_MONSTER_ATTACK_PLAYER_PACKET
{
	unsigned short size;
	char  type;
	ObjID monster_id;
	int32_t hp;
};

struct SC_HEAL_PACKET
{
	unsigned short size;
	char type;
	int32_t hp;
};

struct SC_PLAYER_DIE_PACKET
{
	unsigned short size;
	char type;
	ObjID id;
	uint16_t hp;
};

struct SC_PLAYER_RESPAWN_PACKET
{
	unsigned short size;
	char type;
	ObjID id;
	short x, y;
	uint16_t hp;
};

struct SC_STAT_CHANGE_PACKET
{
	unsigned short size;
	char   type;
	uint8_t  level;
	uint16_t hp;
	uint16_t maxhp;
	uint32_t exp;
};
#pragma pack (pop)
