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

	// 전체 세션의 스냅샷(월드 공지 같은 드문 전역 브로드캐스트용). 락은 복사하는
	// 동안만 잡고, 실제 송신(PostSend — 세션별 락)은 락을 놓은 뒤에 한다.
	[[nodiscard]] vector<shared_ptr<GameSession>> SnapshotSessions() const;

private:
	mutable shared_mutex                                 _lock;
	unordered_map<GameSession*, shared_ptr<GameSession>> _sessions;
	uint32_t                                             _nextObjectId = PLAYER_ID_START;
};
