// ─────────────────────────────────────────────────────────────────────────────
// 샌드박스용 인메모리 DB — GameServer/Core/DBConnection.cpp 대신 링크한다.
//
// 목적: MSSQL + ODBC가 없는 곳(리눅스 샌드박스에서 MinGW로 교차 빌드한 서버를 Wine으로
// 띄울 때)에서도 로그인 · 인벤토리 · 그로시 · 상점 · 진행 저장 · 결사 · 경매가 **같은 의미**로 돌게 한다.
// Database/setup.sql의 프로시저를 한 줄씩 옮겼다(이름 · 인자 · 반환 모양이 같다).
//
//  · 모든 테이블은 프로세스 메모리 안의 맵이고, 한 번에 한 호출만(전역 뮤텍스) 처리한다.
//    프로시저 하나 = 락 한 번 = 트랜잭션 하나(중간 상태가 다른 호출에 보이지 않는다).
//  · **커밋 일지(Phase 5 장애 시험)**: HARVEN_MEMDB_JOURNAL=경로 이면 바꾸는 호출마다 한 줄(적용한 뒤, 락 안에서 쓰고 flush)을
//    붙이고, 켤 때 그 파일을 처음부터 다시 적용한다. 줄 하나 = 커밋 하나 — 끝이 잘린 줄(쓰다가 죽음)은 버린다.
//    그래서 프로세스를 죽였다 켜면 "커밋된 것만 남는" DB처럼 동작한다. 일지가 없으면 예전처럼 켤 때마다 비어 있다.
//  · HARVEN_DB_BUY_DELAY_MS=N 이면 AuctionBuy가 트랜잭션 안(적용 전)에서 N ms 멈춘다 — 그 사이 서버를 죽이는 장애 시험용.
//  · 실제 DB 동작 확인은 "실기 확인 필요". MSVC 빌드(GameServer.vcxproj)는 이 파일을 쓰지 않는다 — tools/sandbox/build_server.sh 전용.
// ─────────────────────────────────────────────────────────────────────────────
#include "pch.h"
#include "DBConnection.h"
#include "Data/WorldData.h"
#include "Data/GuildAuction.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

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

	struct GuildRowM { std::string name, nameKey, notice; uint8_t emblem = 0; int leaderId = 0; };
	struct MemberRowM { int guildId = 0; uint8_t rank = 3; int64_t lastSeen = 0; };

	struct MemDb
	{
		std::mutex lock;
		std::map<std::string, PlayerRow> players;                    // name → 행 (Players)
		std::map<int, std::string> nameOf;                           // playerId → name
		std::map<int, std::map<uint16_t, InvRow>> inventory;         // playerId → slot → 행 (Inventory)
		std::map<int, std::vector<uint8_t>> progress;                // PlayerProgress
		std::map<int, std::map<uint16_t, DB_SKILL_INFO>> skills;     // PlayerSkills(playerId → skillId → 행)
		int nextPlayerId = 1;
		// Phase 5
		std::map<int, GuildRowM> guilds;                             // Guilds
		std::map<int, MemberRowM> members;                           // GuildMembers(playerId → 행)
		std::map<int64_t, DB_LISTING_ROW> listings;                  // AuctionListings(모든 상태)
		std::map<int64_t, DB_SETTLEMENT_ROW> settlements;            // AuctionSettlements(받은 행은 claimedAt으로 표시 → 지움)
		std::map<int64_t, int64_t> claimedAt;                        // settlementId → 받은 시각
		std::vector<DB_PRICE_ROW> prices;                            // AuctionPriceHistory
		// 일지
		FILE* journal = nullptr;
		bool replaying = false;
	};

	MemDb& DbRaw()
	{
		static MemDb db;
		return db;
	}

	// ── 일지 한 줄: 필드를 '|'로 잇는다. 문자열은 16진으로(구분자 · 줄바꿈과 겹치지 않게) ──
	struct Rec
	{
		std::ostringstream o;
		explicit Rec(const char* op) { o << op; }
		Rec& operator<<(long long v) { o << '|' << v; return *this; }
		Rec& s(const std::string& str)
		{
			static const char* kHex = "0123456789abcdef";
			o << '|';
			if (str.empty()) o << '-';
			for (unsigned char c : str) o << kHex[c >> 4] << kHex[c & 15];
			return *this;
		}
		Rec& u64(uint64_t v) { o << '|' << v; return *this; }
	};

	void Journal(MemDb& db, const Rec& r)
	{
		if (db.replaying || db.journal == nullptr) return;
		const std::string line = r.o.str() + "\n";
		std::fwrite(line.data(), 1, line.size(), db.journal);
		std::fflush(db.journal);   // 프로세스가 죽어도 OS에 넘어간 줄은 남는다(전원 차단은 시험하지 않는다)
	}

	// 슬롯 한 칸(upsert 또는 삭제)
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
	void RecSlot(Rec& r, const DB_ITEM_SLOT_SAVE& s)
	{
		r << s.slotIndex << (s.hasItem ? 1 : 0) << s.itemId << s.count << (s.equipped ? 1 : 0) << s.enchant;
		r.u64(s.extra);
	}
	PlayerRow* PlayerById(MemDb& db, int playerId)
	{
		const auto n = db.nameOf.find(playerId);
		if (n == db.nameOf.end()) return nullptr;
		const auto it = db.players.find(n->second);
		return it == db.players.end() ? nullptr : &it->second;
	}

	// ── Phase 5 적용 함수(살아 있는 호출과 일지 다시 적용이 같은 함수를 쓴다) ──
	bool ApplyGuildCreate(MemDb& db, int pid, uint32_t adena, int gid, const std::string& name, const std::string& key, uint8_t emblem)
	{
		if (db.guilds.count(gid) || db.members.count(pid)) return false;
		for (const auto& [id, g] : db.guilds) if (g.nameKey == key) return false;   // UQ_Guilds_NameKey
		PlayerRow* p = PlayerById(db, pid);
		if (p == nullptr) return false;
		p->adena = adena;
		db.guilds[gid] = GuildRowM{ name, key, "", emblem, pid };
		db.members[pid] = MemberRowM{ gid, static_cast<uint8_t>(GuildRule::Rank::Leader), 0 };
		return true;
	}
	bool ApplyMemberSet(MemDb& db, int gid, int pid, uint8_t rank)
	{
		if (!db.guilds.count(gid)) return false;
		auto it = db.members.find(pid);
		if (it != db.members.end() && it->second.guildId != gid) return false;   // 다른 결사
		if (it == db.members.end()) db.members[pid] = MemberRowM{ gid, rank, 0 };
		else it->second.rank = rank;
		return true;
	}
	bool ApplyMemberRemove(MemDb& db, int gid, int pid)
	{
		auto it = db.members.find(pid);
		if (it == db.members.end() || it->second.guildId != gid) return false;
		db.members.erase(it);
		return true;
	}
	bool ApplyTransfer(MemDb& db, int gid, int from, int to)
	{
		auto g = db.guilds.find(gid);
		auto a = db.members.find(from), b = db.members.find(to);
		if (g == db.guilds.end() || a == db.members.end() || b == db.members.end() || a->second.guildId != gid || b->second.guildId != gid) return false;
		a->second.rank = static_cast<uint8_t>(GuildRule::Rank::Officer);
		b->second.rank = static_cast<uint8_t>(GuildRule::Rank::Leader);
		g->second.leaderId = to;
		return true;
	}
	bool ApplySetInfo(MemDb& db, int gid, uint8_t emblem, const std::string& notice)
	{
		auto g = db.guilds.find(gid);
		if (g == db.guilds.end()) return false;
		g->second.emblem = emblem;
		g->second.notice = notice;
		return true;
	}
	bool ApplyDisband(MemDb& db, int gid)
	{
		if (!db.guilds.erase(gid)) return false;
		for (auto it = db.members.begin(); it != db.members.end();)
			it = it->second.guildId == gid ? db.members.erase(it) : std::next(it);
		return true;
	}
	bool ApplyRegister(MemDb& db, int pid, uint32_t adena, const DB_ITEM_SLOT_SAVE& slot, const DB_LISTING_ROW& l)
	{
		PlayerRow* p = PlayerById(db, pid);
		if (p == nullptr || db.listings.count(l.listingId)) return false;
		p->adena = adena;
		ApplySlot(db, pid, slot);
		DB_LISTING_ROW row = l;
		row.status = 0;
		db.listings[l.listingId] = row;
		return true;
	}
	bool ApplyBuy(MemDb& db, int pid, uint32_t adena, int64_t lid, uint32_t price, int64_t bs, int64_t ss, uint32_t gets, int64_t now)
	{
		auto it = db.listings.find(lid);
		if (it == db.listings.end() || it->second.status != 0 || it->second.price != price) return false;   // 판매 중일 때만
		PlayerRow* p = PlayerById(db, pid);
		if (p == nullptr || db.settlements.count(bs) || db.settlements.count(ss)) return false;
		DB_LISTING_ROW& l = it->second;
		l.status = 1;
		p->adena = adena;
		DB_SETTLEMENT_ROW item{};
		item.settlementId = bs; item.playerId = pid; item.kind = 2; item.itemId = l.itemId; item.count = l.count; item.enchant = l.enchant; item.extra = l.extra;
		item.listingId = lid; item.reason = static_cast<uint8_t>(AuctionSettleReason::Bought); item.createdAt = now;
		DB_SETTLEMENT_ROW money{};
		money.settlementId = ss; money.playerId = l.sellerId; money.kind = 1; money.adena = gets; money.listingId = lid;
		money.reason = static_cast<uint8_t>(AuctionSettleReason::Sold); money.createdAt = now;
		db.settlements[bs] = item;
		db.settlements[ss] = money;
		db.prices.push_back(DB_PRICE_ROW{ l.itemId, l.count ? price / l.count : price, now });
		return true;
	}
	bool ApplyClose(MemDb& db, int64_t lid, uint8_t status, int64_t sid, int64_t now)
	{
		auto it = db.listings.find(lid);
		if (it == db.listings.end() || it->second.status != 0 || (status != 2 && status != 3) || db.settlements.count(sid)) return false;
		DB_LISTING_ROW& l = it->second;
		l.status = status;
		DB_SETTLEMENT_ROW item{};
		item.settlementId = sid; item.playerId = l.sellerId; item.kind = 2; item.itemId = l.itemId; item.count = l.count; item.enchant = l.enchant; item.extra = l.extra;
		item.listingId = lid; item.createdAt = now;
		item.reason = static_cast<uint8_t>(status == 2 ? AuctionSettleReason::Cancelled : AuctionSettleReason::Expired);
		db.settlements[sid] = item;
		return true;
	}
	bool ApplyClaim(MemDb& db, int pid, int64_t sid, uint32_t adena, const DB_ITEM_SLOT_SAVE& slot, int64_t now)
	{
		auto it = db.settlements.find(sid);
		if (it == db.settlements.end() || it->second.playerId != pid || db.claimedAt.count(sid)) return false;   // 받지 않은 내 행만
		PlayerRow* p = PlayerById(db, pid);
		if (p == nullptr) return false;
		p->adena = adena;
		if (it->second.kind == 2) ApplySlot(db, pid, slot);
		db.claimedAt[sid] = now;
		return true;
	}

	// ── 일지 다시 적용 ──
	std::vector<std::string> Split(const std::string& line)
	{
		std::vector<std::string> f;
		size_t a = 0;
		for (size_t i = 0; i <= line.size(); ++i)
			if (i == line.size() || line[i] == '|') { f.push_back(line.substr(a, i - a)); a = i + 1; }
		return f;
	}
	std::string Unhex(const std::string& h)
	{
		if (h == "-") return {};
		std::string out;
		for (size_t i = 0; i + 1 < h.size(); i += 2)
		{
			auto v = [](char c) { return c >= 'a' ? c - 'a' + 10 : c - '0'; };
			out.push_back(static_cast<char>((v(h[i]) << 4) | v(h[i + 1])));
		}
		return out;
	}
	struct F
	{
		const std::vector<std::string>& f;
		size_t i = 1;
		long long n() { return i < f.size() ? std::atoll(f[i++].c_str()) : 0; }
		uint64_t u() { return i < f.size() ? std::strtoull(f[i++].c_str(), nullptr, 10) : 0; }
		std::string s() { return i < f.size() ? Unhex(f[i++]) : std::string(); }
		DB_ITEM_SLOT_SAVE slot()
		{
			DB_ITEM_SLOT_SAVE sl;
			sl.slotIndex = static_cast<uint16_t>(n()); sl.hasItem = n() != 0; sl.itemId = static_cast<uint16_t>(n()); sl.count = static_cast<uint16_t>(n());
			sl.equipped = n() != 0; sl.enchant = static_cast<uint8_t>(n()); sl.extra = u();
			return sl;
		}
	};

	void ReplayLine(MemDb& db, const std::string& line)
	{
		const auto f = Split(line);
		if (f.empty()) return;
		F r{ f };
		const std::string& op = f[0];
		if (op == "P")
		{
			PlayerRow row;
			const std::string name = r.s();
			row.password = r.s();
			row.playerId = static_cast<int>(r.n()); row.x = static_cast<int>(r.n()); row.y = static_cast<int>(r.n());
			row.level = static_cast<uint8_t>(r.n()); row.exp = static_cast<uint32_t>(r.n()); row.adena = static_cast<uint32_t>(r.n());
			db.players[name] = row;
			db.nameOf[row.playerId] = name;
			db.nextPlayerId = std::max(db.nextPlayerId, row.playerId + 1);
		}
		else if (op == "U")
		{
			const std::string name = r.s();
			auto it = db.players.find(name);
			if (it != db.players.end()) { it->second.x = static_cast<int>(r.n()); it->second.y = static_cast<int>(r.n()); it->second.level = static_cast<uint8_t>(r.n()); it->second.exp = static_cast<uint32_t>(r.n()); }
		}
		else if (op == "A") { const int pid = static_cast<int>(r.n()); if (auto* p = PlayerById(db, pid)) p->adena = static_cast<uint32_t>(r.n()); }
		else if (op == "L") { const int pid = static_cast<int>(r.n()); if (auto* p = PlayerById(db, pid)) { p->alignment = static_cast<int16_t>(r.n()); p->pkCount = static_cast<uint16_t>(r.n()); } }
		else if (op == "S") { const int pid = static_cast<int>(r.n()); ApplySlot(db, pid, r.slot()); }
		else if (op == "T") { const int pid = static_cast<int>(r.n()); const auto a = r.slot(); const auto b = r.slot(); ApplySlot(db, pid, a); ApplySlot(db, pid, b); }
		else if (op == "X") { const int pid = static_cast<int>(r.n()); const auto adena = static_cast<uint32_t>(r.n()); if (auto* p = PlayerById(db, pid)) p->adena = adena; ApplySlot(db, pid, r.slot()); }
		else if (op == "G")
		{
			const int pid = static_cast<int>(r.n());
			const std::string data = r.s();
			db.progress[pid] = std::vector<uint8_t>(data.begin(), data.end());
		}
		else if (op == "K")
		{
			const int pid = static_cast<int>(r.n());
			DB_SKILL_INFO sk{};
			sk._skillId = static_cast<uint16_t>(r.n()); sk._level = static_cast<uint8_t>(r.n()); sk._pick1 = static_cast<uint8_t>(r.n()); sk._pick2 = static_cast<uint8_t>(r.n());
			if (sk._level <= 1 && sk._pick1 == 0 && sk._pick2 == 0) db.skills[pid].erase(sk._skillId); else db.skills[pid][sk._skillId] = sk;
		}
		else if (op == "R") db.skills.erase(static_cast<int>(r.n()));
		else if (op == "GC")
		{
			const int pid = static_cast<int>(r.n()); const auto adena = static_cast<uint32_t>(r.n()); const int gid = static_cast<int>(r.n());
			const std::string name = r.s(), key = r.s(); const auto emblem = static_cast<uint8_t>(r.n());
			ApplyGuildCreate(db, pid, adena, gid, name, key, emblem);
		}
		else if (op == "GM") { const int gid = static_cast<int>(r.n()); const int pid = static_cast<int>(r.n()); ApplyMemberSet(db, gid, pid, static_cast<uint8_t>(r.n())); }
		else if (op == "GR") { const int gid = static_cast<int>(r.n()); ApplyMemberRemove(db, gid, static_cast<int>(r.n())); }
		else if (op == "GT") { const int gid = static_cast<int>(r.n()); const int a = static_cast<int>(r.n()); ApplyTransfer(db, gid, a, static_cast<int>(r.n())); }
		else if (op == "GI") { const int gid = static_cast<int>(r.n()); const auto em = static_cast<uint8_t>(r.n()); ApplySetInfo(db, gid, em, r.s()); }
		else if (op == "GD") ApplyDisband(db, static_cast<int>(r.n()));
		else if (op == "GS") { const int pid = static_cast<int>(r.n()); auto it = db.members.find(pid); if (it != db.members.end()) it->second.lastSeen = r.n(); }
		else if (op == "AR")
		{
			const int pid = static_cast<int>(r.n()); const auto adena = static_cast<uint32_t>(r.n());
			const auto slot = r.slot();
			DB_LISTING_ROW l{};
			l.listingId = r.n(); l.sellerId = pid; l.itemId = static_cast<uint16_t>(r.n()); l.count = static_cast<uint16_t>(r.n()); l.enchant = static_cast<uint8_t>(r.n()); l.extra = r.u();
			l.price = static_cast<uint32_t>(r.n()); l.deposit = static_cast<uint32_t>(r.n()); l.createdAt = r.n(); l.expiresAt = r.n();
			ApplyRegister(db, pid, adena, slot, l);
		}
		else if (op == "AB")
		{
			const int pid = static_cast<int>(r.n()); const auto adena = static_cast<uint32_t>(r.n()); const int64_t lid = r.n(); const auto price = static_cast<uint32_t>(r.n());
			const int64_t bs = r.n(), ss = r.n(); const auto gets = static_cast<uint32_t>(r.n()); const int64_t now = r.n();
			ApplyBuy(db, pid, adena, lid, price, bs, ss, gets, now);
		}
		else if (op == "AC") { const int64_t lid = r.n(); const auto st = static_cast<uint8_t>(r.n()); const int64_t sid = r.n(); ApplyClose(db, lid, st, sid, r.n()); }
		else if (op == "SC")
		{
			const int pid = static_cast<int>(r.n()); const int64_t sid = r.n(); const auto adena = static_cast<uint32_t>(r.n());
			const auto slot = r.slot();
			ApplyClaim(db, pid, sid, adena, slot, r.n());
		}
	}

	MemDb& Db()
	{
		MemDb& db = DbRaw();
		static std::once_flag once;
		std::call_once(once, [&db]
		{
			const char* path = std::getenv("HARVEN_MEMDB_JOURNAL");
			if (path == nullptr || *path == '\0') return;
			int applied = 0, dropped = 0;
			if (FILE* in = std::fopen(path, "rb"))
			{
				db.replaying = true;
				std::string cur;
				int c;
				while ((c = std::fgetc(in)) != EOF)
				{
					if (c == '\n') { ReplayLine(db, cur); ++applied; cur.clear(); }
					else cur.push_back(static_cast<char>(c));
				}
				if (!cur.empty()) ++dropped;   // 끝이 잘린 줄 = 커밋되지 않은 트랜잭션
				db.replaying = false;
				std::fclose(in);
			}
			db.journal = std::fopen(path, "ab");
			std::cout << "[MemDB] journal " << path << " replayed=" << applied << " dropped=" << dropped << "\n" << std::flush;
		});
		return db;
	}

	int64_t NowUnix() { return static_cast<int64_t>(std::time(nullptr)); }
}

DBConnection::~DBConnection() { Clear(); }

bool DBConnection::Connect() { (void)Db(); return true; }
void DBConnection::Clear() {}
bool DBConnection::Execute(const WCHAR*) { return true; }
bool DBConnection::Fetch() { return false; }
int DBConnection::GetRowCount() { return 0; }
void DBConnection::Unbind() {}
bool DBConnection::BindParam(SQLUSMALLINT, SQLSMALLINT, SQLSMALLINT, SQLULEN, SQLPOINTER, SQLLEN*) { return true; }
bool DBConnection::BindCol(SQLUSMALLINT, SQLSMALLINT, SQLULEN, SQLPOINTER, SQLLEN*) { return true; }
void DBConnection::HandleError(SQLRETURN) {}

namespace
{
	void JournalNewPlayer(MemDb& db, const std::string& name, const PlayerRow& row)
	{
		Rec r("P");
		r.s(name).s(row.password) << row.playerId << row.x << row.y << row.level << static_cast<long long>(row.exp) << static_cast<long long>(row.adena);
		Journal(db, r);
	}
}

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
		row.adena = 50000;   // 시험 캐릭터: 옛길 이동 · 상점을 바로 쓸 수 있게(Phase 3-C 보스전 녹화)
		db.players.emplace(name, row);
		db.nameOf[row.playerId] = name;
		JournalNewPlayer(db, name, row);
		// Phase 5 경매 시험: 이름에 "_ak"가 있으면 올릴 물건 꾸러미(희귀 · 영웅 장비 넷 + 약초 물약 20)를 쥐여 준다(착용하지 않은 채)
		if (name.find("_ak") != string::npos)
		{
			auto inst = [](Rarity r, ItemOption a, uint8_t av, ItemOption b, uint8_t bv)
			{
				ItemInstance i;
				i.rarity = r;
				i.opts[0] = OptionRoll{ a, av };
				i.opts[1] = OptionRoll{ b, bv };
				return i.Pack();
			};
			const struct { uint16_t slot, item, count; uint8_t enchant; uint64_t extra; } kit[] = {
				{ 0, 3,  1, 2, inst(Rarity::Rare, ItemOption::Attack, 4, ItemOption::Str, 3) },          // 철검 +2(희귀)
				{ 1, 33, 1, 0, inst(Rarity::Uncommon, ItemOption::Armor, 3, ItemOption::Vit, 2) },       // 사슬 갑옷(고급)
				{ 2, 44, 1, 0, inst(Rarity::Heroic, ItemOption::CritChance, 12, ItemOption::Luk, 4) },   // 은 목걸이(영웅)
				{ 3, 47, 1, 0, inst(Rarity::Rare, ItemOption::MaxHp, 6, ItemOption::Wis, 3) },           // 서약의 투구(희귀)
				{ 4, 1, 20, 0, 0 },                                                                      // 약초 물약 20
			};
			for (const auto& k : kit)
			{
				DB_ITEM_SLOT_SAVE s;
				s.slotIndex = k.slot; s.hasItem = true; s.itemId = k.item; s.count = k.count; s.enchant = k.enchant; s.extra = k.extra;
				ApplySlot(db, row.playerId, s);
				Rec r("S");
				r << row.playerId;
				RecSlot(r, s);
				Journal(db, r);
			}
		}
		// Phase 6 전체 플레이 녹화: 이름에 "_fp"가 있으면 경매에 올릴 은 목걸이(영웅) · 귀환 두루마리 · 물약을 쥐여 준다
		if (name.find("_fp") != string::npos)
		{
			ItemInstance neck;
			neck.rarity = Rarity::Heroic;
			neck.opts[0] = OptionRoll{ ItemOption::CritChance, 12 };
			neck.opts[1] = OptionRoll{ ItemOption::Luk, 4 };
			const struct { uint16_t slot, item, count; uint8_t enchant; uint64_t extra; } kit[] = {
				{ 0, 44, 1, 0, neck.Pack() },   // 은 목걸이(영웅) — 경매장에 올린다
				{ 1, 10, 3, 0, 0 },             // 귀환 두루마리 3
				{ 2, 1, 30, 0, 0 },             // 약초 물약 30(물약 칸 F1)
				{ 3, 8, 10, 0, 0 },             // 진한 약초 물약 10(전투 칸 1)
			};
			for (const auto& k : kit)
			{
				DB_ITEM_SLOT_SAVE s;
				s.slotIndex = k.slot; s.hasItem = true; s.itemId = k.item; s.count = k.count; s.enchant = k.enchant; s.extra = k.extra;
				ApplySlot(db, row.playerId, s);
				Rec r("S");
				r << row.playerId;
				RecSlot(r, s);
				Journal(db, r);
			}
		}
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
		JournalNewPlayer(db, name, row);
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
	Rec r("U");
	r.s(name) << x << y << level << static_cast<long long>(exp);
	Journal(db, r);
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
	Rec r("S");
	r << playerId;
	RecSlot(r, s);
	Journal(db, r);
	return true;
}

bool DBConnection::DeleteInventorySlot(int playerId, uint16_t slotIndex)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	DB_ITEM_SLOT_SAVE s;
	s.slotIndex = slotIndex;
	s.hasItem = false;
	ApplySlot(db, playerId, s);
	Rec r("S");
	r << playerId;
	RecSlot(r, s);
	Journal(db, r);
	return true;
}

bool DBConnection::SaveTwoInventorySlots(int playerId, const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);   // 두 슬롯이 한 트랜잭션
	ApplySlot(db, playerId, a);
	ApplySlot(db, playerId, b);
	Rec r("T");
	r << playerId;
	RecSlot(r, a);
	RecSlot(r, b);
	Journal(db, r);
	return true;
}

bool DBConnection::SavePlayerAdena(int playerId, uint32_t adena)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (auto* p = PlayerById(db, playerId))
	{
		p->adena = adena;
		Rec r("A");
		r << playerId << static_cast<long long>(adena);
		Journal(db, r);
	}
	return true;
}

bool DBConnection::SavePlayerAlignment(int playerId, int16_t alignment, uint16_t pkCount)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (auto* p = PlayerById(db, playerId))
	{
		p->alignment = alignment;
		p->pkCount = pkCount;
		Rec r("L");
		r << playerId << alignment << pkCount;
		Journal(db, r);
	}
	return true;
}

bool DBConnection::ShopTransaction(int playerId, uint32_t adena, const DB_ITEM_SLOT_SAVE& slot)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);   // 그로시 + 슬롯 = 한 트랜잭션
	if (auto* p = PlayerById(db, playerId))
		p->adena = adena;
	ApplySlot(db, playerId, slot);
	Rec r("X");
	r << playerId << static_cast<long long>(adena);
	RecSlot(r, slot);
	Journal(db, r);
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
	Rec r("G");
	r << playerId;
	r.s(std::string(data.begin(), data.end()));
	Journal(db, r);
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
	Rec r("K");
	r << playerId << skill._skillId << skill._level << skill._pick1 << skill._pick2;
	Journal(db, r);
	return true;
}

bool DBConnection::ResetPlayerSkills(int playerId)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	db.skills.erase(playerId);
	Rec r("R");
	r << playerId;
	Journal(db, r);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 결사 · 경매장 — setup.sql 5~6장 프로시저와 같은 의미(상태 검사에 걸리면 false · 아무것도 바뀌지 않는다)
// ─────────────────────────────────────────────────────────────────────────────
bool DBConnection::LoadGuilds(vector<DB_GUILD_ROW>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	for (const auto& [gid, r] : db.guilds)
		out.push_back(DB_GUILD_ROW{ gid, r.name, r.emblem, r.notice, r.leaderId });
	return true;
}

bool DBConnection::LoadGuildMembers(vector<DB_GUILD_MEMBER_ROW>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	for (const auto& [pid, m] : db.members)
	{
		DB_GUILD_MEMBER_ROW r;
		r.guildId = m.guildId;
		r.playerId = pid;
		r.rank = m.rank;
		const auto n = db.nameOf.find(pid);
		r.name = n != db.nameOf.end() ? n->second : std::string();
		const PlayerRow* p = PlayerById(db, pid);
		r.level = p ? p->level : 1;
		r.lastSeenMin = m.lastSeen > 0 ? static_cast<uint32_t>(m.lastSeen / 60) : 0u;
		out.push_back(r);
	}
	return true;
}

bool DBConnection::GuildCreate(int playerId, uint32_t adenaAfter, int guildId, const string& name, const string& nameKey, uint8_t emblem)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyGuildCreate(db, playerId, adenaAfter, guildId, name, nameKey, emblem)) return false;
	Rec r("GC");
	r << playerId << static_cast<long long>(adenaAfter) << guildId;
	r.s(name).s(nameKey) << emblem;
	Journal(db, r);
	return true;
}

bool DBConnection::GuildMemberSet(int guildId, int playerId, uint8_t rank)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyMemberSet(db, guildId, playerId, rank)) return false;
	Rec r("GM");
	r << guildId << playerId << rank;
	Journal(db, r);
	return true;
}

bool DBConnection::GuildMemberRemove(int guildId, int playerId)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyMemberRemove(db, guildId, playerId)) return false;
	Rec r("GR");
	r << guildId << playerId;
	Journal(db, r);
	return true;
}

bool DBConnection::GuildTransfer(int guildId, int fromId, int toId)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyTransfer(db, guildId, fromId, toId)) return false;
	Rec r("GT");
	r << guildId << fromId << toId;
	Journal(db, r);
	return true;
}

bool DBConnection::GuildSetInfo(int guildId, uint8_t emblem, const string& notice)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplySetInfo(db, guildId, emblem, notice)) return false;
	Rec r("GI");
	r << guildId << emblem;
	r.s(notice);
	Journal(db, r);
	return true;
}

bool DBConnection::GuildDisband(int guildId)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyDisband(db, guildId)) return false;
	Rec r("GD");
	r << guildId;
	Journal(db, r);
	return true;
}

bool DBConnection::GuildMemberSeen(int playerId, int64_t unixSec)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	auto it = db.members.find(playerId);
	if (it == db.members.end()) return true;
	it->second.lastSeen = unixSec;
	Rec r("GS");
	r << playerId << static_cast<long long>(unixSec);
	Journal(db, r);
	return true;
}

bool DBConnection::LoadAuctionListings(vector<DB_LISTING_ROW>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	for (const auto& [id, l] : db.listings)
		if (l.status == 0) out.push_back(l);
	return true;
}

bool DBConnection::LoadAuctionSettlements(vector<DB_SETTLEMENT_ROW>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	for (const auto& [id, s] : db.settlements)
		if (!db.claimedAt.count(id)) out.push_back(s);
	return true;
}

bool DBConnection::LoadAuctionPrices(vector<DB_PRICE_ROW>& out, int64_t sinceUnix)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	for (const auto& p : db.prices)
		if (p.soldAt >= sinceUnix) out.push_back(p);
	return true;
}

bool DBConnection::AuctionMaxIds(int64_t& maxListing, int64_t& maxSettlement, int& maxGuild)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	maxListing = db.listings.empty() ? 0 : db.listings.rbegin()->first;
	maxSettlement = db.settlements.empty() ? 0 : db.settlements.rbegin()->first;
	maxGuild = db.guilds.empty() ? 0 : db.guilds.rbegin()->first;
	return true;
}

bool DBConnection::AuctionRegister(int sellerId, uint32_t adenaAfter, const DB_ITEM_SLOT_SAVE& slot, const DB_LISTING_ROW& l)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyRegister(db, sellerId, adenaAfter, slot, l)) return false;
	Rec r("AR");
	r << sellerId << static_cast<long long>(adenaAfter);
	RecSlot(r, slot);
	r << static_cast<long long>(l.listingId) << l.itemId << l.count << l.enchant;
	r.u64(l.extra);
	r << static_cast<long long>(l.price) << static_cast<long long>(l.deposit) << static_cast<long long>(l.createdAt) << static_cast<long long>(l.expiresAt);
	Journal(db, r);
	return true;
}

bool DBConnection::AuctionBuy(int buyerId, uint32_t adenaAfter, int64_t listingId, uint32_t price, int64_t buyerSettleId, int64_t sellerSettleId, uint32_t sellerGets, int64_t nowUnix)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	// 장애 시험: 트랜잭션 안(아직 아무것도 적용하지 않음)에서 멈춘다 — 이 사이에 죽으면 없었던 일(롤백)
	if (const char* d = std::getenv("HARVEN_DB_BUY_DELAY_MS"))
	{
		const int ms = std::atoi(d);
		if (ms > 0)
		{
			std::cout << "[MemDB] AuctionBuy listing=" << listingId << " — 트랜잭션 안에서 " << ms << "ms 멈춤(장애 시험)\n" << std::flush;
			std::this_thread::sleep_for(std::chrono::milliseconds(ms));
		}
	}
	if (!ApplyBuy(db, buyerId, adenaAfter, listingId, price, buyerSettleId, sellerSettleId, sellerGets, nowUnix)) return false;
	Rec r("AB");
	r << buyerId << static_cast<long long>(adenaAfter) << static_cast<long long>(listingId) << static_cast<long long>(price)
	  << static_cast<long long>(buyerSettleId) << static_cast<long long>(sellerSettleId) << static_cast<long long>(sellerGets) << static_cast<long long>(nowUnix);
	Journal(db, r);
	return true;
}

bool DBConnection::AuctionClose(int64_t listingId, uint8_t status, int64_t settleId, int64_t nowUnix)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyClose(db, listingId, status, settleId, nowUnix)) return false;
	Rec r("AC");
	r << static_cast<long long>(listingId) << status << static_cast<long long>(settleId) << static_cast<long long>(nowUnix);
	Journal(db, r);
	return true;
}

bool DBConnection::SettlementClaim(int playerId, int64_t settlementId, uint32_t adenaAfter, const DB_ITEM_SLOT_SAVE& slot, int64_t nowUnix)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyClaim(db, playerId, settlementId, adenaAfter, slot, nowUnix)) return false;
	Rec r("SC");
	r << playerId << static_cast<long long>(settlementId) << static_cast<long long>(adenaAfter);
	RecSlot(r, slot);
	r << static_cast<long long>(nowUnix);
	Journal(db, r);
	return true;
}

bool DBConnection::AuditTotals(uint64_t& walletAdena, uint64_t& settleAdena, uint64_t& invUnits, uint64_t& listingUnits, uint64_t& settleUnits, uint64_t& feesAndDeposits)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	walletAdena = settleAdena = invUnits = listingUnits = settleUnits = feesAndDeposits = 0;
	for (const auto& [name, p] : db.players) walletAdena += p.adena;
	for (const auto& [pid, inv] : db.inventory) for (const auto& [slot, r] : inv) invUnits += r.count;
	for (const auto& [id, l] : db.listings)
	{
		feesAndDeposits += l.deposit;
		if (l.status == 0) listingUnits += l.count;
		if (l.status == 1) feesAndDeposits += AuctionRule::Fee(l.price);
	}
	for (const auto& [id, s] : db.settlements)
	{
		if (db.claimedAt.count(id)) continue;
		if (s.kind == 1) settleAdena += s.adena; else settleUnits += s.count;
	}
	return true;
}
