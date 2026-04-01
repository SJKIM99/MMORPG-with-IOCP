#include "pch.h"
#include "TimerThread.h"
#include "GameLogicThread.h"
#include "User.h"
#include "WorkerThread.h"

bool TimerThread::TimerEventCompare::operator()(const TIMER_EVENT& lhs, const TIMER_EVENT& rhs) const noexcept
{
	if (lhs.wakeup_time != rhs.wakeup_time)
		return lhs.wakeup_time > rhs.wakeup_time;

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

void TimerThread::ScheduleNow(uint32 playerId, TIMER_EVENT_TYPE eventType, uint32 aiTargetId)
{
	ScheduleAfter(playerId, 0, Duration::zero(), eventType, aiTargetId);
}

void TimerThread::ScheduleNow(uint32 playerId, uint64 sourceEpoch, TIMER_EVENT_TYPE eventType, uint32 aiTargetId)
{
	ScheduleAfter(playerId, sourceEpoch, Duration::zero(), eventType, aiTargetId);
}

void TimerThread::ScheduleAfter(uint32 playerId, Duration delay, TIMER_EVENT_TYPE eventType, uint32 aiTargetId)
{
	ScheduleAfter(playerId, 0, delay, eventType, aiTargetId);
}

void TimerThread::ScheduleAfter(uint32 playerId, uint64 sourceEpoch, Duration delay, TIMER_EVENT_TYPE eventType, uint32 aiTargetId)
{
	TIMER_EVENT timerEvent{};
	timerEvent.player_id = playerId;
	timerEvent.wakeup_time = Now() + delay;
	timerEvent.event = eventType;
	timerEvent.aiTargetId = aiTargetId;
	timerEvent.sourceEpoch = sourceEpoch;

	Schedule(std::move(timerEvent));
}

void TimerThread::Dispatch(const TIMER_EVENT& timerEvent)
{
	if (timerEvent.sourceEpoch != 0 && GClients[timerEvent.player_id]->GetTimerEpoch() != timerEvent.sourceEpoch)
		return;

	switch (timerEvent.event) {
	case TIMER_EVENT_TYPE::EV_RANOM_MOVE: {
		GGameLogicThread->Enqueue([npcId = timerEvent.player_id]()
		{
			GWorkerThread->HandleNpcRandomMove(npcId);
		});
		break;
	}
	case TIMER_EVENT_TYPE::EV_NPC_RESPAWN: {
		GGameLogicThread->Enqueue([npcId = timerEvent.player_id]()
		{
			GWorkerThread->HandleNpcRespawn(npcId);
		});
		break;
	}
	case TIMER_EVENT_TYPE::EV_NPC_ATTACK_TO_PLAYER: {
		GGameLogicThread->Enqueue([npcId = timerEvent.player_id, playerId = timerEvent.aiTargetId]()
		{
			GWorkerThread->HandleNpcAttackToPlayer(npcId, playerId);
		});
		break;
	}
	case TIMER_EVENT_TYPE::EV_HEAL: {
		GGameLogicThread->Enqueue([playerId = timerEvent.player_id]()
		{
			GWorkerThread->HandleHeal(playerId);
		});
		break;
	}
	case TIMER_EVENT_TYPE::EV_PLAYER_RESPAWN: {
		GGameLogicThread->Enqueue([playerId = timerEvent.player_id]()
		{
			GWorkerThread->HandlePlayerRespawn(playerId);
		});
		break;
	}
	case TIMER_EVENT_TYPE::EV_AGGRO_MOVE: {
		GGameLogicThread->Enqueue([npcId = timerEvent.player_id, playerId = timerEvent.aiTargetId]()
		{
			GWorkerThread->HandleNpcAggroMove(npcId, playerId);
		});
		break;
	}
	}
}

void TimerThread::DoTimer()
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

			TIMER_EVENT timerEvent = _events.top();
			_events.pop();

			lock.unlock();
			Dispatch(timerEvent);
			lock.lock();
		}
	}
}
