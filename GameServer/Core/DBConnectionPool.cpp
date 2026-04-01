#include "pch.h"
#include "DBConnectionPool.h"

DBConnectionPool::DBConnectionPool()
{
}

DBConnectionPool::~DBConnectionPool()
{
	Clear();
}

bool DBConnectionPool::Connect(int connectionCount)
{
	std::vector<std::unique_ptr<DBConnection>> newConnections;
	newConnections.reserve(connectionCount);

	for (int32 i = 0; i < connectionCount; i++)
	{
		auto connection = std::make_unique<DBConnection>();
		if (connection->Connect() == false)
			return false;

		newConnections.push_back(std::move(connection));
	}

	{
		std::scoped_lock lock(_lock);
		for (auto& connection : newConnections)
		{
			_idleConnections.push_back(connection.get());
			_ownedConnections.push_back(std::move(connection));
		}
	}

	_cv.notify_all();
	cout << "DB Connect Success" << endl;
	return true;
}

void DBConnectionPool::Clear()
{
	std::scoped_lock lock(_lock);
	_idleConnections.clear();
	_ownedConnections.clear();
}

DBConnection* DBConnectionPool::Pop()
{
	std::unique_lock lock(_lock);
	_cv.wait(lock, [this]()
	{
		return _idleConnections.empty() == false;
	});

	DBConnection* connection = _idleConnections.back();
	_idleConnections.pop_back();
	return connection;
}

void DBConnectionPool::Push(DBConnection* connection)
{
	{
		std::scoped_lock lock(_lock);
		_idleConnections.push_back(connection);
	}

	_cv.notify_one();
}
