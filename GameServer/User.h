#pragma once

#include "AStar.h"
#include "GameObject.h"

class User : public GameObject
{
public:
	User();
	virtual ~User() = default;

	static void InitializePlayers();

	void InitInstance() override;
	void OnUpdate(const UpdateTimePoint& updateTime) override;
	virtual void ResetGameplayState();

	void SendMovePacket(uint32 clientId);
	void SendAddPlayerPacket(uint32 clientId);
	void SendRemovePlayerPacket(uint32 clientId);
	void SendLoginSuccessPacket();
	void SendPlayerAtackToNPCPacket(uint32 clientId);
	void SendNPCDiePacket(uint32 clientId);
	void SendRespawnNPCPacket(uint32 clientId);
	void SendNPCAttackToPlayerPacket(uint32 clientId);
	void SendHealPacket();
	void SendPlayerDiePacket(uint32 clientId);
	void SendRespawnPlayerPacket(uint32 clientId);

	[[nodiscard]] uint64 GetTimerEpoch() const { return _timerEpoch.load(); }

public:
	char					_name[NAME_SIZE]{};
	short					_x = -1;
	short					_y = -1;
	uint16					_maxHp = 0;
	Atomic<uint16>			_hp = 0;
	uint16					_offensive = 0;
	Atomic<bool>			_die = true;
	uint32					_lastMoveTime = 0;
	short					_sectorX = -1;
	short					_sectorY = -1;
	unordered_set<uint32>	_viewList;
	Atomic<bool>			_active = false;
	Atomic<bool>			_attack = false;
	Atomic<uint64>			_timerEpoch = 1;
};

class Player : public User
{
public:
	Player();

	void InitInstance() override;
	void ResetGameplayState() override;
	void Heal();
	[[nodiscard]] bool TryBuildSaveInfo(DB_PLAYER_INFO& outPlayerInfo) const;

	int GetTarget() const { return _traceNpcId.load(); }
	void SetTarget(int id) { _traceNpcId.store(id); }

private:
	Atomic<int> _traceNpcId;
};

class Monster : public User
{
public:
	Monster();

	void InitInstance() override;
	void ResetGameplayState() override;

	MONSTER_TYPE GetType() const { return _type; }
	void SetType(MONSTER_TYPE type) { _type = type; }
	vector<NODE>& GetPath() { return _astarPath; }
	void ClearPath() { _astarPath.clear(); }

private:
	MONSTER_TYPE _type;
	vector<NODE> _astarPath;
};

extern array<shared_ptr<User>, MAX_USER + MAX_NPC> GClients;

inline Player* AsPlayer(uint32 id)
{
	return static_cast<Player*>(GClients[id].get());
}

inline Monster* AsMonster(uint32 id)
{
	return static_cast<Monster*>(GClients[id].get());
}
