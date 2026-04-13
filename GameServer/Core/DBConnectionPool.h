#pragma once
#include "DBConnection.h"

class DBConnectionPool
{
public:
	DBConnectionPool();
	~DBConnectionPool();

	bool                        Connect(int connectionCount);
	void                        Clear();

	[[nodiscard]] shared_ptr<DBConnection> Pop();
	void                        Push(shared_ptr<DBConnection> connection);

private:
	std::mutex                  _lock;
	std::condition_variable     _cv;
	vector<shared_ptr<DBConnection>> _ownedConnections;
	deque<shared_ptr<DBConnection>>  _idleConnections;
};
