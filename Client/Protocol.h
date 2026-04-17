#pragma once
#include "Types.h"

constexpr int PORT_NUM = 4000;

constexpr int NAME_SIZE = 20;
constexpr int CHAT_SIZE = 20;

constexpr int MAX_USER    = 10000;
constexpr int MAX_MONSTER = 200000;

constexpr int W_WIDTH  = 2000;
constexpr int W_HEIGHT = 2000;

constexpr int SECTOR_RANGE = 10;

constexpr int VIEW_RANGE   = 5;
constexpr int ATTACK_RANGE = 1;

constexpr int PLAYER_MAX_HP  = 100;
constexpr int MONSTER_MAX_HP = 50;

constexpr int PLAYER_OFFENSIVE  = 10;
constexpr int MONSTER_OFFENSIVE = 5;

constexpr int BUF_SIZE = 1024;

enum class PacketType : uint16
{
	// Client → Server (REQ)
	USER_LOGIN_REQ,
	USER_MOVE_REQ,
	USER_ATTACK_REQ,

	// Server → Client, unicast ACK
	USER_LOGIN_ACK,
	USER_LOGIN_FAIL_ACK,
	OBJECT_ADD_INF,
	OBJECT_MOVE_INF,
	OBJECT_REMOVE_INF,
	USER_ATTACK_ACK,
	MONSTER_RESPAWN_INF
};

#pragma pack (push, 1)
struct USER_LOGIN_REQ_PACKET
{
	unsigned short	size;
	char			type;
	char			name[NAME_SIZE];
};

struct USER_MOVE_REQ_PACKET
{
	unsigned short	size;
	char			type;
	char			direction;  // 0 : UP, 1 : DOWN, 2 : LEFT, 3 : RIGHT
	uint32			move_time;
};

struct USER_ATTACK_REQ_PACKET
{
	unsigned short	size;
	char			type;
	uint32			attack_time;
};

struct CS_CHAT_PACKET
{
	unsigned short size;
	char	type;
	char	mess[CHAT_SIZE];
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

struct USER_LOGIN_ACK_PACKET
{
	unsigned short size;
	char	type;
	int32		id;
	short	x, y;
};

struct OBJECT_ADD_INF_PACKET
{
	unsigned short size;
	char	type;
	int32		id;
	short	x, y;
	char	name[NAME_SIZE];
};

struct OBJECT_REMOVE_INF_PACKET
{
	unsigned short size;
	char	type;
	int32		id;
};

struct OBJECT_MOVE_INF_PACKET
{
	unsigned short size;
	char	type;
	int32		id;
	short	x, y;
	unsigned int move_time;
};

struct SC_CHAT_PACKET
{
	unsigned short size;
	char	type;
	int32		id;
	char	mess[CHAT_SIZE];
};


struct USER_LOGIN_FAIL_ACK_PACKET
{
	unsigned short size;
	char	type;

};

struct USER_STAT_CHANGE_INF_PACKET
{
	unsigned short size;
	char	type;
	int		hp;
	int		max_hp;
	int		exp;
	int		level;

};

struct SC_PC_DIE_PACKET
{
	unsigned short size;
	int type;
	int id;
	int hp;
	int exp;
	int x;
	int y;
};

struct USER_HEAL_INF_PACKET
{
	unsigned short size;
	int type;
	int hp;
};

struct MONSTER_DIE_INF_PACKET
{
	unsigned short size;
	int type;
	int monster_id;
};

struct MONSTER_RESPAWN_INF_PACKET
{
	unsigned short size;
	int type;
	int monster_id;
	short	x, y;
};

struct USER_ATTACK_ACK_PACKET
{
	unsigned short size;
	int type;
	int id;
	int hp;
};

struct MONSTER_ATTACK_INF_PACKET
{
	unsigned short size;
	int type;
	int id;
	int hp;
};
#pragma pack (pop)
