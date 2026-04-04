#include "pch.h"
#include "WorldHelper.h"
#include "User.h"
#include "UserHelper.h"
#include "Sector.h"
#include "TimerThread.h"
#include "Collision.h"

namespace WorldHelper
{
	uint32 GetNowTime()
	{
		return static_cast<uint32>(chrono::duration_cast<chrono::milliseconds>(
			chrono::steady_clock::now().time_since_epoch()).count());
	}

	void MovePositionByDirection(short& x, short& y, char direction)
	{
		switch (direction)
		{
		case 0: if (y > 0)            { --y; if (isCollision(x, y)) ++y; } break;
		case 1: if (y < W_HEIGHT - 1) { ++y; if (isCollision(x, y)) --y; } break;
		case 2: if (x > 0)            { --x; if (isCollision(x, y)) ++x; } break;
		case 3: if (x < W_WIDTH - 1)  { ++x; if (isCollision(x, y)) --x; } break;
		}
	}

	bool CanSee(uint32 from, uint32 to)
	{
		if (abs((*GObjectManager)[from]->_transform.GetX() - (*GObjectManager)[to]->_transform.GetX()) >= VIEW_RANGE)
			return false;

		return abs((*GObjectManager)[from]->_transform.GetY() - (*GObjectManager)[to]->_transform.GetY()) <= VIEW_RANGE;
	}

	bool CanAttack(uint32 from, uint32 to)
	{
		if (abs((*GObjectManager)[from]->_transform.GetX() - (*GObjectManager)[to]->_transform.GetX()) >= ATTACK_RANGE)
			return false;

		return abs((*GObjectManager)[from]->_transform.GetY() - (*GObjectManager)[to]->_transform.GetY()) <= ATTACK_RANGE;
	}

	void UpdateObjectPosition(uint32 objectId, short nextX, short nextY)
	{
		auto& object = *(*GObjectManager)[objectId];
		const bool updated = GSector->UpdateObjectSector(
			objectId,
			nextX,
			nextY,
			object._transform.RefSectorX(),
			object._transform.RefSectorY());

		ASSERT_CRASH(updated || GSector->GetSectorCoord(nextX, nextY).IsAssigned());
		object._transform.SetPosition(nextX, nextY);
	}

	void PlaceObjectAtRandomWalkablePosition(uint32 objectId)
	{
		while (true)
		{
			const short x = static_cast<short>(rand() % W_WIDTH);
			const short y = static_cast<short>(rand() % W_HEIGHT);
			if (isCollision(x, y))
				continue;

			UpdateObjectPosition(objectId, x, y);
			return;
		}
	}

	std::vector<uint32> CollectVisiblePlayersAround(uint32 npcId)
	{
		std::vector<uint32> visiblePlayers;
		visiblePlayers.reserve(16);
		const auto& npc = *(*GObjectManager)[npcId];

		GSector->ForEachNeighborObject(npc._transform.GetSectorX(), npc._transform.GetSectorY(), [&](uint32 id)
		{
			const auto& object = (*GObjectManager)[id];
			if (IsNPC(id)) return;
			if (object->_state != ST_INGAME) return;
			if (!CanSee(object->_id, npcId)) return;
			visiblePlayers.push_back(object->_id);
		});

		return visiblePlayers;
	}

	void BroadcastNpcVisibilityDelta(
		uint32 npcId,
		const std::vector<uint32>& oldList,
		const std::vector<uint32>& newList)
	{
		// Lists are tiny (VIEW_RANGE=5) so linear scan beats hash lookup.
		const auto inOld = [&](uint32 id) {
			return std::find(oldList.begin(), oldList.end(), id) != oldList.end();
		};
		const auto inNew = [&](uint32 id) {
			return std::find(newList.begin(), newList.end(), id) != newList.end();
		};

		for (const uint32 id : newList)
		{
			if (!inOld(id)) UserHelper::SendAddPlayerPacket(*(*GObjectManager)[id], npcId);
			else            UserHelper::SendMovePacket(*(*GObjectManager)[id], npcId);
		}

		for (const uint32 id : oldList)
		{
			if (!inNew(id))
				UserHelper::SendRemovePlayerPacket(*(*GObjectManager)[id], npcId);
		}
	}

	void NotifyPlayerEnteredWorld(uint32 playerId, bool isRespawn)
	{
		const auto& player = *(*GObjectManager)[playerId];
		GSector->ForEachNeighborObject(player._transform.GetSectorX(), player._transform.GetSectorY(), [&](uint32 id)
		{
			auto& object = (*GObjectManager)[id];
			if (id == playerId) return;
			if (object->_state != SOCKET_STATE::ST_INGAME) return;
			if (!CanSee(playerId, id)) return;

			if (IsPc(id))
			{
				if (isRespawn) UserHelper::SendRespawnPlayerPacket(*object, playerId);
				else           UserHelper::SendAddPlayerPacket(*object, playerId);
			}
			else
			{
				WakeUpNpc(id, playerId);
			}

			UserHelper::SendAddPlayerPacket(*(*GObjectManager)[playerId], id);
		});
	}

	void UpdatePlayerViewList(uint32 clientId)
	{
		auto& myPlayer = (*GObjectManager)[clientId];

		// vector instead of unordered_set: no hash bucket allocation, cache-friendly for small N.
		std::vector<uint32> nearList;
		nearList.reserve(64);

		GSector->ForEachNeighborObject(myPlayer->_transform.GetSectorX(), myPlayer->_transform.GetSectorY(), [&](uint32 id)
		{
			if (id == clientId) return;
			if ((*GObjectManager)[id]->_state != SOCKET_STATE::ST_INGAME) return;
			if (!CanSee(id, clientId)) return;
			nearList.push_back(id);
		});

		const auto inNear = [&](uint32 id) {
			return std::find(nearList.begin(), nearList.end(), id) != nearList.end();
		};

		// Collect removals before modifying _viewList (avoids a full copy of the set).
		std::vector<uint32> toRemove;
		toRemove.reserve(myPlayer->_viewList.size());
		for (const uint32 id : myPlayer->_viewList)
		{
			if (!inNear(id))
				toRemove.push_back(id);
		}

		// Process additions: viewList is still unmodified at this point.
		for (const uint32 id : nearList)
		{
			if (IsPc(id))
			{
				if ((*GObjectManager)[id]->_viewList.count(clientId)) UserHelper::SendMovePacket(*(*GObjectManager)[id], clientId);
				else                                         UserHelper::SendAddPlayerPacket(*(*GObjectManager)[id], clientId);
			}
			else
			{
				WakeUpNpc(id, clientId);
			}

			if (myPlayer->_viewList.count(id) == 0)
				UserHelper::SendAddPlayerPacket(*myPlayer, id);
		}

		// Process removals: notify about objects that left the view.
		for (const uint32 id : toRemove)
		{
			UserHelper::SendRemovePlayerPacket(*myPlayer, id);
			if (IsPc(id))
				UserHelper::SendRemovePlayerPacket(*(*GObjectManager)[id], clientId);
		}
	}

	void WakeUpNpc(uint32 npcId, uint32 wakerId)
	{
		if ((*GObjectManager)[npcId]->_stat.IsDead()) return;
		if ((*GObjectManager)[npcId]->_attack.load()) return;
		if ((*GObjectManager)[npcId]->_active.load()) return;

		Monster* npc   = AsMonster(npcId);
		User*    waker = AsUser(wakerId);

		bool expected = false;

		switch (npc->GetType())
		{
		case MONSTER_TYPE::PASSIVE:
			if (!(*GObjectManager)[npcId]->_active.compare_exchange_strong(expected, true)) return;
			GTimerThread->ScheduleAfter(npcId, npc->GetTimerEpoch(), 1s, TIMER_EVENT_TYPE::EV_RANOM_MOVE);
			return;

		case MONSTER_TYPE::AGGRO:
			if (waker->GetTarget() != -1) return;
			if (!(*GObjectManager)[npcId]->_active.compare_exchange_strong(expected, true)) return;
			waker->SetTarget(npcId);
			GTimerThread->ScheduleAfter(npcId, npc->GetTimerEpoch(), 1s, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, wakerId);
			return;
		}
	}

	void AttackNpc(uint32 npcId, uint32 playerId)
	{
		auto& npc = (*GObjectManager)[npcId];
		if (npc->_stat.IsDead())
			return;

		Monster* monster  = AsMonster(npcId);
		User*    attacker = AsUser(playerId);

		const uint16 remaining = npc->_stat.TakeDamage(PLAYER_OFFENSIVE);
		if (remaining > 0)
		{
			UserHelper::SendPlayerAttackToNpcPacket(*(*GObjectManager)[playerId], npcId);
			return;
		}

		if (monster->GetType() == MONSTER_TYPE::AGGRO)
		{
			if (attacker->GetTarget() == static_cast<int>(npcId))
				attacker->SetTarget(-1);
		}

		GSector->ForEachNeighborObject(npc->_transform.GetSectorX(), npc->_transform.GetSectorY(), [&](uint32 id)
		{
			if ((*GObjectManager)[id]->_state != SOCKET_STATE::ST_INGAME) return;
			if (IsNPC(id)) return;
			if (CanSee(id, npcId)) UserHelper::SendNpcDiePacket(*(*GObjectManager)[id], npcId);
		});

		UserHelper::SendNpcDiePacket(*(*GObjectManager)[playerId], npcId);
		GSector->RemoveObject(npcId, npc->_transform.RefSectorX(), npc->_transform.RefSectorY());
		npc->ResetGameplayState();
		GTimerThread->ScheduleAfter(npcId, npc->GetTimerEpoch(), 10s, TIMER_EVENT_TYPE::EV_NPC_RESPAWN);
	}
}
