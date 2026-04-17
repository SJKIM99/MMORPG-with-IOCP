#pragma once

#include "Types.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

enum EnumCategory : uint16_t
{
	eNone = 0,
	eAccount = 1,
	eUser = 2,
	eMonster = 3,
	eStat = 4,
	eUserLv = 5,
	eHp = 6,
	eExp = 7,
};

struct ContentID
{
protected:
	union
	{
		struct
		{
			uint16_t m_Category;
			uint16_t m_KIND;
			uint32_t m_Serial;
		};
		uint64_t m_ContentID;
	};

public:
	constexpr ContentID() noexcept : m_Category(0), m_KIND(0), m_Serial(0) {}
	constexpr explicit ContentID(uint64_t initContentID) noexcept : m_ContentID(initContentID) {}

	[[nodiscard]] constexpr uint16_t GetCategory() const noexcept { return m_Category; }
	[[nodiscard]] constexpr uint64_t GetContentID() const noexcept { return m_ContentID; }

	template <typename T>
	[[nodiscard]] constexpr T GetCategory() const noexcept
	{
		return static_cast<T>(m_Category);
	}

	template <typename T>
	constexpr void SetCategory(T initCategory) noexcept
	{
		m_Category = static_cast<uint16_t>(initCategory);
	}

	constexpr void SetContentID(uint64_t initContentID) noexcept
	{
		m_ContentID = initContentID;
	}
};

struct ObjID : public ContentID
{
	uint64_t m_DatabaseID{};

	constexpr ObjID() noexcept = default;
	constexpr ObjID(const ObjID&) noexcept = default;
	constexpr ObjID(const ContentID& initContentID) noexcept : ContentID(initContentID), m_DatabaseID(0) {}
	constexpr ObjID(uint64_t initContentID, uint64_t initDatabaseID) noexcept
		: ContentID(initContentID), m_DatabaseID(initDatabaseID) {}

	template <typename T>
	constexpr ObjID(T initCategory, uint64_t initDatabaseID) noexcept
	{
		SetCategory(initCategory);
		SetDatabaseID(initDatabaseID);
	}

	[[nodiscard]] constexpr uint64_t GetDatabaseID() const noexcept { return m_DatabaseID; }
	constexpr void SetDatabaseID(uint64_t value) noexcept { m_DatabaseID = value; }

	[[nodiscard]] constexpr ObjID& GetObjID() noexcept { return *this; }
	[[nodiscard]] constexpr const ObjID& GetObjID() const noexcept { return *this; }

	constexpr ObjID& operator=(const ObjID&) noexcept = default;
	[[nodiscard]] constexpr bool operator==(const ObjID& other) const noexcept
	{
		return m_ContentID == other.m_ContentID && m_DatabaseID == other.m_DatabaseID;
	}
	[[nodiscard]] constexpr bool operator!=(const ObjID& other) const noexcept
	{
		return !(*this == other);
	}
};

static_assert(sizeof(ContentID) == 8, "ContentID layout must match GameServer.");
static_assert(sizeof(ObjID) == 16, "ObjID layout must match GameServer.");

constexpr int PORT_NUM = 4000;

constexpr int NAME_SIZE     = 20;
constexpr int PASSWORD_SIZE = 20;
constexpr int CHAT_SIZE     = 20;

constexpr int MAX_USER    = 20000;
constexpr int MAX_MONSTER = 200000;

constexpr uint32_t PLAYER_ID_START  = 1;
constexpr uint32_t MONSTER_ID_START = 1'000'000'000;
constexpr uint32_t AGGRO_MONSTER_BOUNDARY = MONSTER_ID_START + static_cast<uint32_t>(MAX_MONSTER / 4) - 1;

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
constexpr int WAKE_RANGE   = 3;

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

	// Server → Client, unicast ACK (response to requester only)
	USER_LOGIN_ACK,
	USER_LOGIN_FAIL_ACK,
	USER_ATTACK_ACK,

	// Server → Client(s), INF (server-initiated notification / broadcast)
	SUBJECT_ADD_NFY,
	SUBJECT_MOVE_NFY,
	SUBJECT_REMOVE_NFY,
	SUBJECT_DIE_NFY,
	SUBJECT_RESPAWN_NFY,
	SUBJECT_ATTACK_NFY,
	USER_HEAL_INF,
	USER_STAT_CHANGE_INF,
};

#pragma pack(push, 1)
struct USER_LOGIN_REQ_PACKET
{
	unsigned short size;
	char type;
	char name[NAME_SIZE];
	char password[PASSWORD_SIZE];
};

struct USER_MOVE_REQ_PACKET
{
	unsigned short size;
	char type;
	char direction;
	uint32_t move_time;
};

struct USER_ATTACK_REQ_PACKET
{
	unsigned short size;
	char type;
	uint32_t attack_time;
	uint8_t facing;
};

struct USER_SKILL_REQ_PACKET
{
	unsigned short size;
	char type;
};

struct USER_TELEPORT_REQ_PACKET
{
	unsigned short size;
	char type;
};

struct USER_LOGOUT_REQ_PACKET
{
	unsigned short size;
	char type;
};

constexpr size_t ProtocolConstMaxSize(size_t lhs, size_t rhs)
{
	return (lhs > rhs) ? lhs : rhs;
}

constexpr size_t MAX_CLIENT_PACKET_SIZE =
	ProtocolConstMaxSize(
		sizeof(USER_LOGIN_REQ_PACKET),
		ProtocolConstMaxSize(
			sizeof(USER_MOVE_REQ_PACKET),
			ProtocolConstMaxSize(
				sizeof(USER_ATTACK_REQ_PACKET),
				ProtocolConstMaxSize(
					sizeof(USER_SKILL_REQ_PACKET),
					ProtocolConstMaxSize(sizeof(USER_TELEPORT_REQ_PACKET), sizeof(USER_LOGOUT_REQ_PACKET))
				)
			)
		)
	);

struct USER_LOGIN_ACK_PACKET
{
	unsigned short size;
	char type;
	ObjID id;
	short x, y;
	uint16_t maxhp;
	uint16_t hp;
	uint8_t level;
	uint32_t exp;
};

struct USER_LOGIN_FAIL_ACK_PACKET
{
	unsigned short size;
	char type;
};

struct SUBJECT_ADD_NFY_PACKET
{
	unsigned short size;
	char type;
	char monster_type;
	ObjID id;
	short x, y;
	char name[NAME_SIZE];
};

struct SUBJECT_REMOVE_NFY_PACKET
{
	unsigned short size;
	char type;
	ObjID id;
};

struct SUBJECT_MOVE_NFY_PACKET
{
	unsigned short size;
	char type;
	ObjID id;
	short x, y;
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
	short x, y;
	uint16_t hp;
	char name[NAME_SIZE];
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
	char type;
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
	char type;
	uint8_t level;
	uint16_t hp;
	uint16_t maxhp;
	uint32_t exp;
};
#pragma pack(pop)
