#pragma once

#include "Types.h"
#include "CoreMacro.h"
#include "CoreTLS.h"
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

#include "Lock.h"
#include "Memory.h"

enum DB_EVENT_TYPE
{
	EV_LOGIN_PLAYER,
	EV_SAVE_PLAYER_INFO,
	EV_ADD_PLAYER_INFO
};

struct DB_PLAYER_INFO
{
	string _name;
	string _password;
	int    _x = 0;
	int    _y = 0;
};

struct DB_EVENT
{
	uint32 player_id;
	std::chrono::steady_clock::time_point wakeup_time;
	DB_EVENT_TYPE event;
	DB_PLAYER_INFO player_info;
	uint64 session_token = 0;
	uint64 sequence = 0;
};

enum TIMER_EVENT_TYPE
{
	EV_RANOM_MOVE,
	EV_NPC_RESPAWN,
	EV_NPC_ATTACK_TO_PLAYER,
	EV_HEAL,
	EV_PLAYER_RESPAWN,
	EV_AGGRO_MOVE
};

struct TIMER_EVENT
{
	uint32 player_id;
	std::chrono::steady_clock::time_point wakeup_time;
	TIMER_EVENT_TYPE event;
	uint32 aiTargetId;
	uint64 sourceEpoch = 0;
	uint64 sequence = 0;
};
