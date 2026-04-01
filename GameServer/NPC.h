#pragma once

class GameSession;
class Sector;
class WorkerThread;
class AStar;

class NPC
{
public:
	NPC() {};
	~NPC() {};
	static void InitNPC();
	void NPCRandomMove(uint32 npcId);
	void NPCAStarMove(uint32 npcId,short nextX,short nextY);
};

