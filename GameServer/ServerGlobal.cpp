#include "pch.h"
#include "ServerGlobal.h"
#include "GameObjectManager.h"
#include "DBThread.h"
#include "GameLogicThread.h"
#include "Zone/ZoneManager.h"
#include "WorkerThread.h"
#include "TimerThread.h"
#include "GameSessionManager.h"

shared_ptr<DBThread>           GDBThread = nullptr;
thread_local Sector*           GSector = nullptr;
thread_local ZoneId            LCurrentZoneId = InvalidZoneId;
shared_ptr<GameLogicThread>    GGameLogicThread = nullptr;
shared_ptr<ZoneManager>        GZoneManager = nullptr;
shared_ptr<WorkerThread>       GWorkerThread = nullptr;
shared_ptr<TimerThread>        GTimerThread = nullptr;
shared_ptr<GameSessionManager> GSessionManager = nullptr;
shared_ptr<GameObjectManager>  GGameObjectManager = nullptr;

// Previously in Core/CoreGlobal.cpp (now defined here since Core is removed)
shared_ptr<ThreadManager>    GThreadManager    = make_shared<ThreadManager>();
shared_ptr<DBConnectionPool> GDBConnectionPool = make_shared<DBConnectionPool>();

class ServerGlobal
{
public:
    ServerGlobal()
    {
        GGameObjectManager = make_shared<GameObjectManager>();
        GDBThread = make_shared<DBThread>();
        GGameLogicThread = make_shared<GameLogicThread>();
        GZoneManager = make_shared<ZoneManager>(GGameLogicThread);
        GWorkerThread = make_shared<WorkerThread>();
        GTimerThread = make_shared<TimerThread>();
        GSessionManager = make_shared<GameSessionManager>();
    }

    ~ServerGlobal()
    {
        GSessionManager.reset();
        GTimerThread.reset();
        GWorkerThread.reset();
        GZoneManager.reset();
        GGameLogicThread.reset();
        GDBThread.reset();
        GGameObjectManager.reset();
    }
} GServerGlobal;
