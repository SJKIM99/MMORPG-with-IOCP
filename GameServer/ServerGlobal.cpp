#include "pch.h"
#include "ServerGlobal.h"
#include "Sector.h"
#include "DBThread.h"
#include "GameLogicThread.h"
#include "WorkerThread.h"
#include "TimerThread.h"
#include "NPC.h"
#include "GameSessionManager.h"

DBThread*		GDBThread = nullptr;
Sector*			GSector = nullptr;
GameLogicThread* GGameLogicThread = nullptr;
WorkerThread*	GWorkerThread = nullptr;
TimerThread*	GTimerThread = nullptr;
NPC*			GNPC = nullptr;
GameSessionManager* GSessionManager = nullptr;
class ServerGlobal
{
public:
	ServerGlobal()
	{
		GSector = new Sector();
		GDBThread = new DBThread();
		GGameLogicThread = new GameLogicThread();
		GWorkerThread = new WorkerThread();
		GTimerThread = new TimerThread();
		GNPC = new NPC();
		GSessionManager = new GameSessionManager();
	}
	~ServerGlobal()
	{
		delete GSector;
		delete GDBThread;
		delete GGameLogicThread;
		delete GWorkerThread;
		delete GTimerThread;
		delete GNPC;
		delete GSessionManager;
	}
}GServerGlobal;
