#pragma once

class GameLogicThread
{
public:
	using Task = function<void()>;

public:
	GameLogicThread();
	~GameLogicThread();

	void Enqueue(Task task);
	void Run();

private:
	void Drain();

private:
	mutex       _queueLock;
	queue<Task> _queue;
	HANDLE      _wakeEvent = nullptr;
};

extern shared_ptr<GameLogicThread> GGameLogicThread;
