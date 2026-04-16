#pragma once

class DBConnectionPool;
class GameSession;

class DBThread
{
public:
	using Clock    = std::chrono::steady_clock;
	using Duration = Clock::duration;

	DBThread() = default;
	~DBThread() = default;

	void DoDataBase();
	void RequestLogin(const shared_ptr<GameSession>& session, const std::string& name, const std::string& password);
	void RequestAddUser(const ObjID& subjectId, const DB_USER_INFO& info);
	void RequestSaveUser(const ObjID& subjectId, const DB_USER_INFO& info);
	void Schedule(shared_ptr<DB_EVENT_BASE> event);

	[[nodiscard]] static Clock::time_point Now() noexcept { return Clock::now(); }

private:
	struct DBEventCompare
	{
		bool operator()(const shared_ptr<DB_EVENT_BASE>& lhs, const shared_ptr<DB_EVENT_BASE>& rhs) const noexcept;
	};

	void ProcessEvent(const shared_ptr<DB_EVENT_BASE>& event);

private:
	std::priority_queue<shared_ptr<DB_EVENT_BASE>, std::vector<shared_ptr<DB_EVENT_BASE>>, DBEventCompare> _events;
	std::mutex             _lock;
	std::condition_variable _cv;
	uint64_t               _nextSequence = 0;
};
