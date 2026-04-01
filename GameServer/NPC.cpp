#include "pch.h"
#include "NPC.h"
#include "Collision.h"
#include "User.h"
#include "Sector.h"
#include "WorkerThread.h"
#include "AStar.h"

namespace
{
	unordered_set<uint32> CollectVisiblePlayersAround(uint32 npcId)
	{
		unordered_set<uint32> visiblePlayers;
		const auto& npc = *GClients[npcId];

		GSector->ForEachNeighborObject(npc._sectorX, npc._sectorY, [&](uint32 id)
		{
			const auto& object = GClients[id];
			if (IsNPC(id))
				return;
			if (object->_state != ST_INGAME)
				return;
			if (CanSee(object->_id, npcId) == false)
				return;

			visiblePlayers.insert(object->_id);
		});

		return visiblePlayers;
	}

	void BroadcastNpcVisibilityDelta(uint32 npcId, const unordered_set<uint32>& oldList, const unordered_set<uint32>& newList)
	{
		for (const uint32 id : newList) {
			if (oldList.count(id) == 0)
				GClients[id]->SendAddPlayerPacket(npcId);
			else
				GClients[id]->SendMovePacket(npcId);
		}

		for (const uint32 id : oldList) {
			if (newList.count(id) == 0) {
				GClients[id]->SendRemovePlayerPacket(npcId);
			}
		}
	}

	void UpdateNpcPosition(uint32 npcId, short nextX, short nextY)
	{
		auto& npc = *GClients[npcId];
		const bool sectorUpdated = GSector->UpdateObjectSector(npcId, nextX, nextY, npc._sectorX, npc._sectorY);
		ASSERT_CRASH(sectorUpdated || (npc._x == nextX && npc._y == nextY));
		npc._x = nextX;
		npc._y = nextY;
	}
}

void NPC::InitNPC()
{
	for (int32 i = MAX_USER; i < MAX_USER + MAX_NPC; ++i) {

		auto monster = MakeShared<Monster>();
		monster->InitInstance();
		GClients[i] = monster;
		while (true) {

			GClients[i]->_x = rand() % W_WIDTH;
			GClients[i]->_y = rand() % W_HEIGHT;
			if (!isCollision(GClients[i]->_x, GClients[i]->_y)) {
				const bool sectorUpdated = GSector->UpdateObjectSector(i, GClients[i]->_x, GClients[i]->_y, GClients[i]->_sectorX, GClients[i]->_sectorY);
				ASSERT_CRASH(sectorUpdated);
				break;
			}
		}

		GClients[i]->_id = i;

		if (i <= 70000)
			AsMonster(i)->SetType(MONSTER_TYPE::AGGRO);
		else
			AsMonster(i)->SetType(MONSTER_TYPE::PASSIVE);
	
		GClients[i]->_maxHp = NPC_MAX_HP;
		GClients[i]->_hp = NPC_MAX_HP;
		GClients[i]->_offensive = NPC_OFFENSIVE;
		GClients[i]->_die.store(false);
		sprintf_s(GClients[i]->_name, "NPC%d", i);
		GClients[i]->_state = ST_INGAME;
		GClients[i]->_active.store(false);
		GClients[i]->_attack.store(false);
	}
	cout << "NPC Init Success" << endl;
}

void NPC::NPCRandomMove(uint32 npcId)
{
	User& npc = *GClients[npcId];
	const unordered_set<uint32> oldList = CollectVisiblePlayersAround(npcId);

	short x = npc._x;
	short y = npc._y;

	GWorkerThread->MovePlayer(x, y, rand() % 4);
	UpdateNpcPosition(npcId, x, y);
	const unordered_set<uint32> newList = CollectVisiblePlayersAround(npcId);
	BroadcastNpcVisibilityDelta(npcId, oldList, newList);
}

void NPC::NPCAStarMove(uint32 npcId, short nextX, short nextY)
{
	const unordered_set<uint32> oldList = CollectVisiblePlayersAround(npcId);
	UpdateNpcPosition(npcId, nextX, nextY);
	const unordered_set<uint32> newList = CollectVisiblePlayersAround(npcId);
	BroadcastNpcVisibilityDelta(npcId, oldList, newList);
}
