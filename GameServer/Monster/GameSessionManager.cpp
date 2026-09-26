#include "pch.h"
#include "GameSessionManager.h"
#include "GameSession.h"

shared_ptr<GameSession> GameSessionManager::CreateSession()
{
	unique_lock lock(_lock);

	if (_nextObjectId >= MONSTER_ID_START)
		return nullptr;

	auto session      = std::make_shared<GameSession>();
	session->m_objectId = _nextObjectId++;
	_sessions.emplace(session.get(), session);
	return session;
}

shared_ptr<GameSession> GameSessionManager::FindSession(GameSession* key) const
{
	shared_lock lock(_lock);
	auto it = _sessions.find(key);
	return it != _sessions.end() ? it->second : nullptr;
}

bool GameSessionManager::ReleaseSession(GameSession* key)
{
	{
		// 이름 색인: 이 세션이 가진 이름이면 뺀다(같은 이름으로 다시 들어온 새 세션의 자리는 건드리지 않는다)
		unique_lock nameLock(_nameLock);
		if (auto it = _nameOf.find(key); it != _nameOf.end())
		{
			if (auto nit = _byName.find(it->second); nit != _byName.end())
			{
				const auto cur = nit->second.lock();
				if (cur == nullptr || cur.get() == key)
					_byName.erase(nit);
			}
			_nameOf.erase(it);
		}
	}
	unique_lock lock(_lock);
	return _sessions.erase(key) > 0;
}

string GameSessionManager::NameKey(const string& name)
{
	string key = name;
	for (char& c : key)
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
	return key;
}

void GameSessionManager::RegisterName(const string& name, const shared_ptr<GameSession>& session)
{
	if (name.empty() || session == nullptr)
		return;
	const string key = NameKey(name);
	unique_lock lock(_nameLock);
	_byName[key] = session;
	_nameOf[session.get()] = key;
}

shared_ptr<GameSession> GameSessionManager::FindByName(const string& name) const
{
	if (name.empty())
		return nullptr;
	const string key = NameKey(name);
	shared_lock lock(_nameLock);
	auto it = _byName.find(key);
	return it != _byName.end() ? it->second.lock() : nullptr;
}

vector<shared_ptr<GameSession>> GameSessionManager::SnapshotSessions() const
{
	vector<shared_ptr<GameSession>> sessions;
	shared_lock lock(_lock);
	sessions.reserve(_sessions.size());
	for (const auto& [key, session] : _sessions)
		sessions.push_back(session);
	return sessions;
}
