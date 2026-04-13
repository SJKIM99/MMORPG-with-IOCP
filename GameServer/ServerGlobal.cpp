#include "pch.h"
#include "ServerGlobal.h"
#include "GameObjectManager.h"
#include "Sector.h"
#include "DBThread.h"
#include "GameLogicThread.h"
#include "WorkerThread.h"
#include "TimerThread.h"
#include "GameSessionManager.h"

shared_ptr<DBThread>           GDBThread = nullptr;
shared_ptr<Sector>             GSector = nullptr;
shared_ptr<GameLogicThread>    GGameLogicThread = nullptr;
shared_ptr<WorkerThread>       GWorkerThread = nullptr;
shared_ptr<TimerThread>        GTimerThread = nullptr;
shared_ptr<GameSessionManager> GSessionManager = nullptr;
shared_ptr<GameObjectManager>  GGameObjectManager = nullptr;

class ServerGlobal
{
public:
    ServerGlobal()
    {
        GGameObjectManager = make_shared<GameObjectManager>();
        GSector = make_shared<Sector>();
        GDBThread = make_shared<DBThread>();
        GGameLogicThread = make_shared<GameLogicThread>();
        GWorkerThread = make_shared<WorkerThread>();
        GTimerThread = make_shared<TimerThread>();
        GSessionManager = make_shared<GameSessionManager>();
    }

    ~ServerGlobal()
    {
        GSessionManager.reset();
        GTimerThread.reset();
        GWorkerThread.reset();
        GGameLogicThread.reset();
        GDBThread.reset();
        GSector.reset();
        GGameObjectManager.reset();
    }
} GServerGlobal;
