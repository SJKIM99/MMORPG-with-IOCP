#pragma once

class SocketManager;
class DBThread;
class Sector;
class TimerThread;
class AStar;

class WorkerThread
{
public:
	WorkerThread() = default;
	~WorkerThread() = default;

	void			Disconnect(ObjID clientId);
	void			DoWork();
};
