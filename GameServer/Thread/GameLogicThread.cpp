#include "pch.h"
#include "GameLogicThread.h"

void GameLogicThread::Enqueue(Task task)
{
	{
		std::scoped_lock lock(_queueLock);
		_queue.push(std::move(task));
	}
	_cv.notify_one();
}

void GameLogicThread::Run()
{
	std::queue<Task> localQueue;

	while (true)
	{
		{
			std::unique_lock lock(_queueLock);

			_cv.wait(lock, [this]() { return !_queue.empty(); });
			_queue.swap(localQueue);
		} 

		// 락이 완전히 해제된 상태에서 큐에 쌓인 비즈니스 로직(Task) 연속 처리
		while (!localQueue.empty())
		{
			Task task = std::move(localQueue.front());
			localQueue.pop();

			if (task)
				task();
		}
	}
}