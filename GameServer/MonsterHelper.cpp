#include "pch.h"
#include "MonsterHelper.h"
#include "User.h"
#include "WorldHelper.h"
#include "UserHelper.h"
#include "Sector.h"
#include "TimerThread.h"
#include "AStar.h"
#include "Transform.h"

namespace
{
	std::atomic<uint64_t> _nextMonsterId{ NPC_ID_START + MAX_NPC };
}

namespace MonsterHelper
{
	void RandomMove(ObjID& monsterId)
	{
		const auto oldList = WorldHelper::CollectVisiblePlayersAround(monsterId);
		const auto monster = GGameObjectManager->Seek<Monster>(monsterId);
		if (monster == nullptr)
			return;

		short x = monster->GetX();
		short y = monster->GetY();

		WorldHelper::MovePositionByDirection(x, y, static_cast<char>(rand() % 4));
		WorldHelper::UpdateObjectPosition(monsterId, x, y);

		const auto newList = WorldHelper::CollectVisiblePlayersAround(monsterId);
		WorldHelper::BroadcastNpcVisibilityDelta(monsterId, oldList, newList);
	}

	void AStarMove(ObjID& monsterId, short nextX, short nextY)
	{
		const auto oldList = WorldHelper::CollectVisiblePlayersAround(monsterId);
		WorldHelper::UpdateObjectPosition(monsterId, nextX, nextY);
		const auto newList = WorldHelper::CollectVisiblePlayersAround(monsterId);
		WorldHelper::BroadcastNpcVisibilityDelta(monsterId, oldList, newList);
	}

	void HandleRandomMove(const ObjID& npcId)
	{
		auto npc = GGameObjectManager->Seek<Monster>(npcId);
		if (npc == nullptr || npc->GetStat()->IsDead())
			return;

		bool hasNearbyPlayer  = false;
		ObjID attackTargetId;

		GSector->ForEachNeighborObject(npc->GetSectorX(), npc->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;

			auto user = GGameObjectManager->Seek<User>(id);
			if (!user) return;
			auto session = user->GetGameSession();
			if (!session || session->_state != SOCKET_STATE::ST_INGAME) return;
			if (!WorldHelper::CanSee(npcId, id)) return;

			hasNearbyPlayer = true;
			if (WorldHelper::CanAttack(npcId, id))
				attackTargetId = id;
		});

		if (attackTargetId)
		{
			GTimerThread->ScheduleNow(npcId, TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER, attackTargetId);
		}
		else if (hasNearbyPlayer)
		{
			ObjID mutableId = npcId;
			RandomMove(mutableId);
			GTimerThread->ScheduleAfter(npcId, 1s, TIMER_EVENT_TYPE::EV_RANOM_MOVE);
		}
		else
		{
			npc->SetActive(false);
		}
	}

	void HandleRespawn(const ObjID& npcId)
	{
		GGameObjectManager->Delete(npcId);

		ObjID newId(EnumCategory::eMonster, _nextMonsterId.fetch_add(1));
		auto newMonster = MakeNewSubject(newId, ObjID::npos);
		GGameObjectManager->Insert(newId, newMonster);
		WorldHelper::PlaceObjectAtRandomWalkablePosition(newId);

		auto npc = GGameObjectManager->Seek<Monster>(newId);
		if (npc == nullptr)
			return;

		GSector->ForEachNeighborObject(npc->GetSectorX(), npc->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;

			auto user = GGameObjectManager->Seek<User>(id);
			if (!user) return;
			auto session = user->GetGameSession();
			if (!session || session->_state != SOCKET_STATE::ST_INGAME) return;
			if (!WorldHelper::CanSee(id, newId)) return;

			UserHelper::SendRespawnNpcPacket(user, newId);
		});
	}

	void HandleAggroMove(const ObjID& npcId, const ObjID& targetId)
	{
		auto npc = GGameObjectManager->Seek<Monster>(npcId);
		if (npc == nullptr || npc->GetStat()->IsDead())
			return;

		auto target = GGameObjectManager->Seek<User>(targetId);
		if (!target)
		{
			npc->SetActive(false);
			npc->SetAttack(false);
			return;
		}
		auto targetSession = target->GetGameSession();
		if (!targetSession || targetSession->_state != SOCKET_STATE::ST_INGAME)
		{
			npc->SetActive(false);
			npc->SetAttack(false);
			return;
		}

		vector<NODE>& path = npc->GetPath();

		if (path.empty())
			path = FindPath(npc->GetX(), npc->GetY(), target->GetX(), target->GetY());

		if (!path.empty())
		{
			const short nx = path.back()._x;
			const short ny = path.back()._y;
			path.pop_back();
			ObjID mutableId = npcId;
			AStarMove(mutableId, nx, ny);
		}

		if (WorldHelper::CanAttack(npcId, targetId) && !npc->IsAttacking())
			GTimerThread->ScheduleNow(npcId, TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER, targetId);

		if (WorldHelper::CanSee(npcId, targetId))
		{
			npc->SetActive(true);
			GTimerThread->ScheduleAfter(npcId, 1s, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
		}
		else
		{
			npc->SetActive(false);
		}
	}

	void HandleAttackToPlayer(const ObjID& npcId, const ObjID& playerId)
	{
		auto npc = GGameObjectManager->Seek<Monster>(npcId);
		auto victim = GGameObjectManager->Seek<User>(playerId);
		if (npc == nullptr || victim == nullptr)
			return;

		npc->SetAttack(true);

		auto victimSession = victim->GetGameSession();
		if (!victimSession || victimSession->_state != SOCKET_STATE::ST_INGAME
			|| victim->GetStat()->IsDead()
			|| !WorldHelper::CanAttack(playerId, npcId)
			|| npc->GetStat()->IsDead())
		{
			npc->SetAttack(false);
			return;
		}

		const uint16 remaining = victim->GetStat()->TakeDamage(NPC_OFFENSIVE);

		if (!victim->GetStat()->IsDead())
		{
			GSector->ForEachNeighborObject(victim->GetSectorX(), victim->GetSectorY(), [&](const shared_ptr<Subject>& object)
			{
				ObjID id = object->GetObjID();
				if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;
				if (!WorldHelper::CanSee(playerId, id)) return;
				auto viewer = GGameObjectManager->Seek<User>(id);
				if (!viewer) return;
				auto session = viewer->GetGameSession();
				if (!session || session->_state != SOCKET_STATE::ST_INGAME) return;
				UserHelper::SendNpcAttackToPlayerPacket(viewer);
			});

			if (WorldHelper::CanAttack(npcId, playerId))
				GTimerThread->ScheduleAfter(npcId, 1s, TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER, playerId);
			else
				npc->SetAttack(false);
			return;
		}

		// 플레이어 사망 처리
		GSector->ForEachNeighborObject(victim->GetSectorX(), victim->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;
			if (!WorldHelper::CanSee(playerId, id)) return;
			auto viewer = GGameObjectManager->Seek<User>(id);
			if (!viewer) return;
			auto session = viewer->GetGameSession();
			if (!session || session->_state != SOCKET_STATE::ST_INGAME) return;
			UserHelper::SendPlayerDiePacket(viewer, playerId);
		});

		npc->SetAttack(false);
		npc->SetActive(false);
		GSector->RemoveObject(const_cast<ObjID&>(playerId), victim->RefSectorX(), victim->RefSectorY());
		GTimerThread->ScheduleAfter(playerId, 30s, TIMER_EVENT_TYPE::EV_USER_RESPAWN);

		(void)remaining;
	}

	bool Init()
	{
		for (int32 i = 0; i < MAX_NPC; ++i)
		{
			ObjID monsterId(EnumCategory::eMonster, NPC_ID_START + static_cast<uint64_t>(i));
			auto newMonster = MakeNewSubject(monsterId, ObjID::npos);
			ASSERT_CRASH(GGameObjectManager->Insert(monsterId, newMonster));
			WorldHelper::PlaceObjectAtRandomWalkablePosition(monsterId);
		}

		cout << "Monster Init Success" << endl;
		return true;
	}
}
