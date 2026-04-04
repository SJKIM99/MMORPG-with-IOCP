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
	struct TaskNode
	{
		explicit TaskNode(Task&& taskValue) : task(move(taskValue)) { }

		atomic<TaskNode*> next = nullptr;
		Task task;
	};

	class Queue
	{
	public:
		Queue();
		~Queue();

		void Push(TaskNode* node);
		TaskNode* Pop();

	private:
		TaskNode* _stub = nullptr;
		atomic<TaskNode*> _head = nullptr;
		TaskNode* _tail = nullptr;
	};

private:
	void Drain();

private:
	Queue          _queue;
	HANDLE         _wakeEvent = nullptr;
	Atomic<bool>   _sleeping{ false };
};

extern class GameLogicThread* GGameLogicThread;
