// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 시험 시나리오(--script) — 결사 · 경매장. 판정 줄은 [g3] · [auc] (tools/sandbox/phase5_check.py가 읽는다)
//
//  guild3:a|b|c  세 클라: a가 회관지기 즈데노에게서 결사를 세우고 b · c를 초대한다. c는 한 번 거절했다가 다시 받는다.
//                a가 결사 채팅을 하고 b를 정예로 올린다. 이름은 --name(lv20_ga · lv20_gb · lv20_gc — 끝 글자만 다르다).
//  auction:sell  (lv20_ak… 시험 꾸러미) 로렌초에게 가서 철검 +2 · 은 목걸이 · 약초 물약 10개를 올리고, 팔리면 정산함을 모두 받는다.
//  auction:buy   로렌초에게 가서 무기 분류를 찾아 철검을 사고, 정산함에서 받는다.
//  auction:race  두 클라가 같은 목록을 같은 순간(HARVEN_RACE_AT 유닉스 ms)에 산다 — 한 명만 산다.
//  auction:dup   같은 순번의 구매 요청을 두 번 보낸다 — 두 번째는 Duplicate.
//  auction:sellone · auction:buyone  장애 시험용(하나만 올린다 · 하나만 산다 — 서버를 죽이는 쪽은 스크립트 밖)
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
	const bool auction = s.rfind("auction:", 0) == 0;
	if (!guild && !auction)
		return false;
	auto elapsed = [this] { return m_scriptT - m_scriptMark; };
	auto next = [this](int step) { m_scriptStep = step; m_scriptMark = m_scriptT; std::printf("[p5] step %d t=%.1f\n", step, m_scriptT); };
	auto say = [this](const std::string& text) { SendChatLine(chat::Target{ chat::Ch::Guild, "" }, text); };
	if (!m_hasSelf || m_scene != Scene::Game)
		return true;

	if (guild)
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
					CloseNpcWindows();   // 대화창의 "결사 세우기"와 같은 길
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
				// 초대는 창 없이(/초대와 같은 요청) — 결사 창은 구성원을 다룰 때(10단계) 연다. 그 사이 화면에는 b · c 머리 위에 문장이 붙는 것이 보인다
				if (elapsed() > 2.f)
				{
					SendGuildReq(GuildOp::Invite, 0, 0, nameB);
					m_p5.seqA = m_guild.seq;   // 이 초대의 순번 — 사이에 다른 요청(깃발 뜰 순위 등)이 끼어도 같은 순번으로 되풀이하게
					next(5);
				}
				break;
			case 5:
				// 같은 순번을 한 번 더 보낸다(멱등 시험) — 두 번째는 Duplicate여야 한다
				if (elapsed() > 0.5f && m_p5.step == 0)
				{
					GUILD_REQ_PACKET p{};
					p.op = static_cast<uint8_t>(GuildOp::Invite);
					p.seq = m_p5.seqA;     // 방금 보낸 초대와 같은 순번
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
					m_guild.tab = 1;
					OpenGuildWindow();
					SendGuildReq(GuildOp::SetRank, static_cast<uint8_t>(GuildRule::Rank::Elite), 0, nameB);
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
			case 14:
				// 창을 닫고 마당을 본다 — a · b 머리 위에는 문장과 ‹새벽서약›. 추방된 c는 따로 「서리매」를 세운다
				if (elapsed() > 2.5f) { m_guildOpen = false; next(15); }
				break;
			case 15:
				// 결사 회관 깃발 뜰(자기 검토): 인원 많은 순 — 새벽서약(2) · 서리매(1)가 서 · 동 깃대에
				if (m_hallTop.size() >= 2 || elapsed() > 20.f)
				{
					std::printf("[g3] flags n=%zu first=%s second=%s\n", m_hallTop.size(), m_hallTop.empty() ? "-" : m_hallTop[0].name.c_str(),
						m_hallTop.size() > 1 ? m_hallTop[1].name.c_str() : "-");
					// 녹화: 카메라를 조금 물려 깃발 뜰 전체(깃대 둘 · 문장 천 · 이름)가 화면에 들어오게
					m_cam.SetZoomFree(0.8f);
					next(16);
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
			case 6:
				// c: 쫓겨난 김에 제 결사를 세운다 — 먼저 금칙어 이름(서버가 NameInvalid로 거른다), 다음에 「서리매」(매 · 청록)
				if (elapsed() > 1.5f)
				{
					CloseNpcWindows();
					OpenGuildCreate(false);
					m_guild.createName.text = "서리매";
					m_guild.shape = 4;   // 매
					m_guild.color = 5;   // 청록
					SendGuildReq(GuildOp::Create, GuildRule::Emblem(4, 5), 0, std::string(), "운영자모임");
					next(7);
				}
				break;
			case 7:
				if (elapsed() > 2.f)
				{
					SendGuildReq(GuildOp::Create, GuildRule::Emblem(m_guild.shape, m_guild.color), 0, std::string(), m_guild.createName.text);
					next(8);
				}
				break;
			case 8:
				if (m_guild.id != 0)
				{
					std::printf("[g3] c founded id=%u name=%s emblem=%d\n", m_guild.id, m_guild.name.c_str(), m_guild.emblem);
					next(9);
				}
				else if (elapsed() > 12.f) { std::printf("[g3] FAIL c found timeout\n"); next(99); }
				break;
			default:
				break;
			}
		}
		return true;
	}

	// ── 경매장 ──
	const std::string mode = s.substr(8);
	auto reqRegister = [&](int slot, int count, uint32_t price, int duration)
	{
		const InvSlot& sl = m_inv.slots[static_cast<size_t>(slot)];
		m_auction.regSlot = slot;
		m_auction.regCount = count;
		m_auction.regPrice.text = std::to_string(price);
		m_auction.regDuration = duration;
		AUCTION_REQ_PACKET q{};
		q.op = static_cast<uint8_t>(AuctionOp::Register);
		q.slot = static_cast<uint16_t>(slot);
		q.count = static_cast<uint16_t>(count);
		q.price = price;
		q.duration = static_cast<uint8_t>(duration);
		q.itemId = sl.itemId;
		SendAuctionReq(q);
	};
	auto findRow = [&](uint16_t itemId) -> const AuctionRow*
	{
		for (const auto& r : m_auction.rows) if (r.itemId == itemId && (r.flags & 3) == 0) return &r;
		return nullptr;
	};
	const bool acked = m_auction.pendingSeq == 0;

	if (mode == "sell" || mode == "sellone")
	{
		switch (m_scriptStep)
		{
		case 0:
			if (m_scriptT > 2.f) { SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, AuctionRule::kAuctioneerNpcId }); next(1); }
			break;
		case 1:
			if ((m_dialogNpc == AuctionRule::kAuctioneerNpcId && elapsed() > 1.2f) || elapsed() > 40.f)
			{
				CloseNpcWindows();
				OpenAuction(1);
				std::printf("[auc] seller adena0=%u\n", m_inv.adena);
				next(2);
			}
			break;
		case 2:
			if (elapsed() > 1.5f)
			{
				// 철검 +2(희귀) — 시세를 먼저 묻는 모습(힌트 줄)
				m_auction.regSlot = 0;
				m_auction.regCount = 1;
				m_auction.hintItem = m_inv.slots[0].itemId;
				AUCTION_REQ_PACKET q{};
				q.op = static_cast<uint8_t>(AuctionOp::PriceHint);
				q.itemId = m_inv.slots[0].itemId;
				SendAuctionReq(q);
				m_auction.regPrice.text = "12000";
				next(3);
			}
			break;
		case 3:
			if (elapsed() > 2.f) { reqRegister(0, 1, 12000, 1); next(4); }
			break;
		case 4:
			if (acked && elapsed() > 1.f)
			{
				if (mode == "sellone") { std::printf("[auc] sellone registered result=%d adena=%u\n", m_auction.lastResult, m_inv.adena); next(20); break; }
				reqRegister(2, 1, 30000, 2);
				next(5);
			}
			break;
		case 5:
			if (acked && elapsed() > 1.f) { reqRegister(4, 10, 600, 0); next(6); }
			break;
		case 6:
			if (acked && elapsed() > 1.f) { OpenAuction(2); std::printf("[auc] seller listed adena=%u\n", m_inv.adena); next(7); }
			break;
		case 7:
			// 팔릴 때까지(구매자 스크립트) — 알림이 오면 정산함으로. 빠른 시간(시험 배율)이면 물약(12시간)이 만료돼 돌아올 때까지
			if (m_auction.settleCount >= (std::getenv("HARVEN_AUCTION_HOUR_SEC") ? 2 : 1) && elapsed() > 2.f)
			{
				std::printf("[auc] seller sold settle=%u\n", m_auction.settleCount);
				OpenAuction(3);
				next(8);
			}
			else if (elapsed() > 120.f) { std::printf("[auc] FAIL seller no sale\n"); next(99); }
			break;
		case 8:
			if (elapsed() > 2.f && !m_auction.settles.empty())
			{
				m_auction.claimAll = true;
				AUCTION_REQ_PACKET q{};
				q.op = static_cast<uint8_t>(AuctionOp::Claim);
				q.id = m_auction.settles.front().id;
				SendAuctionReq(q);
				next(9);
			}
			break;
		case 9:
			if (m_auction.settleCount == 0 && !m_auction.claimAll && elapsed() > 1.f)
			{
				std::printf("[auc] seller done adena=%u\n", m_inv.adena);
				next(10);
			}
			break;
		default:
			break;
		}
		return true;
	}

	if (mode == "buy" || mode == "buyone" || mode == "dup" || mode == "race")
	{
		switch (m_scriptStep)
		{
		case 0:
			if (m_scriptT > 2.f) { SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, AuctionRule::kAuctioneerNpcId }); next(1); }
			break;
		case 1:
			if ((m_dialogNpc == AuctionRule::kAuctioneerNpcId && elapsed() > 1.2f) || elapsed() > 40.f)
			{
				CloseNpcWindows();
				m_auction.cat = static_cast<uint8_t>(AuctionRule::Cat::Weapon);
				m_auction.sub = 0;
				OpenAuction(0);
				std::printf("[auc] buyer adena0=%u\n", m_inv.adena);
				m_p5.seqA = m_inv.adena;
				next(2);
			}
			break;
		case 2:
		{
			// 찾을 때까지 2초마다 다시 찾는다(판매자가 올리는 중일 수 있다)
			const uint16_t want = 3;   // 철검(시험 꾸러미 0번 칸)
			if (const AuctionRow* r = findRow(want); r && elapsed() > 1.f)
			{
				m_p5.target = r->id;
				m_auction.confirmId = r->id;       // 확인 창을 띄워 보여 준다
				m_auction.confirmRow = *r;
				std::printf("[auc] found listing=%u price=%u\n", r->id, r->price);
				next(3);
			}
			else if (elapsed() > 2.f && m_scriptT - m_p5.at > 2.f) { m_p5.at = m_scriptT; AuctionSearch(0); }
			if (elapsed() > 120.f) { std::printf("[auc] FAIL buyer nothing found\n"); next(99); }
			break;
		}
		case 3:
		{
			const uint32_t price = m_auction.confirmRow.price;
			if (mode == "race")
			{
				// 같은 순간: HARVEN_RACE_AT(유닉스 ms)
				const char* at = std::getenv("HARVEN_RACE_AT");
				const long long t = at ? std::atoll(at) : 0;
				if (t == 0 || UnixMs() >= t)
				{
					AUCTION_REQ_PACKET q{};
					q.op = static_cast<uint8_t>(AuctionOp::Buy);
					q.id = m_p5.target;
					q.price = price;
					SendAuctionReq(q);
					m_auction.confirmId = 0;
					std::printf("[auc] race sent at=%lld listing=%u\n", UnixMs(), m_p5.target);
					next(4);
				}
			}
			else if (elapsed() > 2.f)
			{
				AUCTION_REQ_PACKET q{};
				q.op = static_cast<uint8_t>(AuctionOp::Buy);
				q.id = m_p5.target;
				q.price = price;
				q.seq = ++m_auction.seq;
				m_net.SendAuction(q);
				m_auction.pendingSeq = q.seq;
				m_auction.pendingAt = m_now;
				std::printf("[auction] req op=2 seq=%u id=%u price=%u\n", q.seq, q.id, q.price);
				if (mode == "dup") { m_net.SendAuction(q); std::printf("[auc] dup-sent seq=%u\n", q.seq); }
				m_auction.confirmId = 0;
				next(4);
			}
			break;
		}
		case 4:
			if (acked && elapsed() > 1.5f)
			{
				std::printf("[auc] buy result=%d adena=%u (before %u)\n", m_auction.lastResult, m_inv.adena, m_p5.seqA);
				if (mode == "buyone") { next(20); break; }
				if (m_auction.lastResult == static_cast<int>(AuctionResult::Ok)) { OpenAuction(3); next(5); }
				else { std::printf("[auc] %s done lost adena=%u before=%u unchanged=%d\n", mode.c_str(), m_inv.adena, m_p5.seqA, m_inv.adena == m_p5.seqA ? 1 : 0); next(20); }
			}
			else if (elapsed() > 30.f) { std::printf("[auc] FAIL buy no ack\n"); next(99); }
			break;
		case 5:
			if (elapsed() > 2.f && !m_auction.settles.empty())
			{
				AUCTION_REQ_PACKET q{};
				q.op = static_cast<uint8_t>(AuctionOp::Claim);
				q.id = m_auction.settles.front().id;
				SendAuctionReq(q);
				next(6);
			}
			else if (elapsed() > 15.f) { std::printf("[auc] FAIL no settlement\n"); next(99); }
			break;
		case 6:
			if (acked && elapsed() > 1.5f)
			{
				const uint16_t want = 3;
				int have = 0;
				for (const auto& sl : m_inv.slots) if (sl.itemId == want) have += sl.count;
				std::printf("[auc] %s done won item=%u have=%d adena=%u settle=%u\n", mode.c_str(), want, have, m_inv.adena, m_auction.settleCount);
				m_invOpen = true;
				next(7);
			}
			break;
		default:
			break;
		}
		return true;
	}
	return true;
}
