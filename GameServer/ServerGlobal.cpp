#include "pch.h"
#include "ServerGlobal.h"
#include "GameObjectManager.h"
#include "Sector.h"
#include "DBThread.h"
#include "GameLogicThread.h"
#include "WorkerThread.h"
#include "TimerThread.h"
#include "GameSessionManager.h"

DBThread*            GDBThread       = nullptr;
Sector*              GSector         = nullptr;
GameLogicThread*     GGameLogicThread = nullptr;
WorkerThread*        GWorkerThread   = nullptr;
TimerThread*         GTimerThread    = nullptr;
GameSessionManager*  GSessionManager = nullptr;
GameObjectManager*   GObjectManager  = nullptr;

class ServerGlobal
{
public:
    ServerGlobal()
    {
        GObjectManager  = new GameObjectManager();
        GSector         = new Sector();
        GDBThread       = new DBThread();
        GGameLogicThread = new GameLogicThread();
        GWorkerThread   = new WorkerThread();
        GTimerThread    = new TimerThread();
        GSessionManager = new GameSessionManager();
    }
    ~ServerGlobal()
    {
        delete GObjectManager;
        delete GSector;
        delete GDBThread;
        delete GGameLogicThread;
        delete GWorkerThread;
        delete GTimerThread;
        delete GSessionManager;
    }
} GServerGlobal;
