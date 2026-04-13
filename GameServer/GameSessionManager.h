#pragma once

#include <shared_mutex>
#include <unordered_map>

class GameSession;

class GameSessionManager
{
public:
	GameSessionManager() = default;
	~GameSessionManager() = default;

	[[nodiscard]] shared_ptr<GameSession> CreateSession();
	[[nodiscard]] shared_ptr<GameSession> FindSession(GameSession* key) const;
	bool                                  ReleaseSession(GameSession* key);

private:
	mutable shared_mutex                                 _lock;
	unordered_map<GameSession*, shared_ptr<GameSession>> _sessions;
	uint32                                               _nextObjectId = PLAYER_ID_START;
};
