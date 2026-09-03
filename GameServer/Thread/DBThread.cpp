#include "pch.h"
#include "DBThread.h"
#include "DBConnectionPool.h"
#include "UserHelper.h"
#include "Zone/ZoneManager.h"

namespace
{
	class ScopedDBConnection
	{
	public:
		explicit ScopedDBConnection(const shared_ptr<DBConnectionPool>& pool)
			: _pool(pool), _connection(pool != nullptr ? pool->Pop() : nullptr) {}

		~ScopedDBConnection()
		{
			if (_pool != nullptr && _connection != nullptr)
				_pool->Push(_connection);
		}

		[[nodiscard]] const shared_ptr<DBConnection>& Get() const noexcept { return _connection; }

	private:
		shared_ptr<DBConnectionPool> _pool;
		shared_ptr<DBConnection>     _connection;
	};
}

bool DBThread::DBEventCompare::operator()(const shared_ptr<DB_EVENT_BASE>& lhs, const shared_ptr<DB_EVENT_BASE>& rhs) const noexcept
{
	if (lhs->wakeupTime != rhs->wakeupTime)
		return lhs->wakeupTime > rhs->wakeupTime;
	return lhs->sequence > rhs->sequence;
}

int DBThread::ShardForPlayerId(int playerId) noexcept
{
	// playerId는 DB auto-increment라 순차 정수다 — 나머지 연산만으로 샤드에
	// 거의 정확히 균등하게 흩어진다(해시를 따로 돌릴 이유가 없다).
	return static_cast<int>(static_cast<uint32_t>(playerId) % kShardCount);
}

int DBThread::ShardForName(const std::string& name) noexcept
{
	return static_cast<int>(std::hash<std::string>{}(name) % kShardCount);
}

int DBThread::NextRoundRobinShard() noexcept
{
	return static_cast<int>(_roundRobinCursor.fetch_add(1, std::memory_order_relaxed) % kShardCount);
}

void DBThread::ScheduleOnShard(shared_ptr<DB_EVENT_BASE> event, int shardIndex)
{
	ASSERT_CRASH(shardIndex >= 0 && shardIndex < kShardCount);
	Shard& shard = _shards[shardIndex];

	{
		std::scoped_lock lock(shard.lock);
		event->sequence = _nextSequence.fetch_add(1, std::memory_order_relaxed);
		shard.events.push(std::move(event));
	}
	shard.cv.notify_one();
}

void DBThread::RequestLogin(const shared_ptr<GameSession>& session, const std::string& name, const std::string& password)
{
	auto event      = std::make_shared<DB_LOGIN_EVENT>();
	event->wakeupTime = Now();
	event->session  = session;
	event->name     = name;
	event->password = password;
	// 로그인은 아직 playerId를 모르는 상태에서 하는 조회 작업이고, 순서를 맞춰야
	// 할 다른 이벤트도 없다 — 부하만 고르게 흩어지도록 라운드로빈으로 보낸다.
	ScheduleOnShard(std::move(event), NextRoundRobinShard());
}

void DBThread::RequestSaveUser(const ObjID& subjectId, const DB_USER_INFO& info)
{
	auto event        = std::make_shared<DB_SAVE_EVENT>();
	event->wakeupTime = Now();
	event->subjectId  = subjectId;
	event->name       = info._name;
	event->x          = static_cast<short>(info._x);
	event->y          = static_cast<short>(info._y);
	event->level      = info._level;
	event->exp        = info._exp;
	// Players 테이블은 name이 키다 — 같은 유저의 저장이 순서대로 반영되도록
	// name 기준으로 샤드를 고정한다(인벤토리와 같은 샤드일 필요는 없다. 서로
	// 다른 테이블/행이라 순서를 맞출 대상 자체가 다르다).
	const int shardIndex = ShardForName(event->name);
	ScheduleOnShard(std::move(event), shardIndex);
}

void DBThread::RequestSaveItem(int playerId, uint16_t slotIndex, uint16_t itemId, uint16_t count, bool equipped)
{
	auto event        = std::make_shared<DB_ITEM_SAVE_EVENT>();
	event->wakeupTime = Now();
	event->playerId   = playerId;
	event->slotIndex  = slotIndex;
	event->itemId     = itemId;
	event->count      = count;
	event->equipped   = equipped;
	ScheduleOnShard(std::move(event), ShardForPlayerId(playerId));
}

void DBThread::RequestDeleteItem(int playerId, uint16_t slotIndex)
{
	auto event        = std::make_shared<DB_ITEM_DELETE_EVENT>();
	event->wakeupTime = Now();
	event->playerId   = playerId;
	event->slotIndex  = slotIndex;
	ScheduleOnShard(std::move(event), ShardForPlayerId(playerId));
}

void DBThread::RequestSaveTwoItems(int playerId, const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b)
{
	auto event        = std::make_shared<DB_ITEM_SAVE_TWO_EVENT>();
	event->wakeupTime = Now();
	event->playerId   = playerId;
	event->a          = a;
	event->b          = b;
	ScheduleOnShard(std::move(event), ShardForPlayerId(playerId));
}

void DBThread::ProcessEvent(const shared_ptr<DB_EVENT_BASE>& event)
{
	ScopedDBConnection scopedConnection(GDBConnectionPool);
	const auto& connection = scopedConnection.Get();
	if (connection == nullptr)
		return;

	if (const auto e = std::dynamic_pointer_cast<DB_LOGIN_EVENT>(event))
	{
		const bool isRegistered = connection->IsUserRegistered(e->name);

		if (isRegistered) {
			// Skip password verification — dev prototype with no auth requirement
			DB_USER_INFO userInfo = connection->ExtractUserInfo(e->name);
			vector<DB_ITEM_INFO> items = connection->ExtractInventory(userInfo._playerId);
			GZoneManager->EnqueueByWorld(userInfo._x, userInfo._y, [session = e->session, userInfo, items]()
			{
				UserHelper::HandleGetUserInfo(session, userInfo, items);
			});
		}
		else {
			// playerId는 DB가 발급하는 값이라 User 객체를 만들기 전에 반드시 먼저
			// 확보해야 한다 — 그래서 계정 INSERT를 여기(DB 워커 스레드)에서 동기적으로
			// 끝내고, 결과 playerId를 쥔 채로 Zone 스레드에 진입시킨다. 이전에는 인메모리
			// User를 먼저 만들고 나중에(RequestAddUser) 비동기로 INSERT했는데, 그 방식은
			// User가 생겨서 월드에 들어간 뒤에도 한동안 DB에는 아직 그 계정 행이 없는
			// 창구가 있었고, 이번 최적화로 Inventory가 playerId를 즉시 필요로 하게 되면서
			// 그 창구가 실제 버그(0/미확정 playerId로 저장 시도)가 될 수 있어 없앴다.
			int playerId = 0;
			const bool added = connection->AddUserInfoInDataBase(e->name, e->password, 0, 0, 1, 0, playerId);
			if (!added)
			{
				UserHelper::HandleLoginFail(e->session);
				return;
			}

			// New users have no saved position — assign a zone via round-robin for
			// even load distribution. GetRandomPosition places the player within
			// that zone's bounds; UpdatePosition then registers the correct zone ID.
			static std::atomic<uint32_t> s_counter{ 0 };
			const ZoneId zoneId = static_cast<ZoneId>(
				s_counter.fetch_add(1, std::memory_order_relaxed) % ZoneLayout::ZoneCount);

			DB_USER_INFO info;
			info._playerId = playerId;
			info._name     = e->name;
			info._password = e->password;
			GZoneManager->EnqueueByZone(zoneId, [session = e->session, info]()
			{
				UserHelper::HandleAddUserInfo(session, info);
			});
		}
	}
	else if (const auto e = std::dynamic_pointer_cast<DB_SAVE_EVENT>(event))
	{
		connection->SaveUserInfo(e->name, e->x, e->y, e->level, e->exp);
	}
	else if (const auto e = std::dynamic_pointer_cast<DB_ITEM_SAVE_EVENT>(event))
	{
		connection->SaveInventorySlot(e->playerId, e->slotIndex, e->itemId, e->count, e->equipped);
	}
	else if (const auto e = std::dynamic_pointer_cast<DB_ITEM_DELETE_EVENT>(event))
	{
		connection->DeleteInventorySlot(e->playerId, e->slotIndex);
	}
	else if (const auto e = std::dynamic_pointer_cast<DB_ITEM_SAVE_TWO_EVENT>(event))
	{
		connection->SaveTwoInventorySlots(e->playerId, e->a, e->b);
	}
}

void DBThread::DoDataBase(int shardIndex)
{
	ASSERT_CRASH(shardIndex >= 0 && shardIndex < kShardCount);
	Shard& shard = _shards[shardIndex];

	std::unique_lock lock(shard.lock);

	while (true) {
		shard.cv.wait(lock, [&shard]() { return !shard.events.empty(); });

		while (!shard.events.empty()) {
			const auto nextWakeup = shard.events.top()->wakeupTime;
			const bool rescheduledEarlierEvent = shard.cv.wait_until(lock, nextWakeup, [&shard, nextWakeup]()
			{
				return shard.events.empty() || shard.events.top()->wakeupTime < nextWakeup;
			});

			if (shard.events.empty())
				break;
			if (rescheduledEarlierEvent)
				continue;
			if (shard.events.top()->wakeupTime > Now())
				continue;

			auto event = shard.events.top();
			shard.events.pop();

			// 이 샤드는 스레드 하나가 전담하므로, 여기서 락을 놓고 ODBC 왕복을
			// 하는 동안 같은 샤드의 다음 이벤트를 다른 스레드가 먼저 처리해버리는
			// 일이 없다 — 그래서 같은 플레이어의 저장이 항상 넣은 순서대로 커밋된다.
			lock.unlock();
			ProcessEvent(event);
			lock.lock();
		}
	}
}
