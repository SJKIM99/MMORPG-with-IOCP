#pragma once

class DBConnectionPool;

class DBThread
{
public:
	using Clock = std::chrono::steady_clock;
	using Duration = Clock::duration;

	DBThread() = default;
	~DBThread() = default;

	void DoDataBase();
	void RequestLogin(uint32 playerId, uint64 sessionToken, const std::string& playerName, const std::string& password);
	void RequestAddPlayer(uint32 playerId, const DB_PLAYER_INFO& playerInfo);
	void RequestSavePlayer(uint32 playerId, const DB_PLAYER_INFO& playerInfo);
	void Schedule(DB_EVENT event);
	void ScheduleNow(uint32 playerId, DB_EVENT_TYPE eventType, DB_PLAYER_INFO playerInfo = {}, uint64 sessionToken = 0);
	void ScheduleAfter(uint32 playerId, Duration delay, DB_EVENT_TYPE eventType, DB_PLAYER_INFO playerInfo = {}, uint64 sessionToken = 0);

	[[nodiscard]] static Clock::time_point Now() noexcept
	{
		return Clock::now();
	}

private:
	struct DBEventCompare
	{
		bool operator()(const DB_EVENT& lhs, const DB_EVENT& rhs) const noexcept;
	};

	void ProcessEvent(const DB_EVENT& event);

private:
	std::priority_queue<DB_EVENT, std::vector<DB_EVENT>, DBEventCompare> _events;
	std::mutex _lock;
	std::condition_variable _cv;
	uint64 _nextSequence = 0;
};
