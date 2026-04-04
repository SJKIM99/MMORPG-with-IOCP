#pragma once

// Forward declaration only — full definition requires Subject.h which
// needs IO_TYPE/SOCKET_STATE defined in pch.h first.
class GameObjectManager;
extern GameObjectManager* GObjectManager;

extern class DBThread*           GDBThread;
extern class Sector*             GSector;
extern class GameLogicThread*    GGameLogicThread;
extern class WorkerThread*       GWorkerThread;
extern class TimerThread*        GTimerThread;
extern class GameSessionManager* GSessionManager;

