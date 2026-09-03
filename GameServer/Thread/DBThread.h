#pragma once

class DBConnectionPool;
class GameSession;

// DB 작업을 게임 로직 스레드에서 떼어내 비동기로 처리한다.
//
// 큐 하나를 여러 스레드가 나눠 갖는 대신, "샤드(shard)"를 kShardCount개 두고
// 스레드 하나가 샤드 하나를 전담한다. 그리고 같은 행(row)을 건드리는 이벤트는
// 항상 같은 샤드로 보낸다(플레이어 아이템은 playerId 기준, 유저 정보는 name 기준).
//
// 왜 이렇게 하는가 — 예전처럼 공용 큐를 여러 스레드가 나눠 가지면, 큐에서 꺼내는
// 순서는 맞아도 꺼낸 뒤 각자 ODBC 왕복을 하므로 "먼저 꺼낸 것이 먼저 커밋된다"는
// 보장이 없다. 같은 플레이어가 장착 -> 해제를 빠르게 하면 두 저장이 서로 다른
// 스레드로 가서 순서가 뒤바뀐 채 커밋될 수 있고, 그 상태로 서버가 죽으면 DB에는
// 옛날 상태가 영구히 남는다. 샤드를 고정하면 같은 플레이어의 쓰기는 한 스레드가
// 넣은 순서 그대로 하나씩 처리하므로 이 역전이 구조적으로 불가능해진다.
//
// 병렬성은 그대로다 — 스레드도 커넥션도 여전히 kShardCount개가 전부 돌고, 서로
// 다른 플레이어끼리는 예전처럼 완전히 병렬로 처리된다. playerId가 DB
// auto-increment 정수라 나머지 연산만으로도 샤드에 거의 균등하게 흩어진다.
class DBThread
{
public:
	using Clock    = std::chrono::steady_clock;
	using Duration = Clock::duration;

	// 샤드 수 = DB 워커 스레드 수. GameServer.cpp의 스레드 생성 루프와 반드시
	// 같아야 하므로(샤드에 담당 스레드가 없으면 그 샤드 이벤트는 영원히 처리되지
	// 않는다) 그쪽에서도 이 상수를 그대로 쓴다.
	static constexpr int kShardCount = 8;

	DBThread() = default;
	~DBThread() = default;

	// shardIndex 샤드만 전담해서 처리한다. 스레드 하나당 하나씩, 0 ~ kShardCount-1을
	// 빠짐없이 한 번씩 맡겨야 한다.
	void DoDataBase(int shardIndex);

	void RequestLogin(const shared_ptr<GameSession>& session, const std::string& name, const std::string& password);
	void RequestSaveUser(const ObjID& subjectId, const DB_USER_INFO& info);

	// 인벤토리 슬롯 하나를 즉시(디바운스 없이) 저장/삭제한다. 호출한 존 스레드를
	// 막지 않는 비동기 enqueue — 실제 ODBC 호출은 DB 워커 스레드가 처리한다.
	// playerId는 로그인 시 User에 캐싱해둔 정수 PK를 그대로 넘긴다(name 조회 없음).
	// 같은 playerId의 요청은 항상 같은 샤드로 가므로 넣은 순서대로 반영된다.
	void RequestSaveItem(int playerId, uint16_t slotIndex, uint16_t itemId, uint16_t count, bool equipped);
	void RequestDeleteItem(int playerId, uint16_t slotIndex);

	// 두 슬롯을 하나의 트랜잭션으로 함께 반영한다(장착 시 이전 장비 자동 탈착,
	// 슬롯 교체처럼 "논리적으로 한 동작"인 경우 반드시 이걸 쓴다 — Inventory.cpp 참고).
	void RequestSaveTwoItems(int playerId, const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b);

	[[nodiscard]] static Clock::time_point Now() noexcept { return Clock::now(); }

private:
	struct DBEventCompare
	{
		bool operator()(const shared_ptr<DB_EVENT_BASE>& lhs, const shared_ptr<DB_EVENT_BASE>& rhs) const noexcept;
	};

	using EventQueue = std::priority_queue<shared_ptr<DB_EVENT_BASE>, std::vector<shared_ptr<DB_EVENT_BASE>>, DBEventCompare>;

	struct Shard
	{
		EventQueue              events;
		std::mutex              lock;
		std::condition_variable cv;
	};

	void ScheduleOnShard(shared_ptr<DB_EVENT_BASE> event, int shardIndex);
	void ProcessEvent(const shared_ptr<DB_EVENT_BASE>& event);

	// 같은 행을 건드리는 이벤트가 항상 같은 샤드로 가게 하는 매핑.
	// Players 테이블(name 키)과 Inventory 테이블(playerId 키)은 서로 다른 행이라
	// 같은 샤드에 모일 필요가 없다 — 각자 자기 키 기준으로만 일관되면 된다.
	[[nodiscard]] static int ShardForPlayerId(int playerId) noexcept;
	[[nodiscard]] static int ShardForName(const std::string& name) noexcept;
	// 순서를 맞출 대상이 없는 이벤트(로그인: 아직 playerId를 모르는 조회 작업)용.
	// 아무 샤드에나 가도 되므로 부하만 고르게 흩어준다.
	[[nodiscard]] int NextRoundRobinShard() noexcept;

private:
	std::array<Shard, kShardCount> _shards;
	// 샤드별 락과 무관하게 전역으로 증가한다 — 같은 wakeupTime을 가진 이벤트끼리
	// "먼저 넣은 것이 먼저"를 판정하는 tie-break 용도라 전역 단조 증가면 충분하다.
	std::atomic<uint64_t> _nextSequence{ 0 };
	std::atomic<uint32_t> _roundRobinCursor{ 0 };
};
