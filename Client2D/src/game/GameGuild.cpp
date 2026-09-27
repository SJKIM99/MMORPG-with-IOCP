// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 결사(길드) — 창(Alt+U) · 세우기 · 문장 · 받은 초대 · 머리 위 문장 · 결사 채팅 알림
//
//  판정과 상태는 서버(GuildService)가 갖는다. 클라는 요청(GUILD_REQ + op · seq)을 보내고,
//  결과(GUILD_ACK) · 정보(GUILD_INF) · 사건(GUILD_NFY)을 받아 보여 줄 뿐이다.
//  규칙(직위 · 권한 · 이름 · 문장)은 서버와 같은 헤더(Data/GuildAuction.h)를 읽어 단추를 미리 잠근다.
// ─────────────────────────────────────────────────────────────────────────────
#include "game/Game.h"
#include "game/KeyBinds.h"
#include "render/Emblem.h"
#include "render/Props.h"
#include "ui/Wm.h"
#include "ui/Font.h"
#include "audio/Sfx.h"
#include "core/Util.h"
#include "Data/GuildAuction.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

using util::Rgb;
using GuildRule::Rank;

namespace
{
	std::string Fmt(const char* f, ...)
	{
		char buf[512];
		va_list ap;
		va_start(ap, f);
		std::vsnprintf(buf, sizeof(buf), f, ap);
		va_end(ap);
		return buf;
	}

	std::string Fixed(const char* s, size_t n)
	{
		size_t len = 0;
		while (len < n && s[len] != '\0') ++len;
		return std::string(s, len);
	}

	[[nodiscard]] Rank R(uint8_t r) { return static_cast<Rank>(std::min<uint8_t>(r, 3)); }

	[[nodiscard]] const char* ResultText(GuildResult r)
	{
		switch (r)
		{
		case GuildResult::Ok:                 return "";
		case GuildResult::NotInGuild:         return "결사에 들어 있지 않습니다.";
		case GuildResult::AlreadyInGuild:     return "이미 결사에 들어 있습니다.";
		case GuildResult::NoPermission:       return "그 일을 할 수 있는 직위가 아닙니다.";
		case GuildResult::NameTaken:          return "이미 있는 결사 이름입니다.";
		case GuildResult::NameInvalid:        return "쓸 수 없는 결사 이름입니다(한글 · 영문 · 숫자 2~8자, 금칙어 없이).";
		case GuildResult::NoAdena:            return "그로시가 모자랍니다.";
		case GuildResult::LevelTooLow:        return "레벨 15부터 결사를 세울 수 있습니다.";
		case GuildResult::Full:               return "결사원이 가득 찼습니다(30명).";
		case GuildResult::TargetNotFound:     return "그 사람을 찾을 수 없습니다(접속 중이 아니거나 이름이 다릅니다).";
		case GuildResult::TargetInGuild:      return "그 사람은 이미 다른 결사에 있습니다.";
		case GuildResult::InviteExpired:      return "초대가 이미 끝났습니다.";
		case GuildResult::NotAtNpc:           return "결사 회관의 회관지기 즈데노 곁에서 할 수 있습니다.";
		case GuildResult::Busy:               return "잠시 뒤에 다시 해 보세요.";
		case GuildResult::Invalid:            return "요청을 처리하지 못했습니다.";
		case GuildResult::Duplicate:          return "";
		case GuildResult::LeaderMustTransfer: return "결사장은 결사장을 넘긴 뒤에 떠날 수 있습니다.";
		case GuildResult::NotEmpty:           return "결사원이 남아 있으면 해산할 수 없습니다.";
		}
		return "요청을 처리하지 못했습니다.";
	}

	[[nodiscard]] bool Mutating(GuildOp op) { return op != GuildOp::Info && op != GuildOp::Members && op != GuildOp::Name && op != GuildOp::Top; }

	// "3시간 전" · "2일 전"(유닉스 분)
	std::string SeenText(uint32_t lastSeenMin)
	{
		if (lastSeenMin == 0) return "오래전";
		const long long nowMin = static_cast<long long>(std::time(nullptr) / 60);
		const long long d = std::max(0LL, nowMin - static_cast<long long>(lastSeenMin));
		if (d < 60) return Fmt("%lld분 전", std::max(1LL, d));
		if (d < 60 * 24) return Fmt("%lld시간 전", d / 60);
		return Fmt("%lld일 전", d / (60 * 24));
	}

	[[nodiscard]] Color RankColor(uint8_t r)
	{
		switch (R(r))
		{
		case Rank::Leader:  return ui::theme::kDawnHi;
		case Rank::Officer: return ui::theme::kDawn;
		case Rank::Elite:   return ui::theme::kChatGuild;
		default:            return ui::theme::kText;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 보내기 · 받기
// ─────────────────────────────────────────────────────────────────────────────
void Game::SendGuildReq(GuildOp op, uint8_t arg8, uint32_t arg32, const std::string& target, const std::string& text)
{
	GUILD_REQ_PACKET p{};
	p.op = static_cast<uint8_t>(op);
	p.seq = ++m_guild.seq;
	p.arg8 = arg8;
	p.arg32 = arg32;
	std::memcpy(p.target, target.data(), std::min(target.size(), static_cast<size_t>(NAME_SIZE - 1)));
	// UTF-8 글자 중간에서 자르지 않는다(널 1바이트 확보)
	size_t len = std::min(text.size(), sizeof(p.text) - 1);
	while (len > 0 && len < text.size() && (static_cast<unsigned char>(text[len]) & 0xC0) == 0x80) --len;
	std::memcpy(p.text, text.data(), len);
	m_net.SendGuild(p);
	if (Mutating(op))
	{
		m_guild.pendingSeq = p.seq;
		m_guild.pendingAt = m_now;
	}
	std::printf("[guild] req op=%d seq=%u\n", static_cast<int>(op), p.seq);
}

std::string Game::GuildNameOf(uint32_t guildId) const
{
	if (guildId == 0) return {};
	if (guildId == m_guild.id && !m_guild.name.empty()) return m_guild.name;
	const auto it = m_guildNames.find(guildId);
	return it == m_guildNames.end() ? std::string() : it->second.first;
}

void Game::HandleGuildAck(const GUILD_ACK_PACKET& p)
{
	const auto op = static_cast<GuildOp>(p.op);
	const auto r = static_cast<GuildResult>(p.result);
	std::printf("[guild] ack op=%d result=%d seq=%u param=%u\n", static_cast<int>(op), static_cast<int>(r), p.seq, p.param);
	if (r == GuildResult::Duplicate) return;   // 같은 순번(되풀이) — 처음 것의 결과가 따로 온다(기다림을 풀지 않는다)
	if (p.seq == m_guild.pendingSeq) m_guild.pendingSeq = 0;
	if (r != GuildResult::Ok)
	{
		AddCenter(ResultText(r), ui::theme::kBad);
		sfx::Play(sfx::Id::Error, 0.5f);
		return;
	}
	switch (op)
	{
	case GuildOp::Create:
		m_guild.createOpen = false;
		m_guild.joinedAt = m_now;
		m_guild.celebrateAt = m_now;
		m_guild.celebrateFounded = true;
		m_guild.tab = 0;
		CloseNpcWindows();
		sfx::Play(sfx::Id::Seal, 0.85f);   // 인장 — 쿵 + 작은 종
		break;
	case GuildOp::Invite:
		AddCenter("초대를 보냈습니다. (30초 안에 답이 옵니다)", ui::theme::kChatGuild);
		sfx::Play(sfx::Id::Click, 0.5f);
		break;
	case GuildOp::Leave:
		AddBanner("결사를 떠났습니다", ui::theme::kTextDim, 3.f);
		break;
	case GuildOp::Kick:     AddCenter("추방했습니다.", ui::theme::kTextDim); break;
	case GuildOp::SetRank:  sfx::Play(sfx::Id::StatUp, 0.5f); break;
	case GuildOp::Transfer: sfx::Play(sfx::Id::BellSmall, 0.6f); break;
	case GuildOp::Notice:   m_guild.editNotice = false; AddCenter("공지를 고쳤습니다.", ui::theme::kChatGuild); break;
	case GuildOp::Disband:  AddBanner("결사를 해산했습니다", ui::theme::kTextDim, 3.5f); m_guildOpen = false; break;
	case GuildOp::Emblem:
		m_guild.createOpen = false;
		m_guild.joinedAt = m_now;
		sfx::Play(sfx::Id::BellSmall, 0.7f);
		break;
	default: break;
	}
	if (m_guildOpen && m_guild.tab == 1 && m_guild.id != 0 && (op == GuildOp::SetRank || op == GuildOp::Kick || op == GuildOp::Transfer))
		SendGuildReq(GuildOp::Members, static_cast<uint8_t>(m_guild.page), static_cast<uint32_t>(m_guild.memberSort));
}

void Game::HandleGuildInf(const char* data, uint16_t size)
{
	if (size < 4) return;
	switch (static_cast<GuildInfKind>(static_cast<uint8_t>(data[3])))
	{
	case GuildInfKind::Info:
	{
		GUILD_INFO_INF_PACKET p{};
		if (size < sizeof(p)) return;
		std::memcpy(&p, data, sizeof(p));
		const uint32_t before = m_guild.id;
		m_guild.id = p.guildId;
		m_guild.emblem = p.emblem;
		m_guild.myRank = p.myRank;
		m_guild.members = p.members;
		m_guild.online = p.online;
		m_guild.name = Fixed(p.name, sizeof(p.name));
		m_guild.notice = Fixed(p.notice, sizeof(p.notice));
		m_guild.leader = Fixed(p.leader, sizeof(p.leader));
		m_guild.known = true;
		if (p.guildId != 0) m_guildNames[p.guildId] = { m_guild.name, p.emblem };
		if (before != 0 && p.guildId == 0)
		{
			m_guild.rows.clear();
			m_guild.tab = 0;
			m_guild.editNotice = false;
		}
		if (m_guildOpen && p.guildId != 0 && (before != p.guildId || m_guild.tab == 1))
			SendGuildReq(GuildOp::Members, static_cast<uint8_t>(m_guild.page), static_cast<uint32_t>(m_guild.memberSort));
		break;
	}
	case GuildInfKind::Members:
	{
		GUILD_MEMBERS_INF_PACKET p{};
		if (size < sizeof(p)) return;
		std::memcpy(&p, data, sizeof(p));
		m_guild.page = p.page;
		m_guild.pages = std::max<int>(1, p.pages);
		m_guild.rows.assign(p.rows, p.rows + std::min<int>(p.count, 8));
		break;
	}
	case GuildInfKind::Name:
	{
		GUILD_NAME_INF_PACKET p{};
		if (size < sizeof(p)) return;
		std::memcpy(&p, data, sizeof(p));
		if (p.guildId != 0) m_guildNames[p.guildId] = { Fixed(p.name, sizeof(p.name)), p.emblem };
		break;
	}
	case GuildInfKind::Top:
	{
		// 결사 회관 깃발 뜰: 깃발 천(렌더 텍스처)은 여기서 만든다 — 패킷 처리는 화면 그리기 전(Frame의 m_net.Update)
		GUILD_TOP_INF_PACKET p{};
		if (size < sizeof(p)) return;
		std::memcpy(&p, data, sizeof(p));
		m_hallTop.clear();
		uint8_t emblems[GuildRule::kHallTop]{};
		const int n = std::min<int>(p.count, GuildRule::kHallTop);
		for (int i = 0; i < n; ++i)
		{
			const GuildTopRow& r = p.rows[i];
			m_hallTop.push_back(HallFlag{ r.guildId, r.emblem, r.members, Fixed(r.name, sizeof(r.name)) });
			m_guildNames[r.guildId] = { m_hallTop.back().name, r.emblem };
			emblem::PrepareCloth(r.emblem);
			emblems[i] = r.emblem;
		}
		props::SetHallFlags(emblems, n);
		std::printf("[guild] top n=%d%s%s\n", n, n > 0 ? " first=" : "", n > 0 ? m_hallTop.front().name.c_str() : "");
		break;
	}
	}
}

void Game::HandleGuildNfy(const GUILD_NFY_PACKET& p)
{
	const auto ev = static_cast<GuildEvent>(p.event);
	const std::string who = Fixed(p.who, sizeof(p.who));
	const std::string by = Fixed(p.by, sizeof(p.by));
	const std::string gname = Fixed(p.name, sizeof(p.name));
	if (p.guildId != 0 && !gname.empty()) m_guildNames[p.guildId] = { gname, p.emblem };
	const bool me = who == m_playerName;
	auto line = [&](const std::string& text, Color c = ui::theme::kChatGuild) { AddChatMsg(chat::Ch::Guild, "", 0, text, c); };
	auto refreshMembers = [&]() { if (m_guildOpen && m_guild.tab == 1 && m_guild.id != 0) SendGuildReq(GuildOp::Members, static_cast<uint8_t>(m_guild.page), static_cast<uint32_t>(m_guild.memberSort)); };
	if (ev == GuildEvent::Created || ev == GuildEvent::Joined || ev == GuildEvent::Left || ev == GuildEvent::Kicked
		|| ev == GuildEvent::Disbanded || ev == GuildEvent::EmblemChanged)
		m_hallTopDirtyAt = m_now;   // 깃발 뜰 순위가 바뀌었을 수 있다 — 0.8초 모아서 다시 묻는다(TickHallTop)

	switch (ev)
	{
	case GuildEvent::Tag:
		if (p.guildId == 0) m_guildTags.erase(p.obj);
		else
		{
			m_guildTags[p.obj] = { p.guildId, p.emblem };
			// 이름은 클라 캐시 — 서버가 실어 보내지 못했으면(드묾) 결사 번호마다 한 번 묻는다(10초 안에는 다시 묻지 않는다)
			if (gname.empty() && m_guildNames.count(p.guildId) == 0)
			{
				auto& at = m_guildNameAsked[p.guildId];
				if (at == 0.0 || m_now - at > 10.0) { at = m_now; SendGuildReq(GuildOp::Name, 0, p.guildId); }
			}
		}
		break;
	case GuildEvent::InviteFrom:
		m_guild.inviteGuild = p.guildId;
		m_guild.inviteEmblem = p.emblem;
		m_guild.inviteFrom = who;
		m_guild.inviteName = gname;
		m_guild.inviteAt = m_now;
		AddChatMsg(chat::Ch::System, "", 0, who + "님이 결사 「" + gname + "」에 초대했습니다. (30초 안에 수락 · 거절)", ui::theme::kChatGuild);
		sfx::Play(sfx::Id::Notice, 0.7f);
		sfx::Play(sfx::Id::BellSmall, 0.35f);
		break;
	case GuildEvent::InviteDeclined:
		AddCenter(who + "님이 초대를 거절했습니다.", ui::theme::kTextDim);
		break;
	case GuildEvent::Joined:
		if (me)
		{
			m_guild.joinedAt = m_now;
			m_guild.celebrateAt = m_now;
			m_guild.celebrateFounded = false;
			sfx::Play(sfx::Id::Seal, 0.7f);
		}
		else
		{
			line(who + "님이 결사에 들어왔습니다.");
			sfx::Play(sfx::Id::BellSmall, 0.4f);
		}
		refreshMembers();
		break;
	case GuildEvent::Left:
		if (me) m_guildOpen = false;
		else line(who + "님이 결사를 떠났습니다.", ui::theme::kTextDim);
		refreshMembers();
		break;
	case GuildEvent::Kicked:
		if (me)
		{
			AddBanner("결사 「" + gname + "」에서 추방되었습니다", ui::theme::kBad, 4.f);
			m_guildOpen = false;
			sfx::Play(sfx::Id::Error, 0.6f);
		}
		else line(who + "님이 " + by + "님에게 추방되었습니다.", ui::theme::kTextDim);
		refreshMembers();
		break;
	case GuildEvent::RankChanged:
		line(who + "님의 직위가 " + GuildRule::RankName(p.rank) + "(으)로 바뀌었습니다. (" + by + ")");
		if (me) sfx::Play(sfx::Id::StatUp, 0.6f);
		refreshMembers();
		break;
	case GuildEvent::LeaderChanged:
		line(who + "님이 결사장이 되었습니다.", ui::theme::kDawnHi);
		if (me) AddBanner("결사장이 되었습니다", ui::theme::kDawnHi, 4.f);
		refreshMembers();   // 결사 정보(결사장 · 직위)는 서버가 모두에게 다시 보낸다
		break;
	case GuildEvent::Online:
		line(who + "님이 접속했습니다.", ui::theme::kTextDim);
		for (auto& row : m_guild.rows) if (Fixed(row.name, sizeof(row.name)) == who) row.online = 1;
		m_guild.online = static_cast<uint8_t>(std::min<int>(m_guild.online + 1, m_guild.members));
		break;
	case GuildEvent::Offline:
		line(who + "님이 나갔습니다.", ui::theme::kTextMute);
		for (auto& row : m_guild.rows)
			if (Fixed(row.name, sizeof(row.name)) == who) { row.online = 0; row.lastSeenMin = static_cast<uint32_t>(std::time(nullptr) / 60); }
		if (m_guild.online > 0) --m_guild.online;
		break;
	case GuildEvent::Disbanded:
		AddBanner("결사 「" + gname + "」이(가) 해산되었습니다", ui::theme::kTextDim, 4.f);
		m_guildOpen = false;
		break;
	case GuildEvent::Created:
		AddChatMsg(chat::Ch::System, "", 0, "결사 「" + gname + "」이(가) 세워졌습니다 — 결사장 " + who, ui::theme::kChatNotice);
		break;
	case GuildEvent::NoticeChanged:
		line("결사 공지가 바뀌었습니다. (" + by + ")");
		break;
	case GuildEvent::EmblemChanged:
		line("결사 문장이 바뀌었습니다. (" + by + ")");
		m_guild.joinedAt = m_now;
		break;
	}
	std::printf("[guild] nfy ev=%d who=%s guild=%u\n", static_cast<int>(ev), who.c_str(), p.guildId);
}

// ─────────────────────────────────────────────────────────────────────────────
// 창
// ─────────────────────────────────────────────────────────────────────────────
void Game::OpenGuildWindow()
{
	m_guildOpen = true;
	m_guild.rowMenu = -1;
	sfx::Play(sfx::Id::Open, 0.6f);
	SendGuildReq(GuildOp::Info);
	if (m_guild.id != 0 && m_guild.tab == 1)
		SendGuildReq(GuildOp::Members, static_cast<uint8_t>(m_guild.page), static_cast<uint32_t>(m_guild.memberSort));
}

void Game::OpenGuildCreate(bool emblemOnly)
{
	m_guild.createOpen = true;
	m_guild.emblemOnly = emblemOnly;
	if (emblemOnly)
	{
		m_guild.shape = GuildRule::EmblemShape(m_guild.emblem);
		m_guild.color = GuildRule::EmblemColor(m_guild.emblem);
	}
	m_guild.createName.maxBytes = GuildRule::kNameBytes - 1;
	sfx::Play(sfx::Id::Open, 0.6f);
}

void Game::DrawGuildWindow()
{
	const float W = m_uiW, H = m_uiH;
	const Rectangle r = wm::Window("guild", Rectangle{ std::round(W * 0.5f - 300.f), std::round(std::max(12.f, H * 0.5f - 250.f)), 600.f, 470.f }, wm::kMove);
	ui::Window(r, "결사", &m_guildOpen);
	if (!m_guildOpen) { sfx::Play(sfx::Id::Close, 0.5f); return; }
	const bool busy = m_guild.pendingSeq != 0 && m_now - m_guild.pendingAt < 3.0;

	if (m_guild.id == 0)
	{
		// ── 결사가 없을 때: 안내 + 회관으로 가는 길 ──
		const Vector2 c{ r.x + r.width * 0.5f, r.y + 130.f };
		emblem::Draw(c, 76.f, GuildRule::Emblem(0, 4), 0.35f);
		ui::Text("결사에 들어 있지 않다", Vector2{ c.x, r.y + 190.f }, ui::type::kDisplayS, ui::theme::kTextHi, ui::Align::Center);
		ui::TextWrapped("결사는 함께 사냥하고 이름을 걸고 싸우는 모임이다. 윗마을 결사 회관의 회관지기 즈데노에게서 세울 수 있다 — "
			"레벨 15 이상 · 10,000 그로시 · 이름 2~8자 · 문장 하나. 정예 이상의 결사원에게 초대를 받아 들어갈 수도 있다.",
			Rectangle{ r.x + 60.f, r.y + 224.f, r.width - 120.f, 90.f }, ui::type::kBody, ui::theme::kText, 6.f);
		const Rectangle go{ r.x + r.width * 0.5f - 110.f, r.y + r.height - 76.f, 220.f, 42.f };
		if (ui::Button(go, "결사 회관으로 가기", ui::ButtonKind::Primary))
		{
			SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, GuildRule::kHallNpcId });
			m_guildOpen = false;
			sfx::Play(sfx::Id::Click, 0.5f);
		}
		return;
	}

	// ── 머리: 문장 · 이름 · 인원 · 내 직위 ──
	const float glow = std::max(0.f, 1.f - static_cast<float>(m_now - m_guild.joinedAt) / 1.6f);
	const Vector2 ec{ r.x + 58.f, r.y + 94.f };
	emblem::Draw(ec, 70.f * (1.f + 0.12f * util::Pulse(1.f - glow)), m_guild.emblem, 1.f, glow);
	{
		ui::FaceScope f(ui::Face::Display);
		ui::Text(m_guild.name, Vector2{ r.x + 104.f, r.y + 58.f }, ui::type::kDisplayM, ui::theme::kTextHi);
	}
	ui::Text(Fmt("결사원 %d / %d  ·  접속 %d", m_guild.members, GuildRule::kMaxMembers, m_guild.online), Vector2{ r.x + 105.f, r.y + 96.f }, ui::type::kBodyS, ui::theme::kTextDim);
	ui::Text(std::string("결사장 ") + m_guild.leader, Vector2{ r.x + 105.f, r.y + 116.f }, ui::type::kBodyS, ui::theme::kTextDim);
	{
		const std::string rk = GuildRule::RankName(m_guild.myRank);
		const Vector2 m = ui::Fonts().Measure(rk, ui::type::kCaption, 0.f, ui::Face::Strong);
		const Rectangle chip{ r.x + r.width - 24.f - m.x - 18.f, r.y + 58.f, m.x + 18.f, 22.f };
		ui::Plate(chip, util::WithA(RankColor(m_guild.myRank), 0.16f), RankColor(m_guild.myRank), 3.f, 1.f);
		ui::Text(rk, Vector2{ chip.x + chip.width * 0.5f, chip.y + 4.f }, ui::type::kCaption, RankColor(m_guild.myRank), ui::Align::Center);
		ui::Text("내 직위", Vector2{ chip.x - 8.f, chip.y + 4.f }, ui::type::kCaption, ui::theme::kTextMute, ui::Align::Right);
	}

	// ── 탭: 정보 · 구성원 · 관리(정예 이상) ──
	const Rank my = R(m_guild.myRank);
	const bool canManage = GuildRule::CanInvite(my);
	if (!canManage && m_guild.tab == 2) m_guild.tab = 0;
	const char* tabs[] = { "정보", "구성원", "관리" };
	for (int i = 0; i < (canManage ? 3 : 2); ++i)
	{
		const Rectangle t{ r.x + 16.f + i * 96.f, r.y + 144.f, 92.f, 30.f };
		const bool sel = m_guild.tab == i;
		if (ui::Button(t, tabs[i], sel ? ui::ButtonKind::Primary : ui::ButtonKind::Ghost, true, 15.f) && !sel)
		{
			m_guild.tab = i;
			m_guild.rowMenu = -1;
			if (i == 1) SendGuildReq(GuildOp::Members, static_cast<uint8_t>(m_guild.page), static_cast<uint32_t>(m_guild.memberSort));
		}
	}
	ui::Rule(r.x + 12.f, r.x + r.width - 12.f, r.y + 178.f);
	const Rectangle body{ r.x + 16.f, r.y + 188.f, r.width - 32.f, r.height - 204.f };

	auto armed = [&](int kind, const std::string& target) { return m_guild.armKind == kind && m_guild.armTarget == target && m_now - m_guild.armAt < 3.0; };
	auto arm = [&](int kind, const std::string& target) { m_guild.armKind = kind; m_guild.armTarget = target; m_guild.armAt = m_now; sfx::Play(sfx::Id::Click, 0.4f); };

	if (m_guild.tab == 0)
	{
		// ── 정보: 공지 · 할 수 있는 것 · 탈퇴 ──
		ui::SectionHeader(Vector2{ body.x, body.y + 2.f }, body.width, "공지");
		const Rectangle nb{ body.x, body.y + 24.f, body.width, 120.f };
		ui::Plate(nb, Color{ 11, 13, 16, 200 }, ui::theme::kLine, 3.f, 1.f);
		ui::TextWrapped(m_guild.notice.empty() ? std::string("아직 공지가 없다.") : m_guild.notice, Rectangle{ nb.x + 12.f, nb.y + 12.f, nb.width - 24.f, nb.height - 20.f },
			ui::type::kBody, m_guild.notice.empty() ? ui::theme::kTextMute : ui::theme::kText, 6.f);
		ui::SectionHeader(Vector2{ body.x, nb.y + nb.height + 14.f }, body.width, "내 직위로 할 수 있는 것");
		std::string can = "결사 채팅(/길)";
		if (GuildRule::CanInvite(my)) can += " · 초대(/초대 이름)";
		if (my <= Rank::Officer) can += " · 추방 · 직위(정예 · 결사원) · 공지";
		if (my == Rank::Leader) can = "모든 일 — 직위 · 추방 · 공지 · 문장(회관지기 곁) · 결사장 넘기기 · 해산";
		ui::TextWrapped(can, Rectangle{ body.x + 2.f, nb.y + nb.height + 38.f, body.width - 4.f, 40.f }, ui::type::kBodyS, ui::theme::kTextDim, 4.f);
		if (my != Rank::Leader)
		{
			const Rectangle leave{ r.x + r.width - 16.f - 150.f, r.y + r.height - 54.f, 150.f, 38.f };
			const char* label = armed(1, "") ? "정말 떠날까요?" : "결사 탈퇴";
			if (ui::Button(leave, label, ui::ButtonKind::Danger, !busy, 14.f))
			{
				if (armed(1, "")) { SendGuildReq(GuildOp::Leave); m_guild.armKind = 0; }
				else arm(1, "");
			}
		}
		else
			ui::Text("결사장은 결사장을 넘긴 뒤에 떠날 수 있다(구성원 탭에서 한 사람을 눌러서).", Vector2{ body.x, r.y + r.height - 44.f }, ui::type::kCaption, ui::theme::kTextMute);
	}
	else if (m_guild.tab == 1)
	{
		// ── 구성원: 8명씩 — 머리글을 누르면 그 기준으로 정렬(서버가 정렬해 페이지로 준다) ──
		const float rowH = 30.f;
		const float cx[] = { body.x + 26.f, body.x + 196.f, body.x + 276.f, body.x + 336.f, body.x + body.width - 8.f };
		struct Head { const char* label; int sort; float x; ui::Align align; };
		const Head heads[] = { { "이름", 2, cx[0], ui::Align::Left }, { "직위", 0, cx[1], ui::Align::Left }, { "레벨", 1, cx[2], ui::Align::Left },
			{ "직업", -1, cx[3], ui::Align::Left }, { "접속", 3, cx[4], ui::Align::Right } };
		for (const Head& h : heads)
		{
			const bool sel = h.sort == m_guild.memberSort;
			const std::string label = std::string(h.label) + (sel ? " ▼" : "");
			const Vector2 m = ui::Fonts().Measure(label, ui::type::kCaption, 0.f, ui::Face::Inherit);
			const Rectangle hr{ h.align == ui::Align::Right ? h.x - m.x - 4.f : h.x - 4.f, body.y, m.x + 8.f, 18.f };
			const bool hover = h.sort >= 0 && ui::Hovered(hr);
			ui::Text(label, Vector2{ h.x, body.y + 2.f }, ui::type::kCaption, sel ? ui::theme::kDawn : (hover ? ui::theme::kTextHi : ui::theme::kTextMute), h.align);
			if (h.sort >= 0 && ui::Clicked(hr) && !sel)
			{
				m_guild.memberSort = h.sort;
				SendGuildReq(GuildOp::Members, 0, static_cast<uint32_t>(h.sort));
				sfx::Play(sfx::Id::Click, 0.4f);
			}
		}
		int menuRow = -1;
		Rectangle menuAt{};
		for (size_t i = 0; i < m_guild.rows.size(); ++i)
		{
			const GuildMemberRow& m = m_guild.rows[i];
			const std::string name = Fixed(m.name, sizeof(m.name));
			const Rectangle rr{ body.x, body.y + 20.f + static_cast<float>(i) * rowH, body.width, rowH - 3.f };
			const bool hover = ui::Hovered(rr);
			const bool isMe = name == m_playerName;
			const bool sel = m_guild.rowMenu == static_cast<int>(i);
			ui::Plate(rr, sel ? ui::theme::kSurface3 : (hover ? ui::theme::kSurface2 : ui::theme::kSurface1), sel ? ui::theme::kDawn : BLANK, 2.f, sel ? 1.f : 0.f);
			DrawCircleV(Vector2{ rr.x + 12.f, rr.y + rr.height * 0.5f }, 4.f, m.online ? ui::theme::kGood : ui::theme::kTextMute);
			ui::Text(name + (isMe ? "  (나)" : ""), Vector2{ cx[0], rr.y + 6.f }, ui::type::kBodyS, m.online ? ui::theme::kTextHi : ui::theme::kTextDim);
			ui::Text(GuildRule::RankName(m.rank), Vector2{ cx[1], rr.y + 6.f }, ui::type::kBodyS, RankColor(m.rank));
			ui::Text(std::to_string(m.level), Vector2{ cx[2], rr.y + 6.f }, ui::type::kBodyS, ui::theme::kText);
			ui::Text(ClassTable::IsValid(m.classId) ? ClassTable::Get(static_cast<ClassId>(m.classId)).name : "-", Vector2{ cx[3], rr.y + 6.f }, ui::type::kBodyS, ui::theme::kTextDim);
			ui::Text(m.online ? std::string("접속 중") : SeenText(m.lastSeenMin), Vector2{ cx[4], rr.y + 6.f }, ui::type::kCaption, m.online ? ui::theme::kGood : ui::theme::kTextMute, ui::Align::Right);
			// 누르면 이 사람에게 할 수 있는 일(권한 있는 것만)
			const Rank them = R(m.rank);
			const bool anyAction = !isMe && (GuildRule::CanKick(my, them) || my == Rank::Leader || (my == Rank::Officer && them > Rank::Officer));
			if (anyAction && ui::Clicked(rr))
			{
				m_guild.rowMenu = sel ? -1 : static_cast<int>(i);
				sfx::Play(sfx::Id::Click, 0.4f);
			}
			if (sel) { menuRow = static_cast<int>(i); menuAt = rr; }
		}
		if (m_guild.rows.empty())
			ui::Text("불러오는 중…", Vector2{ body.x + body.width * 0.5f, body.y + 80.f }, ui::type::kBodyS, ui::theme::kTextMute, ui::Align::Center);
		// 줄 메뉴: 직위 올리기 · 내리기 · 결사장 넘기기 · 추방
		if (menuRow >= 0 && menuRow < static_cast<int>(m_guild.rows.size()))
		{
			const GuildMemberRow& m = m_guild.rows[static_cast<size_t>(menuRow)];
			const std::string name = Fixed(m.name, sizeof(m.name));
			const Rank them = R(m.rank);
			float bx = menuAt.x + 150.f;
			const float by = menuAt.y + menuAt.height + 2.f;
			const Rectangle strip{ menuAt.x + 140.f, by - 2.f, menuAt.width - 140.f, 34.f };
			ui::Plate(strip, ui::theme::kSurface0, ui::theme::kLineStrong, 3.f, 1.f);
			ui::Block(strip);
			auto act = [&](const char* label, float w, bool enabled, ui::ButtonKind kind = ui::ButtonKind::Ghost)
			{
				const bool c = ui::Button(Rectangle{ bx, by + 2.f, w, 28.f }, label, kind, enabled && !busy, 13.f);
				bx += w + 4.f;
				return c;
			};
			const Rank up = static_cast<Rank>(std::max(1, static_cast<int>(them) - 1));
			const Rank down = static_cast<Rank>(std::min(3, static_cast<int>(them) + 1));
			if (act("직위 ▲", 74.f, them != up && GuildRule::CanSetRank(my, them, up)))
				SendGuildReq(GuildOp::SetRank, static_cast<uint8_t>(up), 0, name);
			if (act("직위 ▼", 74.f, them != down && GuildRule::CanSetRank(my, them, down)))
				SendGuildReq(GuildOp::SetRank, static_cast<uint8_t>(down), 0, name);
			if (my == Rank::Leader && act(armed(3, name) ? "넘길까요?" : "결사장 넘기기", 104.f, true))
			{
				if (armed(3, name)) { SendGuildReq(GuildOp::Transfer, 0, 0, name); m_guild.armKind = 0; m_guild.rowMenu = -1; }
				else arm(3, name);
			}
			if (act(armed(4, name) ? "추방할까요?" : "추방", 78.f, GuildRule::CanKick(my, them), ui::ButtonKind::Danger))
			{
				if (armed(4, name)) { SendGuildReq(GuildOp::Kick, 0, 0, name); m_guild.armKind = 0; m_guild.rowMenu = -1; }
				else arm(4, name);
			}
		}
		// 페이지
		const float py = r.y + r.height - 44.f;
		if (m_guild.pages > 1)
		{
			if (ui::Button(Rectangle{ body.x + body.width * 0.5f - 90.f, py, 36.f, 28.f }, "◀", ui::ButtonKind::Ghost, m_guild.page > 0, 14.f))
				SendGuildReq(GuildOp::Members, static_cast<uint8_t>(m_guild.page - 1), static_cast<uint32_t>(m_guild.memberSort));
			ui::Text(Fmt("%d / %d", m_guild.page + 1, m_guild.pages), Vector2{ body.x + body.width * 0.5f, py + 6.f }, ui::type::kBodyS, ui::theme::kTextDim, ui::Align::Center);
			if (ui::Button(Rectangle{ body.x + body.width * 0.5f + 54.f, py, 36.f, 28.f }, "▶", ui::ButtonKind::Ghost, m_guild.page + 1 < m_guild.pages, 14.f))
				SendGuildReq(GuildOp::Members, static_cast<uint8_t>(m_guild.page + 1), static_cast<uint32_t>(m_guild.memberSort));
		}
	}
	else
	{
		// ── 관리(정예 이상): 초대 · 공지(부관 이상) · 문장 · 해산(결사장) ──
		float y = body.y;
		ui::SectionHeader(Vector2{ body.x, y + 2.f }, body.width, "초대");
		m_guild.inviteField.maxBytes = NAME_SIZE - 1;
		m_guild.inviteField.asciiOnly = true;
		const bool submit = ui::InputText(Rectangle{ body.x, y + 24.f, 220.f, 36.f }, m_guild.inviteField, "초대할 이름");
		if ((ui::Button(Rectangle{ body.x + 228.f, y + 24.f, 86.f, 36.f }, "초대", ui::ButtonKind::Primary, !m_guild.inviteField.text.empty() && !busy, 14.f) || submit)
			&& !m_guild.inviteField.text.empty())
		{
			SendGuildReq(GuildOp::Invite, 0, 0, m_guild.inviteField.text);
			m_guild.inviteField.text.clear();
		}
		ui::Text("30초 안에 답이 온다 · 채팅 이름 메뉴나 /초대 이름으로도", Vector2{ body.x + 324.f, y + 34.f }, ui::type::kCaption, ui::theme::kTextMute);
		y += 76.f;
		if (GuildRule::CanNotice(my))
		{
			ui::SectionHeader(Vector2{ body.x, y + 2.f }, body.width, "공지 고치기");
			m_guild.noticeField.maxBytes = GuildRule::kNoticeBytes - 1;
			if (!m_guild.editNotice) { m_guild.noticeField.text = m_guild.notice; m_guild.editNotice = true; }
			const bool sub2 = ui::InputText(Rectangle{ body.x, y + 24.f, body.width - 104.f, 36.f }, m_guild.noticeField, "결사원에게 남길 말(30자)");
			if (ui::Button(Rectangle{ body.x + body.width - 96.f, y + 24.f, 96.f, 36.f }, "저장", ui::ButtonKind::Primary,
				!busy && m_guild.noticeField.text != m_guild.notice, 14.f) || (sub2 && m_guild.noticeField.text != m_guild.notice))
			{
				ui::CommitComposition(m_guild.noticeField);
				SendGuildReq(GuildOp::Notice, 0, 0, std::string(), m_guild.noticeField.text);
			}
			ui::Text(Fmt("%zu / %d 바이트", m_guild.noticeField.text.size(), GuildRule::kNoticeBytes - 1), Vector2{ body.x + body.width - 104.f, y + 64.f }, ui::type::kCaption, ui::theme::kTextMute, ui::Align::Right);
			y += 90.f;
		}
		if (my == Rank::Leader)
		{
			ui::SectionHeader(Vector2{ body.x, y + 2.f }, body.width, "결사장");
			if (ui::Button(Rectangle{ body.x, y + 24.f, 150.f, 36.f }, "문장 바꾸기", ui::ButtonKind::Normal, !busy, 14.f))
			{
				// 회관지기 곁에서만 — 멀면 그리로 걸어간다
				const Vector2 npc = NpcTile(GuildRule::kHallNpcId);
				const Entity* self = Self();
				const bool near = self && std::max(std::abs(npc.x - static_cast<float>(self->tx)), std::abs(npc.y - static_cast<float>(self->ty))) <= static_cast<float>(WorldData::kNpcInteractRange) + 0.5f;
				if (near) OpenGuildCreate(true);
				else { SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, GuildRule::kHallNpcId }); AddCenter("회관지기 즈데노에게 갑니다 — 문장은 그의 곁에서 바꿀 수 있습니다.", ui::theme::kTextDim); }
			}
			const bool alone = m_guild.members <= 1;
			const char* label = armed(2, "") ? "정말 해산할까요?" : "결사 해산";
			const Rectangle dis{ body.x + 158.f, y + 24.f, 150.f, 36.f };
			if (ui::Button(dis, label, ui::ButtonKind::Danger, alone && !busy, 14.f))
			{
				if (armed(2, "")) { SendGuildReq(GuildOp::Disband); m_guild.armKind = 0; }
				else arm(2, "");
			}
			ui::Text(alone ? "혼자 남았다 — 해산할 수 있다." : "결사원이 모두 떠난 뒤에 해산할 수 있다. 결사장은 구성원 탭에서 넘긴다.",
				Vector2{ body.x, y + 68.f }, ui::type::kCaption, ui::theme::kTextMute);
		}
	}
}

void Game::DrawGuildCreate()
{
	const float W = m_uiW, H = m_uiH;
	const Rectangle r = wm::Window("guildnew", Rectangle{ std::round(W * 0.5f - 280.f), std::round(std::max(12.f, H * 0.5f - 250.f)), 560.f, 480.f }, wm::kMove);
	bool open = m_guild.createOpen;
	ui::Window(r, m_guild.emblemOnly ? "결사 문장 바꾸기 — 회관지기 즈데노" : "결사 세우기 — 회관지기 즈데노", &open);
	if (!open) { m_guild.createOpen = false; sfx::Play(sfx::Id::Close, 0.5f); return; }
	const bool busy = m_guild.pendingSeq != 0 && m_now - m_guild.pendingAt < 3.0;
	const uint8_t em = GuildRule::Emblem(m_guild.shape, m_guild.color);

	// 왼쪽: 큰 문장 미리 보기(살짝 숨 쉬듯)
	const Vector2 pc{ r.x + 110.f, r.y + 150.f };
	const float breathe = 1.f + 0.025f * std::sin(static_cast<float>(m_now) * 2.2f);
	ui::Plate(Rectangle{ r.x + 20.f, r.y + 52.f, 180.f, 200.f }, Color{ 11, 13, 16, 210 }, ui::theme::kLine, 4.f, 1.f);
	emblem::Draw(pc, 108.f * breathe, em, 1.f, 0.25f);
	ui::Text(std::string(emblem::ShapeName(m_guild.shape)) + " · " + emblem::ColorName(m_guild.color), Vector2{ pc.x, r.y + 222.f }, ui::type::kBodyS, ui::theme::kTextDim, ui::Align::Center);

	// 오른쪽: 이름 · 모양 · 색
	const float x0 = r.x + 218.f, w0 = r.width - 238.f;
	float y = r.y + 54.f;
	bool nameOk = true;
	if (!m_guild.emblemOnly)
	{
		ui::SectionHeader(Vector2{ x0, y }, w0, "결사 이름");
		m_guild.createName.maxBytes = GuildRule::kNameBytes - 1;
		(void)ui::InputText(Rectangle{ x0, y + 22.f, w0, 38.f }, m_guild.createName, "2~8자 · 한글 · 영문 · 숫자");
		const auto chk = GuildRule::CheckName(m_guild.createName.text.c_str());
		nameOk = chk == GuildRule::NameCheck::Ok;
		const char* msg = chk == GuildRule::NameCheck::Ok ? "쓸 수 있는 모양이다(겹치는지는 세울 때 확인)"
			: chk == GuildRule::NameCheck::TooShort ? "2자 이상"
			: chk == GuildRule::NameCheck::TooLong ? "8자까지"
			: chk == GuildRule::NameCheck::Banned ? "쓸 수 없는 말이 들어 있다(운영 사칭 · 욕설)"
			: "한글 음절 · 영문 · 숫자만(띄어쓰기 · 기호 없이)";
		ui::Text(msg, Vector2{ x0 + 2.f, y + 64.f }, ui::type::kCaption, nameOk ? ui::theme::kGood : (m_guild.createName.text.empty() ? ui::theme::kTextMute : ui::theme::kWarn));
		y += 90.f;
	}
	ui::SectionHeader(Vector2{ x0, y }, w0, "모양");
	y += 22.f;
	const float cs = 44.f;
	for (int i = 0; i < GuildRule::kEmblemShapes; ++i)
	{
		const Rectangle c{ x0 + static_cast<float>(i % 6) * (cs + 4.f), y + static_cast<float>(i / 6) * (cs + 4.f), cs, cs };
		const bool sel = m_guild.shape == i, hover = ui::Hovered(c);
		ui::Plate(c, sel ? ui::theme::kSurface3 : (hover ? ui::theme::kSurface2 : ui::theme::kSurface1), sel ? ui::theme::kDawn : ui::theme::kLine, 3.f, sel ? 1.5f : 1.f);
		emblem::DrawShape(Vector2{ c.x + cs * 0.5f, c.y + cs * 0.5f }, cs * 0.9f, i, sel ? ui::theme::kDawnHi : ui::theme::kText, sel ? ui::theme::kSurface3 : (hover ? ui::theme::kSurface2 : ui::theme::kSurface1));
		if (ui::Clicked(c)) { m_guild.shape = i; sfx::Play(sfx::Id::Click, 0.4f); }
		if (hover) ui::Tooltip({ { emblem::ShapeName(i), ui::theme::kTextHi, 13.f, ui::Face::Strong } });
	}
	y += 2.f * (cs + 4.f) + 10.f;
	ui::SectionHeader(Vector2{ x0, y }, w0, "바탕색");
	y += 22.f;
	for (int i = 0; i < GuildRule::kEmblemColors; ++i)
	{
		const Rectangle c{ x0 + static_cast<float>(i) * (cs + 4.f), y, cs, 30.f };
		const bool sel = m_guild.color == i, hover = ui::Hovered(c);
		ui::Plate(c, emblem::Field(i), sel ? ui::theme::kDawnHi : (hover ? ui::theme::kTextDim : ui::theme::kLine), 3.f, sel ? 2.f : 1.f);
		if (ui::Clicked(c)) { m_guild.color = i; sfx::Play(sfx::Id::Click, 0.4f); }
		if (hover) ui::Tooltip({ { emblem::ColorName(i), ui::theme::kTextHi, 13.f, ui::Face::Strong } });
	}
	y += 44.f;

	// 조건(세우기) · 회관지기 거리
	bool near = false;
	if (const Entity* self = Self())
	{
		const Vector2 npc = NpcTile(GuildRule::kHallNpcId);
		near = std::max(std::abs(npc.x - static_cast<float>(self->tx)), std::abs(npc.y - static_cast<float>(self->ty))) <= static_cast<float>(WorldData::kNpcInteractRange) + 0.5f;
	}
	const bool lvOk = m_level >= GuildRule::kMinLevel;
	const bool adenaOk = m_inv.adena >= GuildRule::kCreateCost;
	float ly = r.y + 270.f;
	auto req = [&](bool ok, const std::string& text)
	{
		ui::Text(ok ? "✓" : "✕", Vector2{ r.x + 30.f, ly }, ui::type::kBodyS, ok ? ui::theme::kGood : ui::theme::kBad);
		ui::Text(text, Vector2{ r.x + 48.f, ly }, ui::type::kBodyS, ok ? ui::theme::kText : ui::theme::kTextDim);
		ly += 22.f;
	};
	if (!m_guild.emblemOnly)
	{
		req(lvOk, Fmt("레벨 %d 이상 (지금 %d)", GuildRule::kMinLevel, m_level));
		req(adenaOk, Fmt("%s 그로시 (가진 것 %s)", "10,000", std::to_string(m_inv.adena).c_str()));
	}
	req(near, "회관지기 즈데노 곁에서");

	const bool can = near && !busy && (m_guild.emblemOnly || (nameOk && lvOk && adenaOk));
	const Rectangle ok{ r.x + r.width - 16.f - 170.f, r.y + r.height - 56.f, 170.f, 40.f };
	if (ui::Button(ok, m_guild.emblemOnly ? "문장 바꾸기" : "결사 세우기 (10,000)", ui::ButtonKind::Primary, can, 14.f))
	{
		if (m_guild.emblemOnly) SendGuildReq(GuildOp::Emblem, em);
		else
		{
			ui::CommitComposition(m_guild.createName);
			SendGuildReq(GuildOp::Create, em, 0, std::string(), m_guild.createName.text);
		}
	}
	if (ui::Button(Rectangle{ ok.x - 96.f, ok.y, 88.f, 40.f }, "닫기", ui::ButtonKind::Ghost, true, 14.f))
	{
		m_guild.createOpen = false;
		sfx::Play(sfx::Id::Close, 0.5f);
	}
}

void Game::DrawGuildInvite()
{
	if (m_guild.inviteGuild == 0) return;
	const double left = GuildRule::kInviteMs / 1000.0 - (m_now - m_guild.inviteAt);
	if (left <= 0.0) { m_guild.inviteGuild = 0; return; }
	const float W = m_uiW;
	const float appear = util::EaseOutBack(static_cast<float>(std::min(1.0, (m_now - m_guild.inviteAt) / 0.3)));
	const Rectangle r = wm::Window("guildinv", Rectangle{ std::round(W * 0.5f - 210.f), std::round(m_bannersBottom + 8.f), 420.f, 116.f }, wm::kNoAnim);
	ui::PushXform(Vector2{ r.x + r.width * 0.5f * (1.f - appear), r.y + r.height * 0.5f * (1.f - appear) }, appear);
	const Rectangle lr{ 0.f, 0.f, r.width, r.height };
	ui::Panel(lr, 0.97f, true);
	ui::Plate(Rectangle{ lr.x, lr.y, 3.f, lr.height }, ui::theme::kChatGuild, BLANK, 0.f, 0.f);
	emblem::Draw(Vector2{ 46.f, 52.f }, 52.f, m_guild.inviteEmblem, 1.f, 0.2f + 0.2f * std::sin(static_cast<float>(m_now) * 3.f));
	ui::Text(m_guild.inviteFrom + "님의 초대", Vector2{ 84.f, 14.f }, ui::type::kCaption, ui::theme::kTextDim);
	{
		ui::FaceScope f(ui::Face::Display);
		ui::Text("결사 「" + m_guild.inviteName + "」", Vector2{ 84.f, 32.f }, ui::type::kDisplayS, ui::theme::kTextHi);
	}
	// 남은 시간 띠
	const float frac = static_cast<float>(left / (GuildRule::kInviteMs / 1000.0));
	DrawRectangleRec(Rectangle{ 84.f, 66.f, (lr.width - 100.f), 3.f }, ui::theme::kSurface2);
	DrawRectangleRec(Rectangle{ 84.f, 66.f, (lr.width - 100.f) * frac, 3.f }, frac < 0.25f ? ui::theme::kWarn : ui::theme::kChatGuild);
	if (ui::Button(Rectangle{ lr.width - 200.f, 76.f, 90.f, 30.f }, "수락", ui::ButtonKind::Primary, true, 14.f))
	{
		SendGuildReq(GuildOp::InviteReply, 1, m_guild.inviteGuild);
		m_guild.inviteGuild = 0;
	}
	if (ui::Button(Rectangle{ lr.width - 104.f, 76.f, 90.f, 30.f }, Fmt("거절 %d", static_cast<int>(std::ceil(left))), ui::ButtonKind::Ghost, true, 14.f))
	{
		SendGuildReq(GuildOp::InviteReply, 0, m_guild.inviteGuild);
		m_guild.inviteGuild = 0;
	}
	ui::PopXform();
}

// ─────────────────────────────────────────────────────────────────────────────
// 이름표: 이름 왼쪽 문장 + 이름 위 결사 이름(같은 결사는 풀빛)
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawGuildTag(const Entity& e, Vector2 head, float& y, float nameW)
{
	uint32_t gid = 0;
	uint8_t em = 0;
	if (e.id == m_selfId) { gid = m_guild.id; em = m_guild.emblem; }
	else if (const auto it = m_guildTags.find(e.id); it != m_guildTags.end()) { gid = it->second.first; em = it->second.second; }
	if (gid == 0) return;
	const float nameY = y + 9.f;   // 방금 그린 이름 줄의 가운데(호출 쪽이 y를 이름 줄 위로 올린 뒤에 부른다)
	emblem::Draw(Vector2{ std::round(head.x - nameW * 0.5f - 11.f), std::round(nameY) }, 15.f, em);
	const std::string gname = GuildNameOf(gid);
	if (gname.empty()) return;
	ui::TextStyle st;
	st.size = 11.5f;
	st.face = ui::Face::Strong;
	st.color = gid == m_guild.id ? ui::theme::kChatGuild : ui::theme::kParchment;
	st.outline = 1.3f;
	st.outlineColor = util::A8(ui::theme::kShadow, 210);
	ui::Fonts().DrawCentered("‹" + gname + "›", Vector2{ head.x, y - 5.f }, st);
	y -= 14.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// 결사 회관 깃발 뜰(Phase 5 자기 검토): 인원 많은 결사의 문장이 회관 앞 깃대에 걸린다 — 결사가 창 밖 마을에도 보이게.
// 순위는 서버 메모리(GuildOp::Top · GUILD_INF Top). 회관 30칸 안에 있을 때만 20초마다 묻고, 결사 사건 뒤에는 0.8초 모아서 한 번 더.
// ─────────────────────────────────────────────────────────────────────────────
void Game::TickHallTop()
{
	if (!m_hasSelf || !InTown()) return;
	const int hx = WorldData::HomeX(GuildRule::kHallNpcId), hy = WorldData::HomeY(GuildRule::kHallNpcId);
	if (std::max(std::abs(m_predX - hx), std::abs(m_predY - hy)) > 30) return;
	const bool dirty = m_hallTopDirtyAt >= 0.0 && m_now - m_hallTopDirtyAt > 0.8;
	const bool allowed = m_now - m_hallTopAskAt > GuildRule::kTopQueryMs / 1000.0 + 0.1;   // 서버는 2초 안의 두 번째 요청을 버린다
	if ((dirty && allowed) || m_now - m_hallTopAskAt > 20.0)
	{
		m_hallTopAskAt = m_now;
		m_hallTopDirtyAt = -1.0;
		SendGuildReq(GuildOp::Top);
	}
}

void Game::DrawHallFlagLabels(const Camera2D& cam)
{
	if (m_hallTop.empty() || !InTown()) return;
	for (size_t i = 0; i < m_hallTop.size(); ++i)
	{
		int x = 0, y = 0;
		if (!props::HallFlagTile(static_cast<int>(i), x, y)) break;
		if (std::max(std::abs(m_predX - x), std::abs(m_predY - y)) > 14) continue;   // 가까이 왔을 때만(마을 이름표 규칙)
		// 깃대 밑에서 3.3칸 위(천의 아랫단 바로 밑) — 깃발 천은 깃대 꼭대기 4.2칸에서 0.72칸 드리운다
		const Vector2 at = GetWorldToScreen2D(SurfaceIso(Vector2{ static_cast<float>(x), static_cast<float>(y) }, 3.3f), cam);
		if (at.x < -80.f || at.y < -40.f || at.x > m_screenW + 80.f || at.y > m_screenH + 40.f) continue;
		const HallFlag& f = m_hallTop[i];
		ui::TextStyle st;
		st.size = 12.5f;
		st.face = ui::Face::Strong;
		st.color = f.id == m_guild.id ? ui::theme::kChatGuild : ui::theme::kText;
		st.outline = 1.3f;
		st.outlineColor = util::A8(ui::theme::kShadow, 210);
		ui::Fonts().DrawCentered("‹" + f.name + "›", Vector2{ std::round(at.x + 16.f), std::round(at.y) }, st);
		ui::TextStyle sub = st;
		sub.size = 10.5f;
		sub.face = ui::Face::Body;
		sub.color = util::WithA(ui::theme::kParchment, 0.95f);
		ui::Fonts().DrawCentered(Fmt("결사원 %d", f.members), Vector2{ std::round(at.x + 16.f), std::round(at.y + 13.f) }, sub);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 축하: 화면 위에서 깃발이 펼쳐진다(세움 — 새벽빛 테 · 들어옴 — 결사 풀빛 테). 3.8초
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawGuildCelebration()
{
	const float t = static_cast<float>(m_now - m_guild.celebrateAt);
	if (t < 0.f || t > 3.8f || m_guild.id == 0) return;
	const float W = m_uiW;
	const float drop = util::EaseOutBack(std::min(1.f, t / 0.55f), 1.2f);     // 깃대에서 떨어지듯 내려온다
	const float fade = t > 3.2f ? 1.f - (t - 3.2f) / 0.6f : 1.f;
	const float bw = 176.f, bh = 238.f * drop;
	const float cx = std::round(W * 0.5f), top = 70.f;
	const Color edge = m_guild.celebrateFounded ? ui::theme::kDawn : ui::theme::kChatGuild;
	// 깃대
	DrawRectangleRec(Rectangle{ cx - bw * 0.5f - 14.f, top - 6.f, bw + 28.f, 6.f }, util::MulA(ui::theme::kDawnDeep, fade));
	DrawCircleV(Vector2{ cx - bw * 0.5f - 14.f, top - 3.f }, 5.f, util::MulA(ui::theme::kDawnDeep, fade));
	DrawCircleV(Vector2{ cx + bw * 0.5f + 14.f, top - 3.f }, 5.f, util::MulA(ui::theme::kDawnDeep, fade));
	// 천: 아래 끝이 물결치며 제비꼬리 — 문장 바탕색을 깊게
	const Color cloth = util::Shade(emblem::Field(GuildRule::EmblemColor(m_guild.emblem)), 0.62f);
	const int cols = 24;
	for (int i = 0; i < cols; ++i)
	{
		const float u0 = static_cast<float>(i) / cols, u1 = static_cast<float>(i + 1) / cols;
		auto bottom = [&](float u)
		{
			const float tail = 26.f * (1.f - std::abs(u - 0.5f) * 2.f);   // 가운데가 파인 제비꼬리
			const float wave = 4.f * std::sin(u * 9.f + t * 5.f) * (1.f - drop * 0.6f + 0.4f);
			return top + bh - tail * drop + wave;
		};
		const float x0 = cx - bw * 0.5f + bw * u0, x1 = cx - bw * 0.5f + bw * u1;
		const float y0 = bottom(u0), y1 = bottom(u1);
		const Color c = util::MulA(util::Shade(cloth, 0.92f + 0.12f * std::sin(u0 * 6.f + t * 3.f)), fade);
		DrawTriangle(Vector2{ x0, top }, Vector2{ x0, y0 }, Vector2{ x1, y1 }, c);
		DrawTriangle(Vector2{ x0, top }, Vector2{ x1, y1 }, Vector2{ x1, top }, c);
	}
	// 테두리(좌우 · 위)
	DrawRectangleRec(Rectangle{ cx - bw * 0.5f, top, 3.f, bh - 20.f * drop }, util::MulA(edge, fade));
	DrawRectangleRec(Rectangle{ cx + bw * 0.5f - 3.f, top, 3.f, bh - 20.f * drop }, util::MulA(edge, fade));
	DrawRectangleRec(Rectangle{ cx - bw * 0.5f, top, bw, 3.f }, util::MulA(edge, fade));
	if (drop > 0.6f)
	{
		const float a = fade * util::Clamp01((drop - 0.6f) / 0.4f);
		const float pop = 1.f + 0.25f * std::max(0.f, 1.f - (t - 0.45f) / 0.35f);
		emblem::Draw(Vector2{ cx, top + 84.f }, 92.f * pop, m_guild.emblem, a, std::max(0.f, 1.f - (t - 0.4f) / 1.5f));
		ui::TextStyle st;
		st.size = ui::type::kDisplayS;
		st.face = ui::Face::Display;
		st.color = util::MulA(ui::theme::kTextHi, a);
		st.outline = 1.5f;
		st.outlineColor = util::MulA(util::A8(ui::theme::kShadow, 200), a);
		ui::Fonts().DrawCentered(m_guild.name, Vector2{ cx, top + 158.f }, st);
		st.size = ui::type::kCaption;
		st.face = ui::Face::Strong;
		st.color = util::MulA(edge, a);
		ui::Fonts().DrawCentered(m_guild.celebrateFounded ? "결사를 세웠다" : "결사에 들어왔다", Vector2{ cx, top + 184.f }, st);
	}
}
