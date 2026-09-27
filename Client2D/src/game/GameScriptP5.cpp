// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 시험 시나리오(--script) — 결사. 판정 줄은 [g3] (tools/sandbox/phase5_check.py가 읽는다)
//
//  guild3:a|b|c  세 클라: a가 회관지기 즈데노에게서 결사를 세우고 b · c를 초대한다. c는 한 번 거절했다가 다시 받는다.
//                a가 결사 채팅을 하고 b를 정예로 올린다. 이름은 --name(lv20_ga · lv20_gb · lv20_gc — 끝 글자만 다르다).
// ─────────────────────────────────────────────────────────────────────────────
#include "game/Game.h"
#include "ui/Wm.h"
#include "audio/Sfx.h"
#include "Data/GuildAuction.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
	[[nodiscard]] long long UnixMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}
}

bool Game::RunScriptPhase5(float dt)
{
	(void)dt;
	const std::string& s = m_opts.script;
	const bool guild = s.rfind("guild3:", 0) == 0;
	if (!guild)
		return false;
	auto elapsed = [this] { return m_scriptT - m_scriptMark; };
	auto next = [this](int step) { m_scriptStep = step; m_scriptMark = m_scriptT; std::printf("[p5] step %d t=%.1f\n", step, m_scriptT); };
	auto say = [this](const std::string& text) { SendChatLine(chat::Target{ chat::Ch::Guild, "" }, text); };
	if (!m_hasSelf || m_scene != Scene::Game)
		return true;

	{
		const char role = s.back();
		std::string base = m_playerName;
		if (!base.empty()) base.pop_back();
		const std::string nameB = base + "b", nameC = base + "c";
		static int invitesSeen = 0;
		static uint32_t lastInvite = 0;
		if (m_guild.inviteGuild != 0 && m_guild.inviteAt != 0.0 && lastInvite != static_cast<uint32_t>(m_guild.inviteAt * 1000.0))
		{
			lastInvite = static_cast<uint32_t>(m_guild.inviteAt * 1000.0);
			++invitesSeen;
		}
		if (role == 'a')
		{
			switch (m_scriptStep)
			{
			case 0:
				if (m_scriptT > 2.f) { SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, GuildRule::kHallNpcId }); next(1); }
				break;
			case 1:
				if ((m_dialogNpc == GuildRule::kHallNpcId && elapsed() > 1.2f) || elapsed() > 40.f)
				{
					OpenGuildCreate(false);
					m_guild.createName.text = "새벽서약";
					m_guild.shape = 3;   // 늑대
					m_guild.color = 0;   // 쪽빛
					next(2);
				}
				break;
			case 2:
				if (elapsed() > 2.5f)
				{
					SendGuildReq(GuildOp::Create, GuildRule::Emblem(m_guild.shape, m_guild.color), 0, std::string(), m_guild.createName.text);
					next(3);
				}
				break;
			case 3:
				if (m_guild.id != 0)
				{
					std::printf("[g3] created id=%u name=%s emblem=%d\n", m_guild.id, m_guild.name.c_str(), m_guild.emblem);
					CloseNpcWindows();
					next(4);
				}
				else if (elapsed() > 10.f) { std::printf("[g3] FAIL create timeout\n"); next(99); }
				break;
			case 4:
				if (elapsed() > 2.f)
				{
					OpenGuildWindow();
					SendGuildReq(GuildOp::Invite, 0, 0, nameB);
					next(5);
				}
				break;
			case 5:
				// 같은 순번을 한 번 더 보낸다(멱등 시험) — 두 번째는 Duplicate여야 한다
				if (elapsed() > 0.5f && m_p5.step == 0)
				{
					GUILD_REQ_PACKET p{};
					p.op = static_cast<uint8_t>(GuildOp::Invite);
					p.seq = m_guild.seq;   // 방금 보낸 초대와 같은 순번
					std::memcpy(p.target, nameB.data(), std::min(nameB.size(), static_cast<size_t>(NAME_SIZE - 1)));
					m_net.SendGuild(p);
					std::printf("[g3] dup-sent seq=%u\n", p.seq);
					m_p5.step = 1;
				}
				if (elapsed() > 1.f && m_scriptT - m_p5.at > 1.5f) { m_p5.at = m_scriptT; SendGuildReq(GuildOp::Info); }
				if (m_guild.members >= 2) { std::printf("[g3] b joined members=%d\n", m_guild.members); next(6); }
				else if (elapsed() > 40.f) { std::printf("[g3] FAIL b join timeout\n"); next(99); }
				break;
			case 6:
				if (elapsed() > 1.5f) { SendGuildReq(GuildOp::Invite, 0, 0, nameC); next(7); }
				break;
			case 7:
				// c는 처음에 거절한다 → 다시 초대
				if (elapsed() > 6.f) { SendGuildReq(GuildOp::Invite, 0, 0, nameC); next(8); }
				break;
			case 8:
				if (m_scriptT - m_p5.at > 1.5f) { m_p5.at = m_scriptT; SendGuildReq(GuildOp::Info); }
				if (m_guild.members >= 3) { std::printf("[g3] c joined members=%d\n", m_guild.members); next(9); }
				else if (elapsed() > 40.f) { std::printf("[g3] FAIL c join timeout\n"); next(99); }
				break;
			case 9:
				if (elapsed() > 1.5f) { say("모두 반갑소. 새벽서약의 첫 모임이오!"); next(10); }
				break;
			case 10:
				if (elapsed() > 2.5f)
				{
					SendGuildReq(GuildOp::SetRank, static_cast<uint8_t>(GuildRule::Rank::Elite), 0, nameB);
					m_guild.tab = 1;
					SendGuildReq(GuildOp::Members, 0);
					next(11);
				}
				break;
			case 11:
				if (elapsed() > 3.f)
				{
					for (const auto& r : m_guild.rows)
						if (std::string(r.name, strnlen(r.name, NAME_SIZE)) == nameB && r.rank == static_cast<uint8_t>(GuildRule::Rank::Elite)) m_p5.results |= 1;
					std::printf("[g3] b elite=%d\n", (m_p5.results & 1) ? 1 : 0);
					// 추방: 구성원 탭에서 c의 줄을 눌러 메뉴를 연 모습 → 추방
					for (size_t i = 0; i < m_guild.rows.size(); ++i)
						if (std::string(m_guild.rows[i].name, strnlen(m_guild.rows[i].name, NAME_SIZE)) == nameC) m_guild.rowMenu = static_cast<int>(i);
					next(12);
				}
				break;
			case 12:
				if (elapsed() > 2.f) { SendGuildReq(GuildOp::Kick, 0, 0, nameC); m_guild.rowMenu = -1; next(13); }
				break;
			case 13:
				if (m_scriptT - m_p5.at > 1.5f) { m_p5.at = m_scriptT; SendGuildReq(GuildOp::Info); SendGuildReq(GuildOp::Members, 0); }
				if (elapsed() > 4.f)
				{
					const bool ok = m_guild.members == 2 && (m_p5.results & 1);
					std::printf("[g3] done %s members=%d bElite=%d kicked=%d\n", ok ? "PASS" : "FAIL", m_guild.members, (m_p5.results & 1) ? 1 : 0, m_guild.members == 2 ? 1 : 0);
					next(14);
				}
				break;
			default:
				break;
			}
		}
		else
		{
			// b · c: 회관 앞마당으로 걸어가 초대를 기다린다. c는 첫 초대를 거절한다
			switch (m_scriptStep)
			{
			case 0:
				if (m_scriptT > 2.f)
				{
					SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, GuildRule::kHallNpcId });   // 회관 앞마당(a와 같은 곳)
					next(1);
				}
				break;
			case 1:
				if (m_guild.inviteGuild != 0 && m_now - m_guild.inviteAt > 1.8)
				{
					const bool accept = role == 'b' || invitesSeen >= 2;
					std::printf("[g3] invite #%d from=%s guild=%s -> %s\n", invitesSeen, m_guild.inviteFrom.c_str(), m_guild.inviteName.c_str(), accept ? "accept" : "decline");
					SendGuildReq(GuildOp::InviteReply, accept ? 1 : 0, m_guild.inviteGuild);
					m_guild.inviteGuild = 0;
					if (accept) next(2);
				}
				break;
			case 2:
				if (m_guild.id != 0 && elapsed() > 1.f)
				{
					std::printf("[g3] joined id=%u name=%s rank=%d\n", m_guild.id, m_guild.name.c_str(), m_guild.myRank);
					next(3);
				}
				break;
			case 3:
				if (elapsed() > (role == 'b' ? 5.f : 7.f))
				{
					say(role == 'b' ? "불러 줘서 고맙소!" : "늦었지만 함께하겠소.");
					next(4);
				}
				break;
			case 4:
				if (elapsed() > 6.f)
				{
					std::printf("[g3] %c done id=%u rank=%d tagsSeen=%zu\n", role, m_guild.id, m_guild.myRank, m_guildTags.size());
					next(5);
				}
				break;
			case 5:
				// c는 추방된다 — 결사 정보가 비고 머리 위 표시가 사라진다
				if (role == 'c' && m_guild.known && m_guild.id == 0)
				{
					std::printf("[g3] c kicked id=0\n");
					next(6);
				}
				break;
			default:
				break;
			}
		}
		return true;
	}

	return true;
}
