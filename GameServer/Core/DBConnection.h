#pragma once
#include <sql.h>
#include <sqlext.h>
using namespace std;

class DBConnection
{
public:
	DBConnection() = default;
	~DBConnection();

	bool			Connect();
	void			Clear();

	bool			Execute(const WCHAR* query);
	bool			Fetch();
	int				GetRowCount();
	void			Unbind();

public:
	bool			BindParam(SQLUSMALLINT paramIndex, SQLSMALLINT cType, SQLSMALLINT sqlType, SQLULEN len, SQLPOINTER ptr, SQLLEN* index);
	bool			BindCol(SQLUSMALLINT columnIndex, SQLSMALLINT cType, SQLULEN len, SQLPOINTER value, SQLLEN* index);
	void			HandleError(SQLRETURN ret);

public:
	bool			IsUserRegistered(const string& name);
	bool			VerifyUserPassword(const string& name, const string& password);
	// 성공 시 outPlayerId에 Players.playerId(신규 삽입이면 새로 발급된 값, 이미
	// 존재하는 이름이면 그 행의 값)를 채운다. 실패하면 false를 반환하고 건드리지 않는다.
	bool			AddUserInfoInDataBase(const string& name, const string& password, short x, short y, uint8 level, uint32 exp, int& outPlayerId);
	DB_USER_INFO	ExtractUserInfo(const string& name);
	bool			SaveUserInfo(const string& name, short x, short y, uint8 level, uint32 exp);

	// playerId는 로그인/계정생성 시 확보해 User에 캐싱해둔 정수 PK를 그대로 쓴다 —
	// name(NVARCHAR) 기반 조회/조인이 DB 쪽에서 완전히 사라진다.
	vector<DB_ITEM_INFO> ExtractInventory(int playerId);
	bool			SaveInventorySlot(int playerId, uint16_t slotIndex, uint16_t itemId, uint16_t count, bool equipped, uint8_t enchant, uint64_t extra = 0);
	bool			DeleteInventorySlot(int playerId, uint16_t slotIndex);

	// ── 리니지풍 컨텐츠 확장 ──
	// 그로시/성향치는 name이 아니라 playerId 기준(인벤토리와 같은 샤드)으로 저장한다.
	bool			SavePlayerAdena(int playerId, uint32_t adena);
	bool			SavePlayerAlignment(int playerId, int16_t alignment, uint16_t pkCount);
	// 상점 거래: 그로시 + 슬롯 하나를 하나의 SQL 트랜잭션으로 반영한다(ShopTransaction 프로시저).
	bool			ShopTransaction(int playerId, uint32_t adena, const DB_ITEM_SLOT_SAVE& slot);
	// V2 캐릭터 진행 데이터(직업·능력치 포인트·퀘스트 등)를 한 덩어리(버전 있는 바이너리)로
	// 읽고 쓴다(PlayerProgress 테이블). 기능이 늘 때마다 컬럼을 늘리지 않기 위함.
	bool			LoadPlayerProgress(int playerId, std::vector<uint8_t>& out);
	bool			SavePlayerProgress(int playerId, const std::vector<uint8_t>& data);
	// Phase 2-D 기술 성장(PlayerSkills): 레벨 1 · 갈래 없음이 아닌 기술만 행이 있다
	bool			LoadPlayerSkills(int playerId, std::vector<DB_SKILL_INFO>& out);
	bool			SavePlayerSkill(int playerId, const DB_SKILL_INFO& skill);   // 한 기술 upsert(기본값이면 삭제) — 한 트랜잭션
	bool			ResetPlayerSkills(int playerId);                             // 모두 초기화(행 삭제)

	// a/b 두 슬롯을 하나의 SQL 트랜잭션으로 함께 반영한다(각 슬롯은 독립적으로
	// upsert 또는 삭제될 수 있다 — DB_ITEM_SLOT_SAVE::hasItem 참고). 장착 시 이전
	// 장비 자동 탈착, 슬롯 교체처럼 "두 슬롯이 논리적으로 한 동작"인 경우 반드시
	// 이걸 쓴다 — SaveInventorySlot을 두 번 따로 부르면 그 사이에 서버가 죽었을 때
	// DB에 한쪽만 반영된 불변조건 위반 상태가 남을 수 있다.
	bool			SaveTwoInventorySlots(int playerId, const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b);

	// ── Phase 5 결사 · 경매장(docs/adr/0001-guild-auction-services.md) ──
	// 프로시저 하나 = 트랜잭션 하나. bool = 커밋했는가(상태 검사에 걸리면 false — 아무것도 바뀌지 않는다).
	// 결사 이름 · 공지는 UTF-8로 받아 UTF-16(NVARCHAR)으로 넘긴다. 시각은 유닉스 초(BIGINT).
	bool			LoadGuilds(vector<DB_GUILD_ROW>& out);
	bool			LoadGuildMembers(vector<DB_GUILD_MEMBER_ROW>& out);
	bool			GuildCreate(int playerId, uint32_t adenaAfter, int guildId, const string& name, const string& nameKey, uint8_t emblem);
	bool			GuildMemberSet(int guildId, int playerId, uint8_t rank);        // 들어옴 · 직위(행이 있으면 바꾼다)
	bool			GuildMemberRemove(int guildId, int playerId);                   // 나감 · 추방
	bool			GuildTransfer(int guildId, int fromId, int toId);               // 결사장 넘기기(두 행 + 결사 행)
	bool			GuildSetInfo(int guildId, uint8_t emblem, const string& notice); // 문장 · 공지
	bool			GuildDisband(int guildId);
	bool			GuildMemberSeen(int playerId, int64_t unixSec);                  // 마지막 접속(나갈 때)
	bool			LoadAuctionListings(vector<DB_LISTING_ROW>& out);               // 판매 중(status 0)
	bool			LoadAuctionSettlements(vector<DB_SETTLEMENT_ROW>& out);         // 받지 않은 행
	bool			LoadAuctionPrices(vector<DB_PRICE_ROW>& out, int64_t sinceUnix);
	bool			AuctionMaxIds(int64_t& maxListing, int64_t& maxSettlement, int& maxGuild);
	bool			AuctionRegister(int sellerId, uint32_t adenaAfter, const DB_ITEM_SLOT_SAVE& slot, const DB_LISTING_ROW& listing);
	bool			AuctionBuy(int buyerId, uint32_t adenaAfter, int64_t listingId, uint32_t price, int64_t buyerSettleId, int64_t sellerSettleId, uint32_t sellerGets, int64_t nowUnix);
	bool			AuctionClose(int64_t listingId, uint8_t status, int64_t settleId, int64_t nowUnix);   // 2 취소 · 3 만료 → 판매자 정산함(물건)
	bool			SettlementClaim(int playerId, int64_t settlementId, uint32_t adenaAfter, const DB_ITEM_SLOT_SAVE& slot, int64_t nowUnix);
	// 장애 시험: 그로시 총량 · 물건 수(소지품 · 판매 중 목록 · 받지 않은 정산함) — 서버를 켤 때 한 줄
	bool			AuditTotals(uint64_t& walletAdena, uint64_t& settleAdena, uint64_t& invUnits, uint64_t& listingUnits, uint64_t& settleUnits, uint64_t& feesAndDeposits);

private:
	SQLHENV			_enviroment = SQL_NULL_HANDLE;
	SQLHDBC			_connection = SQL_NULL_HANDLE;
	SQLHSTMT		_statement = SQL_NULL_HANDLE;
	SQLRETURN		_retcode = SQL_SUCCESS;
};
