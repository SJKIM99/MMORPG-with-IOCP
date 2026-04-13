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
	vector<shared_ptr<DBConnection>> newConnections;
	newConnections.reserve(connectionCount);

	for (int32 i = 0; i < connectionCount; i++)
	{
		auto connection = make_shared<DBConnection>();
		if (connection->Connect() == false)
			return false;

		newConnections.push_back(connection);
	}

	{
		std::scoped_lock lock(_lock);
		for (auto& connection : newConnections)
		{
			_idleConnections.push_back(connection);
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

shared_ptr<DBConnection> DBConnectionPool::Pop()
{
	std::unique_lock lock(_lock);
	_cv.wait(lock, [this]()
	{
		return _idleConnections.empty() == false;
	});

	auto connection = _idleConnections.back();
	_idleConnections.pop_back();
	return connection;
}

void DBConnectionPool::Push(shared_ptr<DBConnection> connection)
{
	{
		std::scoped_lock lock(_lock);
		_idleConnections.push_back(std::move(connection));
	}

	_cv.notify_one();
}
