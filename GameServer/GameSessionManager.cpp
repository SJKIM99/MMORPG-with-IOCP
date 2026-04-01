#include "pch.h"
#include "GameSessionManager.h"

GameSessionManager::GameSessionManager()
{
	_freePlayerIds.reserve(MAX_USER);

	for (int32 playerId = MAX_USER - 1; playerId >= 0; --playerId)
	{
		_freePlayerIds.push_back(static_cast<uint32>(playerId));
	}
}

uint32 GameSessionManager::AcquirePlayerSessionId()
{
	std::scoped_lock lock(_lock);

	if (_freePlayerIds.empty())
		return static_cast<uint32>(-1);

	const uint32 playerId = _freePlayerIds.back();
	_freePlayerIds.pop_back();
	_inUse[playerId] = true;
	return playerId;
}

bool GameSessionManager::ReleasePlayerSessionId(uint32 playerId)
{
	if (playerId >= MAX_USER)
		return false;

	std::scoped_lock lock(_lock);

	if (_inUse[playerId] == false)
		return false;

	_inUse[playerId] = false;
	_freePlayerIds.push_back(playerId);
	return true;
}
