#pragma once

#include <memory>

extern std::shared_ptr<class ThreadManager>   GThreadManager;
extern std::shared_ptr<class Memory>          GMemory;

extern std::shared_ptr<class DeadLockProfiler> GDeadLockProfiler;
extern std::shared_ptr<class DBConnectionPool> GDBConnectionPool;
