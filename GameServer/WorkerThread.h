#pragma once
//#include "Protocol.h"
#include "Collision.h"

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
	WorkerThread() {};
	~WorkerThread() {};

	void			InitializeConnectedClient(uint32 clientId, uint64 sessionToken);
	void			Disconnect(uint32 clientId, uint64 sessionToken);

	void			DoWork();
	uint32			GetNewClientId();
	void			HandlePacket(uint32 clientId, char* packet);
	void			HandleGetPlayerInfo(uint32 clientId, uint64 sessionToken, const DB_PLAYER_INFO& playerInfo);
	void			HandleAddPlayerInfo(uint32 clientId, uint64 sessionToken, const DB_PLAYER_INFO& playerInfo);
	void			HandleNpcRandomMove(uint32 npcId);
	void			HandleNpcRespawn(uint32 npcId);
	void			HandlePlayerRespawn(uint32 playerId);
	void			HandleNpcAggroMove(uint32 npcId, uint32 aiTargetId);
	void			HandleHeal(uint32 playerId);
	void			HandleNpcAttackToPlayer(uint32 npcId, uint32 playerId);
	bool			FlushPlayerSave(uint32 clientId);

public:
	uint32			GetNowTime();
	void			MovePlayer(short& x, short& y, char direction);
	void			UpdateViewList(uint32 clientId);
	void			WakeUpNpc(uint32 npcId, uint32 wakerId);
	void			AttackToNPC(uint32 npcId, uint32 playerId);
};

bool				CanSee(uint32 from, uint32 to);
bool				IsPc(uint32 id);
bool				IsNPC(uint32 id);
bool				CanAttack(uint32 from, uint32 to);

