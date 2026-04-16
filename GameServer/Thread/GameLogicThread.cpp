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

	::SetEvent(_wakeEvent);
}

void GameLogicThread::Run()
{
	while (true)
	{
		Drain();
		::WaitForSingleObject(_wakeEvent, INFINITE);
	}
}

void GameLogicThread::Drain()
{
	while (true)
	{
		Task task;
		{
			scoped_lock lock(_queueLock);
			if (_queue.empty())
				return;

			task = move(_queue.front());
			_queue.pop();
		}

		task();
	}
}
