// ─────────────────────────────────────────────────────────────────────────────
// 샌드박스용 인메모리 DB — GameServer/Core/DBConnection.cpp 대신 링크한다.
//
// 목적: MSSQL + ODBC가 없는 곳(리눅스 샌드박스에서 MinGW로 교차 빌드한 서버를 Wine으로
// 띄울 때)에서도 로그인 · 인벤토리 · 그로시 · 상점 · 진행 저장이 **같은 의미**로 돌게 한다.
// Database/setup.sql의 프로시저를 한 줄씩 옮겼다(이름 · 인자 · 반환 모양이 같다).
//
//  · 모든 테이블은 프로세스 메모리 안의 맵이고, 한 번에 한 호출만(전역 뮤텍스) 처리한다.
//    프로시저 하나 = 락 한 번 = 트랜잭션 하나(중간 상태가 다른 호출에 보이지 않는다).
//  · 서버를 다시 켜면 비어 있다(새 캐릭터로 시작). 실제 DB 동작 확인은 "실기 확인 필요".
//  · MSVC 빌드(GameServer.vcxproj)는 이 파일을 쓰지 않는다 — tools/sandbox/build_server.sh 전용.
// ─────────────────────────────────────────────────────────────────────────────
#include "pch.h"
#include "DBConnection.h"
#include "Data/WorldData.h"

#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>

namespace
{
	struct PlayerRow
	{
		int         playerId = 0;
		std::string password;
		int         x = 0, y = 0;
		uint8_t     level = 1;
		uint32_t    exp = 0;
		uint32_t    adena = 0;
		int16_t     alignment = 0;
		uint16_t    pkCount = 0;
	};

	struct InvRow
	{
		uint16_t itemId = 0;
		uint16_t count = 0;
		bool     equipped = false;
		uint8_t  enchant = 0;
		uint64_t extra = 0;
	};

	struct MemDb
	{
		std::mutex lock;
		std::map<std::string, PlayerRow> players;                    // name → 행 (Players)
		std::map<int, std::string> nameOf;                           // playerId → name
		std::map<int, std::map<uint16_t, InvRow>> inventory;         // playerId → slot → 행 (Inventory)
		std::map<int, std::vector<uint8_t>> progress;                // PlayerProgress
		std::map<int, std::map<uint16_t, DB_SKILL_INFO>> skills;     // PlayerSkills(playerId → skillId → 행)
		int nextPlayerId = 1;
	};

	MemDb& Db()
	{
		static MemDb db;
		return db;
	}

	// SaveInventorySlot / SaveTwoInventorySlots / ShopTransaction의 한 슬롯(upsert 또는 삭제)
	void ApplySlot(MemDb& db, int playerId, const DB_ITEM_SLOT_SAVE& s)
	{
		auto& inv = db.inventory[playerId];
		if (!s.hasItem || s.itemId == 0 || s.count == 0)
		{
			inv.erase(s.slotIndex);
			return;
		}
		InvRow& r = inv[s.slotIndex];
		r.itemId = s.itemId;
		r.count = s.count;
		r.equipped = s.equipped;
		r.enchant = s.enchant;
		r.extra = s.extra & 0x3FFFFFFFFFFFFFFFull;
	}
}

DBConnection::~DBConnection() { Clear(); }

bool DBConnection::Connect() { return true; }
void DBConnection::Clear() {}
bool DBConnection::Execute(const WCHAR*) { return true; }
bool DBConnection::Fetch() { return false; }
int DBConnection::GetRowCount() { return 0; }
void DBConnection::Unbind() {}
bool DBConnection::BindParam(SQLUSMALLINT, SQLSMALLINT, SQLSMALLINT, SQLULEN, SQLPOINTER, SQLLEN*) { return true; }
bool DBConnection::BindCol(SQLUSMALLINT, SQLSMALLINT, SQLULEN, SQLPOINTER, SQLLEN*) { return true; }
void DBConnection::HandleError(SQLRETURN) {}

bool DBConnection::IsUserRegistered(const string& name)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (db.players.count(name) != 0)
		return true;
	// 샌드박스 시험용: "lvNN_…" 이름은 처음 접속에서도 NN레벨로 들어오게, 마을 시작 칸에 미리 만들어 둔다
	// (신규 캐릭터 경로는 레벨 1로 들어오므로). 실제 서버(MSSQL)에는 이런 규칙이 없다.
	if (name.size() > 4 && name[0] == 'l' && name[1] == 'v' && std::isdigit(static_cast<unsigned char>(name[2])))
	{
		const int lv = std::atoi(name.c_str() + 2);
		if (lv < 1 || lv > 60)
			return false;
		PlayerRow row;
		row.playerId = db.nextPlayerId++;
		const size_t pick = std::hash<string>{}(name) % WorldData::kTownSpawnPointCount;
		row.x = WorldData::kTownSpawnPoints[pick].x;
		row.y = WorldData::kTownSpawnPoints[pick].y;
		row.level = static_cast<uint8_t>(lv);
		row.exp = 0;
		db.players.emplace(name, row);
		db.nameOf[row.playerId] = name;
		return true;
	}
	return false;
}

bool DBConnection::VerifyUserPassword(const string& name, const string& password)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	const auto it = db.players.find(name);
	return it != db.players.end() && it->second.password == password;
}

bool DBConnection::AddUserInfoInDataBase(const string& name, const string& password, short x, short y, uint8 level, uint32 exp, int& outPlayerId)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	auto it = db.players.find(name);
	if (it == db.players.end())
	{
		// AddNewPlayer: 없을 때만 넣고(UPDLOCK, HOLDLOCK), 있든 없든 그 이름의 playerId를 돌려준다.
		PlayerRow row;
		row.playerId = db.nextPlayerId++;
		row.password = password;
		row.x = x;
		row.y = y;
		row.level = level;
		row.exp = exp;
		// 샌드박스 시험용: 이름이 "lvNN_…"이면 NN레벨로 만든다(기술 녹화 · 시험장 — 모든 기술을 쓸 수 있게).
		// 이 파일은 샌드박스 빌드 전용이라 실제 서버(MSSQL)에는 이런 규칙이 없다.
		if (name.size() > 4 && name[0] == 'l' && name[1] == 'v' && std::isdigit(static_cast<unsigned char>(name[2])))
		{
			const int lv = std::atoi(name.c_str() + 2);
			if (lv >= 1 && lv <= 60)
				row.level = static_cast<uint8_t>(lv);
		}
		it = db.players.emplace(name, row).first;
		db.nameOf[row.playerId] = name;
	}
	outPlayerId = it->second.playerId;
	return true;
}

DB_PLAYER_INFO DBConnection::ExtractUserInfo(const string& name)
{
	DB_PLAYER_INFO info{};
	auto& db = Db();
	std::scoped_lock g(db.lock);
	const auto it = db.players.find(name);
	if (it == db.players.end())
		return info;
	const PlayerRow& r = it->second;
	info._playerId = r.playerId;
	info._name = name;
	info._x = r.x;
	info._y = r.y;
	info._level = r.level;
	info._exp = r.exp;
	info._adena = r.adena;
	info._alignment = r.alignment;
	info._pkCount = r.pkCount;
	return info;
}

bool DBConnection::SaveUserInfo(const string& name, short x, short y, uint8 level, uint32 exp)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	const auto it = db.players.find(name);
	if (it == db.players.end())
		return true;   // UPDATE ... WHERE name = @name — 행이 없으면 0행 갱신(오류 아님)
	it->second.x = x;
	it->second.y = y;
	it->second.level = level;
	it->second.exp = exp;
	return true;
}

vector<DB_ITEM_INFO> DBConnection::ExtractInventory(int playerId)
{
	vector<DB_ITEM_INFO> items;
	auto& db = Db();
	std::scoped_lock g(db.lock);
	const auto it = db.inventory.find(playerId);
	if (it == db.inventory.end())
		return items;
	for (const auto& [slot, r] : it->second)
	{
		DB_ITEM_INFO info{};
		info._slotIndex = slot;
		info._itemId = r.itemId;
		info._count = r.count;
		info._equipped = r.equipped;
		info._enchant = r.enchant;
		info._extra = r.extra;
		items.push_back(info);
	}
	return items;
}

bool DBConnection::SaveInventorySlot(int playerId, uint16_t slotIndex, uint16_t itemId, uint16_t count, bool equipped, uint8_t enchant, uint64_t extra)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	DB_ITEM_SLOT_SAVE s;
	s.slotIndex = slotIndex;
	s.hasItem = true;
	s.itemId = itemId;
	s.count = count;
	s.equipped = equipped;
	s.enchant = enchant;
	s.extra = extra;
	ApplySlot(db, playerId, s);
	return true;
}

bool DBConnection::DeleteInventorySlot(int playerId, uint16_t slotIndex)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	db.inventory[playerId].erase(slotIndex);
	return true;
}

bool DBConnection::SaveTwoInventorySlots(int playerId, const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);   // 두 슬롯이 한 트랜잭션
	ApplySlot(db, playerId, a);
	ApplySlot(db, playerId, b);
	return true;
}

bool DBConnection::SavePlayerAdena(int playerId, uint32_t adena)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	const auto n = db.nameOf.find(playerId);
	if (n != db.nameOf.end())
		db.players[n->second].adena = adena;
	return true;
}

bool DBConnection::SavePlayerAlignment(int playerId, int16_t alignment, uint16_t pkCount)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	const auto n = db.nameOf.find(playerId);
	if (n != db.nameOf.end())
	{
		db.players[n->second].alignment = alignment;
		db.players[n->second].pkCount = pkCount;
	}
	return true;
}

bool DBConnection::ShopTransaction(int playerId, uint32_t adena, const DB_ITEM_SLOT_SAVE& slot)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);   // 그로시 + 슬롯 = 한 트랜잭션
	const auto n = db.nameOf.find(playerId);
	if (n != db.nameOf.end())
		db.players[n->second].adena = adena;
	ApplySlot(db, playerId, slot);
	return true;
}

bool DBConnection::LoadPlayerProgress(int playerId, std::vector<uint8_t>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	const auto it = db.progress.find(playerId);
	if (it != db.progress.end())
		out = it->second;
	return true;
}

bool DBConnection::SavePlayerProgress(int playerId, const std::vector<uint8_t>& data)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	db.progress[playerId] = data;
	return true;
}

// ── Phase 2-D 기술 성장(PlayerSkills) — setup.sql LoadPlayerSkills · SavePlayerSkill · ResetPlayerSkills와 같은 의미 ──
bool DBConnection::LoadPlayerSkills(int playerId, std::vector<DB_SKILL_INFO>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	const auto it = db.skills.find(playerId);
	if (it != db.skills.end())
		for (const auto& [id, row] : it->second)
			out.push_back(row);
	return true;
}

bool DBConnection::SavePlayerSkill(int playerId, const DB_SKILL_INFO& skill)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);   // upsert(기본값이면 삭제) = 한 트랜잭션
	if (skill._level < 1 || skill._level > 10 || skill._pick1 > 2 || skill._pick2 > 2)
		return false;                 // CHECK 제약과 같다
	if (skill._level <= 1 && skill._pick1 == 0 && skill._pick2 == 0)
		db.skills[playerId].erase(skill._skillId);
	else
		db.skills[playerId][skill._skillId] = skill;
	return true;
}

bool DBConnection::ResetPlayerSkills(int playerId)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	db.skills.erase(playerId);
	return true;
}
