#include "pch.h"
#include "Content/AuctionService.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "Data/GuildAuction.h"
#include "Data/ItemRules.h"
#include "Data/WorldData.h"
#include "Item/ItemTable.h"
#include "Npc/NpcHelper.h"
#include "Thread/DBThread.h"

std::shared_ptr<AuctionService> GAuctionService;

namespace
{
	constexpr uint32_t kMaxAdena = 2'000'000'000u;   // Inventory::AddAdena와 같은 상한(DB INT)
	constexpr int64_t  kPriceLookbackSec = 14 * 24 * 3600;

	[[nodiscard]] AuctionResult FromTake(Inventory::TakeResult r) noexcept
	{
		switch (r)
		{
		case Inventory::TakeResult::Ok:       return AuctionResult::Ok;
		case Inventory::TakeResult::Equipped: return AuctionResult::Equipped;
		default:                              return AuctionResult::BadItem;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 서비스 뼈대 · 불러오기
// ─────────────────────────────────────────────────────────────────────────────
AuctionService::AuctionService() = default;

void AuctionService::Run()
{
	Service::LServiceId = Service::kAuctionServiceId;
	_loop.Run();
}

void AuctionService::Post(Task task)
{
	_loop.Enqueue(std::move(task));
}

uint32_t AuctionService::HourSec() noexcept
{
	static const uint32_t sec = []()
	{
		if (const char* v = std::getenv("HARVEN_AUCTION_HOUR_SEC"))
		{
			const long n = std::atol(v);
			if (n >= 1 && n <= 86400) return static_cast<uint32_t>(n);
		}
		return AuctionRule::kHourSecDefault;
	}();
	return sec;
}

size_t AuctionService::SettlementCount() const noexcept
{
	size_t n = 0;
	for (const auto& [pid, v] : _settlements) n += v.size();
	return n;
}

void AuctionService::Index(Listing& l) const
{
	const ItemTableRow* row = ItemTable::Find(l.row.itemId);
	l.unit = l.row.price / std::max<uint32_t>(1, l.row.count);
	if (row == nullptr)
		return;
	l.cat = static_cast<uint8_t>(AuctionRule::CategoryOf(*row));
	l.sub = static_cast<uint8_t>(AuctionRule::SubOf(*row));
	l.rarity = static_cast<uint8_t>(ItemRules::EffectiveRarity(*row, l.row.extra));
	l.reqLevel = row->reqLevel;
	l.name = row->displayName != nullptr ? row->displayName : "";
}

bool AuctionService::LoadFromDB(DBConnection& db, int64_t maxListingId, int64_t maxSettlementId)
{
	std::vector<DB_LISTING_ROW> listings;
	std::vector<DB_SETTLEMENT_ROW> settles;
	std::vector<DB_PRICE_ROW> prices;
	if (!db.LoadAuctionListings(listings) || !db.LoadAuctionSettlements(settles)
		|| !db.LoadAuctionPrices(prices, Service::UnixNow() - kPriceLookbackSec))
		return false;
	for (const auto& r : listings)
	{
		if (r.status != 0)
			continue;
		Listing l;
		l.row = r;
		Index(l);
		++_perSeller[r.sellerId];
		_byCat[l.cat].insert(static_cast<uint32_t>(r.listingId));
		_listings.emplace(static_cast<uint32_t>(r.listingId), std::move(l));
	}
	for (const auto& r : settles)
	{
		Settlement s;
		s.row = r;
		_settlements[r.playerId].push_back(std::move(s));
	}
	for (auto& [pid, v] : _settlements)
		std::sort(v.begin(), v.end(), [](const Settlement& a, const Settlement& b) { return a.row.settlementId < b.row.settlementId; });
	std::sort(prices.begin(), prices.end(), [](const DB_PRICE_ROW& a, const DB_PRICE_ROW& b) { return a.soldAt < b.soldAt; });
	for (const auto& p : prices)
		RecordPrice(p.itemId, p.price);
	_nextListingId = static_cast<uint32_t>(std::max<int64_t>(maxListingId, 0)) + 1;
	_nextSettlementId = std::max<int64_t>(maxSettlementId, 0) + 1;
	return true;
}

uint16_t AuctionService::MinutesLeft(const DB_LISTING_ROW& r, int64_t now) const noexcept
{
	// 표시용 "게임 분" — 기간(시간) 단위와 같은 배율(시험에서 한 시간을 몇 초로 줄여도 12 · 24 · 48시간으로 보인다)
	const int64_t left = r.expiresAt - now;
	if (left <= 0)
		return 0;
	const int64_t minutes = left * 60 / HourSec();
	return static_cast<uint16_t>(std::clamp<int64_t>(minutes, 1, 65535));
}

int AuctionService::UnclaimedCount(int playerId) const
{
	const auto it = _settlements.find(playerId);
	return it == _settlements.end() ? 0 : static_cast<int>(it->second.size());
}

void AuctionService::RecordPrice(uint16_t itemId, uint32_t unit)
{
	auto& q = _prices[itemId];
	q.push_back(unit);
	while (q.size() > static_cast<size_t>(AuctionRule::kPriceHistory))
		q.pop_front();
}

// ─────────────────────────────────────────────────────────────────────────────
// 보내기
// ─────────────────────────────────────────────────────────────────────────────
void AuctionService::Ack(const Service::Ctx& ctx, AuctionOp op, AuctionResult r, uint32_t seq, uint32_t id,
	uint32_t adena, int32_t delta, uint16_t itemId, uint16_t count) const
{
	AUCTION_ACK_PACKET p;
	Service::Init(p, PacketType::AUCTION_ACK);
	p.op = static_cast<uint8_t>(op);
	p.result = static_cast<uint8_t>(r);
	p.seq = seq;
	p.id = id;
	p.adena = adena;
	p.delta = delta;
	p.itemId = itemId;
	p.count = count;
	Service::SendTo(ctx.session, p);
}

void AuctionService::SendTo(int playerId, const AUCTION_NFY_PACKET& p) const
{
	const auto it = _online.find(playerId);
	if (it != _online.end())
		Service::SendTo(it->second.session, p);
}

void AuctionService::NotifySettleCount(int playerId) const
{
	AUCTION_NFY_PACKET p;
	Service::Init(p, PacketType::AUCTION_NFY);
	p.event = static_cast<uint8_t>(AuctionEvent::SettleCount);
	p.settleCount = static_cast<uint16_t>(UnclaimedCount(playerId));
	SendTo(playerId, p);
}

// ─────────────────────────────────────────────────────────────────────────────
// 접속 · 틱
// ─────────────────────────────────────────────────────────────────────────────
void AuctionService::OnLogin(const Service::Ctx& ctx)
{
	_online[ctx.playerId] = Online{ ctx.obj, ctx.session };
	NotifySettleCount(ctx.playerId);   // 0이어도 보낸다(메뉴 막대 점 초기화)
}

void AuctionService::OnLogout(int playerId)
{
	_online.erase(playerId);
}

void AuctionService::Tick()
{
	// 만료 확인: 기본 1분마다. 시험 배율(한 시간을 짧게)에서는 "게임 1분"마다(최소 1초)
	const uint32_t now = GetNowTime();
	const uint32_t interval = std::clamp<uint32_t>(HourSec() * 1000u / 60u, 1000u, AuctionRule::kExpireTickMs);
	if (_lastExpireAt != 0 && now - _lastExpireAt < interval)
		return;
	_lastExpireAt = now;
	const int64_t unixNow = Service::UnixNow();
	int budget = 64;   // 한 번에 몰리지 않게(남은 것은 다음 틱)
	std::vector<uint32_t> due;
	for (const auto& [id, l] : _listings)
	{
		if (l.state == State::Selling && l.row.expiresAt <= unixNow)
		{
			due.push_back(id);
			if (--budget == 0) break;
		}
	}
	for (const uint32_t id : due)
		Close(id, 3, std::nullopt, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 요청(서비스 스레드)
// ─────────────────────────────────────────────────────────────────────────────
void AuctionService::Handle(const Service::Ctx& ctx, const AUCTION_REQ_PACKET& p)
{
	switch (static_cast<AuctionOp>(p.op))
	{
	case AuctionOp::Search:      Search(ctx, p); break;
	case AuctionOp::Mine:        SendMine(ctx, p.page); break;
	case AuctionOp::Settlements: SendSettlements(ctx, p.page); break;
	case AuctionOp::PriceHint:   PriceHint(ctx, p.itemId, p.minRarity); break;
	case AuctionOp::Register:    Register(ctx, p); break;
	case AuctionOp::Buy:         Buy(ctx, p.seq, p.id, p.price); break;
	case AuctionOp::Cancel:      Cancel(ctx, p.seq, p.id); break;
	case AuctionOp::Claim:       Claim(ctx, p.seq, p.id); break;
	default:                     Ack(ctx, static_cast<AuctionOp>(p.op), AuctionResult::Invalid, p.seq); break;
	}
}

void AuctionService::Search(const Service::Ctx& ctx, const AUCTION_REQ_PACKET& p) const
{
	char query[sizeof(p.query) + 1]{};
	::memcpy(query, p.query, sizeof(p.query));
	const size_t qlen = ::strnlen(query, sizeof(query));
	const uint8_t wantOption = p.option;

	std::vector<const Listing*> hits;
	// 분류를 골랐으면 그 분류의 색인만 훑는다(전체면 모두)
	auto consider = [&](const Listing& l)
	{
		if (l.state == State::Closing) return;
		if (p.sub != 0 && l.sub != p.sub) return;
		if (l.rarity < p.minRarity) return;
		if (l.row.enchant < p.minEnchant) return;
		if (l.reqLevel < p.minLevel) return;
		if (p.maxLevel != 0 && l.reqLevel > p.maxLevel) return;
		if (wantOption != 0)
		{
			const ItemInstance inst = ItemInstance::Unpack(l.row.extra);
			bool has = false;
			for (const auto& o : inst.opts) if (static_cast<uint8_t>(o.kind) == wantOption) has = true;
			if (!has) return;
		}
		if (qlen > 0 && std::strstr(l.name, query) == nullptr) return;
		hits.push_back(&l);
	};
	if (p.cat != 0 && p.cat < static_cast<uint8_t>(AuctionRule::Cat::Count))
	{
		for (const uint32_t id : _byCat[p.cat])
			if (const auto it = _listings.find(id); it != _listings.end()) consider(it->second);
	}
	else
		for (const auto& [id, l] : _listings) consider(l);
	switch (static_cast<AuctionRule::Sort>(p.sort))
	{
	case AuctionRule::Sort::PriceDesc:
		std::sort(hits.begin(), hits.end(), [](const Listing* a, const Listing* b) { return a->unit != b->unit ? a->unit > b->unit : a->row.listingId < b->row.listingId; });
		break;
	case AuctionRule::Sort::TimeLeft:
		std::sort(hits.begin(), hits.end(), [](const Listing* a, const Listing* b) { return a->row.expiresAt != b->row.expiresAt ? a->row.expiresAt < b->row.expiresAt : a->row.listingId < b->row.listingId; });
		break;
	case AuctionRule::Sort::RarityDesc:
		std::sort(hits.begin(), hits.end(), [](const Listing* a, const Listing* b)
		{
			if (a->rarity != b->rarity) return a->rarity > b->rarity;
			if (a->row.enchant != b->row.enchant) return a->row.enchant > b->row.enchant;
			return a->unit != b->unit ? a->unit < b->unit : a->row.listingId < b->row.listingId;
		});
		break;
	default:
		std::sort(hits.begin(), hits.end(), [](const Listing* a, const Listing* b) { return a->unit != b->unit ? a->unit < b->unit : a->row.listingId < b->row.listingId; });
		break;
	}

	const int per = AuctionRule::kPageRows;
	const int total = static_cast<int>(hits.size());
	const int pages = std::max(1, (total + per - 1) / per);
	const int page = std::min<int>(p.page, pages - 1);
	const int64_t now = Service::UnixNow();

	AUCTION_LIST_INF_PACKET out;
	Service::Init(out, PacketType::AUCTION_LIST_INF);
	out.kind = static_cast<uint8_t>(AuctionListKind::Search);
	out.page = static_cast<uint16_t>(page);
	out.pages = static_cast<uint16_t>(pages);
	out.total = static_cast<uint32_t>(total);
	for (int i = page * per; i < total && out.count < per; ++i)
	{
		const Listing& l = *hits[static_cast<size_t>(i)];
		AuctionRow& row = out.rows[out.count++];
		row.id = static_cast<uint32_t>(l.row.listingId);
		row.itemId = l.row.itemId;
		row.count = l.row.count;
		row.enchant = l.row.enchant;
		row.extra = l.row.extra;
		row.price = l.row.price;
		row.minutesLeft = MinutesLeft(l.row, now);
		row.flags = static_cast<uint8_t>((l.row.sellerId == ctx.playerId ? 1 : 0) | (l.state == State::Buying ? 2 : 0));
	}
	Service::SendTo(ctx.session, out);
}

void AuctionService::SendMine(const Service::Ctx& ctx, uint16_t page) const
{
	std::vector<const Listing*> mine;
	for (const auto& [id, l] : _listings)
		if (l.row.sellerId == ctx.playerId) mine.push_back(&l);
	std::sort(mine.begin(), mine.end(), [](const Listing* a, const Listing* b) { return a->row.listingId > b->row.listingId; });
	const int per = AuctionRule::kPageRows;
	const int total = static_cast<int>(mine.size());
	const int pages = std::max(1, (total + per - 1) / per);
	const int pg = std::min<int>(page, pages - 1);
	const int64_t now = Service::UnixNow();

	AUCTION_LIST_INF_PACKET out;
	Service::Init(out, PacketType::AUCTION_LIST_INF);
	out.kind = static_cast<uint8_t>(AuctionListKind::Mine);
	out.page = static_cast<uint16_t>(pg);
	out.pages = static_cast<uint16_t>(pages);
	out.total = static_cast<uint32_t>(total);
	for (int i = pg * per; i < total && out.count < per; ++i)
	{
		const Listing& l = *mine[static_cast<size_t>(i)];
		AuctionRow& row = out.rows[out.count++];
		row.id = static_cast<uint32_t>(l.row.listingId);
		row.itemId = l.row.itemId;
		row.count = l.row.count;
		row.enchant = l.row.enchant;
		row.extra = l.row.extra;
		row.price = l.row.price;
		row.minutesLeft = MinutesLeft(l.row, now);
		row.flags = static_cast<uint8_t>(1 | (l.state != State::Selling ? 2 : 0));
	}
	Service::SendTo(ctx.session, out);
}

void AuctionService::SendSettlements(const Service::Ctx& ctx, uint16_t page) const
{
	AUCTION_LIST_INF_PACKET out;
	Service::Init(out, PacketType::AUCTION_LIST_INF);
	out.kind = static_cast<uint8_t>(AuctionListKind::Settlements);
	const auto it = _settlements.find(ctx.playerId);
	const int total = it == _settlements.end() ? 0 : static_cast<int>(it->second.size());
	const int per = AuctionRule::kPageRows;
	const int pages = std::max(1, (total + per - 1) / per);
	const int pg = std::min<int>(page, pages - 1);
	out.page = static_cast<uint16_t>(pg);
	out.pages = static_cast<uint16_t>(pages);
	out.total = static_cast<uint32_t>(total);
	for (int i = pg * per; i < total && out.count < per; ++i)
	{
		const Settlement& s = it->second[static_cast<size_t>(i)];
		AuctionRow& row = out.rows[out.count++];
		row.id = static_cast<uint32_t>(s.row.settlementId);
		row.itemId = s.row.kind == 2 ? s.row.itemId : 0;
		row.count = s.row.kind == 2 ? s.row.count : 0;
		row.enchant = s.row.enchant;
		row.extra = s.row.extra;
		row.price = s.row.kind == 1 ? s.row.adena : 0;
		row.minutesLeft = s.row.reason;          // 정산함: 사유(AuctionSettleReason)
		row.flags = s.claiming ? 2 : 0;
	}
	Service::SendTo(ctx.session, out);
}

void AuctionService::PriceHint(const Service::Ctx& ctx, uint16_t itemId, uint8_t minRarity) const
{
	AUCTION_NFY_PACKET p;
	Service::Init(p, PacketType::AUCTION_NFY);
	p.event = static_cast<uint8_t>(AuctionEvent::PriceHint);
	p.itemId = itemId;
	for (const auto& [id, l] : _listings)
		if (l.row.itemId == itemId && l.state == State::Selling && l.rarity >= minRarity && (p.lowest == 0 || l.unit < p.lowest))
			p.lowest = l.unit;
	if (const auto it = _prices.find(itemId); it != _prices.end() && !it->second.empty())
	{
		uint64_t sum = 0;
		for (const uint32_t v : it->second) sum += v;
		p.lastSold = it->second.back();
		p.soldCount = static_cast<uint16_t>(it->second.size());
		p.avgSold = static_cast<uint32_t>(sum / it->second.size());
	}
	Service::SendTo(ctx.session, p);
}

// ── 등록 ──
void AuctionService::Register(const Service::Ctx& ctx, const AUCTION_REQ_PACKET& p)
{
	const uint32_t seq = p.seq;
	if (_inflight.count(ctx.playerId))
		return Ack(ctx, AuctionOp::Register, AuctionResult::Busy, seq);
	if (_perSeller[ctx.playerId] >= AuctionRule::kMaxListingsPerSeller)
		return Ack(ctx, AuctionOp::Register, AuctionResult::TooMany, seq);

	const int64_t now = Service::UnixNow();
	DB_LISTING_ROW row;
	row.listingId = _nextListingId++;
	row.sellerId = ctx.playerId;
	row.price = p.price;
	row.deposit = AuctionRule::Deposit(p.price);
	row.createdAt = now;
	row.expiresAt = now + static_cast<int64_t>(AuctionRule::kDurationHours[p.duration % AuctionRule::kDurationCount]) * HourSec();
	++_perSeller[ctx.playerId];
	_inflight.insert(ctx.playerId);

	const uint16_t slot = p.slot;
	const uint16_t count = p.count;
	const uint16_t expectItem = p.itemId;
	Service::PostToOwner(ctx.obj, [ctx, seq, row, slot, count, expectItem](const std::shared_ptr<User>& user)
	{
		auto abort = [=](AuctionResult r)
		{
			GAuctionService->Post([=]()
			{
				auto& svc = *GAuctionService;
				--svc._perSeller[ctx.playerId];
				svc._inflight.erase(ctx.playerId);
				svc.Ack(ctx, AuctionOp::Register, r, seq);
			});
		};
		if (user == nullptr)
			return abort(AuctionResult::Invalid);
		auto inv = user->GetInventory();
		if (inv->GetAdena() < row.deposit)
			return abort(AuctionResult::NoAdena);
		const auto& slots = inv->GetSlots();
		const auto si = slots.find(slot);
		if (si == slots.end() || si->second->GetItemTableId() != expectItem)
			return abort(AuctionResult::BadItem);   // 그 사이 칸이 바뀌었다

		Inventory::TakenItem taken;
		DB_ITEM_SLOT_SAVE remain;
		const auto tr = inv->TakeForListing(slot, count, taken, remain);
		if (tr != Inventory::TakeResult::Ok)
			return abort(FromTake(tr));
		(void)inv->ReserveAdena(row.deposit, AdenaReason::AuctionDeposit);   // 위에서 확인했다
		const uint32_t adenaAfter = inv->GetAdena();

		DB_LISTING_ROW full = row;
		full.itemId = taken.itemId;
		full.count = taken.count;
		full.enchant = taken.enchant;
		full.extra = taken.extra;
		const int sellerId = ctx.playerId;
		const ObjID obj = ctx.obj;
		GDBThread->RequestTx(sellerId, "AuctionRegister",
			[=](DBConnection& c) { return c.AuctionRegister(sellerId, adenaAfter, remain, full); },
			[=](bool ok)
			{
				if (!ok)
					Service::PostToOwner(obj, [=](const std::shared_ptr<User>& u)
					{
						if (u == nullptr) return;   // 나갔다 — 커밋되지 않았으니 DB는 그대로
						u->GetInventory()->RestoreTaken(slot, taken);
						u->GetInventory()->ReturnAdena(full.deposit, AdenaReason::Refund);
					});
				GAuctionService->Post([=]() { GAuctionService->FinishRegister(ok, ctx, seq, full, adenaAfter); });
			});
	});
}

void AuctionService::FinishRegister(bool ok, const Service::Ctx& ctx, uint32_t seq, const DB_LISTING_ROW& row, uint32_t adenaAfter)
{
	_inflight.erase(ctx.playerId);
	if (!ok)
	{
		--_perSeller[ctx.playerId];
		return Ack(ctx, AuctionOp::Register, AuctionResult::Invalid, seq);
	}
	Listing l;
	l.row = row;
	Index(l);
	_byCat[l.cat].insert(static_cast<uint32_t>(row.listingId));
	_listings.emplace(static_cast<uint32_t>(row.listingId), std::move(l));
	Ack(ctx, AuctionOp::Register, AuctionResult::Ok, seq, static_cast<uint32_t>(row.listingId), adenaAfter,
		-static_cast<int32_t>(row.deposit), row.itemId, row.count);
}

// ── 구매 ──
void AuctionService::Buy(const Service::Ctx& ctx, uint32_t seq, uint32_t listingId, uint32_t price)
{
	if (_inflight.count(ctx.playerId))
		return Ack(ctx, AuctionOp::Buy, AuctionResult::Busy, seq, listingId);
	const auto it = _listings.find(listingId);
	if (it == _listings.end() || it->second.state == State::Closing)
		return Ack(ctx, AuctionOp::Buy, AuctionResult::NotFound, seq, listingId);
	Listing& l = it->second;
	if (l.state == State::Buying)
		return Ack(ctx, AuctionOp::Buy, AuctionResult::Busy, seq, listingId);   // 다른 사람이 사는 중
	if (l.row.sellerId == ctx.playerId || l.row.price != price)
		return Ack(ctx, AuctionOp::Buy, AuctionResult::Invalid, seq, listingId);

	// 잠금 — 이 순간부터 같은 목록의 다른 구매는 위에서 걸러진다(진 쪽은 그로시가 빠지지 않는다)
	l.state = State::Buying;
	l.buyer = ctx.playerId;
	_inflight.insert(ctx.playerId);
	const int64_t buyerSettle = _nextSettlementId++;
	const int64_t sellerSettle = _nextSettlementId++;
	const uint32_t sellerGets = AuctionRule::SellerGets(price);
	const int64_t now = Service::UnixNow();

	Service::PostToOwner(ctx.obj, [=](const std::shared_ptr<User>& user)
	{
		auto unlock = [=](AuctionResult r)
		{
			GAuctionService->Post([=]()
			{
				auto& svc = *GAuctionService;
				svc._inflight.erase(ctx.playerId);
				if (const auto li = svc._listings.find(listingId); li != svc._listings.end() && li->second.state == State::Buying && li->second.buyer == ctx.playerId)
					li->second.state = State::Selling;
				svc.Ack(ctx, AuctionOp::Buy, r, seq, listingId);
			});
		};
		if (user == nullptr)
			return unlock(AuctionResult::Invalid);
		if (!user->GetInventory()->ReserveAdena(price, AdenaReason::AuctionBuy))
			return unlock(AuctionResult::NoAdena);
		const uint32_t adenaAfter = user->GetInventory()->GetAdena();
		const int buyerId = ctx.playerId;
		const ObjID obj = ctx.obj;
		GDBThread->RequestTx(buyerId, "AuctionBuy",
			[=](DBConnection& c) { return c.AuctionBuy(buyerId, adenaAfter, listingId, price, buyerSettle, sellerSettle, sellerGets, now); },
			[=](bool ok)
			{
				if (!ok)
					Service::PostToOwner(obj, [price](const std::shared_ptr<User>& u)
					{
						if (u != nullptr) u->GetInventory()->ReturnAdena(price, AdenaReason::Refund);
					});
				GAuctionService->Post([=]() { GAuctionService->FinishBuy(ok, ctx, seq, listingId, buyerSettle, sellerSettle, adenaAfter, now); });
			});
	});
}

void AuctionService::FinishBuy(bool ok, const Service::Ctx& ctx, uint32_t seq, uint32_t listingId, int64_t buyerSettle, int64_t sellerSettle,
	uint32_t adenaAfter, int64_t now)
{
	_inflight.erase(ctx.playerId);
	const auto it = _listings.find(listingId);
	if (it == _listings.end())
		return Ack(ctx, AuctionOp::Buy, AuctionResult::NotFound, seq, listingId);
	Listing& l = it->second;
	if (!ok)
	{
		if (l.state == State::Buying && l.buyer == ctx.playerId)
			l.state = State::Selling;
		return Ack(ctx, AuctionOp::Buy, AuctionResult::Invalid, seq, listingId);
	}

	// 커밋됐다: 목록을 닫고 정산함 두 줄(구매자 물건 · 판매자 대금), 시세
	const DB_LISTING_ROW row = l.row;
	_byCat[l.cat].erase(listingId);
	_listings.erase(it);
	if (--_perSeller[row.sellerId] <= 0)
		_perSeller.erase(row.sellerId);

	Settlement item;
	item.row.settlementId = buyerSettle;
	item.row.playerId = ctx.playerId;
	item.row.kind = 2;
	item.row.itemId = row.itemId;
	item.row.count = row.count;
	item.row.enchant = row.enchant;
	item.row.extra = row.extra;
	item.row.listingId = row.listingId;
	item.row.reason = static_cast<uint8_t>(AuctionSettleReason::Bought);
	item.row.createdAt = now;
	_settlements[ctx.playerId].push_back(item);

	const uint32_t gets = AuctionRule::SellerGets(row.price);
	Settlement money;
	money.row.settlementId = sellerSettle;
	money.row.playerId = row.sellerId;
	money.row.kind = 1;
	money.row.adena = gets;
	money.row.listingId = row.listingId;
	money.row.reason = static_cast<uint8_t>(AuctionSettleReason::Sold);
	money.row.createdAt = now;
	_settlements[row.sellerId].push_back(money);

	RecordPrice(row.itemId, row.price / std::max<uint32_t>(1, row.count));

	Ack(ctx, AuctionOp::Buy, AuctionResult::Ok, seq, listingId, adenaAfter, -static_cast<int32_t>(row.price), row.itemId, row.count);
	NotifySettleCount(ctx.playerId);

	AUCTION_NFY_PACKET sold;
	Service::Init(sold, PacketType::AUCTION_NFY);
	sold.event = static_cast<uint8_t>(AuctionEvent::Sold);
	sold.id = listingId;
	sold.itemId = row.itemId;
	sold.price = gets;
	sold.settleCount = static_cast<uint16_t>(UnclaimedCount(row.sellerId));
	SendTo(row.sellerId, sold);
	std::cout << "[Auction] sold listing=" << listingId << " buyer=" << ctx.playerId << " seller=" << row.sellerId << " price=" << row.price << "\n" << std::flush;
}

// ── 취소 · 만료 ──
void AuctionService::Cancel(const Service::Ctx& ctx, uint32_t seq, uint32_t listingId)
{
	if (_inflight.count(ctx.playerId))
		return Ack(ctx, AuctionOp::Cancel, AuctionResult::Busy, seq, listingId);
	const auto it = _listings.find(listingId);
	if (it == _listings.end())
		return Ack(ctx, AuctionOp::Cancel, AuctionResult::NotFound, seq, listingId);
	if (it->second.row.sellerId != ctx.playerId)
		return Ack(ctx, AuctionOp::Cancel, AuctionResult::NotYours, seq, listingId);
	if (it->second.state != State::Selling)
		return Ack(ctx, AuctionOp::Cancel, AuctionResult::Busy, seq, listingId);   // 누가 사는 중
	_inflight.insert(ctx.playerId);
	Close(listingId, 2, ctx, seq);
}

void AuctionService::Close(uint32_t listingId, uint8_t status, const std::optional<Service::Ctx>& ctx, uint32_t seq)
{
	const auto it = _listings.find(listingId);
	if (it == _listings.end())
		return;
	it->second.state = State::Closing;
	const int64_t settleId = _nextSettlementId++;
	const int64_t now = Service::UnixNow();
	const int sellerId = it->second.row.sellerId;
	const std::optional<Service::Ctx> c2 = ctx;
	GDBThread->RequestTx(sellerId, status == 2 ? "AuctionCancel" : "AuctionExpire",
		[=](DBConnection& c) { return c.AuctionClose(listingId, status, settleId, now); },
		[=](bool ok) { GAuctionService->Post([=]() { GAuctionService->FinishClose(ok, listingId, status, settleId, now, c2, seq); }); });
}

void AuctionService::FinishClose(bool ok, uint32_t listingId, uint8_t status, int64_t settleId, int64_t now, const std::optional<Service::Ctx>& ctx, uint32_t seq)
{
	if (ctx)
		_inflight.erase(ctx->playerId);
	const auto it = _listings.find(listingId);
	if (it == _listings.end())
	{
		if (ctx) Ack(*ctx, AuctionOp::Cancel, AuctionResult::NotFound, seq, listingId);
		return;
	}
	if (!ok)
	{
		if (it->second.state == State::Closing) it->second.state = State::Selling;
		if (ctx) Ack(*ctx, AuctionOp::Cancel, AuctionResult::Invalid, seq, listingId);
		return;
	}
	const DB_LISTING_ROW row = it->second.row;
	_byCat[it->second.cat].erase(listingId);
	_listings.erase(it);
	if (--_perSeller[row.sellerId] <= 0)
		_perSeller.erase(row.sellerId);

	Settlement back;
	back.row.settlementId = settleId;
	back.row.playerId = row.sellerId;
	back.row.kind = 2;
	back.row.itemId = row.itemId;
	back.row.count = row.count;
	back.row.enchant = row.enchant;
	back.row.extra = row.extra;
	back.row.listingId = row.listingId;
	back.row.reason = static_cast<uint8_t>(status == 2 ? AuctionSettleReason::Cancelled : AuctionSettleReason::Expired);
	back.row.createdAt = now;
	_settlements[row.sellerId].push_back(back);

	if (ctx)
		Ack(*ctx, AuctionOp::Cancel, AuctionResult::Ok, seq, listingId, 0, 0, row.itemId, row.count);
	if (status == 3)
	{
		AUCTION_NFY_PACKET p;
		Service::Init(p, PacketType::AUCTION_NFY);
		p.event = static_cast<uint8_t>(AuctionEvent::Expired);
		p.id = listingId;
		p.itemId = row.itemId;
		p.settleCount = static_cast<uint16_t>(UnclaimedCount(row.sellerId));
		SendTo(row.sellerId, p);
		std::cout << "[Auction] expired listing=" << listingId << " seller=" << row.sellerId << "\n" << std::flush;
	}
	NotifySettleCount(row.sellerId);
}

// ── 정산함 받기 ──
void AuctionService::Claim(const Service::Ctx& ctx, uint32_t seq, uint32_t settlementId)
{
	if (_inflight.count(ctx.playerId))
		return Ack(ctx, AuctionOp::Claim, AuctionResult::Busy, seq, settlementId);
	const auto vi = _settlements.find(ctx.playerId);
	if (vi == _settlements.end())
		return Ack(ctx, AuctionOp::Claim, AuctionResult::NotFound, seq, settlementId);
	Settlement* s = nullptr;
	for (auto& e : vi->second)
		if (!e.claiming && (settlementId == 0 || e.row.settlementId == static_cast<int64_t>(settlementId))) { s = &e; break; }
	if (s == nullptr)
		return Ack(ctx, AuctionOp::Claim, AuctionResult::NotFound, seq, settlementId);
	s->claiming = true;
	_inflight.insert(ctx.playerId);
	const DB_SETTLEMENT_ROW row = s->row;
	const int64_t now = Service::UnixNow();

	Service::PostToOwner(ctx.obj, [=](const std::shared_ptr<User>& user)
	{
		const int64_t sid = row.settlementId;
		auto abort = [=](AuctionResult r)
		{
			GAuctionService->Post([=]()
			{
				auto& svc = *GAuctionService;
				svc._inflight.erase(ctx.playerId);
				if (const auto v = svc._settlements.find(ctx.playerId); v != svc._settlements.end())
					for (auto& e : v->second) if (e.row.settlementId == sid) e.claiming = false;
				svc.Ack(ctx, AuctionOp::Claim, r, seq, static_cast<uint32_t>(sid));
			});
		};
		if (user == nullptr)
			return abort(AuctionResult::Invalid);
		auto inv = user->GetInventory();
		const int pid = ctx.playerId;
		const ObjID obj = ctx.obj;
		if (row.kind == 1)
		{
			if (inv->GetAdena() > kMaxAdena - std::min(kMaxAdena, row.adena))
				return abort(AuctionResult::InventoryFull);   // 지갑 상한
			inv->CreditAdenaNoSave(row.adena, AdenaReason::AuctionClaim);
			const uint32_t adenaAfter = inv->GetAdena();
			const DB_ITEM_SLOT_SAVE none{};
			const uint32_t amount = row.adena;
			GDBThread->RequestTx(pid, "SettlementClaim",
				[=](DBConnection& c) { return c.SettlementClaim(pid, sid, adenaAfter, none, now); },
				[=](bool ok)
				{
					if (!ok)
						Service::PostToOwner(obj, [amount](const std::shared_ptr<User>& u)
						{
							if (u != nullptr) (void)u->GetInventory()->TrySpendAdena(amount, AdenaReason::Refund);
						});
					GAuctionService->Post([=]() { GAuctionService->FinishClaim(ok, ctx, seq, sid, adenaAfter); });
				});
			return;
		}
		const uint16_t slot = inv->FindSlotForIncoming(row.itemId, row.count);
		if (slot >= MAX_INVENTORY_SLOTS)
			return abort(AuctionResult::InventoryFull);
		const Inventory::TakenItem item{ row.itemId, row.count, row.enchant, row.extra };
		DB_ITEM_SLOT_SAVE out;
		if (!inv->PlaceIncoming(slot, item, out))
			return abort(AuctionResult::InventoryFull);
		const uint32_t adenaAfter = inv->GetAdena();
		GDBThread->RequestTx(pid, "SettlementClaim",
			[=](DBConnection& c) { return c.SettlementClaim(pid, sid, adenaAfter, out, now); },
			[=](bool ok)
			{
				if (!ok)
					Service::PostToOwner(obj, [slot, item](const std::shared_ptr<User>& u)
					{
						if (u != nullptr) u->GetInventory()->UndoIncoming(slot, item);
					});
				GAuctionService->Post([=]() { GAuctionService->FinishClaim(ok, ctx, seq, sid, adenaAfter); });
			});
	});
}

void AuctionService::FinishClaim(bool ok, const Service::Ctx& ctx, uint32_t seq, int64_t settlementId, uint32_t adenaAfter)
{
	_inflight.erase(ctx.playerId);
	const auto vi = _settlements.find(ctx.playerId);
	if (vi == _settlements.end())
		return Ack(ctx, AuctionOp::Claim, AuctionResult::NotFound, seq, static_cast<uint32_t>(settlementId));
	auto& v = vi->second;
	const auto it = std::find_if(v.begin(), v.end(), [&](const Settlement& s) { return s.row.settlementId == settlementId; });
	if (it == v.end())
		return Ack(ctx, AuctionOp::Claim, AuctionResult::NotFound, seq, static_cast<uint32_t>(settlementId));
	if (!ok)
	{
		it->claiming = false;
		return Ack(ctx, AuctionOp::Claim, AuctionResult::Invalid, seq, static_cast<uint32_t>(settlementId));
	}
	const DB_SETTLEMENT_ROW row = it->row;
	v.erase(it);
	if (v.empty())
		_settlements.erase(vi);
	Ack(ctx, AuctionOp::Claim, AuctionResult::Ok, seq, static_cast<uint32_t>(settlementId), adenaAfter,
		row.kind == 1 ? static_cast<int32_t>(row.adena) : 0, row.kind == 2 ? row.itemId : 0, row.kind == 2 ? row.count : 0);
	NotifySettleCount(ctx.playerId);
}

// ─────────────────────────────────────────────────────────────────────────────
// AuctionHelper — 요청한 사람의 소유 Zone 스레드
// ─────────────────────────────────────────────────────────────────────────────
namespace AuctionHelper
{
	namespace
	{
		void ZoneAck(const std::shared_ptr<User>& user, AuctionOp op, AuctionResult r, uint32_t seq, uint32_t id = 0)
		{
			AUCTION_ACK_PACKET p;
			Service::Init(p, PacketType::AUCTION_ACK);
			p.op = static_cast<uint8_t>(op);
			p.result = static_cast<uint8_t>(r);
			p.seq = seq;
			p.id = id;
			p.adena = user->GetInventory()->GetAdena();
			if (auto s = user->GetGameSession()) s->PostSend(p);
		}
		[[nodiscard]] bool IsMutating(AuctionOp op) noexcept
		{
			return op == AuctionOp::Register || op == AuctionOp::Buy || op == AuctionOp::Cancel || op == AuctionOp::Claim;
		}
	}

	void HandleRequest(const std::shared_ptr<User>& user, const AUCTION_REQ_PACKET& p)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		const auto op = static_cast<AuctionOp>(p.op);
		if (GAuctionService == nullptr)
			return ZoneAck(user, op, AuctionResult::Busy, p.seq, p.id);   // 켤 때 DB를 못 읽었다(서버 로그 [Phase5])
		const uint32_t now = GetNowTime();
		if (IsMutating(op))
		{
			if (p.seq <= user->m_lastAuctionSeq)
				return ZoneAck(user, op, AuctionResult::Duplicate, p.seq, p.id);
			user->m_lastAuctionSeq = p.seq;
			if (!NpcHelper::IsNear(AuctionRule::kAuctioneerNpcId, user->GetX(), user->GetY()))
				return ZoneAck(user, op, AuctionResult::NotAtNpc, p.seq, p.id);   // 거래(올리기 · 사기 · 내리기 · 받기)는 경매인 곁에서 — 상점과 같은 거리 규칙. 검색은 어디서나
			if (user->GetStat()->IsDead())
				return ZoneAck(user, op, AuctionResult::Invalid, p.seq, p.id);
		}
		else
		{
			// 검색 · 목록 · 시세: 너무 잦은 요청은 버린다(창을 넘기는 속도로 충분)
			if (user->m_lastAuctionQueryAt != 0 && now - user->m_lastAuctionQueryAt < 120)
				return;
			user->m_lastAuctionQueryAt = now;
		}

		switch (op)
		{
		case AuctionOp::Register:
		{
			if (!AuctionRule::ValidPrice(p.price))
				return ZoneAck(user, op, AuctionResult::BadPrice, p.seq);
			if (p.duration >= AuctionRule::kDurationCount || p.count == 0)
				return ZoneAck(user, op, AuctionResult::Invalid, p.seq);
			const auto& slots = user->GetInventory()->GetSlots();
			const auto si = slots.find(p.slot);
			if (si == slots.end() || si->second->GetItemTableId() != p.itemId || si->second->GetCount() < p.count)
				return ZoneAck(user, op, AuctionResult::BadItem, p.seq);
			const ItemTableRow* row = ItemTable::Find(p.itemId);
			if (row == nullptr || !AuctionRule::Listable(*row))
				return ZoneAck(user, op, AuctionResult::BadItem, p.seq);
			if (user->GetInventory()->GetAdena() < AuctionRule::Deposit(p.price))
				return ZoneAck(user, op, AuctionResult::NoAdena, p.seq);
			break;
		}
		case AuctionOp::Buy:
			if (p.id == 0 || !AuctionRule::ValidPrice(p.price))
				return ZoneAck(user, op, AuctionResult::Invalid, p.seq, p.id);
			if (user->GetInventory()->GetAdena() < p.price)
				return ZoneAck(user, op, AuctionResult::NoAdena, p.seq, p.id);
			break;
		case AuctionOp::Cancel:
			if (p.id == 0)
				return ZoneAck(user, op, AuctionResult::Invalid, p.seq);
			break;
		case AuctionOp::Claim:
		case AuctionOp::Search: case AuctionOp::Mine: case AuctionOp::Settlements: case AuctionOp::PriceHint:
			break;
		default:
			return ZoneAck(user, op, AuctionResult::Invalid, p.seq);
		}

		const Service::Ctx ctx = Service::MakeCtx(user);
		const AUCTION_REQ_PACKET copy = p;
		GAuctionService->Post([ctx, copy]() { GAuctionService->Handle(ctx, copy); });
	}

	void OnEnterWorld(const std::shared_ptr<User>& user)
	{
		if (user == nullptr || !user->IsExtended() || GAuctionService == nullptr)
			return;
		const Service::Ctx ctx = Service::MakeCtx(user);
		GAuctionService->Post([ctx]() { GAuctionService->OnLogin(ctx); });
	}

	void OnLeaveWorld(const std::shared_ptr<User>& user)
	{
		if (user == nullptr || !user->IsExtended() || GAuctionService == nullptr)
			return;
		const int playerId = user->GetPlayerId();
		GAuctionService->Post([playerId]() { GAuctionService->OnLogout(playerId); });
	}
}
