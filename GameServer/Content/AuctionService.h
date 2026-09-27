#pragma once

#include <deque>
#include <functional>
#include <array>
#include <map>
#include <set>
#include <optional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Content/ServiceCommon.h"
#include "Data/GuildAuction.h"

class DBConnection;
class User;
struct AUCTION_REQ_PACKET;

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 경매장 · 정산함 — docs/adr/0001-guild-auction-services.md, 규칙 Data/GuildAuction.h
//
//  AuctionService: 판매 중 목록 · 받지 않은 정산함 · 시세의 주인. 전용 스레드 하나만 상태를 만진다(락 없음).
//   · 구매: 서비스가 목록을 잠근다(거래 중) → 구매자 Zone이 그로시를 확인해 메모리에서 빼고 → 구매자 샤드 한 트랜잭션
//     (목록 판매 중 확인 · 판매됨 · 구매자 그로시 · 구매자 물건 정산 행 · 판매자 대금 정산 행 · 시세) → 커밋되면 서비스가 확정.
//     이미 잠긴 · 팔린 목록은 잠그는 단계에서 거절 — 진 쪽의 그로시는 한 번도 빠지지 않는다.
//   · 판매자에게는 대금을 직접 넣지 않고 정산함 행으로 — 판매자가 오프라인 · 다른 Zone이어도 그로시의 작성자는 그 사람의 Zone 하나뿐.
//   · 등록: 서비스가 자리(1인 10개) · 번호를 예약 → 판매자 Zone이 슬롯에서 빼고 보증금을 뺀다 → 판매자 샤드 한 트랜잭션 → 공개.
//   · 취소 · 만료: 서비스가 잠그고 판매자 샤드 한 트랜잭션(목록 상태 · 판매자 물건 정산 행). 만료는 1분마다.
//   · 정산함 받기: 받는 사람 Zone이 자리를 잡아 메모리에 넣고 → 그 사람 샤드 한 트랜잭션(행 받음 · 그로시 · 슬롯).
//   · 한 사람의 진행 중인 경매 거래(등록 · 구매 · 취소 · 받기)는 한 번에 하나(_inflight).
//
//  AuctionHelper: 요청한 사람의 Zone 스레드 쪽(검사 · 멱등 → 서비스 큐).
// ─────────────────────────────────────────────────────────────────────────────
class AuctionService
{
public:
	using Task = GameLogicThread::Task;

	AuctionService();

	// 켤 때(메인 스레드 · 접속을 받기 전)
	bool LoadFromDB(DBConnection& db, int64_t maxListingId, int64_t maxSettlementId);
	void Run();
	void Post(Task task);

	// ── 서비스 스레드(Post 안)에서만 ──
	void Tick();                                   // 1초마다 — 만료는 kExpireTickMs마다
	void OnLogin(const Service::Ctx& ctx);
	void OnLogout(int playerId);
	void Handle(const Service::Ctx& ctx, const AUCTION_REQ_PACKET& p);

	[[nodiscard]] size_t ListingCount() const noexcept { return _listings.size(); }
	[[nodiscard]] size_t SettlementCount() const noexcept;
	[[nodiscard]] static uint32_t HourSec() noexcept;   // 기간 한 시간의 실제 초(시험 배율)

private:
	enum class State : uint8_t { Selling, Buying, Closing };
	struct Listing
	{
		DB_LISTING_ROW row;
		State    state = State::Selling;
		int      buyer = 0;                     // Buying일 때
		// 검색 색인(등록 · 불러올 때 한 번)
		uint8_t  cat = 0, sub = 0, rarity = 0, reqLevel = 0;
		uint32_t unit = 0;                      // 한 개 값
		const char* name = "";                  // 표시 이름(UTF-8, 표의 상수)
	};
	struct Settlement
	{
		DB_SETTLEMENT_ROW row;
		bool claiming = false;
	};
	struct Online
	{
		ObjID obj{};
		std::weak_ptr<GameSession> session;
	};

	void Index(Listing& l) const;
	[[nodiscard]] uint16_t MinutesLeft(const DB_LISTING_ROW& r, int64_t now) const noexcept;
	[[nodiscard]] int UnclaimedCount(int playerId) const;

	void Ack(const Service::Ctx& ctx, AuctionOp op, AuctionResult r, uint32_t seq, uint32_t id = 0,
		uint32_t adena = 0, int32_t delta = 0, uint16_t itemId = 0, uint16_t count = 0) const;
	void NotifySettleCount(int playerId) const;
	void SendTo(int playerId, const AUCTION_NFY_PACKET& p) const;

	void Search(const Service::Ctx& ctx, const AUCTION_REQ_PACKET& p) const;
	void SendMine(const Service::Ctx& ctx, uint16_t page) const;
	void SendSettlements(const Service::Ctx& ctx, uint16_t page) const;
	void PriceHint(const Service::Ctx& ctx, uint16_t itemId, uint8_t minRarity) const;

	void Register(const Service::Ctx& ctx, const AUCTION_REQ_PACKET& p);
	void FinishRegister(bool ok, const Service::Ctx& ctx, uint32_t seq, const DB_LISTING_ROW& row, uint32_t adenaAfter);
	void Buy(const Service::Ctx& ctx, uint32_t seq, uint32_t listingId, uint32_t price);
	void FinishBuy(bool ok, const Service::Ctx& ctx, uint32_t seq, uint32_t listingId, int64_t buyerSettle, int64_t sellerSettle,
		uint32_t adenaAfter, int64_t now);
	void Cancel(const Service::Ctx& ctx, uint32_t seq, uint32_t listingId);
	void Close(uint32_t listingId, uint8_t status, const std::optional<Service::Ctx>& ctx, uint32_t seq);   // 2 취소(ctx 있음) · 3 만료
	void FinishClose(bool ok, uint32_t listingId, uint8_t status, int64_t settleId, int64_t now, const std::optional<Service::Ctx>& ctx, uint32_t seq);
	void Claim(const Service::Ctx& ctx, uint32_t seq, uint32_t settlementId);
	void FinishClaim(bool ok, const Service::Ctx& ctx, uint32_t seq, int64_t settlementId, uint32_t adenaAfter);

	void RecordPrice(uint16_t itemId, uint32_t unit);

private:
	GameLogicThread                                     _loop;
	std::map<uint32_t, Listing>                         _listings;       // 판매 중 · 거래 중 · 닫는 중
	std::array<std::set<uint32_t>, static_cast<size_t>(AuctionRule::Cat::Count)> _byCat;   // 분류 → 목록 번호(검색 색인)
	std::unordered_map<int, int>                        _perSeller;      // 등록 수(예약 포함)
	std::unordered_map<int, std::vector<Settlement>>    _settlements;    // 받는 사람 → 받지 않은 행(번호 순)
	std::unordered_map<uint16_t, std::deque<uint32_t>>  _prices;         // 품목 → 최근 한 개 값
	std::unordered_map<int, Online>                     _online;
	std::unordered_set<int>                             _inflight;       // 거래가 진행 중인 사람
	uint32_t                                            _nextListingId = 1;
	int64_t                                             _nextSettlementId = 1;
	uint32_t                                            _lastExpireAt = 0;
};

extern std::shared_ptr<AuctionService> GAuctionService;

namespace AuctionHelper
{
	// ── 요청한 사람의 소유 Zone 스레드 ──
	void HandleRequest(const std::shared_ptr<User>& user, const AUCTION_REQ_PACKET& p);
	void OnEnterWorld(const std::shared_ptr<User>& user);
	void OnLeaveWorld(const std::shared_ptr<User>& user);
}
