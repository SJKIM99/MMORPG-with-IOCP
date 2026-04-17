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
	[[nodiscard]] bool Drain();

private:
	mutex              _queueLock;
	queue<Task>        _queue;
	HANDLE             _wakeEvent = nullptr;
	atomic<bool>       _sleeping{ false };
};

extern shared_ptr<GameLogicThread> GGameLogicThread;
