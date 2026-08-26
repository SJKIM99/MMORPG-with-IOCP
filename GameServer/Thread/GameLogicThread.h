#pragma once

class GameLogicThread
{
public:
	using Task = std::function<void()>;

public:
	GameLogicThread() = default;
	~GameLogicThread() = default;

	void Enqueue(Task task);
	void Run();

private:
	std::mutex              _queueLock;
	std::condition_variable _cv;
	std::queue<Task>        _queue;
};

extern std::shared_ptr<GameLogicThread> GGameLogicThread;