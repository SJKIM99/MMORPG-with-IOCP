#pragma once
#include "Types.h"

constexpr int PORT_NUM = 4000;

constexpr int NAME_SIZE     = 20;
constexpr int PASSWORD_SIZE = 20;
constexpr int CHAT_SIZE     = 20;

constexpr int MAX_USER = 20000;
constexpr int MAX_MONSTER = 200000;
constexpr int AGGRO_MONSTER_BOUNDARY = MAX_USER + MAX_MONSTER / 4;  // First 25% of NPCs are AGGRO

constexpr int W_WIDTH = 2000;
constexpr int W_HEIGHT = 2000;

constexpr int SECTOR_RANGE = 10;

constexpr int VIEW_RANGE = 5;
constexpr int ATTACK_RANGE = 1;

constexpr int PLAYER_MAX_HP = 100;
constexpr int MONSTER_MAX_HP = 50;


constexpr int PLAYER_OFFENSIVE = 10;
constexpr int MONSTER_OFFENSIVE = 3;
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
	SC_PLAYER_ATTACK_MONSTER,
	SC_MONSTER_DIE,
	SC_MONSTER_RESPAWN,
	SC_MONSTER_ATTACK_PLAYER,
	SC_HEAL,
	SC_PLAYER_DIE,
	SC_PLAYER_RESPAWN
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
	int32		id;
	short	x, y;
	uint16	maxhp;
	uint16	hp;
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
	int32		id;
	short	x, y;
	char	name[NAME_SIZE];
};

struct SC_REMOVE_OBJECT_PACKET
{
	unsigned short size;
	char	type;
	int32		id;
};

struct SC_MOVE_OBJECT_PACKET
{
	unsigned short size;
	char	type;
	int32		id;
	short	x, y;
	unsigned int move_time;
};


struct SC_MONSTER_DIE_PACKET
{
	unsigned short size;
	char type;
	int32 monster_id;
};

struct SC_MONSTER_RESPAWN_PACKET
{
	unsigned short size;
	char type;
	int32 monster_id;
	short	x, y;
};

struct SC_PLAYER_ATTACK_MONSTER_PACKET
{
	unsigned short size;
	char type;
	int32 id;
	int32 hp;
};

struct SC_MONSTER_ATTACK_PLAYER_PACKET
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
	uint32 id;
	uint16 hp;
};

struct SC_PLAYER_RESPAWN_PACKET
{
	unsigned short size;
	char type;
	uint32 id;
	short x, y;
	uint16 hp;
};
#pragma pack (pop)