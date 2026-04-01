#pragma once

class TimerThread
{
public:
	using Clock = std::chrono::steady_clock;
	using Duration = Clock::duration;

	TimerThread() = default;
	~TimerThread() = default;

	void DoTimer();
	void Schedule(TIMER_EVENT timerEvent);
	void ScheduleNow(uint32 playerId, TIMER_EVENT_TYPE eventType, uint32 aiTargetId = 0);
	void ScheduleNow(uint32 playerId, uint64 sourceEpoch, TIMER_EVENT_TYPE eventType, uint32 aiTargetId = 0);
	void ScheduleAfter(uint32 playerId, Duration delay, TIMER_EVENT_TYPE eventType, uint32 aiTargetId = 0);
	void ScheduleAfter(uint32 playerId, uint64 sourceEpoch, Duration delay, TIMER_EVENT_TYPE eventType, uint32 aiTargetId = 0);

	[[nodiscard]] static Clock::time_point Now() noexcept
	{
		return Clock::now();
	}

private:
	struct TimerEventCompare
	{
		bool operator()(const TIMER_EVENT& lhs, const TIMER_EVENT& rhs) const noexcept;
	};

	void Dispatch(const TIMER_EVENT& timerEvent);

private:
	std::priority_queue<TIMER_EVENT, std::vector<TIMER_EVENT>, TimerEventCompare> _events;
	std::mutex _lock;
	std::condition_variable _cv;
	uint64 _nextSequence = 0;
};

