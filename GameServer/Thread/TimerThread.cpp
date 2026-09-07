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
	// 이 이벤트를 나중에 어느 Zone 큐로 보낼지 여기서 미리 찍어둔다.
	// 예약을 거는 쪽은 사실상 언제나 그 오브젝트를 소유한 Zone 스레드다 —
	// 몬스터 예약은 전부 해당 몬스터의 Zone 핸들러 안에서 일어나고, 필드 아이템
	// 예약도 아이템을 떨어뜨린 그 Zone 스레드에서 일어난다. 그래서 thread_local
	// LCurrentZoneId를 읽는 것만으로 목적지가 정해지고, Dispatch가 전역 조회표를
	// 뒤질 필요가 없어진다(동기화 비용 0).
	//
	// Zone 스레드가 아닌 곳에서 예약하면(로그인 처리 등) InvalidZoneId가 찍히고,
	// 그 경우 Dispatch가 기존처럼 조회 경로로 되돌아간다.
	timerEvent.zoneId = LCurrentZoneId;

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

	// 예약 시점에 찍어둔 Zone으로 곧장 넣는다. EnqueueByObject는 전역 조회표의
	// 배타 뮤텍스를 잡는데, 깨어 있는 몬스터가 저마다 0.5초에 한 번씩 이벤트를
	// 걸기 때문에 이 함수 하나 때문에 그 락이 서버에서 가장 자주 잡히는 락이 된다.
	//
	// 힌트를 믿어도 되는 범위 — 몬스터는 Zone을 벗어날 수 없고(RandomMove가 존
	// 밖 걸음을 거부하고, 어그로 추격은 ZONE_BOUNDARY_MARGIN 안에서 되돌아간다),
	// 필드 아이템은 떨어진 자리에서 움직이지 않는다. 그래서 이 둘은 예약 시점의
	// Zone이 곧 실행 시점의 Zone이다.
	//
	// 반면 플레이어는 회복(5초)이나 부활(30초)을 기다리는 사이에 Zone 경계를
	// 넘을 수 있어 힌트가 낡을 수 있다. 그때만 기존대로 조회해서 보낸다 —
	// 플레이어 이벤트는 몬스터 이벤트에 비해 빈도가 훨씬 낮아 이득의 대부분은
	// 그대로 남는다.
	const ZoneId hintedZone =
		(subjectId.GetCategory<EnumCategory>() == EnumCategory::eUser)
			? InvalidZoneId
			: timerEvent.zoneId;

	auto enqueue = [&](ZoneManager::Task task)
	{
		if (!ZoneLayout::IsValidZoneId(hintedZone))
		{
			GZoneManager->EnqueueByObject(subjectId, std::move(task));
			return;
		}

#ifdef _DEBUG
		// 위 전제가 훗날 깨지면(예: 몬스터가 Zone을 넘게 바뀌면) 남의 Zone
		// 스레드에서 조용히 실행되는 대신 도착 즉시 크래시로 드러나게 한다.
		// Release에서는 이 래핑 자체가 사라져 조회 없는 경로만 남는다.
		//
		// 대상이 이미 사라진 경우는 정상이다 — 필드 아이템이 소멸 타이머보다
		// 먼저 습득되면 핸들러가 "대상 없음"을 확인하고 조용히 넘어가는 설계라,
		// 살아 있을 때만 소유 Zone이 힌트와 같은지 따진다.
		GZoneManager->EnqueueByZone(hintedZone, [subjectId, inner = std::move(task)]()
		{
			const auto subject = ::GetGameObject<Subject>(subjectId);
			ASSERT_CRASH(subject == nullptr || subject->GetZoneId() == LCurrentZoneId);
			inner();
		});
#else
		GZoneManager->EnqueueByZone(hintedZone, std::move(task));
#endif
	};

	switch (timerEvent.event)
	{
	case TIMER_EVENT_TYPE::EV_RANOM_MOVE:
		enqueue([subjectId]()
		{
			MonsterHelper::HandleRandomMove(subjectId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_MONSTER_RESPAWN:
		enqueue([subjectId]()
		{
			MonsterHelper::HandleRespawn(subjectId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER:
		enqueue([subjectId, targetId]()
		{
			MonsterHelper::HandleAttackToPlayer(subjectId, targetId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_AGGRO_MOVE:
		enqueue([subjectId, targetId]()
		{
			MonsterHelper::HandleAggroMove(subjectId, targetId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_HEAL:
		enqueue([subjectId]()
		{
			UserHelper::HandleHeal(subjectId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_USER_RESPAWN:
		enqueue([subjectId]()
		{
			UserHelper::HandleRespawn(subjectId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_ITEM_DESPAWN:
		enqueue([subjectId]()
		{
			ItemHelper::HandleDespawn(subjectId);
		});
		break;

	case TIMER_EVENT_TYPE::EV_ITEM_LOOT_PRIORITY_EXPIRE:
		enqueue([subjectId]()
		{
			ItemHelper::HandleLootPriorityExpire(subjectId);
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
