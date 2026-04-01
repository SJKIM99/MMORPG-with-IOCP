#include "pch.h"
#include "GameLogicThread.h"

GameLogicThread::Queue::Queue()
{
	_stub = new TaskNode([] {});
	_head.store(_stub);
	_tail = _stub;
}

GameLogicThread::Queue::~Queue()
{
	while (TaskNode* node = Pop())
		delete node;

	delete _stub;
	_stub = nullptr;
	_tail = nullptr;
}

void GameLogicThread::Queue::Push(TaskNode* node)
{
	node->next.store(nullptr, memory_order_relaxed);
	TaskNode* prev = _head.exchange(node, memory_order_acq_rel);
	prev->next.store(node, memory_order_release);
}

GameLogicThread::TaskNode* GameLogicThread::Queue::Pop()
{
	TaskNode* tail = _tail;
	TaskNode* next = tail->next.load(memory_order_acquire);

	if (tail == _stub)
	{
		if (next == nullptr)
			return nullptr;

		_tail = next;
		tail = next;
		next = tail->next.load(memory_order_acquire);
	}

	if (next != nullptr)
	{
		_tail = next;
		return tail;
	}

	TaskNode* head = _head.load(memory_order_acquire);
	if (tail != head)
		return nullptr;

	Push(_stub);
	next = tail->next.load(memory_order_acquire);
	if (next != nullptr)
	{
		_tail = next;
		return tail;
	}

	return nullptr;
}

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
	TaskNode* node = new TaskNode(move(task));
	_queue.Push(node);
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
	while (TaskNode* node = _queue.Pop())
	{
		node->task();
		delete node;
	}
}
