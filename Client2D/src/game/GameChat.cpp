#include "game/Game.h"
#include "Data/GuildAuction.h"
#include "game/Texts.h"
#include "game/KeyBinds.h"
#include "ui/Wm.h"
#include "ui/Font.h"
#include "render/Icons.h"
#include "audio/Sfx.h"
#include "core/Util.h"
#include "platform/Ime.h"
#include "Data/ItemRules.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// 상시 채팅창(Phase 3-B) — 창 관리자의 "chat" 창(늘 떠 있다 · 옮기기 · 크기 · 잠금)
//
//   [모두][일반][결사][귓속말][거래][시스템][+]              [⚙]   ← 탭 줄(끌어서 창 옮기기 · 우클릭 탭 설정)
//   13:05 [전체] 아린: 경매장 언제 열려요?                         ← 줄: 시각(선택) · 채널 · 이름(누르면 메뉴) · 글([아이템] 고리)
//   …                                   [▼ 새 메시지 3]            ← 올려 보는 중에 온 줄
//   [일반] 메시지 입력…                               42          ← Enter로 열기 · 남은 바이트
//
//   · 배경 투명도: 평소 35% · 마우스를 올리거나 입력 중 85%(설정) — 글자는 늘 보인다(외곽선).
//   · 줄 높이는 (줄 번호 → 높이) 캐시 — 폭 · 글자 크기가 바뀔 때만 다시 잰다(500줄을 올려 봐도 매 프레임 잴 필요가 없다).
// ─────────────────────────────────────────────────────────────────────────────

using util::Rgb;

namespace
{
	std::string Fmt(const char* f, ...)
	{
		char buf[256];
		va_list ap;
		va_start(ap, f);
		std::vsnprintf(buf, sizeof(buf), f, ap);
		va_end(ap);
		return buf;
	}

	enum PartKind { kText, kName, kItem, kClock };
	struct Run
	{
		std::string text;
		Color color{};
		ui::Face face = ui::Face::Body;
		int kind = kText;
		uint16_t itemId = 0;
		uint8_t enchant = 0, rarity = 0;
	};
	struct Placed
	{
		float x = 0.f, w = 0.f;
		std::string text;
		Color color{};
		ui::Face face = ui::Face::Body;
		int kind = kText;
		uint16_t itemId = 0;
		uint8_t enchant = 0, rarity = 0;
	};
	using Row = std::vector<Placed>;

	float LineH(float fs) { return std::round(fs + 5.f); }

	std::string Clock()
	{
		const std::time_t t = std::time(nullptr);
		std::tm tmv{};
#if defined(_WIN32)
		localtime_s(&tmv, &t);
#else
		localtime_r(&t, &tmv);
#endif
		char buf[8];
		std::snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
		return buf;
	}

	std::vector<Run> BuildRuns(const chat::Msg& m, bool clock)
	{
		std::vector<Run> runs;
		const Color cc = m.color.a > 0 ? m.color : chat::ChColor(m.ch);
		if (clock && !m.clock.empty())
			runs.push_back(Run{ m.clock + " ", ui::theme::kTextMute, ui::Face::Number, kClock });
		if (const char* pre = chat::ChPrefix(m.ch); pre && *pre)
			runs.push_back(Run{ std::string(pre) + " ", cc, ui::Face::Strong, kText });
		if (m.ch == chat::Ch::WhisperIn || m.ch == chat::Ch::WhisperOut)
		{
			runs.push_back(Run{ m.ch == chat::Ch::WhisperIn ? "[귓속말] " : "[귓속말] → ", cc, ui::Face::Strong, kText });
		}
		if (!m.sender.empty())
		{
			runs.push_back(Run{ m.sender, util::Mix(cc, WHITE, 0.22f), ui::Face::Strong, kName });
			runs.push_back(Run{ ": ", cc, ui::Face::Body, kText });
		}
		for (const auto& pc : chat::SplitPieces(m.text))
		{
			if (!pc.item) { runs.push_back(Run{ pc.text, cc, ui::Face::Body, kText }); continue; }
			Run r;
			r.text = "[" + texts::ItemNameEnchanted(pc.itemId, pc.enchant) + "]";
			const int rr = std::min<int>(pc.rarity, static_cast<int>(Rarity::Count) - 1);
			r.color = rr > 0 ? icons::RarityColor(static_cast<Rarity>(rr)) : ui::theme::kTextHi;
			r.face = ui::Face::Strong;
			r.kind = kItem;
			r.itemId = pc.itemId; r.enchant = pc.enchant; r.rarity = pc.rarity;
			runs.push_back(std::move(r));
		}
		return runs;
	}

	// 조각들을 폭 maxW 안의 줄들로. 이름 · 아이템은 한 덩어리(줄 끝에서 넘치면 다음 줄로), 글은 공백 · 글자에서 끊는다
	void LayoutRuns(const std::vector<Run>& runs, float fs, float maxW, std::vector<Row>& rows)
	{
		rows.assign(1, Row{});
		float x = 0.f;
		auto place = [&](const Run& r, const std::string& text, float w)
		{
			Placed p;
			p.x = x; p.w = w; p.text = text; p.color = r.color; p.face = r.face; p.kind = r.kind;
			p.itemId = r.itemId; p.enchant = r.enchant; p.rarity = r.rarity;
			rows.back().push_back(std::move(p));
			x += w;
		};
		for (const Run& r : runs)
		{
			const float w = ui::Fonts().Measure(r.text, fs, 0.f, r.face).x;
			if (r.kind != kText)
			{
				if (x > 0.f && x + w > maxW) { rows.emplace_back(); x = 0.f; }
				place(r, r.text, w);
				continue;
			}
			if (x + w <= maxW) { place(r, r.text, w); continue; }
			// 글: 한 글자씩 넣다가 넘치면 마지막 공백(없으면 그 글자 앞)에서 끊는다
			std::vector<int> cps;
			ui::DecodeUtf8(r.text, cps);
			std::string cur;
			size_t lastSpace = std::string::npos;
			for (size_t i = 0; i < cps.size(); ++i)
			{
				std::string cand = cur;
				ui::AppendUtf8(cand, cps[i]);
				const float cw = ui::Fonts().Measure(cand, fs, 0.f, r.face).x;
				if (x + cw <= maxW || (cur.empty() && x == 0.f))
				{
					if (cps[i] == ' ') lastSpace = cur.size();
					cur.swap(cand);
					continue;
				}
				// 넘쳤다
				std::string carry;
				if (lastSpace != std::string::npos && lastSpace > 0)
				{
					carry = cur.substr(lastSpace + 1);
					cur.erase(lastSpace);
				}
				if (!cur.empty()) place(r, cur, ui::Fonts().Measure(cur, fs, 0.f, r.face).x);
				rows.emplace_back();
				x = 0.f;
				cur = carry;
				lastSpace = std::string::npos;
				if (cps[i] != ' ' || !cur.empty()) ui::AppendUtf8(cur, cps[i]);
			}
			if (!cur.empty()) place(r, cur, ui::Fonts().Measure(cur, fs, 0.f, r.face).x);
		}
	}

	// 줄 높이 캐시(폭 · 글자 크기 · 시각 표시가 같을 때만 유효)
	struct HeightCache
	{
		float w = -1.f, fs = -1.f;
		bool clock = false;
		std::unordered_map<uint32_t, float> h;
	};
	HeightCache g_hc;

	float MsgHeight(const chat::Msg& m, float fs, float maxW, bool clock)
	{
		if (g_hc.w != maxW || g_hc.fs != fs || g_hc.clock != clock || g_hc.h.size() > 6000)
		{
			g_hc.w = maxW; g_hc.fs = fs; g_hc.clock = clock;
			g_hc.h.clear();
		}
		if (auto it = g_hc.h.find(m.serial); it != g_hc.h.end()) return it->second;
		std::vector<Row> rows;
		LayoutRuns(BuildRuns(m, clock), fs, maxW, rows);
		const float h = static_cast<float>(rows.size()) * LineH(fs);
		g_hc.h[m.serial] = h;
		return h;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 줄 넣기 · 받기 · 보내기
// ─────────────────────────────────────────────────────────────────────────────
bool Game::ChatBlocked(const std::string& name) const
{
	if (name.empty()) return false;
	for (const auto& n : m_chatSet.blocked)
		if (n.size() == name.size() && std::equal(n.begin(), n.end(), name.begin(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }))
			return true;
	return false;
}

// "/귓 " 뒤 Tab 이름 완성 후보: 최근 귓속말 상대 → 채팅에서 최근에 말한 사람 → 주변 사람(가까운 순). 나 · 차단은 뺀다
std::vector<std::string> Game::WhisperCandidates(const std::string& partial) const
{
	std::vector<std::string> out;
	auto add = [&](const std::string& n)
	{
		if (n.empty() || n == m_playerName || ChatBlocked(n) || !chat::NamePrefix(n, partial)) return;
		if (std::find(out.begin(), out.end(), n) != out.end()) return;
		out.push_back(n);
	};
	add(m_lastWhisperFrom);
	add(m_lastWhisperTo);
	// 줄이 가장 많이 모이는 탭(기본 "모두")의 새 줄부터 200줄
	const chat::Tab* widest = nullptr;
	for (const auto& t : m_chatTabs)
		if (!widest || std::popcount(t.mask) > std::popcount(widest->mask)) widest = &t;
	if (widest)
	{
		int seen = 0;
		for (auto it = widest->lines.rbegin(); it != widest->lines.rend() && seen < 200; ++it, ++seen)
			if ((*it)->ch != chat::Ch::System && (*it)->ch != chat::Ch::Notice && !(*it)->history)
				add((*it)->sender);
	}
	std::vector<std::pair<float, const std::string*>> near;
	const Entity* me = nullptr;
	if (m_hasSelf)
		if (auto it = m_ents.find(m_selfId); it != m_ents.end()) me = &it->second;
	for (const auto& [id, e] : m_ents)
		if (e.kind == EntityKind::Player && !(m_hasSelf && id == m_selfId) && !e.name.empty())
			near.emplace_back(me ? std::fabs(e.pos.x - me->pos.x) + std::fabs(e.pos.y - me->pos.y) : 0.f, &e.name);
	std::sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	for (const auto& [d, n] : near) { add(*n); if (out.size() >= 12) break; }
	return out;
}

// Tab 한 번: 입력칸이 "/귓 친부분"이면 첫 후보로 채우고, 채운 그대로면 다음 후보로. 채웠으면 true
bool Game::CompleteWhisperName()
{
	std::string cmd, part;
	if (!chat::WhisperNamePart(m_chatField.text, cmd, part)) return false;
	if (m_chatField.text != m_whisperCandText || m_whisperCands.empty())
	{
		m_whisperCands = WhisperCandidates(part);
		m_whisperCandIdx = 0;
	}
	else
		m_whisperCandIdx = (m_whisperCandIdx + 1) % m_whisperCands.size();
	if (m_whisperCands.empty())
	{
		sfx::Play(sfx::Id::Error, 0.25f);
		return false;
	}
	m_chatField.text = cmd + " " + m_whisperCands[m_whisperCandIdx];
	m_whisperCandText = m_chatField.text;
	sfx::Play(sfx::Id::Hover, 0.3f);
	return true;
}

std::string Game::ChatDisplayText(const std::string& text) const
{
	std::string out;
	for (const auto& pc : chat::SplitPieces(text))
		out += pc.item ? "[" + texts::ItemNameEnchanted(pc.itemId, pc.enchant) + "]" : pc.text;
	return out;
}

void Game::AddChatMsg(chat::Ch ch, const std::string& sender, uint64_t senderKey, const std::string& text, Color color, bool history)
{
	if (m_chatTabs.empty()) chat::DefaultTabs(m_chatTabs);
	auto msg = std::make_shared<chat::Msg>();
	msg->serial = ++m_chatSerial;
	msg->ch = ch;
	msg->sender = sender;
	msg->senderKey = senderKey;
	msg->text = text;
	msg->color = color;
	msg->time = m_now;
	msg->clock = Clock();
	msg->history = history;
	const uint32_t bit = chat::Bit(ch);
	for (int i = 0; i < static_cast<int>(m_chatTabs.size()); ++i)
	{
		chat::Tab& t = m_chatTabs[static_cast<size_t>(i)];
		if (!(t.mask & bit)) continue;
		t.lines.push_back(msg);
		while (t.lines.size() > chat::kTabLines) t.lines.pop_front();
		if (i != m_chatTab && !history) { ++t.unread; t.lastAt = m_now; }
		if (i == m_chatTab && t.scroll > 0.5f) ++t.newBelow;
	}
	// 시험(--script chat3:*): 받은 줄을 찍는다(tools/sandbox/chat3.sh가 모아 판정)
	if (m_opts.script.rfind("chat3", 0) == 0)
		std::printf("[%s] t=%.2f ch=%s%s from=%s text=%s\n", history ? "chat-hist" : "chat", m_scriptT, chat::ChName(ch), ch == chat::Ch::WhisperOut ? "(보냄)" : "", sender.c_str(), text.c_str());
	// 예전 기록(스크립트 · 도구 호환): 글만
	m_chat.push_back({ (sender.empty() ? "" : sender + ": ") + ChatDisplayText(text), color.a ? color : chat::ChColor(ch), m_now });
	while (m_chat.size() > 80) m_chat.pop_front();
}

void Game::HandleChat(const ObjID& sender, const std::string& senderName, const std::string& text, uint8_t channelRaw)
{
	const bool history = (channelRaw & kChatHistoryBit) != 0;
	const auto channel = static_cast<ChatChannel>(channelRaw & 0x7F);
	chat::Ch ch = chat::Ch::Normal;
	switch (channel)
	{
	case ChatChannel::World: ch = chat::Ch::World; break;
	case ChatChannel::Trade: ch = chat::Ch::Trade; break;
	case ChatChannel::Guild: ch = chat::Ch::Guild; break;
	case ChatChannel::WhisperIn: ch = chat::Ch::WhisperIn; break;
	case ChatChannel::WhisperOut: ch = chat::Ch::WhisperOut; break;
	case ChatChannel::System: ch = chat::Ch::System; break;
	default: ch = chat::Ch::Normal; break;
	}
	if (ch != chat::Ch::WhisperOut && ch != chat::Ch::System && sender != m_selfId && ChatBlocked(senderName))
		return;   // 차단(클라만)
	AddChatMsg(ch, senderName, sender.GetDatabaseID(), text, BLANK, history);
	if (history)
		return;
	if (ch == chat::Ch::WhisperIn)
	{
		m_lastWhisperFrom = senderName;
		sfx::Play(sfx::Id::Notice, 0.35f);
		return;
	}
	if (ch == chat::Ch::WhisperOut)
		return;
	if (ch == chat::Ch::Normal)
	{
		// 머리 위 말풍선은 일반 대화만(보이는 사람)
		if (Entity* e = Find(sender))
		{
			e->chat = ChatDisplayText(text);
			e->chatT = 5.f;
		}
	}
	sfx::Play(sfx::Id::Chat, ch == chat::Ch::Normal ? 0.4f : 0.3f);
}

void Game::OpenChatInput(const std::string& prefill)
{
	m_chatOpen = true;
	m_chatOpenedAt = m_now;
	m_chatField.maxBytes = 240;   // 표시는 [아이템 이름]이 길다 — 보낼 때 표식으로 바꾼 길이(127바이트)로 검사
	if (!prefill.empty()) m_chatField.text = prefill;
	m_chatHistIdx = -1;
	ui::Focus(&m_chatField);
}

void Game::ChatInsertItemLink(uint16_t itemId, uint8_t enchant, uint8_t rarity)
{
	const std::string shown = "[" + texts::ItemNameEnchanted(itemId, enchant) + "]";
	if (!m_chatOpen || ui::Focused() != &m_chatField) OpenChatInput();
	if (!m_chatField.text.empty() && m_chatField.text.back() != ' ') m_chatField.text += ' ';
	m_chatField.text += shown;
	m_chatLinks.push_back(ChatLinkRef{ shown, chat::ItemToken(itemId, enchant, rarity) });
	sfx::Play(sfx::Id::Hover, 0.4f);
}

namespace
{
	// 입력칸 글 → 보낼 글: [아이템 이름]을 표식으로(입력칸에 남아 있는 것만)
	std::string ConvertLinks(const std::string& text, const std::vector<std::pair<std::string, std::string>>& links)
	{
		std::string out = text;
		for (const auto& [shown, token] : links)
			if (const size_t at = out.find(shown); at != std::string::npos)
				out.replace(at, shown.size(), token);
		return out;
	}
}

void Game::SendChatLine(const chat::Target& t, const std::string& body)
{
	if (body.empty()) return;
	if (!chat::ValidUtf8(body))
	{
		AddChatMsg(chat::Ch::System, "", 0, "보낼 수 없는 글자가 들어 있습니다.", ui::theme::kChatSystem);
		return;
	}
	if (body.size() > chat::kMaxBytes)
	{
		AddChatMsg(chat::Ch::System, "", 0, Fmt("한 번에 %d바이트(한글 약 42자)까지 보낼 수 있습니다.", static_cast<int>(chat::kMaxBytes)), ui::theme::kChatSystem);
		sfx::Play(sfx::Id::Error, 0.35f);
		return;
	}
	uint8_t ch = static_cast<uint8_t>(ChatChannel::Normal);
	switch (t.ch)
	{
	case chat::Ch::World: ch = static_cast<uint8_t>(ChatChannel::World); break;
	case chat::Ch::Trade: ch = static_cast<uint8_t>(ChatChannel::Trade); break;
	case chat::Ch::Guild: ch = static_cast<uint8_t>(ChatChannel::Guild); break;
	case chat::Ch::WhisperOut: ch = static_cast<uint8_t>(ChatChannel::WhisperOut); break;
	default: break;
	}
	if (t.ch == chat::Ch::WhisperOut)
	{
		if (t.whisperTo.empty()) return;
		m_lastWhisperTo = t.whisperTo;
	}
	m_net.SendChatEx(ch, t.whisperTo, body);
}

void Game::SubmitChat()
{
	std::vector<std::pair<std::string, std::string>> links;
	for (const auto& l : m_chatLinks) links.emplace_back(l.shown, l.token);
	const std::string raw = m_chatField.text;
	m_chatField.text.clear();
	m_chatLinks.clear();
	if (raw.empty()) return;
	if (m_chatHistory.empty() || m_chatHistory.back() != raw)
	{
		m_chatHistory.push_back(raw);
		if (m_chatHistory.size() > 30) m_chatHistory.erase(m_chatHistory.begin());
	}
	const chat::Parsed p = chat::ParseInput(ConvertLinks(raw, links), m_chatTarget, m_lastWhisperFrom);
	switch (p.kind)
	{
	case chat::Parsed::Kind::Empty:
		break;
	case chat::Parsed::Kind::Switch:
		m_chatTarget = p.target;
		sfx::Play(sfx::Id::Hover, 0.4f);
		break;
	case chat::Parsed::Kind::Send:
		// 귓속말 · 채널 명령으로 보낸 뒤에도 그 채널에 머문다(다음 줄도 같은 곳으로)
		if (p.target.ch != m_chatTarget.ch || p.target.whisperTo != m_chatTarget.whisperTo) m_chatTarget = p.target;
		SendChatLine(p.target, p.body);
		break;
	case chat::Parsed::Kind::Invite:
		if (p.arg.empty())
			AddChatMsg(chat::Ch::System, "", 0, "쓰는 법: /초대 이름", ui::theme::kChatSystem);
		else if (m_guild.id == 0)
			AddChatMsg(chat::Ch::System, "", 0, "결사에 들어 있지 않습니다. 결사 회관의 회관지기 즈데노에게서 세울 수 있습니다.", ui::theme::kChatSystem);
		else
			SendGuildReq(GuildOp::Invite, 0, 0, p.arg);   // 결과는 GUILD_ACK(보냄 · 없음 · 이미 결사 …)
		break;
	case chat::Parsed::Kind::Unknown:
		if (p.arg == "/r" || p.arg == "/답" || p.arg == "/ㄹ")
			AddChatMsg(chat::Ch::System, "", 0, "답할 귓속말이 없습니다.", ui::theme::kChatSystem);
		else if (p.arg == "/귓" || p.arg == "/귓속말" || p.arg == "/w" || p.arg == "/ㄱ")
			AddChatMsg(chat::Ch::System, "", 0, "쓰는 법: /귓 이름 내용", ui::theme::kChatSystem);
		else
			AddChatMsg(chat::Ch::System, "", 0, "모르는 명령입니다: " + p.arg + "  (/귓 · /r · /일반 · /전 · /거래 · /길)", ui::theme::kChatSystem);
		sfx::Play(sfx::Id::Error, 0.3f);
		break;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 창
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawChat()
{
	if (m_chatTabs.empty()) chat::DefaultTabs(m_chatTabs);
	m_chatTab = std::clamp(m_chatTab, 0, static_cast<int>(m_chatTabs.size()) - 1);
	const float W = m_uiW, H = m_uiH;
	const float fs = chat::FontSize(m_chatSet.font);
	const float lh = LineH(fs);
	// 기본 자리: 왼쪽 아래 420×220. 전투 판 왼쪽에 자리가 모자라면(1280×720 등) 전투 판 위 왼쪽으로
	Rectangle def{ 12.f, std::round(H - 20.f - 220.f), 420.f, 220.f };
	if (HudLeft() - 12.f - 12.f < 380.f)
		def.y = std::round(std::max(110.f, HudTop() - 8.f - def.height));
	else
		def.width = std::min(420.f, std::round(HudLeft() - 24.f));
	const Rectangle r = wm::Window("chat", def, wm::kMove | wm::kResize | wm::kLockable | wm::kPinned | wm::kNoAnim,
		Vector2{ 280.f, 120.f }, Vector2{ std::max(280.f, W * 0.6f), std::max(120.f, H * 0.6f) });
	const bool typing = m_chatOpen && ui::Focused() == &m_chatField;
	const bool hover = ui::Hovered(r);
	const bool locked = wm::Locked("chat");

	// 배경: 평소 · 활성 투명도 사이를 부드럽게
	const bool engaged = hover || typing || m_chatMenu != 0;
	const float wantA = engaged ? m_chatSet.activeAlpha : m_chatSet.idleAlpha;
	m_chatBgA += (wantA - m_chatBgA) * std::min(1.f, GetFrameTime() * 10.f);
	// 조용하면 글자도 흐리게(Phase 3 자기 검토): 보는 탭에 새 줄 · 가리킴 · 입력이 20초 없으면 천천히(≈1.2초) 28%까지,
	// 새 줄이 오거나 마우스를 올리면 바로 돌아온다 — 싸우는 동안 채팅이 월드를 덜 가린다(설정 "조용하면 흐리게")
	if (engaged) m_chatActiveAt = m_now;
	{
		const auto& cur = m_chatTabs[static_cast<size_t>(m_chatTab)];
		const double lastLine = cur.lines.empty() ? -1e9 : cur.lines.back()->time;
		const bool quiet = m_chatSet.fadeText && !m_chatOpen && m_now - std::max(m_chatActiveAt, lastLine) > chat::kQuietSec;
		const float wantT = quiet ? chat::kQuietTextA : 1.f;
		m_chatTextA += (wantT - m_chatTextA) * std::min(1.f, GetFrameTime() * (quiet ? 1.6f : 14.f));
	}
	const float fade = 0.4f + 0.6f * m_chatTextA;   // 판 · 탭 줄은 글자만큼 흐리지 않는다
	ui::Plate(r, util::WithA(ui::theme::kInk, 0.92f * m_chatBgA * fade), util::WithA(ui::theme::kLineStrong, (0.35f + 0.65f * m_chatBgA) * fade), 4.f, 1.f);
	ui::Block(r);

	// ── 탭 줄 ──
	const Rectangle strip{ r.x + 1.f, r.y + 1.f, r.width - 2.f, 24.f };
	// 탭 줄은 평소에도 짙게(≥0.7) — 뒤로 비치는 이름표 · 불빛 위에서도 탭 이름이 읽혀야 한다
	DrawRectangleRec(Rectangle{ strip.x, strip.y, strip.width, strip.height }, util::WithA(ui::theme::kInk, (0.62f + 0.3f * m_chatBgA) * fade));
	ui::Rule(r.x + 4.f, r.x + r.width - 4.f, strip.y + strip.height, util::WithA(ui::theme::kLine, (0.5f + 0.5f * m_chatBgA) * fade));
	wm::DragHandle(strip);
	float tx = strip.x + 6.f;
	const Vector2 mouse = ui::Mouse();
	const bool clickOk = !wm::JustDragged();
	ui::TextStyle tsBase; tsBase.size = ui::type::kCaption + 0.5f; tsBase.face = ui::Face::Strong; tsBase.outline = 1.f; tsBase.outlineColor = util::MulA(util::A8(ui::theme::kShadow, 170), fade);
	int clickedTab = -1, menuTab = -1;
	for (int i = 0; i < static_cast<int>(m_chatTabs.size()); ++i)
	{
		chat::Tab& t = m_chatTabs[static_cast<size_t>(i)];
		const float tw = std::round(ui::Fonts().Measure(t.name, tsBase.size, 0.f, ui::Face::Strong).x + 16.f);
		const Rectangle b{ tx, strip.y + 2.f, tw, strip.height - 3.f };
		if (b.x + b.width > strip.x + strip.width - 54.f) break;
		const bool active = i == m_chatTab;
		const bool hb = ui::Hovered(b);
		if (active) DrawRectangleRec(b, util::WithA(ui::theme::kSurface2, 0.5f + 0.4f * m_chatBgA));
		else if (hb) DrawRectangleRec(b, util::WithA(ui::theme::kSurface1, 0.6f));
		ui::TextStyle ts = tsBase;
		ts.color = active ? ui::theme::kTextHi : ui::theme::kTextDim;
		if (!active && t.unread > 0 && t.flash)
		{
			// 새 줄: 3초 동안 반짝이고, 그 뒤에는 새벽빛으로 남는다
			const float since = static_cast<float>(m_now - t.lastAt);
			const float pulse = since < 3.f ? 0.5f + 0.5f * std::sin(since * 9.f) : 1.f;
			ts.color = util::Mix(ui::theme::kTextDim, ui::theme::kDawnHi, pulse);
		}
		ts.color = util::MulA(ts.color, fade);
		ui::Fonts().Draw(t.name, Vector2{ b.x + 8.f, std::round(b.y + (b.height - ts.size) * 0.5f - 1.f) }, ts);
		if (active) DrawRectangleRec(Rectangle{ b.x + 4.f, b.y + b.height - 2.f, b.width - 8.f, 2.f }, util::MulA(ui::theme::kDawn, fade));
		if (!active && t.unread > 0)
			DrawCircleV(Vector2{ b.x + b.width - 4.f, b.y + 5.f }, 2.5f, ui::theme::kDawn);
		if (clickOk && ui::Clicked(b)) clickedTab = i;
		if (ui::Pressed(b, MOUSE_BUTTON_RIGHT)) menuTab = i;
		tx += tw + 2.f;
	}
	if (clickedTab >= 0 && clickedTab != m_chatTab)
	{
		m_chatTab = clickedTab;
		chat::Tab& t = m_chatTabs[static_cast<size_t>(clickedTab)];
		t.unread = 0;
		t.scroll = 0.f;
		t.newBelow = 0;
		wm::MarkDirty();
		sfx::Play(sfx::Id::Click, 0.35f);
	}
	if (menuTab >= 0)
	{
		m_chatMenu = 2;
		m_chatMenuTab = menuTab;
		m_chatMenuPos = Vector2{ mouse.x, strip.y + strip.height + 2.f };
		wm::Raise("chatmenu");
		ui::ConsumeMouse();
	}
	// "+" 탭 추가 · ⚙ 설정 · 잠금 표시
	if (m_chatTabs.size() < 10)
	{
		const Rectangle plus{ tx, strip.y + 3.f, 20.f, strip.height - 5.f };
		if (plus.x + plus.width <= strip.x + strip.width - 30.f)
		{
			const bool hp = ui::Hovered(plus);
			ui::TextStyle ps = tsBase; ps.color = hp ? ui::theme::kDawnHi : util::MulA(ui::theme::kTextDim, fade);
			ui::Fonts().DrawCentered("+", Vector2{ plus.x + plus.width * 0.5f, plus.y + plus.height * 0.5f }, ps);
			if (hp) ui::Tooltip({ { "탭 만들기", ui::theme::kTextHi, 13.f, ui::Face::Strong }, { "만든 뒤 탭을 오른쪽 클릭해 채널을 고른다.", ui::theme::kText, 12.5f } });
			if (clickOk && ui::Clicked(plus))
			{
				chat::Tab nt;
				nt.name = Fmt("탭 %d", static_cast<int>(m_chatTabs.size()) - 4);
				nt.user = true;
				nt.mask = chat::kAllMask;
				m_chatTabs.push_back(std::move(nt));
				m_chatTab = static_cast<int>(m_chatTabs.size()) - 1;
				m_chatMenu = 2;
				m_chatMenuTab = m_chatTab;
				m_chatMenuPos = Vector2{ plus.x, strip.y + strip.height + 2.f };
				wm::Raise("chatmenu");
				wm::MarkDirty();
				sfx::Play(sfx::Id::Open, 0.4f);
			}
		}
	}
	{
		const Rectangle gear{ strip.x + strip.width - 24.f, strip.y + 2.f, 20.f, strip.height - 3.f };
		const bool hg = ui::Hovered(gear);
		{
			// 톱니(그린 것 — 글꼴에 ⚙가 없다)
			const Color gc = hg ? ui::theme::kDawnHi : util::MulA(ui::theme::kTextDim, fade);
			const Vector2 c{ std::round(gear.x + gear.width * 0.5f), std::round(gear.y + gear.height * 0.5f) };
			for (int k = 0; k < 8; ++k)
			{
				const float a = k * util::kPi / 4.f;
				DrawLineEx(Vector2{ c.x + std::cos(a) * 3.5f, c.y + std::sin(a) * 3.5f }, Vector2{ c.x + std::cos(a) * 6.5f, c.y + std::sin(a) * 6.5f }, 2.2f, gc);
			}
			DrawCircleV(c, 4.6f, gc);
			DrawCircleV(c, 2.f, util::MulA(ui::theme::kInk, fade));
		}
		if (clickOk && ui::Clicked(gear))
		{
			m_chatMenu = m_chatMenu == 2 ? 0 : 2;
			m_chatMenuTab = m_chatTab;
			m_chatMenuPos = Vector2{ gear.x + gear.width - 250.f, strip.y + strip.height + 2.f };
			wm::Raise("chatmenu");
			sfx::Play(sfx::Id::Click, 0.35f);
		}
		if (locked)
		{
			// 자물쇠(그린 것): 고리 + 몸통
			const Color lc = util::MulA(ui::theme::kTextDim, fade);
			const Vector2 c{ std::round(gear.x - 12.f), std::round(gear.y + gear.height * 0.5f) };
			DrawRing(Vector2{ c.x, c.y - 2.f }, 2.2f, 3.6f, 180.f, 360.f, 12, lc);
			DrawRectangleRec(Rectangle{ c.x - 4.5f, c.y - 1.5f, 9.f, 7.f }, lc);
			if (ui::Hovered(Rectangle{ c.x - 6.f, c.y - 8.f, 12.f, 16.f }))
				ui::Tooltip({ { "창 잠금", ui::theme::kTextHi, 13.f, ui::Face::Strong }, { "옮기기 · 크기 조절이 막혀 있다(톱니 단추에서 풀기).", ui::theme::kText, 12.5f } });
		}
	}

	// ── 줄들 ──
	chat::Tab& tab = m_chatTabs[static_cast<size_t>(m_chatTab)];
	tab.unread = 0;
	const float inputH = typing ? 30.f : 0.f;
	const Rectangle area{ r.x + 8.f, strip.y + strip.height + 3.f, r.width - 16.f, r.height - (strip.height + 3.f) - 6.f - inputH };
	const float maxW = area.width - 2.f;
	// 올려 보는 중: 새로 온 줄만큼 내려 고정(보고 있던 줄이 그대로 있게)
	if (!tab.lines.empty())
	{
		const uint32_t newest = tab.lines.back()->serial;
		if (tab.scroll > 0.5f && newest > m_chatSeen)
			for (auto it = tab.lines.rbegin(); it != tab.lines.rend() && (*it)->serial > m_chatSeen; ++it)
				tab.scroll += MsgHeight(**it, fs, maxW, m_chatSet.clock);
		m_chatSeen = newest;
	}
	float total = 0.f;
	for (const auto& m : tab.lines) total += MsgHeight(*m, fs, maxW, m_chatSet.clock);
	const float maxScroll = std::max(0.f, total - area.height);
	if (ui::Hovered(area))
	{
		const float wheel = ui::Wheel();
		if (wheel != 0.f) tab.scroll += wheel * lh * 3.f;
	}
	tab.scroll = std::clamp(tab.scroll, 0.f, maxScroll);
	if (tab.scroll < 0.5f) tab.newBelow = 0;

	ui::BeginClip(area);
	float y = area.y + area.height + tab.scroll;
	const int count = static_cast<int>(tab.lines.size());
	std::vector<Row> rows;
	for (int i = count - 1; i >= 0; --i)
	{
		const chat::Msg& m = *tab.lines[static_cast<size_t>(i)];
		const float h = MsgHeight(m, fs, maxW, m_chatSet.clock);
		y -= h;
		if (y > area.y + area.height) continue;
		if (y + h < area.y) break;
		LayoutRuns(BuildRuns(m, m_chatSet.clock), fs, maxW, rows);
		// 새 줄은 짧게 밀려 들어온다(맨 아래를 보고 있을 때)
		float off = 0.f, alpha = (m.history ? 0.72f : 1.f) * m_chatTextA;
		const float age = static_cast<float>(m_now - m.time);
		if (i == count - 1 && tab.scroll < 0.5f && age < 0.18f && age >= 0.f)
		{
			const float u = age / 0.18f;
			off = (1.f - util::EaseOutCubic(u)) * lh * 0.9f;
			alpha *= u;
		}
		float ry = y + off;
		for (const Row& row : rows)
		{
			for (const Placed& p : row)
			{
				ui::TextStyle ts;
				ts.size = fs;
				ts.face = p.face;
				ts.color = util::MulA(p.color, alpha);
				ts.outline = 1.2f;
				ts.outlineColor = util::MulA(util::A8(ui::theme::kShadow, 185), alpha);
				const Vector2 pos{ std::round(area.x + p.x), std::round(ry + 1.f) };
				ui::Fonts().Draw(p.text, pos, ts);
				if (p.kind == kName || p.kind == kItem)
				{
					const Rectangle pr{ pos.x, pos.y, p.w, lh };
					if (ui::Hovered(pr) && ui::Hovered(area))
					{
						DrawRectangleRec(Rectangle{ pos.x, pos.y + fs + 1.f, p.w, 1.f }, util::MulA(p.color, alpha));
						if (p.kind == kItem)
						{
							InvSlot s;
							s.itemId = p.itemId; s.enchant = p.enchant; s.count = 1;
							ItemInstance inst;
							inst.rarity = static_cast<Rarity>(std::min<int>(p.rarity, static_cast<int>(Rarity::Count) - 1));
							s.extra = inst.Pack();
							DrawItemTooltip(s, TipMode::Shop, -1);
						}
						else if (ui::Clicked(pr) || ui::Pressed(pr, MOUSE_BUTTON_RIGHT))
						{
							m_chatMenu = 1;
							m_chatMenuName = p.text;
							m_chatMenuPos = Vector2{ mouse.x + 4.f, mouse.y + 4.f };
							wm::Raise("chatmenu");
							ui::ConsumeMouse();
							sfx::Play(sfx::Id::Click, 0.35f);
						}
					}
				}
			}
			ry += lh;
		}
	}
	ui::EndClip();

	// ▼ 새 메시지 N / 맨 아래로
	if (tab.scroll > 0.5f)
	{
		const std::string label = tab.newBelow > 0 ? Fmt("▼ 새 메시지 %d", tab.newBelow) : std::string("▼ 맨 아래로");
		ui::TextStyle bs; bs.size = ui::type::kCaption; bs.face = ui::Face::Strong; bs.color = tab.newBelow > 0 ? ui::theme::kDawnHi : ui::theme::kText;
		const float bw = std::round(ui::Fonts().Measure(label, bs.size, 0.f, ui::Face::Strong).x + 20.f);
		const Rectangle b{ std::round(area.x + area.width * 0.5f - bw * 0.5f), std::round(area.y + area.height - 22.f), bw, 20.f };
		const bool hb = ui::Hovered(b);
		ui::Plate(b, hb ? util::A8(ui::theme::kDawnWash, 240) : util::A8(ui::theme::kSurface0, 235), tab.newBelow > 0 ? ui::theme::kDawn : ui::theme::kLineStrong, 3.f, 1.f);
		ui::Fonts().DrawCentered(label, Vector2{ b.x + b.width * 0.5f, b.y + b.height * 0.5f }, bs);
		if (ui::Clicked(b)) { tab.scroll = 0.f; tab.newBelow = 0; sfx::Play(sfx::Id::Click, 0.35f); }
	}

	// ── 입력 ──
	if (!typing)
	{
		if (m_chatOpen && ui::Focused() != &m_chatField) m_chatOpen = false;
		return;
	}
	const Rectangle in{ r.x + 6.f, r.y + r.height - 32.f, r.width - 12.f, 27.f };
	// 머리(보낼 곳): 누르면 일반 → 전체 → 거래 → 결사 순으로 바꾼다(귓속말이면 일반으로)
	std::string head = m_chatTarget.ch == chat::Ch::WhisperOut ? "→ " + m_chatTarget.whisperTo : chat::ChName(m_chatTarget.ch);
	ui::TextStyle hs; hs.size = ui::type::kCaption + 0.5f; hs.face = ui::Face::Strong; hs.color = chat::ChColor(m_chatTarget.ch);
	const float hw = std::round(ui::Fonts().Measure(head, hs.size, 0.f, ui::Face::Strong).x + 14.f);
	const Rectangle hb{ in.x, in.y, hw, in.height };
	const bool hhv = ui::Hovered(hb);
	ui::Plate(hb, hhv ? ui::theme::kSurface3 : ui::theme::kSurface2, util::WithA(chat::ChColor(m_chatTarget.ch), 0.7f), 3.f, 1.f);
	ui::Fonts().DrawCentered(head, Vector2{ hb.x + hb.width * 0.5f, hb.y + hb.height * 0.5f }, hs);
	if (ui::Clicked(hb))
	{
		static const chat::Ch kCycle[] = { chat::Ch::Normal, chat::Ch::World, chat::Ch::Trade, chat::Ch::Guild };
		int idx = 0;
		for (int i = 0; i < 4; ++i) if (kCycle[i] == m_chatTarget.ch) idx = i;
		m_chatTarget = chat::Target{ m_chatTarget.ch == chat::Ch::WhisperOut ? chat::Ch::Normal : kCycle[(idx + 1) % 4], "" };
		ui::Focus(&m_chatField);
		sfx::Play(sfx::Id::Click, 0.35f);
	}
	const Rectangle field{ hb.x + hb.width + 4.f, in.y, in.width - hb.width - 4.f - 38.f, in.height };
	// ↑↓ 기록(자동 완성 목록이 없을 때)
	const std::vector<chat::CmdHint> hints = chat::CommandHints(m_chatField.text);
	if (hints.empty() && !m_chatHistory.empty() && ime::Composition().empty())
	{
		if (IsKeyPressed(KEY_UP))
		{
			m_chatHistIdx = m_chatHistIdx < 0 ? static_cast<int>(m_chatHistory.size()) - 1 : std::max(0, m_chatHistIdx - 1);
			m_chatField.text = m_chatHistory[static_cast<size_t>(m_chatHistIdx)];
		}
		else if (IsKeyPressed(KEY_DOWN) && m_chatHistIdx >= 0)
		{
			++m_chatHistIdx;
			if (m_chatHistIdx >= static_cast<int>(m_chatHistory.size())) { m_chatHistIdx = -1; m_chatField.text.clear(); }
			else m_chatField.text = m_chatHistory[static_cast<size_t>(m_chatHistIdx)];
		}
	}
	// 자동 완성: Tab = 첫 명령
	if (!hints.empty() && IsKeyPressed(KEY_TAB))
		m_chatField.text = std::string(hints.front().cmd) + " ";
	// 귓속말 이름 완성(Phase 3 자기 검토): "/귓 " 뒤에서 Tab — 최근 상대 · 말한 사람 · 주변 사람을 차례로. 이름을 끝까지 칠 필요가 없다
	std::string wCmd, wPart;
	const bool namingWhisper = hints.empty() && chat::WhisperNamePart(m_chatField.text, wCmd, wPart);
	if (namingWhisper && IsKeyPressed(KEY_TAB) && ime::Composition().empty())
		CompleteWhisperName();
	const bool submitted = ui::InputText(field, m_chatField, m_chatTarget.ch == chat::Ch::WhisperOut ? "귓속말 입력 (Esc 취소)" : "메시지 입력 · /귓 이름 · /전 · /거래 (Esc 취소)");
	// 남은 바이트(표식으로 바꾼 길이 기준)
	{
		std::vector<std::pair<std::string, std::string>> links;
		for (const auto& l : m_chatLinks) links.emplace_back(l.shown, l.token);
		const chat::Parsed p = chat::ParseInput(ConvertLinks(m_chatField.text + ime::Composition(), links), m_chatTarget, m_lastWhisperFrom);
		const int left = static_cast<int>(chat::kMaxBytes) - static_cast<int>(p.body.size());
		ui::TextStyle ns; ns.size = ui::type::kCaption; ns.face = ui::Face::Number; ns.color = left < 0 ? ui::theme::kBad : (left < 16 ? ui::theme::kWarn : ui::theme::kTextMute);
		const std::string s = std::to_string(left);
		ui::Fonts().Draw(s, Vector2{ in.x + in.width - 4.f - ui::Fonts().Measure(s, ns.size, 0.f, ui::Face::Number).x, in.y + 6.f }, ns);
	}
	// 자동 완성 목록(입력칸 위)
	if (!hints.empty())
	{
		const float rowH = 22.f;
		const Rectangle box{ field.x, in.y - 6.f - rowH * static_cast<float>(hints.size()) - 8.f, std::min(300.f, r.width - 20.f), rowH * static_cast<float>(hints.size()) + 8.f };
		ui::Plate(box, util::A8(ui::theme::kSlot, 245), ui::theme::kLineStrong, 3.f, 1.f);
		float hy = box.y + 4.f;
		for (size_t i = 0; i < hints.size(); ++i)
		{
			const Rectangle hr{ box.x + 3.f, hy, box.width - 6.f, rowH };
			const bool hov = ui::Hovered(hr);
			if (hov || i == 0) DrawRectangleRec(hr, i == 0 ? util::WithA(ui::theme::kDawnDeep, 0.5f) : util::WithA(ui::theme::kSurface2, 0.8f));
			ui::Text(hints[i].cmd, Vector2{ hr.x + 8.f, hr.y + 4.f }, ui::type::kBodyS, ui::theme::kDawnHi);
			ui::Text(hints[i].help, Vector2{ hr.x + 64.f, hr.y + 5.f }, ui::type::kCaption, ui::theme::kTextDim);
			if (ui::Clicked(hr)) { m_chatField.text = std::string(hints[i].cmd) + " "; ui::Focus(&m_chatField); }
			hy += rowH;
		}
		ui::Block(box);
	}
	// 귓속말 이름 후보(입력칸 위): Tab으로 도는 중이면 목록 + 지금 이름, 아니면 한 줄 안내
	if (namingWhisper)
	{
		const bool cycling = !m_whisperCands.empty() && m_chatField.text == m_whisperCandText;
		const float rowH = 21.f;
		const size_t shown = cycling ? std::min<size_t>(m_whisperCands.size(), 6) : 1;
		const Rectangle box{ field.x, in.y - 6.f - rowH * static_cast<float>(shown) - 8.f, std::min(240.f, r.width - 20.f), rowH * static_cast<float>(shown) + 8.f };
		ui::Plate(box, util::A8(ui::theme::kSlot, 245), ui::theme::kLineStrong, 3.f, 1.f);
		if (!cycling)
			ui::Text("Tab — 이름 채우기(최근 상대 · 주변 사람)", Vector2{ box.x + 8.f, box.y + 7.f }, ui::type::kCaption, ui::theme::kTextDim);
		else
		{
			// 지금 이름이 보이도록 창을 민다
			const size_t first = m_whisperCandIdx >= shown ? m_whisperCandIdx - shown + 1 : 0;
			for (size_t i = 0; i < shown; ++i)
			{
				const size_t k = first + i;
				const Rectangle hr{ box.x + 3.f, box.y + 4.f + rowH * static_cast<float>(i), box.width - 6.f, rowH };
				const bool cur = k == m_whisperCandIdx;
				if (cur) DrawRectangleRec(hr, util::WithA(ui::theme::kDawnDeep, 0.5f));
				ui::Text(m_whisperCands[k], Vector2{ hr.x + 8.f, hr.y + 3.f }, ui::type::kBodyS, cur ? ui::theme::kChatWhisper : ui::theme::kText);
				if (ui::Clicked(hr))
				{
					m_whisperCandIdx = k;
					m_chatField.text = wCmd + " " + m_whisperCands[k] + " ";
					ui::Focus(&m_chatField);
				}
			}
		}
		ui::Block(box);
	}
	if (submitted && m_now - m_chatOpenedAt > 0.05)
	{
		SubmitChat();
		ui::Focus(nullptr);
		m_chatOpen = false;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 작은 창: 이름 메뉴(귓속말 · 결사 초대 · 차단) · 탭 설정(채널 · 반짝임 · 지우기 + 채팅창 설정)
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawChatMenu()
{
	if (m_chatMenu == 0) return;
	const float W = m_uiW, H = m_uiH;
	auto closeMenu = [this] { m_chatMenu = 0; m_chatMenuTab = -1; };
	if (m_chatMenu == 1)
	{
		const std::string& name = m_chatMenuName;
		const bool blocked = ChatBlocked(name);
		const float w = 176.f, rowH = 30.f;
		const float h = 34.f + rowH * 3.f + 8.f;
		Rectangle def{ std::min(m_chatMenuPos.x, W - w - 4.f), std::max(4.f, std::min(m_chatMenuPos.y, H - h - 4.f)), w, h };
		const Rectangle r = wm::Window("chatmenu", def, wm::kNoAnim);
		if (ui::BtnPressed(MOUSE_BUTTON_LEFT) && ui::MouseOwner() != ui::Layer()) { closeMenu(); return; }
		ui::Panel(r, 0.98f, false);
		ui::Text(name, Vector2{ r.x + 12.f, r.y + 9.f }, ui::type::kHeading, ui::theme::kTextHi);
		float y = r.y + 34.f;
		auto item = [&](const char* label, bool enabled)
		{
			const Rectangle b{ r.x + 6.f, y, r.width - 12.f, rowH - 2.f };
			y += rowH;
			return ui::Button(b, label, ui::ButtonKind::Ghost, enabled, 14.f);
		};
		const bool self = name == m_playerName;
		if (item("귓속말", !self))
		{
			m_chatTarget = chat::Target{ chat::Ch::WhisperOut, name };
			closeMenu();
			OpenChatInput();
			return;
		}
		const bool canInvite = !self && m_guild.id != 0 && GuildRule::CanInvite(static_cast<GuildRule::Rank>(std::min<uint8_t>(m_guild.myRank, 3)));
		if (item("결사 초대", canInvite))
		{
			SendGuildReq(GuildOp::Invite, 0, 0, name);
			closeMenu();
			return;
		}
		if (!canInvite && !self && ui::Hovered(Rectangle{ r.x + 6.f, y - rowH, r.width - 12.f, rowH }))
			ui::Tooltip({ { "결사 초대", ui::theme::kTextHi, 13.f, ui::Face::Strong },
				{ m_guild.id == 0 ? "결사에 들어 있지 않다." : "정예 이상만 초대할 수 있다.", ui::theme::kTextDim, 12.5f } });
		if (item(blocked ? "차단 풀기" : "차단", !self))
		{
			if (blocked)
				m_chatSet.blocked.erase(std::remove_if(m_chatSet.blocked.begin(), m_chatSet.blocked.end(), [&](const std::string& n) { return n == name; }), m_chatSet.blocked.end());
			else
				m_chatSet.blocked.push_back(name);
			AddChatMsg(chat::Ch::System, "", 0, blocked ? name + "의 차단을 풀었습니다." : name + "의 말을 더 보지 않습니다. (차단은 이 컴퓨터에만)", ui::theme::kChatSystem);
			wm::MarkDirty();
			closeMenu();
		}
		return;
	}

	// ── 탭 설정 ──
	if (m_chatMenuTab < 0 || m_chatMenuTab >= static_cast<int>(m_chatTabs.size())) { closeMenu(); return; }
	chat::Tab& t = m_chatTabs[static_cast<size_t>(m_chatMenuTab)];
	const float w = 256.f, rowH = 26.f;
	const float h = 40.f + rowH * 7.f + 30.f + rowH * 6.f + 14.f;
	Rectangle def{ std::clamp(m_chatMenuPos.x, 4.f, W - w - 4.f), std::clamp(m_chatMenuPos.y, 4.f, H - h - 4.f), w, h };
	// 창 아래쪽에 붙은 채팅창이면 위로 펼친다
	const Rectangle chatR = wm::LastRect("chat");
	if (chatR.width > 0.f && chatR.y > H * 0.5f) def.y = std::max(4.f, chatR.y - h - 4.f);
	const Rectangle r = wm::Window("chatmenu", def, wm::kNoAnim);
	if (ui::BtnPressed(MOUSE_BUTTON_LEFT) && ui::MouseOwner() != ui::Layer() && !ui::Hovered(chatR)) { closeMenu(); return; }
	ui::Panel(r, 0.98f, false);
	ui::Text("탭 설정 — " + t.name, Vector2{ r.x + 12.f, r.y + 10.f }, ui::type::kHeading, ui::theme::kTextHi);
	{
		const Rectangle x{ r.x + r.width - 30.f, r.y + 7.f, 22.f, 22.f };
		if (ui::IconButton(x, "×")) { closeMenu(); return; }
	}
	float y = r.y + 38.f;
	auto check = [&](const char* label, bool on, Color c) -> bool
	{
		const Rectangle row{ r.x + 8.f, y, r.width - 16.f, rowH - 2.f };
		const bool hv = ui::Hovered(row);
		if (hv) DrawRectangleRec(row, util::WithA(ui::theme::kSurface2, 0.8f));
		const Rectangle box{ row.x + 6.f, row.y + 6.f, 13.f, 13.f };
		ui::Plate(box, on ? util::WithA(c, 0.85f) : ui::theme::kSurface1, on ? c : ui::theme::kLineStrong, 2.f, 1.f);
		ui::Text(label, Vector2{ row.x + 28.f, row.y + 5.f }, ui::type::kBodyS, on ? ui::theme::kText : ui::theme::kTextDim);
		y += rowH;
		return ui::Clicked(row);
	};
	struct ChRow { const char* label; uint32_t mask; chat::Ch color; };
	const ChRow chs[] = {
		{ "일반(같은 지역)", chat::Bit(chat::Ch::Normal), chat::Ch::Normal },
		{ "전체", chat::Bit(chat::Ch::World), chat::Ch::World },
		{ "거래", chat::Bit(chat::Ch::Trade), chat::Ch::Trade },
		{ "결사", chat::Bit(chat::Ch::Guild), chat::Ch::Guild },
		{ "귓속말", chat::Bit(chat::Ch::WhisperIn) | chat::Bit(chat::Ch::WhisperOut), chat::Ch::WhisperIn },
		{ "시스템 · 공지", chat::Bit(chat::Ch::System) | chat::Bit(chat::Ch::Notice), chat::Ch::System },
	};
	for (const auto& c : chs)
		if (check(c.label, (t.mask & c.mask) == c.mask, chat::ChColor(c.color)))
		{
			const uint32_t nm = (t.mask & c.mask) == c.mask ? (t.mask & ~c.mask) : (t.mask | c.mask);
			if (nm != 0) { t.mask = nm; wm::MarkDirty(); }
		}
	if (check("새 메시지 반짝임", t.flash, ui::theme::kDawn)) { t.flash = !t.flash; wm::MarkDirty(); }
	// 탭 지우기(만든 탭만)
	{
		const Rectangle b{ r.x + 8.f, y + 2.f, r.width - 16.f, 24.f };
		if (ui::Button(b, t.user ? "이 탭 지우기" : "기본 탭은 지울 수 없음", ui::ButtonKind::Danger, t.user, 13.f))
		{
			m_chatTabs.erase(m_chatTabs.begin() + m_chatMenuTab);
			m_chatTab = std::clamp(m_chatTab >= m_chatMenuTab ? m_chatTab - 1 : m_chatTab, 0, static_cast<int>(m_chatTabs.size()) - 1);
			wm::MarkDirty();
			closeMenu();
			return;
		}
		y += 30.f;
	}
	ui::SectionHeader(Vector2{ r.x + 12.f, y + 4.f }, r.width - 24.f, "채팅창");
	y += 22.f;
	auto choice = [&](const char* label, const char* const* names, const float* values, int n, float cur, auto&& set)
	{
		ui::Text(label, Vector2{ r.x + 12.f, y + 5.f }, ui::type::kBodyS, ui::theme::kText);
		const float bw = 44.f;
		for (int i = 0; i < n; ++i)
		{
			const Rectangle b{ r.x + r.width - 10.f - (bw + 3.f) * static_cast<float>(n - i), y + 1.f, bw, rowH - 4.f };
			if (ui::Button(b, names[i], std::fabs(cur - values[i]) < 0.001f ? ui::ButtonKind::Primary : ui::ButtonKind::Normal, true, 13.f)) { set(values[i]); wm::MarkDirty(); }
		}
		y += rowH;
	};
	{
		static const char* const kIdle[] = { "20%", "35%", "50%" };
		static const float kIdleV[] = { 0.2f, 0.35f, 0.5f };
		choice("평소 배경", kIdle, kIdleV, 3, m_chatSet.idleAlpha, [&](float v) { m_chatSet.idleAlpha = v; });
		static const char* const kAct[] = { "70%", "85%", "100%" };
		static const float kActV[] = { 0.7f, 0.85f, 1.f };
		choice("활성 배경", kAct, kActV, 3, m_chatSet.activeAlpha, [&](float v) { m_chatSet.activeAlpha = v; });
		static const char* const kFont[] = { "작게", "보통", "크게" };
		static const float kFontV[] = { 0.f, 1.f, 2.f };
		choice("글자", kFont, kFontV, 3, static_cast<float>(m_chatSet.font), [&](float v) { m_chatSet.font = static_cast<int>(v); });
		static const char* const kOn[] = { "끔", "켬" };
		static const float kOnV[] = { 0.f, 1.f };
		choice("시각 표시", kOn, kOnV, 2, m_chatSet.clock ? 1.f : 0.f, [&](float v) { m_chatSet.clock = v > 0.5f; });
		choice("조용하면 흐리게", kOn, kOnV, 2, m_chatSet.fadeText ? 1.f : 0.f, [&](float v) { m_chatSet.fadeText = v > 0.5f; });
		if (ui::Hovered(Rectangle{ r.x + 8.f, y - rowH, r.width - 130.f, rowH }))
			ui::Tooltip({ { "조용하면 흐리게", ui::theme::kTextHi, 13.f, ui::Face::Strong }, { "보는 탭에 20초 동안 새 줄이 없으면 글자까지 흐려진다.", ui::theme::kText, 12.5f }, { "새 줄이 오거나 마우스를 올리면 바로 돌아온다.", ui::theme::kTextDim, 12.5f } });
		const bool lk = wm::Locked("chat");
		choice("창 잠금", kOn, kOnV, 2, lk ? 1.f : 0.f, [&](float v) { wm::SetLocked("chat", v > 0.5f); });
	}
}
