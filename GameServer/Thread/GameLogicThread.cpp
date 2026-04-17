#include "pch.h"
#include "GameLogicThread.h"

GameLogicThread::GameLogicThread()
{
	_wakeEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
	ASSERT_CRASH(_wakeEvent != nullptr);
}

GameLogicThread::~GameLogicThread()
{
	if (_wakeEvent != nullptr)
	{
		::CloseHandle(_wakeEvent);
		_wakeEvent = nullptr;
	}
}

void GameLogicThread::Enqueue(Task task)
{
	{
		scoped_lock lock(_queueLock);
		_queue.push(move(task));
	}

	// 스레드가 실제로 잠든 경우에만 SetEvent 호출.
	// seq_cst 로딩으로 Run()의 _sleeping=true store와 전체 순서를 보장.
	if (_sleeping.load(memory_order_seq_cst))
		::SetEvent(_wakeEvent);
}

void GameLogicThread::Run()
{
	while (true)
	{
		if (Drain() == false)
		{
			_sleeping.store(true, memory_order_seq_cst);

			// _sleeping=true 이전에 Enqueue된 태스크를 놓치지 않기 위해
			// 두 번째 Drain으로 double-check.
			if (Drain() == false)
				::WaitForSingleObject(_wakeEvent, INFINITE);

			_sleeping.store(false, memory_order_relaxed);
		}
	}
}

bool GameLogicThread::Drain()
{
	queue<Task> localQueue;
	{
		scoped_lock lock(_queueLock);
		if (_queue.empty())
			return false;

		_queue.swap(localQueue);
	}

	while (!localQueue.empty())
	{
		Task task = move(localQueue.front());
		localQueue.pop();
		task();
	}

	return true;
}
