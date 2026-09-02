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

void DBThread::Schedule(shared_ptr<DB_EVENT_BASE> event)
{
	{
		std::scoped_lock lock(_lock);
		event->sequence = _nextSequence++;
		_events.push(std::move(event));
	}
	_cv.notify_one();
}

void DBThread::RequestLogin(const shared_ptr<GameSession>& session, const std::string& name, const std::string& password)
{
	auto event      = std::make_shared<DB_LOGIN_EVENT>();
	event->wakeupTime = Now();
	event->session  = session;
	event->name     = name;
	event->password = password;
	Schedule(std::move(event));
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
	Schedule(std::move(event));
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
	Schedule(std::move(event));
}

void DBThread::RequestDeleteItem(int playerId, uint16_t slotIndex)
{
	auto event        = std::make_shared<DB_ITEM_DELETE_EVENT>();
	event->wakeupTime = Now();
	event->playerId   = playerId;
	event->slotIndex  = slotIndex;
	Schedule(std::move(event));
}

void DBThread::RequestSaveTwoItems(int playerId, const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b)
{
	auto event        = std::make_shared<DB_ITEM_SAVE_TWO_EVENT>();
	event->wakeupTime = Now();
	event->playerId   = playerId;
	event->a          = a;
	event->b          = b;
	Schedule(std::move(event));
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

void DBThread::DoDataBase()
{
	std::unique_lock lock(_lock);

	while (true) {
		_cv.wait(lock, [this]() { return !_events.empty(); });

		while (!_events.empty()) {
			const auto nextWakeup = _events.top()->wakeupTime;
			const bool rescheduledEarlierEvent = _cv.wait_until(lock, nextWakeup, [this, nextWakeup]()
			{
				return _events.empty() || _events.top()->wakeupTime < nextWakeup;
			});

			if (_events.empty())
				break;
			if (rescheduledEarlierEvent)
				continue;
			if (_events.top()->wakeupTime > Now())
				continue;

			auto event = _events.top();
			_events.pop();

			lock.unlock();
			ProcessEvent(event);
			lock.lock();
		}
	}
}
