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
	int      _playerId = 0;  // Players.playerId (DB가 발급한 정수 PK). 로그인/계정생성 시 한 번만 조회해 캐싱한다.
	string   _name;
	string   _password;
	int      _x     = 0;
	int      _y     = 0;
	uint8_t  _level = 1;
	uint32_t _exp   = 0;
};
using DB_PLAYER_INFO = DB_USER_INFO;

// ── DB item info (must come before Core\DBConnection.h which uses it) ──
// Inventory 테이블 한 행. ItemTableRow.h를 끌어오지 않도록 itemId는
// ItemTableId가 아니라 그냥 uint16_t로 둔다(DB_USER_INFO가 Stat.h 타입을
// 쓰지 않는 것과 같은 이유).
struct DB_ITEM_INFO
{
	uint16_t _slotIndex = 0;
	uint16_t _itemId    = 0;
	uint16_t _count     = 0;
	bool     _equipped  = false;
};

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

// 인벤토리 슬롯 하나를 즉시(디바운스 없이) 저장/삭제하기 위한 이벤트.
// Inventory의 각 mutating 메서드가 성공할 때마다 바로 Schedule된다.
// playerId는 로그인 시 한 번 캐싱된 정수 PK를 그대로 쓴다 — name(NVARCHAR) 기반
// 조회/조인이 DB 쪽에서 완전히 사라진다.
struct DB_ITEM_SAVE_EVENT : DB_EVENT_BASE
{
	int      playerId  = 0;
	uint16_t slotIndex = 0;
	uint16_t itemId    = 0;
	uint16_t count     = 0;
	bool     equipped  = false;
};

struct DB_ITEM_DELETE_EVENT : DB_EVENT_BASE
{
	int      playerId  = 0;
	uint16_t slotIndex = 0;
};

// ── Timer event types (previously in Core/GameServerCore.h) ──────────────────
enum TIMER_EVENT_TYPE
{
	EV_RANOM_MOVE,
	EV_MONSTER_RESPAWN,
	EV_MONSTER_ATTACK_TO_USER,
	EV_HEAL,
	EV_USER_RESPAWN,
	EV_AGGRO_MOVE,
	EV_ITEM_DESPAWN,
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
