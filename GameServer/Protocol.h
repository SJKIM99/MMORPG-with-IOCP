#pragma once
#include "Types.h"
#include "EnumCategory.h"
#include "ContentID.h"
#include "ObjID.h"

constexpr int PORT_NUM = 4000;

constexpr int NAME_SIZE     = 20;
constexpr int PASSWORD_SIZE = 20;
constexpr int CHAT_SIZE     = 20;

constexpr int MAX_USER = 20000;   // Max concurrent user sessions
constexpr int MAX_NPC = 200000;

constexpr uint32 PLAYER_ID_START = 1;
constexpr uint32 NPC_ID_START = 1'000'000'000;
constexpr uint32 AGGRO_NPC_BOUNDARY = NPC_ID_START + static_cast<uint32>(MAX_NPC / 4) - 1;  // First 25% of NPCs are AGGRO

inline constexpr bool IsPlayerObjectId(uint32 id) noexcept
{
	return id >= PLAYER_ID_START && id < NPC_ID_START;
}

inline constexpr bool IsNpcObjectId(uint32 id) noexcept
{
	return id >= NPC_ID_START;
}

constexpr int W_WIDTH = 2000;
constexpr int W_HEIGHT = 2000;

constexpr int SECTOR_RANGE = 10;

constexpr int VIEW_RANGE = 5;
constexpr int ATTACK_RANGE = 1;

constexpr int PLAYER_MAX_HP = 100;
constexpr int NPC_MAX_HP = 50;


constexpr int PLAYER_OFFENSIVE = 10;
constexpr int NPC_OFFENSIVE = 3;
constexpr int HEAL_SIZE = 10;

constexpr int BUF_SIZE = 1024;

enum MONSTER_TYPE
{
	AGGRO,
	PASSIVE
};

enum class PacketType : uint16
{
	//client to server
	CS_LOGIN,
	CS_MOVE,
	CS_ATTACK,

	//server to client
	SC_LOGIN_SUCCESS,
	SC_LOGIN_FAIL,
	SC_ADD_OBJECT,
	SC_MOVE_OBJECT,
	SC_REMOVE_OBJECT,
	SC_PLAYER_ATTACK_NPC,
	SC_NPC_DIE,
	SC_NPC_RESPAWN,
	SC_NPC_ATTACK_PLAYER,
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
	char			direction;  // 0 : UP, 1 : DOWN, 2 : LEFT, 3 : RIGHT
	uint32			move_time;
};

struct CS_ATTACK_PACKET
{
	unsigned short	size;
	char			type;
	uint32			attack_time;
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
	uint16	maxhp;
	uint16	hp;
	uint8	level;
	uint32	exp;
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


struct SC_NPC_DIE_PACKET
{
	unsigned short size;
	char type;
	ObjID npc_id;
};

struct SC_NPC_RESPAWN_PACKET
{
	unsigned short size;
	char type;
	ObjID npc_id;
	short	x, y;
};

struct SC_PLAYER_ATTACK_NPC_PACKET
{
	unsigned short size;
	char type;
	ObjID id;
	int32 hp;
};

struct SC_NPC_ATTACK_PLAYER_PACKET
{
	unsigned short size;
	char type;
	int32 hp;
};

struct SC_HEAL_PACKET
{
	unsigned short size;
	char type;
	int32 hp;
};

struct SC_PLAYER_DIE_PACKET
{
	unsigned short size;
	char type;
	ObjID id;
	uint16 hp;
};

struct SC_PLAYER_RESPAWN_PACKET
{
	unsigned short size;
	char type;
	ObjID id;
	short x, y;
	uint16 hp;
};

struct SC_STAT_CHANGE_PACKET
{
	unsigned short size;
	char   type;
	uint8  level;
	uint16 hp;
	uint16 maxhp;
	uint32 exp;
};
#pragma pack (pop)
