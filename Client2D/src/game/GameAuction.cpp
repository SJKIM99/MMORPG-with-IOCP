// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 경매장 · 정산함 — 창(Alt+Y · 경매인 로렌초) · 검색 · 판매 등록 · 내 경매 · 정산함 · 알림
//
//  판정 · 상태는 서버(AuctionService). 검색은 서버 메모리 색인이 한 페이지(6줄)씩 돌려준다.
//  돈 · 물건은 모두 정산함을 거친다: 산 물건 · 판 대금 · 취소 · 기간 끝 → 정산함 → "받기"(한 줄 = 한 트랜잭션).
// ─────────────────────────────────────────────────────────────────────────────
#include "game/Game.h"
#include "game/Texts.h"
#include "render/Icons.h"
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

using util::Rgb;
using AuctionRule::Cat;
using AuctionRule::Sub;

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

	[[nodiscard]] const char* ResultText(AuctionResult r)
	{
		switch (r)
		{
		case AuctionResult::Ok:            return "";
		case AuctionResult::NotFound:      return "이미 팔렸거나 내려간 물건입니다.";
		case AuctionResult::Sold:          return "이미 팔린 물건입니다.";
		case AuctionResult::NoAdena:       return "그로시가 모자랍니다.";
		case AuctionResult::TooMany:       return "한 번에 10개까지 올릴 수 있습니다.";
		case AuctionResult::BadPrice:      return "값은 10 ~ 100,000,000 그로시입니다.";
		case AuctionResult::BadItem:       return "올릴 수 없는 물건입니다.";
		case AuctionResult::Equipped:      return "입고 있는 장비는 벗은 뒤에 올릴 수 있습니다.";
		case AuctionResult::InventoryFull: return "가방(또는 지갑)에 자리가 없습니다.";
		case AuctionResult::NotYours:      return "내가 올린 물건이 아닙니다.";
		case AuctionResult::NotAtNpc:      return "경매인 로렌초 곁에서 할 수 있습니다. (검색은 어디서나)";
		case AuctionResult::Busy:          return "다른 거래가 진행 중입니다. 잠시 뒤에 다시 해 보세요.";
		case AuctionResult::Invalid:       return "요청을 처리하지 못했습니다.";
		case AuctionResult::Duplicate:     return "";
		}
		return "요청을 처리하지 못했습니다.";
	}

	[[nodiscard]] bool Mutating(AuctionOp op) { return op == AuctionOp::Register || op == AuctionOp::Buy || op == AuctionOp::Cancel || op == AuctionOp::Claim; }

	// 남은 시간(게임 분) → "11시간 42분" · "40분" · "곧 끝"
	std::string LeftText(uint16_t minutes)
	{
		if (minutes == 0) return "곧 끝";
		if (minutes < 60) return Fmt("%d분", minutes);
		if (minutes % 60 == 0) return Fmt("%d시간", minutes / 60);
		return Fmt("%d시간 %d분", minutes / 60, minutes % 60);
	}

	// 옵션 두 줄 요약("공격력 +4 · 힘 +3")
	std::string OptionsLine(uint64_t extra)
	{
		const ItemInstance inst = ItemInstance::Unpack(extra);
		std::string out;
		int n = 0;
		for (const auto& o : inst.opts)
		{
			if (o.kind == ItemOption::None) continue;
			if (n++ >= 2) { out += " …"; break; }
			if (!out.empty()) out += " · ";
			out += texts::OptionText(o);
		}
		return out;
	}

	// 가격 입력칸: 숫자만(쉼표 없이) → 값
	uint32_t ParsePrice(const std::string& s)
	{
		uint64_t v = 0;
		for (char c : s)
		{
			if (c < '0' || c > '9') continue;
			v = v * 10 + static_cast<uint64_t>(c - '0');
			if (v > 4000000000ull) break;
		}
		return static_cast<uint32_t>(std::min<uint64_t>(v, 4000000000ull));
	}

	const char* kRarityFilter[] = { "희귀도 전체", "고급 이상", "희귀 이상", "영웅 이상", "전설 이상", "신화" };
	const char* kSortName[] = { "낮은 값부터", "높은 값부터", "끝나는 순", "희귀한 순" };
	const char* kSettleReason[] = { "팔린 대금", "산 물건", "취소 — 돌려받음", "기간 끝 — 돌려받음" };

	// 분류 트리: 분류 → 그 아래 세부(없으면 빈)
	struct CatNode { Cat cat; std::vector<Sub> subs; };
	const std::vector<CatNode>& Tree()
	{
		static const std::vector<CatNode> t = {
			{ Cat::All, {} },
			{ Cat::Weapon, { Sub::Knight, Sub::Engraver, Sub::NightHunter } },
			{ Cat::Armor, { Sub::Helmet, Sub::Body, Sub::Gloves, Sub::Boots, Sub::Cloak } },
			{ Cat::Accessory, { Sub::Necklace, Sub::Ring } },
			{ Cat::Consumable, { Sub::Potion, Sub::Scroll } },
			{ Cat::Material, {} },
			{ Cat::Etc, {} },
		};
		return t;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 보내기 · 받기
// ─────────────────────────────────────────────────────────────────────────────
void Game::SendAuctionReq(AUCTION_REQ_PACKET p)
{
	p.seq = ++m_auction.seq;
	m_net.SendAuction(p);
	switch (static_cast<AuctionOp>(p.op))
	{
	case AuctionOp::Search:      m_auction.askAt[0] = m_now; break;
	case AuctionOp::Mine:        m_auction.askAt[1] = m_now; break;
	case AuctionOp::Settlements: m_auction.askAt[2] = m_now; break;
	default: break;
	}
	if (Mutating(static_cast<AuctionOp>(p.op)))
	{
		m_auction.pendingSeq = p.seq;
		m_auction.pendingAt = m_now;
	}
	std::printf("[auction] req op=%d seq=%u id=%u price=%u\n", p.op, p.seq, p.id, p.price);
}

void Game::AuctionSearch(uint16_t page)
{
	AUCTION_REQ_PACKET p{};
	p.op = static_cast<uint8_t>(AuctionOp::Search);
	p.cat = m_auction.cat;
	p.sub = m_auction.sub;
	p.minRarity = m_auction.minRarity;
	p.minEnchant = m_auction.minEnchant;
	p.sort = m_auction.sort;
	static const uint8_t kBand[6][2] = { { 0, 0 }, { 1, 10 }, { 11, 20 }, { 21, 30 }, { 31, 40 }, { 41, 0 } };
	p.minLevel = kBand[std::min<int>(m_auction.levelBand, 5)][0];
	p.maxLevel = kBand[std::min<int>(m_auction.levelBand, 5)][1];
	p.option = m_auction.option;
	p.page = page;
	const std::string& q = m_auction.query.text;
	size_t len = std::min(q.size(), static_cast<size_t>(AuctionRule::kQueryBytes - 1));
	while (len > 0 && len < q.size() && (static_cast<unsigned char>(q[len]) & 0xC0) == 0x80) --len;
	std::memcpy(p.query, q.data(), len);
	SendAuctionReq(p);
	m_auction.searched = true;
}

void Game::OpenAuction(int tab)
{
	m_auctionOpen = true;
	m_auction.tab = tab;
	m_auction.confirmId = 0;
	sfx::Play(sfx::Id::Open, 0.6f);
	AUCTION_REQ_PACKET p{};
	switch (tab)
	{
	case 0: AuctionSearch(m_auction.page); break;
	case 2: p.op = static_cast<uint8_t>(AuctionOp::Mine); p.page = m_auction.minePage; SendAuctionReq(p); break;
	case 3: p.op = static_cast<uint8_t>(AuctionOp::Settlements); p.page = m_auction.settlePage; SendAuctionReq(p); break;
	default: p.op = static_cast<uint8_t>(AuctionOp::Mine); SendAuctionReq(p); break;   // 판매 등록: 올린 수(10개 한도)
	}
}

void Game::HandleAuctionAck(const AUCTION_ACK_PACKET& p)
{
	const auto op = static_cast<AuctionOp>(p.op);
	const auto r = static_cast<AuctionResult>(p.result);
	std::printf("[auction] ack op=%d result=%d seq=%u id=%u adena=%u delta=%d item=%u count=%u\n",
		p.op, p.result, p.seq, p.id, p.adena, p.delta, p.itemId, p.count);
	if (r == AuctionResult::Duplicate) return;   // 같은 순번(되풀이) — 처음 요청의 결과가 따로 온다(기다림을 풀지 않는다)
	if (p.seq == m_auction.pendingSeq) m_auction.pendingSeq = 0;
	m_auction.lastResult = p.result;
	auto refresh = [&](AuctionOp what, uint16_t page)
	{
		AUCTION_REQ_PACKET q{};
		q.op = static_cast<uint8_t>(what);
		q.page = page;
		SendAuctionReq(q);
	};
	if (r != AuctionResult::Ok)
	{
		AddCenter(ResultText(r), ui::theme::kBad);
		sfx::Play(sfx::Id::Error, 0.5f);
		m_auction.claimAll = false;
		if (op == AuctionOp::Buy && m_auctionOpen) AuctionSearch(m_auction.page);   // 그 사이 바뀐 목록
		return;
	}
	const std::string item = p.itemId ? texts::ItemName(p.itemId) + (p.count > 1 ? Fmt(" ×%d", p.count) : std::string()) : std::string();
	switch (op)
	{
	case AuctionOp::Register:
		AddCenter(Fmt("「%s」을(를) 올렸습니다 — 보증금 %s 그로시", item.c_str(), texts::Number(-p.delta).c_str()), ui::theme::kGold);
		sfx::Play(sfx::Id::Drop, 0.6f);
		m_auction.regSlot = -1;
		m_auction.regPrice.text.clear();
		m_auction.hintKnown = false;
		refresh(AuctionOp::Mine, 0);
		break;
	case AuctionOp::Buy:
		AddBanner(Fmt("「%s」을(를) 샀습니다 — 정산함에서 받으세요", item.c_str()), ui::theme::kGold, 3.5f);
		sfx::Play(sfx::Id::Gavel, 0.8f);
		m_auction.lastBoughtId = p.id;
		m_auction.lastBoughtAt = m_now;
		m_auction.confirmId = 0;
		// 산 줄은 1.35초 남겨 "낙찰" 도장을 다 보여 준 뒤 다시 찾는다(바로 찾으면 팔린 줄이 목록에서 빠져 도장이 0.3초 만에 사라졌다)
		for (auto& row : m_auction.rows) if (row.id == p.id) row.flags |= 4;
		m_auction.refreshAt = m_now + 1.35;
		break;
	case AuctionOp::Cancel:
		AddCenter(Fmt("「%s」을(를) 내렸습니다 — 물건은 정산함으로", item.c_str()), ui::theme::kTextDim);
		sfx::Play(sfx::Id::Close, 0.5f);
		refresh(AuctionOp::Mine, m_auction.minePage);
		break;
	case AuctionOp::Claim:
		if (p.itemId != 0)
			AddToast(p.itemId, "정산함에서 받음 · " + item, ui::theme::kText);
		else
			AddCenter(Fmt("+%s 그로시 (정산함)", texts::Number(p.delta).c_str()), ui::theme::kGold);
		sfx::Play(p.itemId != 0 ? sfx::Id::Pickup : sfx::Id::CoinPour, 0.7f);
		refresh(AuctionOp::Settlements, 0);
		break;
	default:
		break;
	}
}

void Game::HandleAuctionList(const AUCTION_LIST_INF_PACKET& p)
{
	const auto kind = static_cast<AuctionListKind>(p.kind);
	const int n = std::min<int>(p.count, AuctionRule::kPageRows);
	if (p.kind < 3) m_auction.gotAt[p.kind] = m_now;
	switch (kind)
	{
	case AuctionListKind::Search:
		m_auction.rows.assign(p.rows, p.rows + n);
		m_auction.page = p.page;
		m_auction.pages = std::max<uint16_t>(1, p.pages);
		m_auction.total = p.total;
		m_auction.rowsAt = m_now;
		break;
	case AuctionListKind::Mine:
		m_auction.mine.assign(p.rows, p.rows + n);
		m_auction.minePage = p.page;
		m_auction.minePages = std::max<uint16_t>(1, p.pages);
		m_auction.mineTotal = p.total;
		break;
	case AuctionListKind::Settlements:
		m_auction.settles.assign(p.rows, p.rows + n);
		m_auction.settlePage = p.page;
		m_auction.settlePages = std::max<uint16_t>(1, p.pages);
		m_auction.settleTotal = p.total;
		m_auction.settleCount = static_cast<uint16_t>(p.total);
		// 모두 받기: 다음 줄(한 줄 = 한 트랜잭션)
		if (m_auction.claimAll)
		{
			if (!m_auction.settles.empty())
			{
				AUCTION_REQ_PACKET q{};
				q.op = static_cast<uint8_t>(AuctionOp::Claim);
				q.id = m_auction.settles.front().id;
				SendAuctionReq(q);
			}
			else
				m_auction.claimAll = false;
		}
		break;
	}
	std::printf("[auction] list kind=%d page=%u/%u total=%u count=%d\n", p.kind, p.page, p.pages, p.total, n);
}

void Game::HandleAuctionNfy(const AUCTION_NFY_PACKET& p)
{
	// 정산함 수가 바뀌었는데 창이 그 탭을 보고 있으면 목록도 다시(자기 검토: 판매자가 정산함 탭을 보는 동안 목걸이가 만료되어
	// 탭에는 "정산함 (1)", 목록에는 "비어 있다"가 1분 넘게 함께 보였다). 모두 받기 · 거래 중에는 그 흐름이 목록을 다시 받는다
	auto refreshOpenTab = [&]()
	{
		if (!m_auctionOpen || m_auction.claimAll || m_auction.pendingSeq != 0) return;
		AUCTION_REQ_PACKET q{};
		if (m_auction.tab == 3) { q.op = static_cast<uint8_t>(AuctionOp::Settlements); q.page = m_auction.settlePage; SendAuctionReq(q); }
		else if (m_auction.tab == 2) { q.op = static_cast<uint8_t>(AuctionOp::Mine); q.page = m_auction.minePage; SendAuctionReq(q); }
	};
	switch (static_cast<AuctionEvent>(p.event))
	{
	case AuctionEvent::Sold:
		m_auction.settleCount = p.settleCount;
		m_auction.soldFlashAt = m_now;
		AddToast(p.itemId, Fmt("팔렸다 · 대금 %s 그로시(정산함)", texts::Number(p.price).c_str()), ui::theme::kGold);
		AddChatMsg(chat::Ch::System, "", 0, Fmt("경매 — 「%s」이(가) 팔렸습니다. 대금 %s 그로시는 정산함에서 받으세요.", texts::ItemName(p.itemId).c_str(), texts::Number(p.price).c_str()), ui::theme::kChatNotice);
		sfx::Play(sfx::Id::CoinPour, 0.7f);
		sfx::Play(sfx::Id::Gavel, 0.45f);
		refreshOpenTab();
		break;
	case AuctionEvent::Expired:
		m_auction.settleCount = p.settleCount;
		AddChatMsg(chat::Ch::System, "", 0, "경매 기간이 끝난 「" + texts::ItemName(p.itemId) + "」이(가) 정산함으로 돌아왔습니다.", ui::theme::kChatSystem);
		refreshOpenTab();
		break;
	case AuctionEvent::SettleCount:
		if (p.settleCount != m_auction.settleCount && m_auction.tab == 3) { m_auction.settleCount = p.settleCount; refreshOpenTab(); }
		m_auction.settleCount = p.settleCount;
		break;
	case AuctionEvent::PriceHint:
		if (p.itemId == m_auction.hintItem)
		{
			m_auction.hintLowest = p.lowest;
			m_auction.hintLast = p.lastSold;
			m_auction.hintAvg = p.avgSold;
			m_auction.hintCount = p.soldCount;
			m_auction.hintKnown = true;
		}
		break;
	default:
		break;
	}
	std::printf("[auction] nfy ev=%d id=%u item=%u price=%u settle=%u\n", p.event, p.id, p.itemId, p.price, p.settleCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// 창
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawAuctionWindow()
{
	const float W = m_uiW, H = m_uiH;
	const Rectangle r = wm::Window("auction", Rectangle{ std::round(W * 0.5f - 440.f), std::round(std::max(12.f, H * 0.5f - 290.f)), 880.f, 566.f }, wm::kMove);
	ui::Window(r, "경매장", &m_auctionOpen);
	if (!m_auctionOpen) { sfx::Play(sfx::Id::Close, 0.5f); m_auction.claimAll = false; return; }
	const bool busy = m_auction.pendingSeq != 0 && m_now - m_auction.pendingAt < 4.0;
	if (m_auction.refreshAt > 0.0 && m_now >= m_auction.refreshAt)
	{
		m_auction.refreshAt = -1.0;
		if (m_auction.tab == 0) AuctionSearch(m_auction.page);
	}
	// 답이 오기 전(탭을 막 바꿨다)에는 "비어 있다" 대신 "불러오는 중" — 빈 말이 번쩍였다가 줄이 들어오지 않게
	auto loading = [&](int k) { return m_auction.askAt[k] > m_auction.gotAt[k] && m_now - m_auction.askAt[k] < 2.0; };
	// 거래(올리기 · 사기 · 내리기 · 받기)는 경매인 로렌초 곁에서 — 서버와 같은 거리(상점 규칙). 검색은 어디서나
	bool inTown = false;
	if (const Entity* self = Self())
	{
		const Vector2 npc = NpcTile(AuctionRule::kAuctioneerNpcId);
		inTown = std::max(std::abs(npc.x - static_cast<float>(self->tx)), std::abs(npc.y - static_cast<float>(self->ty))) <= static_cast<float>(WorldData::kNpcInteractRange) + 0.5f;
	}

	// ── 탭 ──
	const std::string settleTab = m_auction.settleCount > 0 ? Fmt("정산함 (%d)", m_auction.settleCount) : std::string("정산함");
	const char* tabs[] = { "검색", "판매 등록", "내 경매", settleTab.c_str() };
	for (int i = 0; i < 4; ++i)
	{
		const Rectangle t{ r.x + 16.f + i * 118.f, r.y + 46.f, 112.f, 32.f };
		const bool sel = m_auction.tab == i;
		if (ui::Button(t, tabs[i], sel ? ui::ButtonKind::Primary : ui::ButtonKind::Ghost, true, 15.f) && !sel)
			OpenAuction(i);
		if (i == 3 && m_auction.settleCount > 0)
		{
			const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 4.f);
			const float flash = std::max(0.f, 1.f - static_cast<float>(m_now - m_auction.soldFlashAt) / 1.2f);
			DrawCircleV(Vector2{ t.x + t.width - 8.f, t.y + 7.f }, 3.6f + pulse * 0.8f + flash * 4.f, ui::theme::kDawn);
		}
	}
	ui::Text(Fmt("가진 그로시  %s", texts::Number(m_inv.adena).c_str()), Vector2{ r.x + r.width - 18.f, r.y + 54.f }, ui::type::kBodyS, ui::theme::kGold, ui::Align::Right);
	if (!inTown)
	{
		ui::Text("둘러보기만 — 거래는 경매인 로렌초 곁에서", Vector2{ r.x + r.width - 132.f, r.y + 70.f }, ui::type::kCaption, ui::theme::kWarn, ui::Align::Right);
		if (ui::Button(Rectangle{ r.x + r.width - 124.f, r.y + 64.f, 108.f, 24.f }, "로렌초에게 가기", ui::ButtonKind::Ghost, true, 12.f))
		{
			SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, AuctionRule::kAuctioneerNpcId });
			sfx::Play(sfx::Id::Click, 0.4f);
		}
	}
	ui::Rule(r.x + 12.f, r.x + r.width - 12.f, r.y + 84.f);

	auto rowItem = [&](const AuctionRow& row, Rectangle rr, bool settle)
	{
		// 아이콘 · 이름(희귀도 빛) · 옵션 요약
		const Rectangle ic{ rr.x + 6.f, rr.y + 6.f, rr.height - 12.f, rr.height - 12.f };
		Rarity rar = Rarity::Common;
		if (const ItemTableRow* ir = row.itemId ? ItemTableData::Find(row.itemId) : nullptr; ir && ir->type == ItemType::eEquipment)
			rar = ItemRules::EffectiveRarity(*ir, row.extra);
		ui::SlotFrame(ic, false, false, rar != Rarity::Common ? icons::RarityColor(rar) : ui::theme::kBorder);
		if (rar != Rarity::Common)
			DrawRectangleRec(Rectangle{ ic.x + 2.f, ic.y + ic.height - 4.f, ic.width - 4.f, 2.f }, icons::RarityColor(rar));   // 아래 희귀도 띠
		if (row.itemId != 0)
		{
			icons::Draw(row.itemId, Rectangle{ ic.x + 3.f, ic.y + 3.f, ic.width - 6.f, ic.height - 6.f });
			if (row.count > 1) ui::Text(std::to_string(row.count), Vector2{ ic.x + ic.width - 3.f, ic.y + ic.height - 15.f }, ui::type::kMicro, ui::theme::kTextHi, ui::Align::Right, true);
			const std::string nm = texts::ItemNameFull(row.itemId, row.enchant, row.extra);
			ui::Text(nm, Vector2{ ic.x + ic.width + 10.f, rr.y + 8.f }, ui::type::kBody, icons::ItemNameColor(row.itemId, row.extra));
			const std::string opt = OptionsLine(row.extra);
			if (!opt.empty()) ui::Text(opt, Vector2{ ic.x + ic.width + 10.f, rr.y + 29.f }, ui::type::kCaption, ui::theme::kOption);
		}
		else
		{
			// 정산함의 그로시 줄: 동전 무더기
			const Vector2 c{ ic.x + ic.width * 0.5f, ic.y + ic.height * 0.5f + 3.f };
			for (int k = 0; k < 3; ++k)
			{
				DrawEllipse(static_cast<int>(c.x - 5.f + k * 5.f), static_cast<int>(c.y + 4.f - k * 5.f), 9.f, 4.f, ui::theme::kDawnDeep);
				DrawEllipse(static_cast<int>(c.x - 5.f + k * 5.f), static_cast<int>(c.y + 3.f - k * 5.f), 9.f, 4.f, ui::theme::kGold);
			}
			ui::Text(Fmt("%s 그로시", texts::Number(row.price).c_str()), Vector2{ ic.x + ic.width + 10.f, rr.y + 8.f }, ui::type::kBody, ui::theme::kGold);
		}
		if (settle)
			ui::Text(kSettleReason[std::min<int>(row.minutesLeft, 3)], Vector2{ ic.x + ic.width + 10.f, rr.y + (row.itemId != 0 && !OptionsLine(row.extra).empty() ? 46.f : 29.f) }, ui::type::kCaption, ui::theme::kTextDim);
		// 줄 어디에 올려도(단추 빼고) 착용 장비와 비교 툴팁
		if (row.itemId != 0 && ui::Hovered(Rectangle{ rr.x, rr.y, rr.width - 112.f, rr.height }))
		{
			InvSlot s;
			s.itemId = row.itemId;
			s.count = row.count;
			s.enchant = row.enchant;
			s.extra = row.extra;
			DrawItemTooltip(s, TipMode::Auction, -1);
		}
	};

	const Rectangle content{ r.x + 16.f, r.y + 94.f, r.width - 32.f, r.height - 110.f };
	if (m_auction.tab == 0)
	{
		// ── 왼쪽: 분류 ──
		const Rectangle tree{ content.x, content.y, 176.f, content.height };
		ui::Plate(tree, Color{ 11, 13, 16, 200 }, ui::theme::kLine, 3.f, 1.f);
		float ty = tree.y + 6.f;
		for (const auto& node : Tree())
		{
			const Rectangle cr{ tree.x + 4.f, ty, tree.width - 8.f, 26.f };
			const bool sel = m_auction.cat == static_cast<uint8_t>(node.cat) && (m_auction.sub == 0 || node.subs.empty());
			const bool open = m_auction.cat == static_cast<uint8_t>(node.cat);
			if (sel || ui::Hovered(cr)) ui::Plate(cr, sel ? ui::theme::kSurface3 : ui::theme::kSurface2, BLANK, 2.f, 0.f);
			if (sel) DrawRectangleRec(Rectangle{ cr.x, cr.y + 4.f, 2.f, cr.height - 8.f }, ui::theme::kDawn);
			ui::Text(std::string(node.subs.empty() ? "  " : (open ? "▼ " : "▶ ")) + AuctionRule::kCatName[static_cast<int>(node.cat)], Vector2{ cr.x + 8.f, cr.y + 5.f }, ui::type::kBodyS, sel ? ui::theme::kTextHi : ui::theme::kText);
			if (ui::Clicked(cr))
			{
				m_auction.cat = static_cast<uint8_t>(node.cat);
				m_auction.sub = 0;
				AuctionSearch(0);
				sfx::Play(sfx::Id::Click, 0.4f);
			}
			ty += 28.f;
			if (open)
				for (Sub sub : node.subs)
				{
					const Rectangle sr{ tree.x + 16.f, ty, tree.width - 20.f, 24.f };
					const bool ss = m_auction.sub == static_cast<uint8_t>(sub);
					if (ss || ui::Hovered(sr)) ui::Plate(sr, ss ? ui::theme::kSurface3 : ui::theme::kSurface2, BLANK, 2.f, 0.f);
					ui::Text(AuctionRule::kSubName[static_cast<int>(sub)], Vector2{ sr.x + 10.f, sr.y + 4.f }, ui::type::kCaption + 0.5f, ss ? ui::theme::kDawnHi : ui::theme::kTextDim);
					if (ui::Clicked(sr))
					{
						m_auction.sub = static_cast<uint8_t>(sub);
						AuctionSearch(0);
						sfx::Play(sfx::Id::Click, 0.4f);
					}
					ty += 25.f;
				}
		}

		// ── 위: 거르기 두 줄 — 이름 · 정렬 / 희귀도 · 각인 · 착용 레벨 · 옵션(누르면 다음, 오른쪽 클릭은 앞) ──
		const float fx = tree.x + tree.width + 12.f, fw = content.x + content.width - fx;
		m_auction.query.maxBytes = AuctionRule::kQueryBytes - 1;
		const bool submit = ui::InputText(Rectangle{ fx, content.y, 250.f, 32.f }, m_auction.query, "이름으로 찾기");
		auto cycle = [&](Rectangle b, const char* label, uint8_t& v, int n)
		{
			if (ui::Button(b, label, ui::ButtonKind::Normal, true, 13.f)) { v = static_cast<uint8_t>((v + 1) % n); AuctionSearch(0); }
			if (ui::Clicked(b, MOUSE_BUTTON_RIGHT)) { v = static_cast<uint8_t>((v + n - 1) % n); AuctionSearch(0); }
		};
		if (ui::Button(Rectangle{ fx + 258.f, content.y, 72.f, 32.f }, "찾기", ui::ButtonKind::Primary, true, 14.f) || submit)
		{
			ui::CommitComposition(m_auction.query);
			AuctionSearch(0);
		}
		cycle(Rectangle{ fx + fw - 120.f, content.y, 120.f, 32.f }, kSortName[std::min<int>(m_auction.sort, 3)], m_auction.sort, 4);
		const float fy = content.y + 38.f;
		cycle(Rectangle{ fx, fy, 112.f, 30.f }, kRarityFilter[std::min<int>(m_auction.minRarity, 5)], m_auction.minRarity, 6);
		const std::string ench = m_auction.minEnchant == 0 ? std::string("각인 전체") : Fmt("각인 +%d 이상", m_auction.minEnchant);
		cycle(Rectangle{ fx + 118.f, fy, 104.f, 30.f }, ench.c_str(), m_auction.minEnchant, 8);
		static const char* kLevelName[] = { "착용 레벨 전체", "레벨 1~10", "레벨 11~20", "레벨 21~30", "레벨 31~40", "레벨 41 이상" };
		cycle(Rectangle{ fx + 228.f, fy, 118.f, 30.f }, kLevelName[std::min<int>(m_auction.levelBand, 5)], m_auction.levelBand, 6);
		static const ItemOption kOptions[] = { ItemOption::None, ItemOption::Attack, ItemOption::Magic, ItemOption::Armor, ItemOption::MaxHp,
			ItemOption::CritChance, ItemOption::CritDamage, ItemOption::AttackSpeed, ItemOption::MoveSpeed, ItemOption::Str, ItemOption::Dex, ItemOption::Int };
		const int nOpt = static_cast<int>(sizeof(kOptions) / sizeof(kOptions[0]));
		const std::string optLabel = m_auction.optionIdx == 0 ? std::string("옵션 전체") : std::string(ItemRules::Option(kOptions[std::min<int>(m_auction.optionIdx, nOpt - 1)]).name) + " 포함";
		cycle(Rectangle{ fx + 352.f, fy, 138.f, 30.f }, optLabel.c_str(), m_auction.optionIdx, nOpt);
		m_auction.option = static_cast<uint8_t>(kOptions[std::min<int>(m_auction.optionIdx, nOpt - 1)]);

		// ── 결과: 6줄 ──
		const float rowH = 56.f;
		const float ry0 = content.y + 76.f;
		if (!m_auction.searched && m_auction.rows.empty())
			AuctionSearch(0);
		for (size_t i = 0; i < m_auction.rows.size(); ++i)
		{
			const AuctionRow& row = m_auction.rows[i];
			// 결과가 바뀌면 줄이 차례로 떠오른다(0.03초 간격)
			const float appear = util::EaseOutCubic(static_cast<float>((m_now - m_auction.rowsAt - 0.03 * static_cast<double>(i)) / 0.18));
			const Rectangle rr{ fx, ry0 + static_cast<float>(i) * rowH + (1.f - appear) * 8.f, fw, rowH - 4.f };
			const bool hover = ui::Hovered(rr);
			const bool mine = (row.flags & 1) != 0, locked = (row.flags & 2) != 0, won = (row.flags & 4) != 0;
			const float stamp = std::max(0.f, 1.f - static_cast<float>(m_now - m_auction.lastBoughtAt) / 1.4f);
			ui::Plate(rr, util::MulA(hover ? ui::theme::kSurface2 : ui::theme::kSurface1, appear), mine ? util::WithA(ui::theme::kDawn, 0.45f) : ui::theme::kLine, 3.f, 1.f);
			rowItem(row, rr, false);
			// 값: 모두 · 개당
			const float px = rr.x + rr.width - 118.f;
			ui::Text(texts::Number(row.price), Vector2{ px, rr.y + 7.f }, ui::type::kBody + 1.f, ui::theme::kGold, ui::Align::Right);
			if (row.count > 1)
				ui::Text(Fmt("개당 %s", texts::Number(row.price / std::max<uint16_t>(1, row.count)).c_str()), Vector2{ px, rr.y + 28.f }, ui::type::kCaption, ui::theme::kTextDim, ui::Align::Right);
			ui::Text(LeftText(row.minutesLeft), Vector2{ px - 124.f, rr.y + 9.f }, ui::type::kCaption, row.minutesLeft < 60 ? ui::theme::kWarn : ui::theme::kTextDim, ui::Align::Right);
			const Rectangle buy{ rr.x + rr.width - 104.f, rr.y + 10.f, 96.f, 34.f };
			if (won) ui::Text("낙찰", Vector2{ buy.x + buy.width * 0.5f, buy.y + 8.f }, ui::type::kBody, ui::theme::kEmber, ui::Align::Center);
			else if (mine) ui::Text("내 물건", Vector2{ buy.x + buy.width * 0.5f, buy.y + 9.f }, ui::type::kCaption, ui::theme::kDawn, ui::Align::Center);
			else if (locked) ui::Text("거래 중", Vector2{ buy.x + buy.width * 0.5f, buy.y + 9.f }, ui::type::kCaption, ui::theme::kWarn, ui::Align::Center);
			else if (ui::Button(buy, "사기", ui::ButtonKind::Primary, inTown && !busy && m_inv.adena >= row.price, 14.f))
			{
				m_auction.confirmId = row.id;
				m_auction.confirmRow = row;
				sfx::Play(sfx::Id::Click, 0.5f);
			}
			if (row.id == m_auction.lastBoughtId && stamp > 0.f)
			{
				// 낙찰 도장: 줄 위에 붉은 "낙찰"이 찍혔다가 흐려진다
				const float k = 1.f + 0.6f * util::EaseInCubic(stamp);
				ui::TextStyle st; st.size = 24.f * k; st.color = util::WithA(ui::theme::kEmber, stamp); st.face = ui::Face::Display; st.outline = 1.5f; st.outlineColor = util::WithA(ui::theme::kHpBack, stamp * 0.8f);
				ui::Fonts().DrawCentered("낙찰", Vector2{ rr.x + rr.width * 0.55f, rr.y + rr.height * 0.5f }, st);
			}
		}
		if (m_auction.searched && m_auction.rows.empty())
			ui::Text(loading(0) ? "찾는 중…" : "찾는 물건이 없다. 분류나 거르기를 바꿔 보자.", Vector2{ fx + fw * 0.5f, ry0 + 120.f }, ui::type::kBody, ui::theme::kTextMute, ui::Align::Center);
		// 페이지
		const float py = content.y + content.height - 30.f;
		ui::Text(Fmt("%s건", texts::Number(m_auction.total).c_str()), Vector2{ fx, py + 6.f }, ui::type::kCaption, ui::theme::kTextDim);
		if (ui::Button(Rectangle{ fx + fw * 0.5f - 90.f, py, 36.f, 28.f }, "◀", ui::ButtonKind::Ghost, m_auction.page > 0, 14.f)) AuctionSearch(static_cast<uint16_t>(m_auction.page - 1));
		ui::Text(Fmt("%d / %d", m_auction.page + 1, m_auction.pages), Vector2{ fx + fw * 0.5f, py + 6.f }, ui::type::kBodyS, ui::theme::kTextDim, ui::Align::Center);
		if (ui::Button(Rectangle{ fx + fw * 0.5f + 54.f, py, 36.f, 28.f }, "▶", ui::ButtonKind::Ghost, m_auction.page + 1 < m_auction.pages, 14.f)) AuctionSearch(static_cast<uint16_t>(m_auction.page + 1));
	}
	else if (m_auction.tab == 1)
	{
		// ── 판매 등록: 왼쪽 가방(올릴 수 있는 것) · 오른쪽 값 매기기 ──
		const Rectangle bag{ content.x, content.y, 330.f, content.height };
		ui::SectionHeader(Vector2{ bag.x, bag.y + 2.f }, bag.width, "가방 — 올릴 물건");
		const float cs = 50.f;
		int shown = 0;
		for (int i = 0; i < MAX_INVENTORY_SLOTS; ++i)
		{
			const InvSlot& s = m_inv.slots[static_cast<size_t>(i)];
			if (s.Empty() || s.equipped) continue;
			const ItemTableRow* row = ItemTableData::Find(s.itemId);
			if (!row || !AuctionRule::Listable(*row)) continue;
			const Rectangle c{ bag.x + static_cast<float>(shown % 6) * (cs + 4.f), bag.y + 26.f + static_cast<float>(shown / 6) * (cs + 4.f), cs, cs };
			if (c.y + cs > bag.y + bag.height) break;
			++shown;
			const bool sel = m_auction.regSlot == i, hover = ui::Hovered(c);
			ui::SlotFrame(c, hover, sel, icons::RarityColor(SlotRarity(s)));
			icons::Draw(s.itemId, Rectangle{ c.x + 4.f, c.y + 4.f, cs - 8.f, cs - 8.f });
			if (s.count > 1) ui::Text(std::to_string(s.count), Vector2{ c.x + cs - 4.f, c.y + cs - 16.f }, ui::type::kMicro, ui::theme::kTextHi, ui::Align::Right, true);
			if (hover) DrawItemTooltip(s, TipMode::Auction, i);
			if (ui::Clicked(c))
			{
				m_auction.regSlot = i;
				m_auction.regCount = s.count;
				m_auction.hintItem = s.itemId;
				m_auction.hintKnown = false;
				AUCTION_REQ_PACKET q{};
				q.op = static_cast<uint8_t>(AuctionOp::PriceHint);
				q.itemId = s.itemId;
				SendAuctionReq(q);
				sfx::Play(sfx::Id::Click, 0.4f);
			}
		}
		if (shown == 0)
			ui::TextWrapped("올릴 수 있는 물건이 없다. 입고 있는 장비는 벗어야 올릴 수 있다.", Rectangle{ bag.x + 8.f, bag.y + 40.f, bag.width - 16.f, 60.f }, ui::type::kBodyS, ui::theme::kTextMute, 4.f);

		const float x0 = bag.x + bag.width + 24.f, w0 = content.x + content.width - x0;
		// 소지품 창에서 끌어 놓아도 고른다(가방 칸 = 드래그 종류 1)
		if (ui::Dragging() && ui::Drag().kind == 1 && ui::DragReleased() && ui::Hovered(Rectangle{ x0, content.y, w0, content.height }))
		{
			const int idx = ui::Drag().index;
			ui::EndDrag();
			if (idx >= 0 && idx < MAX_INVENTORY_SLOTS && !m_inv.slots[static_cast<size_t>(idx)].Empty() && !m_inv.slots[static_cast<size_t>(idx)].equipped)
			{
				m_auction.regSlot = idx;
				m_auction.regCount = m_inv.slots[static_cast<size_t>(idx)].count;
				m_auction.hintItem = m_inv.slots[static_cast<size_t>(idx)].itemId;
				m_auction.hintKnown = false;
				AUCTION_REQ_PACKET q{};
				q.op = static_cast<uint8_t>(AuctionOp::PriceHint);
				q.itemId = m_auction.hintItem;
				SendAuctionReq(q);
				sfx::Play(sfx::Id::Drop, 0.5f);
			}
		}
		if (ui::Dragging() && ui::Drag().kind == 1)
			ui::Plate(Rectangle{ x0, content.y + 24.f, w0, content.height - 24.f }, util::WithA(ui::theme::kDawn, 0.06f), ui::theme::kDawn, 4.f, 1.f);
		ui::Text(Fmt("올린 물건 %u / %d", m_auction.mineTotal, AuctionRule::kMaxListingsPerSeller), Vector2{ x0 + w0, content.y + 2.f }, ui::type::kCaption, m_auction.mineTotal >= AuctionRule::kMaxListingsPerSeller ? ui::theme::kWarn : ui::theme::kTextDim, ui::Align::Right);
		const InvSlot* sel = m_auction.regSlot >= 0 ? &m_inv.slots[static_cast<size_t>(m_auction.regSlot)] : nullptr;
		if (sel && (sel->Empty() || sel->equipped)) { m_auction.regSlot = -1; sel = nullptr; }
		if (!sel)
		{
			ui::Text("왼쪽에서 올릴 물건을 고르세요", Vector2{ x0 + w0 * 0.5f, content.y + 150.f }, ui::type::kBody, ui::theme::kTextMute, ui::Align::Center);
			ui::TextWrapped("값의 1%를 보증금으로 내고(돌려받지 않는다), 팔리면 5%를 뗀 나머지가 정산함으로 들어온다. 기간이 끝나거나 내리면 물건이 정산함으로 돌아온다.",
				Rectangle{ x0 + 20.f, content.y + 190.f, w0 - 40.f, 80.f }, ui::type::kBodyS, ui::theme::kTextDim, 5.f);
		}
		else
		{
			const ItemTableRow* row = ItemTableData::Find(sel->itemId);
			const Rectangle card{ x0, content.y + 24.f, w0, 76.f };
			ui::Plate(card, ui::theme::kSurface1, ui::theme::kLine, 3.f, 1.f);
			AuctionRow preview{};
			preview.itemId = sel->itemId;
			preview.count = static_cast<uint16_t>(m_auction.regCount);
			preview.enchant = sel->enchant;
			preview.extra = sel->extra;
			rowItem(preview, card, false);
			float y = card.y + card.height + 14.f;
			// 수량(겹치는 물건)
			if (row && row->maxStack > 1)
			{
				ui::Text("수량", Vector2{ x0, y + 8.f }, ui::type::kBodyS, ui::theme::kTextDim);
				if (ui::Button(Rectangle{ x0 + 70.f, y, 34.f, 32.f }, "−", ui::ButtonKind::Ghost, m_auction.regCount > 1, 16.f)) --m_auction.regCount;
				ui::Text(std::to_string(m_auction.regCount), Vector2{ x0 + 134.f, y + 7.f }, ui::type::kBody, ui::theme::kTextHi, ui::Align::Center);
				if (ui::Button(Rectangle{ x0 + 164.f, y, 34.f, 32.f }, "+", ui::ButtonKind::Ghost, m_auction.regCount < sel->count, 16.f)) ++m_auction.regCount;
				if (ui::Button(Rectangle{ x0 + 206.f, y, 60.f, 32.f }, "모두", ui::ButtonKind::Ghost, true, 13.f)) m_auction.regCount = sel->count;
				y += 44.f;
			}
			m_auction.regCount = std::clamp(m_auction.regCount, 1, static_cast<int>(sel->count));
			// 값
			ui::Text("값(모두)", Vector2{ x0, y + 9.f }, ui::type::kBodyS, ui::theme::kTextDim);
			m_auction.regPrice.maxBytes = 10;
			m_auction.regPrice.asciiOnly = true;
			(void)ui::InputText(Rectangle{ x0 + 70.f, y, 200.f, 34.f }, m_auction.regPrice, "그로시");
			const uint32_t price = ParsePrice(m_auction.regPrice.text);
			ui::Text(texts::Number(price) + " 그로시", Vector2{ x0 + 280.f, y + 9.f }, ui::type::kBodyS, price > 0 ? ui::theme::kGold : ui::theme::kTextMute);
			y += 44.f;
			// 시세 힌트
			if (m_auction.hintKnown)
			{
				std::string h = m_auction.hintLowest > 0 ? Fmt("지금 최저 개당 %s", texts::Number(m_auction.hintLowest).c_str()) : std::string("지금 올라온 같은 물건 없음");
				if (m_auction.hintCount > 0) h += Fmt("  ·  최근 거래 %s (평균 %s, %d건)", texts::Number(m_auction.hintLast).c_str(), texts::Number(m_auction.hintAvg).c_str(), m_auction.hintCount);
				ui::Text(h, Vector2{ x0, y }, ui::type::kCaption, ui::theme::kOldway);
				const uint32_t suggest = m_auction.hintLowest > 0 ? m_auction.hintLowest : m_auction.hintAvg;
				if (suggest > 0 && ui::Button(Rectangle{ x0 + w0 - 128.f, y - 6.f, 128.f, 26.f }, "시세대로 채우기", ui::ButtonKind::Ghost, true, 12.5f))
					m_auction.regPrice.text = std::to_string(static_cast<uint64_t>(suggest) * static_cast<uint64_t>(m_auction.regCount));
			}
			else
				ui::Text("시세를 불러오는 중…", Vector2{ x0, y }, ui::type::kCaption, ui::theme::kTextMute);
			y += 30.f;
			// 기간
			ui::Text("기간", Vector2{ x0, y + 8.f }, ui::type::kBodyS, ui::theme::kTextDim);
			for (int d = 0; d < AuctionRule::kDurationCount; ++d)
			{
				const Rectangle b{ x0 + 70.f + d * 86.f, y, 80.f, 32.f };
				if (ui::Button(b, Fmt("%d시간", AuctionRule::kDurationHours[d]).c_str(), m_auction.regDuration == d ? ui::ButtonKind::Primary : ui::ButtonKind::Ghost, true, 13.5f))
					m_auction.regDuration = d;
			}
			y += 46.f;
			// 계산
			const bool priceOk = AuctionRule::ValidPrice(price);
			const uint32_t deposit = priceOk ? AuctionRule::Deposit(price) : 0;
			const uint32_t gets = priceOk ? AuctionRule::SellerGets(price) : 0;
			ui::Rule(x0, x0 + w0, y);
			y += 8.f;
			ui::Text("보증금(1%, 돌려받지 않음)", Vector2{ x0, y }, ui::type::kBodyS, ui::theme::kTextDim);
			ui::Text(texts::Number(deposit), Vector2{ x0 + w0, y }, ui::type::kBodyS, m_inv.adena >= deposit ? ui::theme::kText : ui::theme::kBad, ui::Align::Right);
			y += 22.f;
			ui::Text("팔리면 받을 돈(수수료 5% 뺌)", Vector2{ x0, y }, ui::type::kBodyS, ui::theme::kTextDim);
			ui::Text(texts::Number(gets), Vector2{ x0 + w0, y }, ui::type::kBody, ui::theme::kGold, ui::Align::Right);
			y += 30.f;
			const bool can = inTown && !busy && priceOk && m_inv.adena >= deposit && m_auction.mineTotal < static_cast<uint32_t>(AuctionRule::kMaxListingsPerSeller);
			if (!priceOk && !m_auction.regPrice.text.empty())
				ui::Text("값은 10 ~ 100,000,000 그로시", Vector2{ x0, y + 10.f }, ui::type::kCaption, ui::theme::kWarn);
			if (ui::Button(Rectangle{ x0 + w0 - 170.f, y, 170.f, 40.f }, "경매에 올리기", ui::ButtonKind::Primary, can, 15.f))
			{
				AUCTION_REQ_PACKET q{};
				q.op = static_cast<uint8_t>(AuctionOp::Register);
				q.slot = static_cast<uint16_t>(m_auction.regSlot);
				q.count = static_cast<uint16_t>(m_auction.regCount);
				q.price = price;
				q.duration = static_cast<uint8_t>(m_auction.regDuration);
				q.itemId = sel->itemId;
				SendAuctionReq(q);
			}
		}
	}
	else if (m_auction.tab == 2)
	{
		// ── 내 경매 ──
		const float rowH = 62.f;
		for (size_t i = 0; i < m_auction.mine.size(); ++i)
		{
			const AuctionRow& row = m_auction.mine[i];
			const Rectangle rr{ content.x, content.y + static_cast<float>(i) * rowH, content.width, rowH - 4.f };
			ui::Plate(rr, ui::Hovered(rr) ? ui::theme::kSurface2 : ui::theme::kSurface1, ui::theme::kLine, 3.f, 1.f);
			rowItem(row, rr, false);
			const bool locked = (row.flags & 2) != 0;
			ui::Text(texts::Number(row.price), Vector2{ rr.x + rr.width - 250.f, rr.y + 10.f }, ui::type::kBody + 1.f, ui::theme::kGold, ui::Align::Right);
			ui::Text(Fmt("받을 돈 %s", texts::Number(AuctionRule::SellerGets(row.price)).c_str()), Vector2{ rr.x + rr.width - 250.f, rr.y + 32.f }, ui::type::kCaption, ui::theme::kTextDim, ui::Align::Right);
			ui::Text(locked ? std::string("거래 중") : LeftText(row.minutesLeft), Vector2{ rr.x + rr.width - 130.f, rr.y + 20.f }, ui::type::kCaption, locked ? ui::theme::kWarn : ui::theme::kTextDim, ui::Align::Right);
			const bool armed = m_guild.armKind == 9 && m_guild.armTarget == std::to_string(row.id) && m_now - m_guild.armAt < 3.0;
			if (ui::Button(Rectangle{ rr.x + rr.width - 110.f, rr.y + 12.f, 100.f, 34.f }, armed ? "내릴까요?" : "내리기", ui::ButtonKind::Danger, !locked && inTown && !busy, 13.5f))
			{
				if (armed)
				{
					AUCTION_REQ_PACKET q{};
					q.op = static_cast<uint8_t>(AuctionOp::Cancel);
					q.id = row.id;
					SendAuctionReq(q);
					m_guild.armKind = 0;
				}
				else { m_guild.armKind = 9; m_guild.armTarget = std::to_string(row.id); m_guild.armAt = m_now; }
			}
		}
		if (m_auction.mine.empty())
			ui::Text(loading(1) ? "불러오는 중…" : "올린 물건이 없다.", Vector2{ content.x + content.width * 0.5f, content.y + 140.f }, ui::type::kBody, ui::theme::kTextMute, ui::Align::Center);
		const float py = content.y + content.height - 30.f;
		ui::Text(Fmt("올린 물건 %u / %d", m_auction.mineTotal, AuctionRule::kMaxListingsPerSeller), Vector2{ content.x, py + 6.f }, ui::type::kCaption, ui::theme::kTextDim);
		if (m_auction.minePages > 1)
		{
			AUCTION_REQ_PACKET q{};
			q.op = static_cast<uint8_t>(AuctionOp::Mine);
			if (ui::Button(Rectangle{ content.x + content.width * 0.5f - 90.f, py, 36.f, 28.f }, "◀", ui::ButtonKind::Ghost, m_auction.minePage > 0, 14.f)) { q.page = static_cast<uint16_t>(m_auction.minePage - 1); SendAuctionReq(q); }
			ui::Text(Fmt("%d / %d", m_auction.minePage + 1, m_auction.minePages), Vector2{ content.x + content.width * 0.5f, py + 6.f }, ui::type::kBodyS, ui::theme::kTextDim, ui::Align::Center);
			if (ui::Button(Rectangle{ content.x + content.width * 0.5f + 54.f, py, 36.f, 28.f }, "▶", ui::ButtonKind::Ghost, m_auction.minePage + 1 < m_auction.minePages, 14.f)) { q.page = static_cast<uint16_t>(m_auction.minePage + 1); SendAuctionReq(q); }
		}
	}
	else
	{
		// ── 정산함 ──
		const float rowH = 62.f;
		for (size_t i = 0; i < m_auction.settles.size(); ++i)
		{
			const AuctionRow& row = m_auction.settles[i];
			const Rectangle rr{ content.x, content.y + static_cast<float>(i) * rowH, content.width, rowH - 4.f };
			ui::Plate(rr, ui::Hovered(rr) ? ui::theme::kSurface2 : ui::theme::kSurface1, row.itemId == 0 ? util::WithA(ui::theme::kGold, 0.35f) : ui::theme::kLine, 3.f, 1.f);
			rowItem(row, rr, true);
			const bool claiming = (row.flags & 2) != 0;
			if (ui::Button(Rectangle{ rr.x + rr.width - 110.f, rr.y + 12.f, 100.f, 34.f }, claiming ? "받는 중" : "받기", ui::ButtonKind::Primary, !claiming && inTown && !busy && !m_auction.claimAll, 14.f))
			{
				AUCTION_REQ_PACKET q{};
				q.op = static_cast<uint8_t>(AuctionOp::Claim);
				q.id = row.id;
				SendAuctionReq(q);
			}
		}
		if (m_auction.settles.empty())
			ui::Text(loading(2) ? "불러오는 중…" : "정산함이 비어 있다.", Vector2{ content.x + content.width * 0.5f, content.y + 140.f }, ui::type::kBody, ui::theme::kTextMute, ui::Align::Center);
		const float py = content.y + content.height - 34.f;
		ui::Text(Fmt("받지 않은 것 %u줄", m_auction.settleTotal), Vector2{ content.x, py + 9.f }, ui::type::kCaption, ui::theme::kTextDim);
		if (ui::Button(Rectangle{ content.x + content.width - 150.f, py, 150.f, 34.f }, m_auction.claimAll ? "받는 중…" : "모두 받기", ui::ButtonKind::Primary,
			!m_auction.settles.empty() && inTown && !busy && !m_auction.claimAll, 14.f))
		{
			m_auction.claimAll = true;
			AUCTION_REQ_PACKET q{};
			q.op = static_cast<uint8_t>(AuctionOp::Claim);
			q.id = m_auction.settles.front().id;
			SendAuctionReq(q);
		}
		if (m_auction.settlePages > 1)
		{
			AUCTION_REQ_PACKET q{};
			q.op = static_cast<uint8_t>(AuctionOp::Settlements);
			if (ui::Button(Rectangle{ content.x + content.width * 0.5f - 90.f, py + 3.f, 36.f, 28.f }, "◀", ui::ButtonKind::Ghost, m_auction.settlePage > 0, 14.f)) { q.page = static_cast<uint16_t>(m_auction.settlePage - 1); SendAuctionReq(q); }
			ui::Text(Fmt("%d / %d", m_auction.settlePage + 1, m_auction.settlePages), Vector2{ content.x + content.width * 0.5f, py + 9.f }, ui::type::kBodyS, ui::theme::kTextDim, ui::Align::Center);
			if (ui::Button(Rectangle{ content.x + content.width * 0.5f + 54.f, py + 3.f, 36.f, 28.f }, "▶", ui::ButtonKind::Ghost, m_auction.settlePage + 1 < m_auction.settlePages, 14.f)) { q.page = static_cast<uint16_t>(m_auction.settlePage + 1); SendAuctionReq(q); }
		}
	}
}

void Game::DrawAuctionBuyConfirm()
{
	if (m_auction.confirmId == 0) return;
	const AuctionRow& row = m_auction.confirmRow;
	const float W = m_uiW, H = m_uiH;
	const Rectangle r = wm::Window("auctionbuy", Rectangle{ std::round(W * 0.5f - 200.f), std::round(H * 0.5f - 100.f), 400.f, 196.f }, wm::kNoAnim);
	bool open = true;
	ui::Window(r, "구매 확인", &open);
	if (!open) { m_auction.confirmId = 0; return; }
	const Rectangle ic{ r.x + 20.f, r.y + 54.f, 52.f, 52.f };
	ui::SlotFrame(ic, false, false);
	icons::Draw(row.itemId, Rectangle{ ic.x + 4.f, ic.y + 4.f, 44.f, 44.f });
	ui::Text(texts::ItemNameFull(row.itemId, row.enchant, row.extra) + (row.count > 1 ? Fmt(" ×%d", row.count) : std::string()),
		Vector2{ ic.x + 64.f, ic.y + 4.f }, ui::type::kBody, icons::ItemNameColor(row.itemId, row.extra));
	ui::Text(Fmt("%s 그로시에 살까요?", texts::Number(row.price).c_str()), Vector2{ ic.x + 64.f, ic.y + 28.f }, ui::type::kBodyS, ui::theme::kGold);
	ui::Text("산 물건은 정산함으로 들어간다.", Vector2{ r.x + 20.f, r.y + 118.f }, ui::type::kCaption, ui::theme::kTextDim);
	const bool busy = m_auction.pendingSeq != 0 && m_now - m_auction.pendingAt < 4.0;
	if (ui::Button(Rectangle{ r.x + r.width - 200.f, r.y + r.height - 50.f, 90.f, 36.f }, "사기", ui::ButtonKind::Primary, !busy && m_inv.adena >= row.price, 14.f))
	{
		AUCTION_REQ_PACKET q{};
		q.op = static_cast<uint8_t>(AuctionOp::Buy);
		q.id = row.id;
		q.price = row.price;
		SendAuctionReq(q);
		m_auction.confirmId = 0;
	}
	if (ui::Button(Rectangle{ r.x + r.width - 104.f, r.y + r.height - 50.f, 90.f, 36.f }, "그만두기", ui::ButtonKind::Ghost, true, 14.f))
		m_auction.confirmId = 0;
}
