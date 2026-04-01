#pragma once
#include "DBConnection.h"

class DBConnectionPool
{
public:
	DBConnectionPool();
	~DBConnectionPool();

	bool					Connect(int connectionCount);
	void					Clear();

	DBConnection* Pop();
	void					Push(DBConnection* connection);

private:
	std::mutex							_lock;
	std::condition_variable				_cv;
	std::vector<std::unique_ptr<DBConnection>> _ownedConnections;
	std::deque<DBConnection*>			_idleConnections;
};

