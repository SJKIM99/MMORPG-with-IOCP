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
	void ScheduleNow(const ObjID& subjectId, TIMER_EVENT_TYPE eventType);
	void ScheduleNow(const ObjID& subjectId, TIMER_EVENT_TYPE eventType, const ObjID& targetId);
	void ScheduleAfter(const ObjID& subjectId, Duration delay, TIMER_EVENT_TYPE eventType);
	void ScheduleAfter(const ObjID& subjectId, Duration delay, TIMER_EVENT_TYPE eventType, const ObjID& targetId);

	[[nodiscard]] static Clock::time_point Now() noexcept
	{
		return Clock::now();
	}

private:
	struct TimerEventCompare
	{
		bool operator()(const TIMER_EVENT& lhs, const TIMER_EVENT& rhs) const noexcept;
	};

	[[nodiscard]] bool ShouldNotifyForNewEventLocked(const TIMER_EVENT& timerEvent) const;
	void DrainReadyEventsLocked(std::vector<TIMER_EVENT>& readyEvents, Clock::time_point now);
	void Dispatch(const TIMER_EVENT& timerEvent);

private:
	std::priority_queue<TIMER_EVENT, std::vector<TIMER_EVENT>, TimerEventCompare> _events;
	std::mutex _lock;
	std::condition_variable _cv;
	uint64_t _nextSequence = 0;
};

