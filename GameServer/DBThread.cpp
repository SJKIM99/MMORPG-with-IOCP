#include "pch.h"
#include "DBThread.h"
#include "DBConnectionPool.h"
#include "GameLogicThread.h"
#include "WorkerThread.h"

namespace
{
	class ScopedDBConnection
	{
	public:
		explicit ScopedDBConnection(DBConnectionPool& pool) : _pool(pool), _connection(pool.Pop())
		{
		}

		~ScopedDBConnection()
		{
			if (_connection != nullptr)
				_pool.Push(_connection);
		}

		[[nodiscard]] DBConnection* Get() const noexcept
		{
			return _connection;
		}

		DBConnection* operator->() const noexcept
		{
			return _connection;
		}

	private:
		DBConnectionPool& _pool;
		DBConnection* _connection = nullptr;
	};
}

bool DBThread::DBEventCompare::operator()(const DB_EVENT& lhs, const DB_EVENT& rhs) const noexcept
{
	if (lhs.wakeup_time != rhs.wakeup_time)
		return lhs.wakeup_time > rhs.wakeup_time;

	return lhs.sequence > rhs.sequence;
}

void DBThread::RequestLogin(uint32 playerId, uint64 sessionToken, const std::string& playerName, const std::string& password)
{
	DB_PLAYER_INFO playerInfo{};
	playerInfo._name     = playerName;
	playerInfo._password = password;
	ScheduleNow(playerId, DB_EVENT_TYPE::EV_LOGIN_PLAYER, std::move(playerInfo), sessionToken);
}

void DBThread::RequestAddPlayer(uint32 playerId, const DB_PLAYER_INFO& playerInfo)
{
	ScheduleNow(playerId, DB_EVENT_TYPE::EV_ADD_PLAYER_INFO, playerInfo);
}

void DBThread::RequestSavePlayer(uint32 playerId, const DB_PLAYER_INFO& playerInfo)
{
	ScheduleNow(playerId, DB_EVENT_TYPE::EV_SAVE_PLAYER_INFO, playerInfo);
}

void DBThread::Schedule(DB_EVENT event)
{
	{
		std::scoped_lock lock(_lock);
		event.sequence = _nextSequence++;
		_events.push(std::move(event));
	}

	_cv.notify_one();
}

void DBThread::ScheduleNow(uint32 playerId, DB_EVENT_TYPE eventType, DB_PLAYER_INFO playerInfo, uint64 sessionToken)
{
	ScheduleAfter(playerId, Duration::zero(), eventType, std::move(playerInfo), sessionToken);
}

void DBThread::ScheduleAfter(uint32 playerId, Duration delay, DB_EVENT_TYPE eventType, DB_PLAYER_INFO playerInfo, uint64 sessionToken)
{
	DB_EVENT event{};
	event.player_id = playerId;
	event.wakeup_time = Now() + delay;
	event.event = eventType;
	event.player_info = std::move(playerInfo);
	event.session_token = sessionToken;

	Schedule(std::move(event));
}

void DBThread::ProcessEvent(const DB_EVENT& event)
{
	ScopedDBConnection connection(*GDBConnectionPool);

	switch (event.event) {
	case DB_EVENT_TYPE::EV_LOGIN_PLAYER: {
		const bool isRegistered = connection->IsPlayerRegistered(event.player_info._name);

		if (isRegistered) {
			const bool passwordOk = connection->VerifyPlayerPassword(event.player_info._name, event.player_info._password);
			if (passwordOk) {
				DB_PLAYER_INFO playerInfo = connection->ExtractPlayerInfo(event.player_info._name);
				GGameLogicThread->Enqueue([playerId = event.player_id, sessionToken = event.session_token, playerInfo]()
				{
					GWorkerThread->HandleGetPlayerInfo(playerId, sessionToken, playerInfo);
				});
			}
			else {
				GGameLogicThread->Enqueue([playerId = event.player_id, sessionToken = event.session_token]()
				{
					GWorkerThread->HandleLoginFail(playerId, sessionToken);
				});
			}
		}
		else {
			// 신규 계정 생성
			GGameLogicThread->Enqueue([playerId = event.player_id, sessionToken = event.session_token, playerInfo = event.player_info]()
			{
				GWorkerThread->HandleAddPlayerInfo(playerId, sessionToken, playerInfo);
			});
		}
		break;
	}
	case DB_EVENT_TYPE::EV_SAVE_PLAYER_INFO: {
		connection->SavePlayerInfo(event.player_info._name, event.player_info._x, event.player_info._y);
		break;
	}
	case DB_EVENT_TYPE::EV_ADD_PLAYER_INFO: {
		connection->AddPlayerInfoInDataBase(
			event.player_info._name,
			event.player_info._password,
			static_cast<short>(event.player_info._x),
			static_cast<short>(event.player_info._y));
		break;
	}
	}
}

void DBThread::DoDataBase()
{
	std::unique_lock lock(_lock);

	while (true) {
		_cv.wait(lock, [this]()
		{
			return _events.empty() == false;
		});

		while (_events.empty() == false) {
			const auto nextWakeup = _events.top().wakeup_time;
			const bool rescheduledEarlierEvent = _cv.wait_until(lock, nextWakeup, [this, nextWakeup]()
			{
				return _events.empty() || _events.top().wakeup_time < nextWakeup;
			});

			if (_events.empty())
				break;

			if (rescheduledEarlierEvent)
				continue;

			if (_events.top().wakeup_time > Now())
				continue;

			DB_EVENT event = _events.top();
			_events.pop();

			lock.unlock();
			ProcessEvent(event);
			lock.lock();
		}
	}
}
