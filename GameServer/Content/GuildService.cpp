#include "pch.h"
#include "Content/GuildService.h"

#include <algorithm>
#include <shared_mutex>

#include "Content/ChatHelper.h"
#include "Data/GuildAuction.h"
#include "GameSessionManager.h"
#include "Npc/NpcHelper.h"
#include "Sector.h"
#include "SubjectHelper.h"
#include "Thread/DBThread.h"
#include "UserHelper.h"

std::shared_ptr<GuildService> GGuildService;

namespace
{
	using GuildRule::Rank;

	[[nodiscard]] Rank R(uint8_t r) noexcept { return static_cast<Rank>(r < static_cast<uint8_t>(Rank::Count) ? r : 3); }

	void CopyStr(char* dst, size_t cap, const std::string& src)
	{
		::strncpy_s(dst, cap, src.c_str(), _TRUNCATE);
	}

	// 결사 번호 → 이름 · 문장(머리 위 표시에 Zone 스레드들이 읽는다). 쓰는 쪽은 서비스뿐 — 세우기 · 문장 · 해산 때만.
	struct NameCache
	{
		std::shared_mutex lock;
		std::unordered_map<uint32_t, std::pair<std::string, uint8_t>> byId;
	};
	NameCache& Names()
	{
		static NameCache cache;
		return cache;
	}
	void CacheName(uint32_t id, const std::string& name, uint8_t emblem)
	{
		std::unique_lock lock(Names().lock);
		Names().byId[id] = { name, emblem };
	}
	void UncacheName(uint32_t id)
	{
		std::unique_lock lock(Names().lock);
		Names().byId.erase(id);
	}
	bool LookupName(uint32_t id, char (&out)[25])
	{
		std::shared_lock lock(Names().lock);
		const auto it = Names().byId.find(id);
		if (it == Names().byId.end())
			return false;
		CopyStr(out, sizeof(out), it->second.first);
		return true;
	}

	[[nodiscard]] uint32_t UnixMinutes(int64_t unixSec) noexcept { return static_cast<uint32_t>(unixSec / 60); }
}

// ─────────────────────────────────────────────────────────────────────────────
// 서비스 뼈대
// ─────────────────────────────────────────────────────────────────────────────
GuildService::GuildService() = default;

void GuildService::Run()
{
	Service::LServiceId = Service::kGuildServiceId;
	_loop.Run();
}

void GuildService::Post(Task task)
{
	_loop.Enqueue(std::move(task));
}

std::string GuildService::NameKey(const std::string& name)
{
	std::string key = name;
	for (char& c : key)
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
	return key;
}

bool GuildService::LoadFromDB(DBConnection& db, int maxGuildId)
{
	std::vector<DB_GUILD_ROW> guilds;
	std::vector<DB_GUILD_MEMBER_ROW> members;
	if (!db.LoadGuilds(guilds) || !db.LoadGuildMembers(members))
		return false;
	for (const auto& row : guilds)
	{
		Guild g;
		g.id = static_cast<uint32_t>(row.guildId);
		g.name = row.name;
		g.key = NameKey(row.name);
		g.emblem = GuildRule::ValidEmblem(row.emblem) ? row.emblem : 0;
		g.notice = row.notice;
		g.leaderId = row.leaderId;
		_byKey[g.key] = g.id;
		CacheName(g.id, g.name, g.emblem);
		_guilds.emplace(g.id, std::move(g));
	}
	for (const auto& row : members)
	{
		const auto it = _guilds.find(static_cast<uint32_t>(row.guildId));
		if (it == _guilds.end())
			continue;
		Member m;
		m.playerId = row.playerId;
		m.name = row.name;
		m.rank = row.rank;
		m.level = row.level;
		m.lastSeenMin = row.lastSeenMin;
		it->second.members.push_back(std::move(m));
		_guildOf[row.playerId] = it->second.id;
	}
	_nextGuildId = static_cast<uint32_t>(std::max(maxGuildId, 0)) + 1;
	return true;
}

GuildService::Guild* GuildService::GuildOf(int playerId)
{
	const auto it = _guildOf.find(playerId);
	if (it == _guildOf.end())
		return nullptr;
	const auto g = _guilds.find(it->second);
	return g == _guilds.end() ? nullptr : &g->second;
}

GuildService::Member* GuildService::FindMember(Guild& g, int playerId)
{
	for (auto& m : g.members)
		if (m.playerId == playerId) return &m;
	return nullptr;
}

GuildService::Member* GuildService::FindMemberByName(Guild& g, const std::string& name)
{
	const std::string key = NameKey(name);
	for (auto& m : g.members)
		if (NameKey(m.name) == key) return &m;
	return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// 보내기
// ─────────────────────────────────────────────────────────────────────────────
void GuildService::Ack(const Service::Ctx& ctx, GuildOp op, GuildResult r, uint32_t seq, uint32_t param) const
{
	GUILD_ACK_PACKET p;
	Service::Init(p, PacketType::GUILD_ACK);
	p.op = static_cast<uint8_t>(op);
	p.result = static_cast<uint8_t>(r);
	p.seq = seq;
	p.param = param;
	Service::SendTo(ctx.session, p);
}

void GuildService::SendInfo(const std::weak_ptr<GameSession>& session, const Guild* g, int playerId) const
{
	GUILD_INFO_INF_PACKET p;
	Service::Init(p, PacketType::GUILD_INF);
	p.kind = static_cast<uint8_t>(GuildInfKind::Info);
	if (g != nullptr)
	{
		p.guildId = g->id;
		p.emblem = g->emblem;
		p.members = static_cast<uint8_t>(g->members.size());
		for (const auto& m : g->members)
		{
			if (m.online) ++p.online;
			if (m.playerId == playerId) p.myRank = m.rank;
			if (m.playerId == g->leaderId) CopyStr(p.leader, sizeof(p.leader), m.name);
		}
		CopyStr(p.name, sizeof(p.name), g->name);
		CopyStr(p.notice, sizeof(p.notice), g->notice);
	}
	Service::SendTo(session, p);
}

void GuildService::SendMembers(const Service::Ctx& ctx, const Guild& g, uint8_t page, uint8_t sort) const
{
	// 정렬: 0 접속 중 → 직위 → 이름(기본) · 1 레벨 높은 순 · 2 이름 · 3 최근 접속 순
	std::vector<const Member*> order;
	order.reserve(g.members.size());
	for (const auto& m : g.members) order.push_back(&m);
	std::sort(order.begin(), order.end(), [sort](const Member* a, const Member* b)
	{
		switch (sort)
		{
		case 1: if (a->level != b->level) return a->level > b->level; break;
		case 2: break;
		case 3:
			if (a->online != b->online) return a->online;
			if (a->lastSeenMin != b->lastSeenMin) return a->lastSeenMin > b->lastSeenMin;
			break;
		default:
			if (a->online != b->online) return a->online;
			if (a->rank != b->rank) return a->rank < b->rank;
			break;
		}
		return a->name < b->name;
	});
	const int per = GuildRule::kMembersPerPage;
	const int pages = std::max(1, (static_cast<int>(order.size()) + per - 1) / per);
	const int pg = std::min<int>(page, pages - 1);

	GUILD_MEMBERS_INF_PACKET p;
	Service::Init(p, PacketType::GUILD_INF);
	p.kind = static_cast<uint8_t>(GuildInfKind::Members);
	p.page = static_cast<uint8_t>(pg);
	p.pages = static_cast<uint8_t>(pages);
	for (int i = pg * per; i < static_cast<int>(order.size()) && p.count < per; ++i)
	{
		const Member& m = *order[static_cast<size_t>(i)];
		GuildMemberRow& row = p.rows[p.count++];
		CopyStr(row.name, sizeof(row.name), m.name);
		row.rank = m.rank;
		row.level = m.level;
		row.classId = m.classId;
		row.online = m.online ? 1 : 0;
		row.lastSeenMin = m.online ? 0 : m.lastSeenMin;
	}
	Service::SendTo(ctx.session, p);
}

void GuildService::SendName(const Service::Ctx& ctx, uint32_t guildId) const
{
	GUILD_NAME_INF_PACKET p;
	Service::Init(p, PacketType::GUILD_INF);
	p.kind = static_cast<uint8_t>(GuildInfKind::Name);
	p.guildId = guildId;
	if (const auto it = _guilds.find(guildId); it != _guilds.end())
	{
		p.emblem = it->second.emblem;
		CopyStr(p.name, sizeof(p.name), it->second.name);
	}
	Service::SendTo(ctx.session, p);
}

void GuildService::NotifyMembers(const Guild& g, GuildEvent ev, const std::string& who, const std::string& by, uint8_t rank, int except) const
{
	GUILD_NFY_PACKET p;
	Service::Init(p, PacketType::GUILD_NFY);
	p.event = static_cast<uint8_t>(ev);
	p.emblem = g.emblem;
	p.rank = rank;
	p.guildId = g.id;
	CopyStr(p.who, sizeof(p.who), who);
	CopyStr(p.by, sizeof(p.by), by);
	CopyStr(p.name, sizeof(p.name), g.name);
	for (const auto& m : g.members)
		if (m.online && m.playerId != except)
			Service::SendTo(m.session, p);
}

void GuildService::ApplyTag(const ObjID& obj, uint32_t guildId, uint8_t emblem, uint8_t rank)
{
	Service::PostToOwner(obj, [guildId, emblem, rank](const std::shared_ptr<User>& user)
	{
		if (user == nullptr)
			return;
		user->m_guildId.store(guildId, std::memory_order_relaxed);
		user->m_guildEmblem.store(emblem, std::memory_order_relaxed);
		user->m_guildRank = rank;
		GuildHelper::BroadcastTag(user);
	});
}

// ─────────────────────────────────────────────────────────────────────────────
// 접속 · 틱
// ─────────────────────────────────────────────────────────────────────────────
void GuildService::OnLogin(const Service::Ctx& ctx)
{
	_online.insert(ctx.playerId);
	Guild* g = GuildOf(ctx.playerId);
	if (g == nullptr)
	{
		SendInfo(ctx.session, nullptr, ctx.playerId);   // 결사 없음(창 초기화)
		return;
	}
	Member* m = FindMember(*g, ctx.playerId);
	if (m == nullptr)
		return;
	m->online = true;
	m->obj = ctx.obj;
	m->session = ctx.session;
	m->level = ctx.level;
	m->classId = ctx.classId;
	m->name = ctx.name;
	SendInfo(ctx.session, g, ctx.playerId);
	NotifyMembers(*g, GuildEvent::Online, ctx.name, "", m->rank, ctx.playerId);
	ApplyTag(ctx.obj, g->id, g->emblem, m->rank);
}

void GuildService::OnLogout(int playerId, int64_t nowUnix)
{
	_online.erase(playerId);
	_invites.erase(playerId);
	Guild* g = GuildOf(playerId);
	if (g == nullptr)
		return;
	Member* m = FindMember(*g, playerId);
	if (m == nullptr || !m->online)
		return;
	m->online = false;
	m->session.reset();
	m->lastSeenMin = UnixMinutes(nowUnix);
	NotifyMembers(*g, GuildEvent::Offline, m->name, "", m->rank, playerId);
	// 마지막 접속 — 사람 샤드(그 사람의 다른 저장과 한 줄). 결과는 기다리지 않는다
	GDBThread->RequestTx(playerId, "GuildMemberSeen",
		[playerId, nowUnix](DBConnection& c) { return c.GuildMemberSeen(playerId, nowUnix); }, nullptr);
}

void GuildService::OnLevel(int playerId, uint8_t level)
{
	if (Guild* g = GuildOf(playerId))
		if (Member* m = FindMember(*g, playerId))
			m->level = level;
}

void GuildService::Tick()
{
	const uint32_t now = GetNowTime();
	for (auto it = _invites.begin(); it != _invites.end();)
	{
		if (now >= it->second.expiresAt)
			it = _invites.erase(it);
		else
			++it;
	}
}

void GuildService::RelayChat(int playerId, const ObjID& senderObj, const std::string& senderName, const std::string& body)
{
	const Guild* g = GuildOf(playerId);
	if (g == nullptr)
		return;
	SC_CHAT_EX_PACKET p;
	Service::Init(p, PacketType::SC_CHAT_EX);
	p.channel = static_cast<uint8_t>(ChatChannel::Guild);
	p.senderId = senderObj;
	CopyStr(p.senderName, sizeof(p.senderName), senderName);
	::strncpy_s(p.mess, CHAT_SIZE, body.c_str(), _TRUNCATE);
	for (const auto& m : g->members)
		if (m.online)
			Service::SendTo(m.session, p);
}

// ─────────────────────────────────────────────────────────────────────────────
// 요청(서비스 스레드) — 검사 → DB(결과 콜백) → 메모리 반영 · 알림
// ─────────────────────────────────────────────────────────────────────────────
void GuildService::Handle(const Service::Ctx& ctx, const GUILD_REQ_PACKET& p)
{
	char target[NAME_SIZE + 1]{};
	::memcpy(target, p.target, NAME_SIZE);
	char text[sizeof(p.text) + 1]{};
	::memcpy(text, p.text, sizeof(p.text));

	switch (static_cast<GuildOp>(p.op))
	{
	case GuildOp::Info:
		SendInfo(ctx.session, GuildOf(ctx.playerId), ctx.playerId);
		break;
	case GuildOp::Members:
		if (const Guild* g = GuildOf(ctx.playerId)) SendMembers(ctx, *g, p.arg8, static_cast<uint8_t>(p.arg32));
		else Ack(ctx, GuildOp::Members, GuildResult::NotInGuild, p.seq);
		break;
	case GuildOp::Name:        SendName(ctx, p.arg32); break;
	case GuildOp::Create:      Create(ctx, p.seq, text, p.arg8); break;
	case GuildOp::Invite:      DoInvite(ctx, p.seq, target); break;
	case GuildOp::InviteReply: Reply(ctx, p.seq, p.arg8 != 0, p.arg32); break;
	case GuildOp::Leave:       Leave(ctx, p.seq); break;
	case GuildOp::Kick:        Kick(ctx, p.seq, target); break;
	case GuildOp::SetRank:     SetRank(ctx, p.seq, target, p.arg8); break;
	case GuildOp::Transfer:    Transfer(ctx, p.seq, target); break;
	case GuildOp::Notice:      SetNotice(ctx, p.seq, text); break;
	case GuildOp::Disband:     Disband(ctx, p.seq); break;
	case GuildOp::Emblem:      SetEmblem(ctx, p.seq, p.arg8); break;
	default:                   Ack(ctx, static_cast<GuildOp>(p.op), GuildResult::Invalid, p.seq); break;
	}
}

void GuildService::Create(const Service::Ctx& ctx, uint32_t seq, const std::string& name, uint8_t emblem)
{
	if (_guildOf.count(ctx.playerId))
		return Ack(ctx, GuildOp::Create, GuildResult::AlreadyInGuild, seq);
	if (_busyPlayers.count(ctx.playerId))
		return Ack(ctx, GuildOp::Create, GuildResult::Busy, seq);
	const std::string key = NameKey(name);
	if (_byKey.count(key) || _pendingKeys.count(key))
		return Ack(ctx, GuildOp::Create, GuildResult::NameTaken, seq);

	const uint32_t id = _nextGuildId++;
	_pendingKeys.insert(key);
	_busyPlayers.insert(ctx.playerId);

	// 그로시는 만드는 사람의 Zone이 뺀다 → 그 사람 샤드에서 한 트랜잭션(그로시 · 결사 행 · 결사장 행)
	Service::PostToOwner(ctx.obj, [ctx, seq, id, name, key, emblem](const std::shared_ptr<User>& user)
	{
		auto abort = [=](GuildResult r, uint32_t param)
		{
			GGuildService->Post([=]()
			{
				GGuildService->_pendingKeys.erase(key);
				GGuildService->_busyPlayers.erase(ctx.playerId);
				GGuildService->Ack(ctx, GuildOp::Create, r, seq, param);
			});
		};
		if (user == nullptr)
			return abort(GuildResult::Invalid, 0);
		if (!user->GetInventory()->ReserveAdena(GuildRule::kCreateCost, AdenaReason::GuildCreate))
			return abort(GuildResult::NoAdena, GuildRule::kCreateCost);
		const uint32_t adenaAfter = user->GetInventory()->GetAdena();
		const int playerId = ctx.playerId;
		GDBThread->RequestTx(playerId, "GuildCreate",
			[=](DBConnection& c) { return c.GuildCreate(playerId, adenaAfter, static_cast<int>(id), name, key, emblem); },
			[=](bool ok)
			{
				if (!ok)
					Service::PostToOwner(ctx.obj, [](const std::shared_ptr<User>& u)
					{
						if (u != nullptr) u->GetInventory()->ReturnAdena(GuildRule::kCreateCost, AdenaReason::Refund);
					});
				GGuildService->Post([=]() { GGuildService->FinishCreate(ok, ctx, seq, id, name, key, emblem); });
			});
	});
}

void GuildService::FinishCreate(bool ok, const Service::Ctx& ctx, uint32_t seq, uint32_t id, const std::string& name, const std::string& key, uint8_t emblem)
{
	_pendingKeys.erase(key);
	_busyPlayers.erase(ctx.playerId);
	if (!ok)
		return Ack(ctx, GuildOp::Create, GuildResult::NameTaken, seq);

	Guild g;
	g.id = id;
	g.name = name;
	g.key = key;
	g.emblem = emblem;
	g.leaderId = ctx.playerId;
	Member m;
	m.playerId = ctx.playerId;
	m.name = ctx.name;
	m.rank = static_cast<uint8_t>(Rank::Leader);
	m.level = ctx.level;
	m.classId = ctx.classId;
	m.online = _online.count(ctx.playerId) > 0;
	m.obj = ctx.obj;
	m.session = ctx.session;
	g.members.push_back(std::move(m));
	_byKey[key] = id;
	_guildOf[ctx.playerId] = id;
	CacheName(id, name, emblem);
	auto& stored = _guilds.emplace(id, std::move(g)).first->second;

	Ack(ctx, GuildOp::Create, GuildResult::Ok, seq, id);
	SendInfo(ctx.session, &stored, ctx.playerId);
	ApplyTag(ctx.obj, id, emblem, static_cast<uint8_t>(Rank::Leader));

	// 월드 공지 — 확장 세션 모두에게(드문 일)
	GUILD_NFY_PACKET n;
	Service::Init(n, PacketType::GUILD_NFY);
	n.event = static_cast<uint8_t>(GuildEvent::Created);
	n.emblem = emblem;
	n.guildId = id;
	CopyStr(n.who, sizeof(n.who), ctx.name);
	CopyStr(n.name, sizeof(n.name), name);
	for (const auto& s : GSessionManager->SnapshotSessions())
		Service::SendTo(s, n);
	std::cout << "[Guild] created id=" << id << " leader=" << ctx.playerId << "\n" << std::flush;
}

void GuildService::DoInvite(const Service::Ctx& ctx, uint32_t seq, const std::string& target)
{
	Guild* g = GuildOf(ctx.playerId);
	if (g == nullptr)
		return Ack(ctx, GuildOp::Invite, GuildResult::NotInGuild, seq);
	const Member* me = FindMember(*g, ctx.playerId);
	if (me == nullptr || !GuildRule::CanInvite(R(me->rank)))
		return Ack(ctx, GuildOp::Invite, GuildResult::NoPermission, seq);
	if (static_cast<int>(g->members.size()) + g->pendingJoins >= GuildRule::kMaxMembers)
		return Ack(ctx, GuildOp::Invite, GuildResult::Full, seq);

	const auto session = GSessionManager->FindByName(target);
	const auto user = session != nullptr ? session->GetOwner() : nullptr;
	if (user == nullptr || session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
		return Ack(ctx, GuildOp::Invite, GuildResult::TargetNotFound, seq);
	const int targetId = user->GetPlayerId();   // 로그인 때 한 번 정해지는 값
	if (targetId == ctx.playerId)
		return Ack(ctx, GuildOp::Invite, GuildResult::Invalid, seq);
	if (_guildOf.count(targetId))
		return Ack(ctx, GuildOp::Invite, GuildResult::TargetInGuild, seq);
	if (_busyPlayers.count(targetId))
		return Ack(ctx, GuildOp::Invite, GuildResult::Busy, seq);

	Invite inv;
	inv.guildId = g->id;
	inv.inviterId = ctx.playerId;
	inv.inviterName = ctx.name;
	inv.expiresAt = GetNowTime() + GuildRule::kInviteMs;
	_invites[targetId] = inv;

	GUILD_NFY_PACKET n;
	Service::Init(n, PacketType::GUILD_NFY);
	n.event = static_cast<uint8_t>(GuildEvent::InviteFrom);
	n.emblem = g->emblem;
	n.guildId = g->id;
	CopyStr(n.who, sizeof(n.who), ctx.name);
	CopyStr(n.name, sizeof(n.name), g->name);
	Service::SendTo(session, n);
	Ack(ctx, GuildOp::Invite, GuildResult::Ok, seq);
}

void GuildService::Reply(const Service::Ctx& ctx, uint32_t seq, bool accept, uint32_t guildId)
{
	const auto it = _invites.find(ctx.playerId);
	if (it == _invites.end() || it->second.guildId != guildId || GetNowTime() >= it->second.expiresAt)
	{
		if (it != _invites.end() && it->second.guildId == guildId) _invites.erase(it);
		return Ack(ctx, GuildOp::InviteReply, GuildResult::InviteExpired, seq);
	}
	const Invite inv = it->second;
	_invites.erase(it);

	const auto gi = _guilds.find(guildId);
	if (gi == _guilds.end())
		return Ack(ctx, GuildOp::InviteReply, GuildResult::InviteExpired, seq);
	Guild& g = gi->second;
	if (!accept)
	{
		// 초대한 사람에게 "거절했다"
		if (const Member* inviter = FindMember(g, inv.inviterId); inviter != nullptr && inviter->online)
		{
			GUILD_NFY_PACKET n;
			Service::Init(n, PacketType::GUILD_NFY);
			n.event = static_cast<uint8_t>(GuildEvent::InviteDeclined);
			n.guildId = g.id;
			n.emblem = g.emblem;
			CopyStr(n.who, sizeof(n.who), ctx.name);
			CopyStr(n.name, sizeof(n.name), g.name);
			Service::SendTo(inviter->session, n);
		}
		return Ack(ctx, GuildOp::InviteReply, GuildResult::Ok, seq);
	}
	if (_guildOf.count(ctx.playerId))
		return Ack(ctx, GuildOp::InviteReply, GuildResult::AlreadyInGuild, seq);
	if (_busyPlayers.count(ctx.playerId))
		return Ack(ctx, GuildOp::InviteReply, GuildResult::Busy, seq);
	if (static_cast<int>(g.members.size()) + g.pendingJoins >= GuildRule::kMaxMembers)
		return Ack(ctx, GuildOp::InviteReply, GuildResult::Full, seq);

	++g.pendingJoins;
	_busyPlayers.insert(ctx.playerId);
	const int playerId = ctx.playerId;
	GDBThread->RequestTxKeyed(guildId, "GuildJoin",
		[=](DBConnection& c) { return c.GuildMemberSet(static_cast<int>(guildId), playerId, static_cast<uint8_t>(Rank::Member)); },
		[=](bool ok) { GGuildService->Post([=]() { GGuildService->FinishJoin(ok, ctx, seq, guildId); }); });
}

void GuildService::FinishJoin(bool ok, const Service::Ctx& ctx, uint32_t seq, uint32_t guildId)
{
	_busyPlayers.erase(ctx.playerId);
	const auto gi = _guilds.find(guildId);
	if (gi == _guilds.end())
		return Ack(ctx, GuildOp::InviteReply, GuildResult::InviteExpired, seq);
	Guild& g = gi->second;
	g.pendingJoins = std::max(0, g.pendingJoins - 1);
	if (!ok)
		return Ack(ctx, GuildOp::InviteReply, GuildResult::Invalid, seq);

	Member m;
	m.playerId = ctx.playerId;
	m.name = ctx.name;
	m.rank = static_cast<uint8_t>(Rank::Member);
	m.level = ctx.level;
	m.classId = ctx.classId;
	m.online = _online.count(ctx.playerId) > 0;
	m.obj = ctx.obj;
	m.session = ctx.session;
	g.members.push_back(std::move(m));
	_guildOf[ctx.playerId] = g.id;

	Ack(ctx, GuildOp::InviteReply, GuildResult::Ok, seq, g.id);
	NotifyMembers(g, GuildEvent::Joined, ctx.name, "", static_cast<uint8_t>(Rank::Member));
	SendInfo(ctx.session, &g, ctx.playerId);
	ApplyTag(ctx.obj, g.id, g.emblem, static_cast<uint8_t>(Rank::Member));
}

void GuildService::Leave(const Service::Ctx& ctx, uint32_t seq)
{
	Guild* g = GuildOf(ctx.playerId);
	if (g == nullptr)
		return Ack(ctx, GuildOp::Leave, GuildResult::NotInGuild, seq);
	if (g->leaderId == ctx.playerId)
		return Ack(ctx, GuildOp::Leave, GuildResult::LeaderMustTransfer, seq);
	if (g->busy || _busyPlayers.count(ctx.playerId))
		return Ack(ctx, GuildOp::Leave, GuildResult::Busy, seq);
	g->busy = true;
	_busyPlayers.insert(ctx.playerId);
	const uint32_t guildId = g->id;
	const int playerId = ctx.playerId;
	GDBThread->RequestTxKeyed(guildId, "GuildLeave",
		[=](DBConnection& c) { return c.GuildMemberRemove(static_cast<int>(guildId), playerId); },
		[=](bool ok) { GGuildService->Post([=]() { GGuildService->FinishRemove(ok, ctx, seq, guildId, playerId, false); }); });
}

void GuildService::Kick(const Service::Ctx& ctx, uint32_t seq, const std::string& target)
{
	Guild* g = GuildOf(ctx.playerId);
	if (g == nullptr)
		return Ack(ctx, GuildOp::Kick, GuildResult::NotInGuild, seq);
	const Member* me = FindMember(*g, ctx.playerId);
	Member* victim = FindMemberByName(*g, target);
	if (victim == nullptr)
		return Ack(ctx, GuildOp::Kick, GuildResult::TargetNotFound, seq);
	if (me == nullptr || victim->playerId == ctx.playerId || !GuildRule::CanKick(R(me->rank), R(victim->rank)))
		return Ack(ctx, GuildOp::Kick, GuildResult::NoPermission, seq);
	if (g->busy || _busyPlayers.count(victim->playerId))
		return Ack(ctx, GuildOp::Kick, GuildResult::Busy, seq);
	g->busy = true;
	_busyPlayers.insert(victim->playerId);
	const uint32_t guildId = g->id;
	const int victimId = victim->playerId;
	GDBThread->RequestTxKeyed(guildId, "GuildKick",
		[=](DBConnection& c) { return c.GuildMemberRemove(static_cast<int>(guildId), victimId); },
		[=](bool ok) { GGuildService->Post([=]() { GGuildService->FinishRemove(ok, ctx, seq, guildId, victimId, true); }); });
}

void GuildService::FinishRemove(bool ok, const Service::Ctx& ctx, uint32_t seq, uint32_t guildId, int targetId, bool kicked)
{
	_busyPlayers.erase(targetId);
	const auto gi = _guilds.find(guildId);
	if (gi == _guilds.end())
		return Ack(ctx, kicked ? GuildOp::Kick : GuildOp::Leave, GuildResult::Invalid, seq);
	Guild& g = gi->second;
	g.busy = false;
	if (!ok)
		return Ack(ctx, kicked ? GuildOp::Kick : GuildOp::Leave, GuildResult::Invalid, seq);

	const auto mi = std::find_if(g.members.begin(), g.members.end(), [&](const Member& m) { return m.playerId == targetId; });
	if (mi == g.members.end())
		return Ack(ctx, kicked ? GuildOp::Kick : GuildOp::Leave, GuildResult::Invalid, seq);
	const Member gone = *mi;
	g.members.erase(mi);
	_guildOf.erase(targetId);

	Ack(ctx, kicked ? GuildOp::Kick : GuildOp::Leave, GuildResult::Ok, seq);
	NotifyMembers(g, kicked ? GuildEvent::Kicked : GuildEvent::Left, gone.name, kicked ? ctx.name : std::string());
	// 나간 사람에게도(창을 닫고 머리 위 표시를 지운다)
	if (gone.online)
	{
		GUILD_NFY_PACKET n;
		Service::Init(n, PacketType::GUILD_NFY);
		n.event = static_cast<uint8_t>(kicked ? GuildEvent::Kicked : GuildEvent::Left);
		n.guildId = g.id;
		n.emblem = g.emblem;
		CopyStr(n.who, sizeof(n.who), gone.name);
		CopyStr(n.by, sizeof(n.by), kicked ? ctx.name : std::string());
		CopyStr(n.name, sizeof(n.name), g.name);
		Service::SendTo(gone.session, n);
		SendInfo(gone.session, nullptr, targetId);
		ApplyTag(gone.obj, 0, 0, static_cast<uint8_t>(Rank::Member));
	}
}

void GuildService::SetRank(const Service::Ctx& ctx, uint32_t seq, const std::string& target, uint8_t rank)
{
	Guild* g = GuildOf(ctx.playerId);
	if (g == nullptr)
		return Ack(ctx, GuildOp::SetRank, GuildResult::NotInGuild, seq);
	const Member* me = FindMember(*g, ctx.playerId);
	Member* who = FindMemberByName(*g, target);
	if (who == nullptr)
		return Ack(ctx, GuildOp::SetRank, GuildResult::TargetNotFound, seq);
	if (me == nullptr || who->playerId == ctx.playerId || rank >= static_cast<uint8_t>(Rank::Count)
		|| !GuildRule::CanSetRank(R(me->rank), R(who->rank), R(rank)))
		return Ack(ctx, GuildOp::SetRank, GuildResult::NoPermission, seq);
	if (who->rank == rank)
		return Ack(ctx, GuildOp::SetRank, GuildResult::Ok, seq);
	if (g->busy)
		return Ack(ctx, GuildOp::SetRank, GuildResult::Busy, seq);
	g->busy = true;
	const uint32_t guildId = g->id;
	const int targetId = who->playerId;
	GDBThread->RequestTxKeyed(guildId, "GuildRank",
		[=](DBConnection& c) { return c.GuildMemberSet(static_cast<int>(guildId), targetId, rank); },
		[=](bool ok)
		{
			GGuildService->Post([=]()
			{
				auto& svc = *GGuildService;
				const auto gi = svc._guilds.find(guildId);
				if (gi == svc._guilds.end())
					return svc.Ack(ctx, GuildOp::SetRank, GuildResult::Invalid, seq);
				Guild& gg = gi->second;
				gg.busy = false;
				Member* m = FindMember(gg, targetId);
				if (!ok || m == nullptr)
					return svc.Ack(ctx, GuildOp::SetRank, GuildResult::Invalid, seq);
				m->rank = rank;
				svc.Ack(ctx, GuildOp::SetRank, GuildResult::Ok, seq);
				svc.NotifyMembers(gg, GuildEvent::RankChanged, m->name, ctx.name, rank);
				if (m->online)
				{
					svc.SendInfo(m->session, &gg, m->playerId);   // 바뀐 사람의 창(내 직위 · 할 수 있는 일)
					ApplyTag(m->obj, gg.id, gg.emblem, rank);
				}
			});
		});
}

void GuildService::Transfer(const Service::Ctx& ctx, uint32_t seq, const std::string& target)
{
	Guild* g = GuildOf(ctx.playerId);
	if (g == nullptr)
		return Ack(ctx, GuildOp::Transfer, GuildResult::NotInGuild, seq);
	if (g->leaderId != ctx.playerId)
		return Ack(ctx, GuildOp::Transfer, GuildResult::NoPermission, seq);
	Member* who = FindMemberByName(*g, target);
	if (who == nullptr || who->playerId == ctx.playerId)
		return Ack(ctx, GuildOp::Transfer, GuildResult::TargetNotFound, seq);
	if (g->busy)
		return Ack(ctx, GuildOp::Transfer, GuildResult::Busy, seq);
	g->busy = true;
	const uint32_t guildId = g->id;
	const int fromId = ctx.playerId;
	const int toId = who->playerId;
	GDBThread->RequestTxKeyed(guildId, "GuildTransfer",
		[=](DBConnection& c) { return c.GuildTransfer(static_cast<int>(guildId), fromId, toId); },
		[=](bool ok)
		{
			GGuildService->Post([=]()
			{
				auto& svc = *GGuildService;
				const auto gi = svc._guilds.find(guildId);
				if (gi == svc._guilds.end())
					return svc.Ack(ctx, GuildOp::Transfer, GuildResult::Invalid, seq);
				Guild& gg = gi->second;
				gg.busy = false;
				Member* from = FindMember(gg, fromId);
				Member* to = FindMember(gg, toId);
				if (!ok || from == nullptr || to == nullptr)
					return svc.Ack(ctx, GuildOp::Transfer, GuildResult::Invalid, seq);
				from->rank = static_cast<uint8_t>(Rank::Officer);
				to->rank = static_cast<uint8_t>(Rank::Leader);
				gg.leaderId = toId;
				svc.Ack(ctx, GuildOp::Transfer, GuildResult::Ok, seq);
				svc.NotifyMembers(gg, GuildEvent::LeaderChanged, to->name, ctx.name, static_cast<uint8_t>(Rank::Leader));
				for (const auto& mm : gg.members)
					if (mm.online) svc.SendInfo(mm.session, &gg, mm.playerId);   // 결사장 이름 · 두 사람의 직위
				if (from->online) ApplyTag(from->obj, gg.id, gg.emblem, from->rank);
				if (to->online) ApplyTag(to->obj, gg.id, gg.emblem, to->rank);
			});
		});
}

void GuildService::SetNotice(const Service::Ctx& ctx, uint32_t seq, const std::string& text)
{
	Guild* g = GuildOf(ctx.playerId);
	if (g == nullptr)
		return Ack(ctx, GuildOp::Notice, GuildResult::NotInGuild, seq);
	const Member* me = FindMember(*g, ctx.playerId);
	if (me == nullptr || !GuildRule::CanNotice(R(me->rank)))
		return Ack(ctx, GuildOp::Notice, GuildResult::NoPermission, seq);
	if (g->busy)
		return Ack(ctx, GuildOp::Notice, GuildResult::Busy, seq);
	g->busy = true;
	const uint32_t guildId = g->id;
	const uint8_t emblem = g->emblem;
	GDBThread->RequestTxKeyed(guildId, "GuildNotice",
		[=](DBConnection& c) { return c.GuildSetInfo(static_cast<int>(guildId), emblem, text); },
		[=](bool ok)
		{
			GGuildService->Post([=]()
			{
				auto& svc = *GGuildService;
				const auto gi = svc._guilds.find(guildId);
				if (gi == svc._guilds.end())
					return svc.Ack(ctx, GuildOp::Notice, GuildResult::Invalid, seq);
				Guild& gg = gi->second;
				gg.busy = false;
				if (!ok)
					return svc.Ack(ctx, GuildOp::Notice, GuildResult::Invalid, seq);
				gg.notice = text;
				svc.Ack(ctx, GuildOp::Notice, GuildResult::Ok, seq);
				svc.NotifyMembers(gg, GuildEvent::NoticeChanged, "", ctx.name);
				for (const auto& m : gg.members)
					if (m.online) svc.SendInfo(m.session, &gg, m.playerId);
			});
		});
}

void GuildService::SetEmblem(const Service::Ctx& ctx, uint32_t seq, uint8_t emblem)
{
	Guild* g = GuildOf(ctx.playerId);
	if (g == nullptr)
		return Ack(ctx, GuildOp::Emblem, GuildResult::NotInGuild, seq);
	if (g->leaderId != ctx.playerId)
		return Ack(ctx, GuildOp::Emblem, GuildResult::NoPermission, seq);
	if (!GuildRule::ValidEmblem(emblem))
		return Ack(ctx, GuildOp::Emblem, GuildResult::Invalid, seq);
	if (g->busy)
		return Ack(ctx, GuildOp::Emblem, GuildResult::Busy, seq);
	g->busy = true;
	const uint32_t guildId = g->id;
	const std::string notice = g->notice;
	GDBThread->RequestTxKeyed(guildId, "GuildEmblem",
		[=](DBConnection& c) { return c.GuildSetInfo(static_cast<int>(guildId), emblem, notice); },
		[=](bool ok)
		{
			GGuildService->Post([=]()
			{
				auto& svc = *GGuildService;
				const auto gi = svc._guilds.find(guildId);
				if (gi == svc._guilds.end())
					return svc.Ack(ctx, GuildOp::Emblem, GuildResult::Invalid, seq);
				Guild& gg = gi->second;
				gg.busy = false;
				if (!ok)
					return svc.Ack(ctx, GuildOp::Emblem, GuildResult::Invalid, seq);
				gg.emblem = emblem;
				CacheName(gg.id, gg.name, emblem);
				svc.Ack(ctx, GuildOp::Emblem, GuildResult::Ok, seq);
				svc.NotifyMembers(gg, GuildEvent::EmblemChanged, "", ctx.name);
				for (const auto& m : gg.members)
					if (m.online)
					{
						svc.SendInfo(m.session, &gg, m.playerId);
						ApplyTag(m.obj, gg.id, emblem, m.rank);
					}
			});
		});
}

void GuildService::Disband(const Service::Ctx& ctx, uint32_t seq)
{
	Guild* g = GuildOf(ctx.playerId);
	if (g == nullptr)
		return Ack(ctx, GuildOp::Disband, GuildResult::NotInGuild, seq);
	if (g->leaderId != ctx.playerId)
		return Ack(ctx, GuildOp::Disband, GuildResult::NoPermission, seq);
	if (g->members.size() > 1 || g->pendingJoins > 0)
		return Ack(ctx, GuildOp::Disband, GuildResult::NotEmpty, seq);
	if (g->busy || _busyPlayers.count(ctx.playerId))
		return Ack(ctx, GuildOp::Disband, GuildResult::Busy, seq);
	g->busy = true;
	_busyPlayers.insert(ctx.playerId);
	const uint32_t guildId = g->id;
	GDBThread->RequestTxKeyed(guildId, "GuildDisband",
		[=](DBConnection& c) { return c.GuildDisband(static_cast<int>(guildId)); },
		[=](bool ok)
		{
			GGuildService->Post([=]()
			{
				auto& svc = *GGuildService;
				svc._busyPlayers.erase(ctx.playerId);
				const auto gi = svc._guilds.find(guildId);
				if (gi == svc._guilds.end())
					return svc.Ack(ctx, GuildOp::Disband, GuildResult::Invalid, seq);
				Guild& gg = gi->second;
				gg.busy = false;
				if (!ok)
					return svc.Ack(ctx, GuildOp::Disband, GuildResult::Invalid, seq);
				svc.NotifyMembers(gg, GuildEvent::Disbanded, ctx.name, ctx.name);
				for (const auto& m : gg.members)
				{
					svc._guildOf.erase(m.playerId);
					if (m.online)
					{
						svc.SendInfo(m.session, nullptr, m.playerId);
						ApplyTag(m.obj, 0, 0, static_cast<uint8_t>(Rank::Member));
					}
				}
				for (auto it = svc._invites.begin(); it != svc._invites.end();)
					it = it->second.guildId == guildId ? svc._invites.erase(it) : std::next(it);
				svc._byKey.erase(gg.key);
				UncacheName(guildId);
				svc._guilds.erase(gi);
				svc.Ack(ctx, GuildOp::Disband, GuildResult::Ok, seq);
				std::cout << "[Guild] disbanded id=" << guildId << "\n" << std::flush;
			});
		});
}

// ─────────────────────────────────────────────────────────────────────────────
// GuildHelper — 요청한 사람의 소유 Zone 스레드
// ─────────────────────────────────────────────────────────────────────────────
namespace GuildHelper
{
	namespace
	{
		[[nodiscard]] bool IsMutating(GuildOp op) noexcept
		{
			return op != GuildOp::Info && op != GuildOp::Members && op != GuildOp::Name;
		}

		void ZoneAck(const std::shared_ptr<User>& user, GuildOp op, GuildResult r, uint32_t seq, uint32_t param = 0)
		{
			GUILD_ACK_PACKET p;
			Service::Init(p, PacketType::GUILD_ACK);
			p.op = static_cast<uint8_t>(op);
			p.result = static_cast<uint8_t>(r);
			p.seq = seq;
			p.param = param;
			if (auto s = user->GetGameSession()) s->PostSend(p);
		}

		void FillTag(GUILD_NFY_PACKET& p, const std::shared_ptr<User>& user)
		{
			Service::Init(p, PacketType::GUILD_NFY);
			p.event = static_cast<uint8_t>(GuildEvent::Tag);
			p.obj = user->GetObjID();
			p.guildId = user->m_guildId.load(std::memory_order_relaxed);
			p.emblem = user->m_guildEmblem.load(std::memory_order_relaxed);
			p.rank = user->m_guildRank;
			if (p.guildId != 0)
				(void)LookupName(p.guildId, p.name);
		}
	}

	void HandleRequest(const std::shared_ptr<User>& user, const GUILD_REQ_PACKET& p)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		const auto op = static_cast<GuildOp>(p.op);
		if (GGuildService == nullptr)
			return ZoneAck(user, op, GuildResult::Busy, p.seq);   // 켤 때 DB를 못 읽었다(서버 로그 [Phase5])
		if (IsMutating(op))
		{
			// 멱등: 같은(또는 지난) 순번은 한 번만 — 소유 Zone이 사람마다 기억한다
			if (p.seq <= user->m_lastGuildSeq)
				return ZoneAck(user, op, GuildResult::Duplicate, p.seq);
			user->m_lastGuildSeq = p.seq;
		}

		switch (op)
		{
		case GuildOp::Create:
		{
			char name[sizeof(p.text) + 1]{};
			::memcpy(name, p.text, sizeof(p.text));
			if (::strnlen(name, sizeof(name)) >= static_cast<size_t>(GuildRule::kNameBytes) || GuildRule::CheckName(name) != GuildRule::NameCheck::Ok)
				return ZoneAck(user, op, GuildResult::NameInvalid, p.seq);
			if (!GuildRule::ValidEmblem(p.arg8))
				return ZoneAck(user, op, GuildResult::Invalid, p.seq);
			if (!NpcHelper::IsNear(GuildRule::kHallNpcId, user->GetX(), user->GetY()))
				return ZoneAck(user, op, GuildResult::NotAtNpc, p.seq);
			if (user->GetStat()->GetLevel() < GuildRule::kMinLevel)
				return ZoneAck(user, op, GuildResult::LevelTooLow, p.seq, GuildRule::kMinLevel);
			if (user->GetInventory()->GetAdena() < GuildRule::kCreateCost)   // 미리 거른다(실제 차감은 이름 확인 뒤)
				return ZoneAck(user, op, GuildResult::NoAdena, p.seq, GuildRule::kCreateCost);
			break;
		}
		case GuildOp::Emblem:
			if (!GuildRule::ValidEmblem(p.arg8))
				return ZoneAck(user, op, GuildResult::Invalid, p.seq);
			if (!NpcHelper::IsNear(GuildRule::kHallNpcId, user->GetX(), user->GetY()))
				return ZoneAck(user, op, GuildResult::NotAtNpc, p.seq);
			break;
		case GuildOp::Notice:
		{
			char text[sizeof(p.text) + 1]{};
			::memcpy(text, p.text, sizeof(p.text));
			const size_t n = ::strnlen(text, sizeof(text));
			if (n >= static_cast<size_t>(GuildRule::kNoticeBytes) || !ChatHelper::IsValidUtf8(text, n))
				return ZoneAck(user, op, GuildResult::Invalid, p.seq);
			break;
		}
		case GuildOp::Invite: case GuildOp::Kick: case GuildOp::SetRank: case GuildOp::Transfer:
			if (p.target[0] == '\0')
				return ZoneAck(user, op, GuildResult::TargetNotFound, p.seq);
			break;
		case GuildOp::Info: case GuildOp::Members: case GuildOp::Name:
		case GuildOp::InviteReply: case GuildOp::Leave: case GuildOp::Disband:
			break;
		default:
			return ZoneAck(user, op, GuildResult::Invalid, p.seq);
		}

		const Service::Ctx ctx = Service::MakeCtx(user);
		const GUILD_REQ_PACKET copy = p;
		GGuildService->Post([ctx, copy]() { GGuildService->Handle(ctx, copy); });
	}

	void OnEnterWorld(const std::shared_ptr<User>& user)
	{
		if (user == nullptr || !user->IsExtended() || GGuildService == nullptr)
			return;
		const Service::Ctx ctx = Service::MakeCtx(user);
		GGuildService->Post([ctx]() { GGuildService->OnLogin(ctx); });
	}

	void OnLeaveWorld(const std::shared_ptr<User>& user)
	{
		if (user == nullptr || !user->IsExtended() || GGuildService == nullptr)
			return;
		const int playerId = user->GetPlayerId();
		const int64_t now = Service::UnixNow();
		GGuildService->Post([playerId, now]() { GGuildService->OnLogout(playerId, now); });
	}

	void OnLevelUp(const std::shared_ptr<User>& user)
	{
		if (user == nullptr || !user->IsExtended() || GGuildService == nullptr || user->m_guildId.load(std::memory_order_relaxed) == 0)
			return;
		const int playerId = user->GetPlayerId();
		const uint8_t level = user->GetStat()->GetLevel();
		GGuildService->Post([playerId, level]() { GGuildService->OnLevel(playerId, level); });
	}

	void RelayChat(const std::shared_ptr<User>& sender, const char* body)
	{
		if (sender == nullptr || GGuildService == nullptr)
			return;
		const int playerId = sender->GetPlayerId();
		const ObjID obj = sender->GetObjID();
		std::string name = sender->GetName();
		std::string text = body;
		GGuildService->Post([playerId, obj, name = std::move(name), text = std::move(text)]()
		{
			GGuildService->RelayChat(playerId, obj, name, text);
		});
	}

	void SendTag(const std::shared_ptr<User>& viewer, const std::shared_ptr<User>& target)
	{
		if (viewer == nullptr || target == nullptr || target->m_guildId.load(std::memory_order_relaxed) == 0)
			return;
		auto s = viewer->GetGameSession();
		if (s == nullptr || !s->IsExtended())
			return;
		GUILD_NFY_PACKET p;
		FillTag(p, target);
		s->PostSend(p);
	}

	void BroadcastTag(const std::shared_ptr<User>& user)
	{
		GUILD_NFY_PACKET p;
		FillTag(p, user);
		if (auto s = user->GetGameSession(); s != nullptr && s->IsExtended())
			s->PostSend(p);
		if (user->GetSectorX() < 0 || user->IsTransferring())
			return;   // 섹터에 없으면(죽음 · 이동 중) 주변도 없다 — 들어갈 때 시야 입장이 표시를 보낸다
		const ObjID self = user->GetObjID();
		GSector->ForEachNeighborObject(user->GetSectorX(), user->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (object->GetObjID() == self || object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
				return;
			auto viewer = static_pointer_cast<User>(object);
			auto s = viewer->GetGameSession();
			if (s == nullptr || s->m_state != SOCKET_STATE::ST_INGAME || !s->IsExtended())
				return;
			if (SubjectHelper::CanSee(object, user))
				s->PostSend(p);
		});
	}
}
