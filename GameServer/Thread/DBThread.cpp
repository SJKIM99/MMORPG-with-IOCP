#include "pch.h"
#include "DBThread.h"
#include "DBConnectionPool.h"
#include "GameLogicThread.h"
#include "UserHelper.h"

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

void DBThread::RequestAddUser(const ObjID& subjectId, const DB_USER_INFO& info)
{
	auto event        = std::make_shared<DB_ADD_EVENT>();
	event->wakeupTime = Now();
	event->subjectId  = subjectId;
	event->name       = info._name;
	event->password   = info._password;
	event->x          = static_cast<short>(info._x);
	event->y          = static_cast<short>(info._y);
	event->level      = info._level;
	event->exp        = info._exp;
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
			const bool passwordOk = connection->VerifyUserPassword(e->name, e->password);
			if (passwordOk) {
				DB_USER_INFO userInfo = connection->ExtractUserInfo(e->name);
				GGameLogicThread->Enqueue([session = e->session, userInfo]()
				{
					UserHelper::HandleGetUserInfo(session, userInfo);
				});
			}
			else {
				GGameLogicThread->Enqueue([session = e->session]()
				{
					UserHelper::HandleLoginFail(session);
				});
			}
		}
		else {
			GGameLogicThread->Enqueue([session = e->session, name = e->name, password = e->password]()
			{
				DB_USER_INFO info;
				info._name     = name;
				info._password = password;
				UserHelper::HandleAddUserInfo(session, info);
			});
		}
	}
	else if (const auto e = std::dynamic_pointer_cast<DB_SAVE_EVENT>(event))
	{
		connection->SaveUserInfo(e->name, e->x, e->y, e->level, e->exp);
	}
	else if (const auto e = std::dynamic_pointer_cast<DB_ADD_EVENT>(event))
	{
		connection->AddUserInfoInDataBase(e->name, e->password, e->x, e->y, e->level, e->exp);
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
