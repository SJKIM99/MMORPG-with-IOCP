#include "pch.h"
#include "TimerThread.h"
#include "GameObjectManager.h"
#include "MonsterHelper.h"
#include "UserHelper.h"
#include "Item/ItemHelper.h"
#include "Zone/ZoneManager.h"

bool TimerThread::TimerEventCompare::operator()(const TIMER_EVENT& lhs, const TIMER_EVENT& rhs) const noexcept
{
	if (lhs.wakeupTime != rhs.wakeupTime)
		return lhs.wakeupTime > rhs.wakeupTime;

	return lhs.sequence > rhs.sequence;
}

bool TimerThread::ShouldNotifyForNewEventLocked(const TIMER_EVENT& timerEvent) const
{
	if (_events.empty())
		return true;

	return timerEvent.wakeupTime < _events.top().wakeupTime;
}

void TimerThread::DrainReadyEventsLocked(std::vector<TIMER_EVENT>& readyEvents, Clock::time_point now)
{
	while (!_events.empty() && _events.top().wakeupTime <= now)
	{
		readyEvents.push_back(_events.top());
		_events.pop();
	}
}

void TimerThread::Schedule(TIMER_EVENT timerEvent)
{
	bool shouldNotify = false;
	{
		std::scoped_lock lock(_lock);
		timerEvent.sequence = _nextSequence++;
		shouldNotify = ShouldNotifyForNewEventLocked(timerEvent);
		_events.push(std::move(timerEvent));
	}

	if (shouldNotify)
		_cv.notify_one();
}

void TimerThread::ScheduleNow(const ObjID& subjectId, TIMER_EVENT_TYPE eventType)
{
	ScheduleAfter(subjectId, Duration::zero(), eventType);
}

void TimerThread::ScheduleNow(const ObjID& subjectId, TIMER_EVENT_TYPE eventType, const ObjID& targetId)
{
	ScheduleAfter(subjectId, Duration::zero(), eventType, targetId);
}

void TimerThread::ScheduleAfter(const ObjID& subjectId, Duration delay, TIMER_EVENT_TYPE eventType)
{
	TIMER_EVENT timerEvent{};
	timerEvent.subjectId = subjectId;
	timerEvent.wakeupTime = Now() + delay;
	timerEvent.event = eventType;

	Schedule(std::move(timerEvent));
}

void TimerThread::ScheduleAfter(const ObjID& subjectId, Duration delay, TIMER_EVENT_TYPE eventType, const ObjID& targetId)
{
	TIMER_EVENT timerEvent{};
	timerEvent.subjectId = subjectId;
	timerEvent.wakeupTime = Now() + delay;
	timerEvent.event = eventType;
	timerEvent.targetId = targetId;

	Schedule(std::move(timerEvent));
}

void TimerThread::Dispatch(const TIMER_EVENT& timerEvent)
{
	const ObjID subjectId = timerEvent.subjectId;
	const ObjID targetId  = timerEvent.targetId;

	switch (timerEvent.event)
	{
	case TIMER_EVENT_TYPE::EV_RANOM_MOVE:
		GZoneManager->EnqueueByObject(subjectId, [subjectId]()
		{
			MonsterHelper::HandleRandomMove(subjectId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_MONSTER_RESPAWN:
		GZoneManager->EnqueueByObject(subjectId, [subjectId]()
		{
			MonsterHelper::HandleRespawn(subjectId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER:
		GZoneManager->EnqueueByObject(subjectId, [subjectId, targetId]()
		{
			MonsterHelper::HandleAttackToPlayer(subjectId, targetId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_AGGRO_MOVE:
		GZoneManager->EnqueueByObject(subjectId, [subjectId, targetId]()
		{
			MonsterHelper::HandleAggroMove(subjectId, targetId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_HEAL:
		GZoneManager->EnqueueByObject(subjectId, [subjectId]()
		{
			UserHelper::HandleHeal(subjectId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_USER_RESPAWN:
		GZoneManager->EnqueueByObject(subjectId, [subjectId]()
		{
			UserHelper::HandleRespawn(subjectId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_ITEM_DESPAWN:
		GZoneManager->EnqueueByObject(subjectId, [subjectId]()
		{
			ItemHelper::HandleDespawn(subjectId);
		});
		break;
	}
}

void TimerThread::DoTimer()
{
	std::unique_lock lock(_lock);
	std::vector<TIMER_EVENT> readyEvents;
	readyEvents.reserve(32);

	while (true)
	{
		_cv.wait(lock, [this]()
		{
			return _events.empty() == false;
		});

		while (!_events.empty())
		{
			const auto now = Now();
			if (_events.top().wakeupTime > now)
			{
				const auto nextWakeup = _events.top().wakeupTime;
				_cv.wait_until(lock, nextWakeup, [this, nextWakeup]()
				{
					return _events.empty() || _events.top().wakeupTime < nextWakeup;
				});
				continue;
			}

			readyEvents.clear();
			DrainReadyEventsLocked(readyEvents, now);

			lock.unlock();
			for (const TIMER_EVENT& timerEvent : readyEvents)
				Dispatch(timerEvent);
			lock.lock();
		}
	}
}
