#pragma once

class GameSessionManager
{
public:
	GameSessionManager();
	~GameSessionManager() = default;

	[[nodiscard]] uint32 AcquirePlayerSessionId();
	bool ReleasePlayerSessionId(uint32 playerId);

private:
	std::mutex _lock;
	std::vector<uint32> _freePlayerIds;
	std::array<bool, MAX_USER> _inUse{};
};

