#pragma once

class SocketManager;
class GameSession;
class DBThread;
class Sector;
class TimerThread;
class NPC;
class AStar;

class WorkerThread
{
public:
	WorkerThread() = default;
	~WorkerThread() = default;

	void			HandleGetPlayerInfo(uint32 clientId, uint64 sessionToken, const DB_PLAYER_INFO& playerInfo);
	void			HandleAddPlayerInfo(uint32 clientId, uint64 sessionToken, const DB_PLAYER_INFO& playerInfo);
	void			HandleNpcRandomMove(uint32 npcId);
	void			HandleNpcRespawn(uint32 npcId);
	void			HandlePlayerRespawn(uint32 playerId);
	void			HandleNpcAggroMove(uint32 npcId, uint32 aiTargetId);
	void			HandleHeal(uint32 playerId);
	void			HandleNpcAttackToPlayer(uint32 npcId, uint32 playerId);
	void			HandleLoginFail(uint32 clientId, uint64 sessionToken);

	void			InitializeConnectedClient(uint32 clientId, uint64 sessionToken);
	void			Disconnect(uint32 clientId, uint64 sessionToken);
	void			DoWork();
	[[nodiscard]] uint32	GetNewClientId();
	void			HandlePacket(uint32 clientId, char* packet);

private:
	void			AttackToNPC(uint32 npcId, uint32 playerId);
};
