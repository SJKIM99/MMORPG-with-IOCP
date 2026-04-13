#include "pch.h"
#include "TimerThread.h"
#include "GameLogicThread.h"
#include "GameObjectManager.h"
#include "MonsterHelper.h"
#include "UserHelper.h"

bool TimerThread::TimerEventCompare::operator()(const TIMER_EVENT& lhs, const TIMER_EVENT& rhs) const noexcept
{
	if (lhs.wakeupTime != rhs.wakeupTime)
		return lhs.wakeupTime > rhs.wakeupTime;

	return lhs.sequence > rhs.sequence;
}

void TimerThread::Schedule(TIMER_EVENT timerEvent)
{
	{
		std::scoped_lock lock(_lock);
		timerEvent.sequence = _nextSequence++;
		_events.push(std::move(timerEvent));
	}

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
		GGameLogicThread->Enqueue([subjectId]()
		{
			MonsterHelper::HandleRandomMove(subjectId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_MONSTER_RESPAWN:
		GGameLogicThread->Enqueue([subjectId]()
		{
			MonsterHelper::HandleRespawn(subjectId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER:
		GGameLogicThread->Enqueue([subjectId, targetId]()
		{
			MonsterHelper::HandleAttackToPlayer(subjectId, targetId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_AGGRO_MOVE:
		GGameLogicThread->Enqueue([subjectId, targetId]()
		{
			MonsterHelper::HandleAggroMove(subjectId, targetId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_HEAL:
		GGameLogicThread->Enqueue([subjectId]()
		{
			UserHelper::HandleHeal(subjectId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_USER_RESPAWN:
		GGameLogicThread->Enqueue([subjectId]()
		{
			UserHelper::HandleRespawn(subjectId);
		});
		break;
	}
}

void TimerThread::DoTimer()
{
	std::unique_lock lock(_lock);

	while (true)
	{
		_cv.wait(lock, [this]()
		{
			return _events.empty() == false;
		});

		while (!_events.empty())
		{
			if (_events.top().wakeupTime > Now())
			{
				const auto nextWakeup = _events.top().wakeupTime;
				_cv.wait_until(lock, nextWakeup, [this, nextWakeup]()
				{
					return _events.empty() || _events.top().wakeupTime < nextWakeup;
				});
				continue;
			}

			TIMER_EVENT timerEvent = _events.top();
			_events.pop();

			lock.unlock();
			Dispatch(timerEvent);
			lock.lock();
		}
	}
}
