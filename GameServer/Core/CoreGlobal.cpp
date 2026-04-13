#include "pch.h"
#include "CoreGlobal.h"
#include "ThreadManager.h"
#include "Memory.h"
#include "DeadLockProfiler.h"
#include "DBConnectionPool.h"

shared_ptr<ThreadManager>    GThreadManager = nullptr;
shared_ptr<Memory>           GMemory = nullptr;
shared_ptr<DeadLockProfiler> GDeadLockProfiler = nullptr;
shared_ptr<DBConnectionPool> GDBConnectionPool = nullptr;

class CoreGlobal
{
public:
	CoreGlobal()
	{
		GThreadManager = make_shared<ThreadManager>();
		GMemory = make_shared<Memory>();
		GDeadLockProfiler = make_shared<DeadLockProfiler>();
		GDBConnectionPool = make_shared<DBConnectionPool>();
	}

	~CoreGlobal()
	{
		GDBConnectionPool.reset();
		GDeadLockProfiler.reset();
		GMemory.reset();
		GThreadManager.reset();
	}
} GCoreGlobal;
