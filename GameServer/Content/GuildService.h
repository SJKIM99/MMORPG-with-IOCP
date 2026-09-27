#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Content/ServiceCommon.h"

class DBConnection;
class User;
struct GUILD_REQ_PACKET;

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 결사(길드) — docs/adr/0001-guild-auction-services.md, 규칙 Data/GuildAuction.h
//
//  GuildService: 결사 · 결사원 · 초대의 주인. 전용 스레드 하나(작업 큐)만 상태를 만진다(락 없음).
//   · 이름 겹침 · 인원 · 직위 · 권한은 서비스 메모리가 기준이다(DB UNIQUE는 뒷문).
//   · DB 쓰기는 먼저 하고(결과 콜백) 커밋되면 메모리에 반영한다 — 메모리와 DB가 어긋나지 않는다.
//     세우기 = 만드는 사람 샤드(그로시와 한 트랜잭션), 나머지 = 결사 번호 샤드(한 결사의 변경이 넣은 순서대로).
//   · 한 사람의 결사 변경(세우기 · 들어옴 · 나감)은 한 번에 하나(_busyPlayers) — 샤드가 달라도 순서가 뒤집히지 않는다.
//   · 한 결사의 구조 변경(추방 · 직위 · 넘기기 · 공지 · 문장 · 해산)도 한 번에 하나(Guild::busy). 들어옴은 여러 건 동시에.
//
//  GuildHelper: 요청한 사람의 Zone 스레드 쪽(검사 → 서비스 큐), 머리 위 표시 · 결사 채팅 전달.
// ─────────────────────────────────────────────────────────────────────────────
class GuildService
{
public:
	using Task = GameLogicThread::Task;

	GuildService();

	// 켤 때(메인 스레드 · 접속을 받기 전): DB의 결사 · 결사원으로 메모리를 만든다. maxGuildId = DB 최댓값
	bool LoadFromDB(DBConnection& db, int maxGuildId);
	void Run();                              // 서비스 스레드
	void Post(Task task);                    // 아무 스레드 → 서비스 큐

	// ── 아래는 전부 서비스 스레드(Post 안)에서만 ──
	void Tick();                             // 1초마다 — 초대 만료
	void OnLogin(const Service::Ctx& ctx);
	void OnLogout(int playerId, int64_t nowUnix);
	void OnLevel(int playerId, uint8_t level);
	void Handle(const Service::Ctx& ctx, const GUILD_REQ_PACKET& p);
	void RelayChat(int playerId, const ObjID& senderObj, const std::string& senderName, const std::string& body);

	[[nodiscard]] size_t GuildCount() const noexcept { return _guilds.size(); }
	[[nodiscard]] size_t MemberCount() const noexcept { return _guildOf.size(); }

private:
	struct Member
	{
		int         playerId = 0;
		std::string name;
		uint8_t     rank = 3;
		uint8_t     level = 1;
		uint8_t     classId = 0;
		uint32_t    lastSeenMin = 0;          // 유닉스 분(나간 때)
		bool        online = false;
		ObjID       obj{};
		std::weak_ptr<GameSession> session;
	};
	struct Guild
	{
		uint32_t            id = 0;
		std::string         name;
		std::string         key;              // 겹침 판정용(ASCII 소문자)
		uint8_t             emblem = 0;
		std::string         notice;
		int                 leaderId = 0;
		std::vector<Member> members;
		int                 pendingJoins = 0; // DB에 넣는 중인 들어옴(인원 한도에 센다)
		bool                busy = false;     // 구조 변경 하나가 DB에 가 있다
	};
	struct Invite
	{
		uint32_t    guildId = 0;
		int         inviterId = 0;
		std::string inviterName;
		uint32_t    expiresAt = 0;            // GetNowTime
	};

	[[nodiscard]] static std::string NameKey(const std::string& name);
	[[nodiscard]] Guild* GuildOf(int playerId);
	[[nodiscard]] static Member* FindMember(Guild& g, int playerId);
	[[nodiscard]] static Member* FindMemberByName(Guild& g, const std::string& name);

	void Ack(const Service::Ctx& ctx, GuildOp op, GuildResult r, uint32_t seq, uint32_t param = 0) const;
	void SendInfo(const std::weak_ptr<GameSession>& session, const Guild* g, int playerId) const;
	void SendMembers(const Service::Ctx& ctx, const Guild& g, uint8_t page, uint8_t sort) const;   // sort: 0 접속·직위 · 1 레벨 · 2 이름 · 3 최근 접속
	void SendName(const Service::Ctx& ctx, uint32_t guildId) const;
	// 깃발 뜰: 인원 많은 순(같으면 번호가 작은 = 먼저 세운) 결사 kHallTop개 — 메모리만 읽는다(DB 없음)
	void SendTop(const Service::Ctx& ctx) const;
	// 온라인 결사원 모두에게(except = 빼고 보낼 사람)
	void NotifyMembers(const Guild& g, GuildEvent ev, const std::string& who, const std::string& by, uint8_t rank = 0, int except = 0) const;
	// 그 사람의 Zone에서 머리 위 표시를 바꾸고 주변에 알린다(guildId 0 = 결사 없음)
	static void ApplyTag(const ObjID& obj, uint32_t guildId, uint8_t emblem, uint8_t rank);

	// 요청별(서비스 스레드)
	void Create(const Service::Ctx& ctx, uint32_t seq, const std::string& name, uint8_t emblem);
	void FinishCreate(bool ok, const Service::Ctx& ctx, uint32_t seq, uint32_t id, const std::string& name, const std::string& key, uint8_t emblem);
	void DoInvite(const Service::Ctx& ctx, uint32_t seq, const std::string& target);
	void Reply(const Service::Ctx& ctx, uint32_t seq, bool accept, uint32_t guildId);
	void FinishJoin(bool ok, const Service::Ctx& ctx, uint32_t seq, uint32_t guildId);
	void Leave(const Service::Ctx& ctx, uint32_t seq);
	void Kick(const Service::Ctx& ctx, uint32_t seq, const std::string& target);
	void FinishRemove(bool ok, const Service::Ctx& ctx, uint32_t seq, uint32_t guildId, int targetId, bool kicked);
	void SetRank(const Service::Ctx& ctx, uint32_t seq, const std::string& target, uint8_t rank);
	void Transfer(const Service::Ctx& ctx, uint32_t seq, const std::string& target);
	void SetNotice(const Service::Ctx& ctx, uint32_t seq, const std::string& text);
	void SetEmblem(const Service::Ctx& ctx, uint32_t seq, uint8_t emblem);
	void Disband(const Service::Ctx& ctx, uint32_t seq);

private:
	GameLogicThread                                 _loop;
	std::unordered_map<uint32_t, Guild>             _guilds;
	std::unordered_map<int, uint32_t>               _guildOf;       // playerId → 결사 번호
	std::unordered_map<std::string, uint32_t>       _byKey;         // 이름 키 → 결사 번호
	std::unordered_set<std::string>                 _pendingKeys;   // 세우는 중인 이름
	std::unordered_set<int>                         _busyPlayers;   // 결사 변경이 DB에 가 있는 사람
	std::unordered_map<int, Invite>                 _invites;       // 받는 사람 playerId → 초대
	std::unordered_set<int>                         _online;        // 접속 중(로그인 · 로그아웃 알림 순서 그대로 — 세션 상태를 직접 보지 않는다)
	uint32_t                                        _nextGuildId = 1;
};

extern std::shared_ptr<GuildService> GGuildService;

namespace GuildHelper
{
	// ── 요청한 사람의 소유 Zone 스레드 ──
	void HandleRequest(const std::shared_ptr<User>& user, const GUILD_REQ_PACKET& p);
	void OnEnterWorld(const std::shared_ptr<User>& user);    // 로그인 직후(확장 세션)
	void OnLeaveWorld(const std::shared_ptr<User>& user);    // 접속 종료
	void OnLevelUp(const std::shared_ptr<User>& user);
	// 결사 채팅: 검사가 끝난 글을 서비스로(서비스가 접속 중인 결사원에게 보낸다)
	void RelayChat(const std::shared_ptr<User>& sender, const char* body);
	// 시야에 들어온 target의 머리 위 결사 표시를 viewer(확장 세션)에게 — 결사가 없으면 보내지 않는다
	void SendTag(const std::shared_ptr<User>& viewer, const std::shared_ptr<User>& target);
	// Zone 스레드: 머리 위 표시를 본인과 주변 확장 세션에
	void BroadcastTag(const std::shared_ptr<User>& user);
}
