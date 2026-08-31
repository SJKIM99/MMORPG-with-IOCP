#pragma once

#include <memory>

#include "Zone/ZoneTypes.h"

// Forward declaration only; full definition requires Subject.h,
// which needs IO_TYPE/SOCKET_STATE defined in pch.h first.
class GameObjectManager;
extern std::shared_ptr<GameObjectManager> GGameObjectManager;

extern std::shared_ptr<class DBThread>           GDBThread;
extern thread_local class Sector*                GSector;
// 이 스레드가 담당하는 Zone. Zone::Run() 시작 시 1회 설정된다(GSector와 동일한 시점).
// ZoneManager::IsCurrentThreadOwner()가 "지금 이 스레드가 어떤 오브젝트의 소유
// Zone 스레드가 맞는가"를 판정하는 데 사용한다.
extern thread_local ZoneId                       LCurrentZoneId;
extern std::shared_ptr<class GameLogicThread>    GGameLogicThread;
extern std::shared_ptr<class ZoneManager>        GZoneManager;
extern std::shared_ptr<class WorkerThread>       GWorkerThread;
extern std::shared_ptr<class TimerThread>        GTimerThread;
extern std::shared_ptr<class GameSessionManager> GSessionManager;

// Previously defined in Core/CoreGlobal.cpp (removed with Core library)
extern std::shared_ptr<class ThreadManager>    GThreadManager;
extern std::shared_ptr<class DBConnectionPool> GDBConnectionPool;
