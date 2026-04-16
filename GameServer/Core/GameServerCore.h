#pragma once

#include "CoreGlobal.h"

#include <array>
#include <chrono>
#include <concepts>
#include <concurrent_priority_queue.h>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <locale>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "odbc32.lib")

using namespace std;

#include "Memory.h"
#include "ContentiD.h"
#include "ObjID.h"

struct DB_USER_INFO
{
	string _name;
	string _password;
	int    _x     = 0;
	int    _y     = 0;
	uint8_t  _level = 1;
	uint32_t _exp   = 0;
};

using DB_PLAYER_INFO = DB_USER_INFO;

class GameSession;

struct DB_EVENT_BASE : std::enable_shared_from_this<DB_EVENT_BASE>
{
	std::chrono::steady_clock::time_point wakeupTime;
	uint64_t sequence = 0;
	virtual ~DB_EVENT_BASE() = default;
};

struct DB_LOGIN_EVENT : DB_EVENT_BASE
{
	shared_ptr<GameSession> session;
	string name;
	string password;
};

struct DB_SAVE_EVENT : DB_EVENT_BASE
{
	ObjID   subjectId;
	string  name;
	short   x     = 0;
	short   y     = 0;
	uint8_t   level = 1;
	uint32_t  exp   = 0;
};

struct DB_ADD_EVENT : DB_EVENT_BASE
{
	ObjID   subjectId;
	string  name;
	string  password;
	short   x     = 0;
	short   y     = 0;
	uint8_t   level = 1;
	uint32_t  exp   = 0;
};

enum TIMER_EVENT_TYPE
{
	EV_RANOM_MOVE,
	EV_MONSTER_RESPAWN,
	EV_MONSTER_ATTACK_TO_USER,
	EV_HEAL,
	EV_USER_RESPAWN,
	EV_AGGRO_MOVE
};

struct TIMER_EVENT
{
	ObjID subjectId;
	std::chrono::steady_clock::time_point wakeupTime;
	TIMER_EVENT_TYPE event;
	ObjID targetId;
	uint64_t sequence = 0;
};
