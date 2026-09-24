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
// windows.h의 min/max 매크로가 std::min/std::max(템플릿 인자 없이 쓴 호출)를 망가뜨리지 않게 한다.
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
	// 리니지풍 컨텐츠 확장 컬럼(Players.adena/alignment/pkCount)
	uint32_t _adena     = 0;
	int16_t  _alignment = 0;
	uint16_t _pkCount   = 0;
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
	uint8_t  _enchant   = 0;   // Inventory.enchant (무기 강화 수치)
};

// 슬롯 두 개를 한 SQL 트랜잭션으로 함께 반영하기 위한 단위 데이터(DBConnection::
// SaveTwoInventorySlots가 파라미터로 받으므로 그 선언보다 먼저 와야 한다).
// hasItem=false면 그 슬롯은 삭제 대상(itemId/count/equipped는 무시된다).
struct DB_ITEM_SLOT_SAVE
{
	uint16_t slotIndex = 0;
	bool     hasItem   = false;
	uint16_t itemId    = 0;
	uint16_t count     = 0;
	bool     equipped  = false;
	uint8_t  enchant   = 0;
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
	uint8_t  enchant   = 0;
};

struct DB_ITEM_DELETE_EVENT : DB_EVENT_BASE
{
	int      playerId  = 0;
	uint16_t slotIndex = 0;
};

// 두 슬롯이 "동시에" 바뀌는 동작(장착 시 이전 장비 자동 탈착, 슬롯 교체)을 위한
// 이벤트. 두 슬롯을 별도 이벤트 두 개로 나눠 보내면, DB 워커 스레드가 8개라
// 어느 한쪽만 먼저 반영된 채로 서버가 죽는 순간 DB에는 "두 개 다 장착됨" 같은
// 불변조건 위반 상태가 영구히 남을 수 있다 — 그래서 반드시 하나의 이벤트,
// 하나의 트랜잭션으로 묶는다(DBConnection::SaveTwoInventorySlots 참고).
struct DB_ITEM_SAVE_TWO_EVENT : DB_EVENT_BASE
{
	int               playerId = 0;
	DB_ITEM_SLOT_SAVE a;
	DB_ITEM_SLOT_SAVE b;
};

// ── 리니지풍 컨텐츠 확장 DB 이벤트 ─────────────────────────────────────────────
// 전부 playerId 기준 샤드로 보낸다 — 인벤토리 슬롯 저장과 같은 샤드라서, 한
// 플레이어의 "아데나 변경"과 "슬롯 변경"이 넣은 순서 그대로 커밋된다.
// 값은 항상 델타가 아니라 절대값(메모리의 최신 상태)을 싣는다: 메모리가 권위이고
// DB는 그 저널이므로, 마지막으로 커밋된 이벤트가 곧 최신 상태가 된다.

// 아데나 획득(줍기) 등, 슬롯 변화 없이 지갑만 바뀐 경우.
struct DB_ADENA_SAVE_EVENT : DB_EVENT_BASE
{
	int      playerId = 0;
	uint32_t adena    = 0;
};

// 상점 구매/판매: 아데나와 슬롯 하나를 "하나의 SQL 트랜잭션"으로 반영한다.
// 따로 저장하면 그 사이에 서버가 죽었을 때 "돈은 나갔는데 물건이 없음" 또는
// "물건은 들어왔는데 돈이 그대로"(=복사) 상태가 DB에 영구히 남는다.
struct DB_SHOP_TX_EVENT : DB_EVENT_BASE
{
	int               playerId = 0;
	uint32_t          adena    = 0;
	DB_ITEM_SLOT_SAVE slot;
};

struct DB_ALIGNMENT_SAVE_EVENT : DB_EVENT_BASE
{
	int      playerId  = 0;
	int16_t  alignment = 0;
	uint16_t pkCount   = 0;
};

// V2 캐릭터 진행 데이터(직업·포인트·퀘스트…) 저장 — 인벤토리와 같은 playerId 샤드
struct DB_PROGRESS_SAVE_EVENT : DB_EVENT_BASE
{
	int                  playerId = 0;
	std::vector<uint8_t> data;
};

// ── Timer event types ────────────────────────────────────────────────────────
enum TIMER_EVENT_TYPE
{
	EV_RANOM_MOVE,
	EV_MONSTER_RESPAWN,
	EV_MONSTER_ATTACK_TO_USER,
	EV_HEAL,
	EV_USER_RESPAWN,
	EV_AGGRO_MOVE,
	EV_ITEM_DESPAWN,
	EV_ITEM_LOOT_PRIORITY_EXPIRE,
	// 리니지풍 컨텐츠 확장
	EV_BOSS_SKILL_CAST,       // 보스 광역기 예고
	EV_BOSS_SKILL_IMPACT,     // 보스 광역기 적중(예고 1.2초 뒤)
	EV_PURPLE_EXPIRE,         // 보라돌이 해제 확인
	EV_HASTE_EXPIRE,          // 가속 해제 확인
	// V2 전투
	EV_SKILL_IMPACT,          // 예고 뒤 스킬 판정(targetId.DatabaseID = 예약 번호)
	EV_STATUS_TICK,           // 지속 피해 틱
	EV_MONSTER_PATTERN,       // 몬스터 강공격 판정(예고 뒤)
	// 살아 있는 마을
	EV_NPC_TICK,              // 마을 NPC 배회·혼잣말(마을 Zone 스레드, 0.25초마다)
};

struct TIMER_EVENT
{
	ObjID subjectId;
	std::chrono::steady_clock::time_point wakeupTime;
	TIMER_EVENT_TYPE event;
	ObjID targetId;
	uint64_t sequence = 0;
	// 예약을 건 시점에 찍어두는 목적지 Zone. TimerThread::Dispatch가 이 값으로
	// 곧장 큐에 넣어, 전역 조회표(ZoneManager::_objectZones)를 뒤지지 않는다.
	// 자세한 조건은 TimerThread::Schedule / Dispatch의 주석 참고.
	ZoneId zoneId = InvalidZoneId;
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
