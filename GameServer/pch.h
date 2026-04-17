#pragma once

#define WIN32_LEAN_AND_MEAN

// ── Standard library ──────────────────────────────────────────────────────────
#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <set>
#include <shared_mutex>
#include <stack>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ── Windows / Winsock ─────────────────────────────────────────────────────────
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "odbc32.lib")

using namespace std;

// ── Primitive type aliases (replaces Core/Types.h) ───────────────────────────
using int8   = int8_t;
using int16  = int16_t;
using int32  = int32_t;
using int64  = int64_t;
using uint8  = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

// Sync primitives used by remaining Core headers
using Mutex     = std::mutex;
using RWLock    = std::shared_mutex;
using LockGuard = std::lock_guard<std::mutex>;
template<typename T>
using Atomic    = std::atomic<T>;

// ── Crash assertion (replaces Core/CoreMacro.h) ──────────────────────────────
#define ASSERT_CRASH(expr) \
	do { if (!(expr)) { volatile int* _p = nullptr; *_p = 0; } } while (false)

// ── DB user info (must come before Core\DBConnection.h which uses DB_USER_INFO) ──
struct DB_USER_INFO
{
	string   _name;
	string   _password;
	int      _x     = 0;
	int      _y     = 0;
	uint8_t  _level = 1;
	uint32_t _exp   = 0;
};
using DB_PLAYER_INFO = DB_USER_INFO;

// ── Core headers still in use ─────────────────────────────────────────────────
#include "Core\ThreadManager.h"
#include "Core\DBConnectionPool.h"
#include "Core\DBConnection.h"

// ── Project headers ───────────────────────────────────────────────────────────
#include "ContentID.h"
#include "ObjID.h"
#include "Protocol.h"
#include "ServerGlobal.h"
#include "EnumCategory.h"

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
	ObjID    subjectId;
	string   name;
	short    x     = 0;
	short    y     = 0;
	uint8_t  level = 1;
	uint32_t exp   = 0;
};

struct DB_ADD_EVENT : DB_EVENT_BASE
{
	ObjID    subjectId;
	string   name;
	string   password;
	short    x     = 0;
	short    y     = 0;
	uint8_t  level = 1;
	uint32_t exp   = 0;
};

// ── Timer event types (previously in Core/GameServerCore.h) ──────────────────
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

[[nodiscard]] inline uint32_t GetNowTime()
{
	return static_cast<uint32_t>(chrono::duration_cast<chrono::milliseconds>(
		chrono::steady_clock::now().time_since_epoch()).count());
}

// ── IO / Socket state enums ───────────────────────────────────────────────────
enum IO_TYPE
{
	IO_ACCEPT,
	IO_RECV,
	IO_SEND,
};

enum SOCKET_STATE
{
	ST_FREE,
	ST_ALLOC,
	ST_INGAME
};

#include "GameObjectManager.h"
