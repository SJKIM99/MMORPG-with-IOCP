#pragma once

#include <memory>

// Forward declaration only; full definition requires Subject.h,
// which needs IO_TYPE/SOCKET_STATE defined in pch.h first.
class GameObjectManager;
extern std::shared_ptr<GameObjectManager> GGameObjectManager;

extern std::shared_ptr<class DBThread>           GDBThread;
extern std::shared_ptr<class Sector>             GSector;
extern std::shared_ptr<class GameLogicThread>    GGameLogicThread;
extern std::shared_ptr<class WorkerThread>       GWorkerThread;
extern std::shared_ptr<class TimerThread>        GTimerThread;
extern std::shared_ptr<class GameSessionManager> GSessionManager;

// Previously defined in Core/CoreGlobal.cpp (removed with Core library)
extern std::shared_ptr<class ThreadManager>    GThreadManager;
extern std::shared_ptr<class DBConnectionPool> GDBConnectionPool;
