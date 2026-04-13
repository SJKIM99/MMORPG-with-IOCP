#include "pch.h"
#include "GameSessionManager.h"
#include "GameSession.h"

shared_ptr<GameSession> GameSessionManager::CreateSession()
{
	unique_lock lock(_lock);

	if (_nextObjectId >= NPC_ID_START)
		return nullptr;

	auto session      = std::make_shared<GameSession>();
	session->_objectId = _nextObjectId++;
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
	unique_lock lock(_lock);
	return _sessions.erase(key) > 0;
}
