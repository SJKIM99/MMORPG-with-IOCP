#include "pch.h"
#include "MonsterHelper.h"
#include "User.h"
#include "UserHelper.h"
#include "SubjectHelper.h"
#include "SectorHelper.h"
#include "Sector.h"
#include "TimerThread.h"
#include "AStar.h"
#include "Transform.h"

namespace
{
	std::atomic<uint64_t> _nextMonsterId{ MONSTER_ID_START + MAX_MONSTER };
}

namespace MonsterHelper
{
	void RandomMove(ObjID& monsterId)
	{
		const auto oldList = SectorHelper::CollectVisiblePlayersAround(monsterId);
		const auto monster = GGameObjectManager->Seek<Monster>(monsterId);
		if (monster == nullptr)
			return;

		short x = monster->GetX();
		short y = monster->GetY();
		const short prevX = x;

		SubjectHelper::MovePositionByDirection(x, y, static_cast<char>(rand() % 4));
		monster->UpdateFacing(x - prevX);
		SectorHelper::UpdateObjectPosition(monsterId, x, y);

		const auto newList = SectorHelper::CollectVisiblePlayersAround(monsterId);
		SectorHelper::BroadcastMonsterVisibilityDelta(monsterId, oldList, newList);
	}

	void AStarMove(ObjID& monsterId, short nextX, short nextY)
	{
		const auto oldList = SectorHelper::CollectVisiblePlayersAround(monsterId);
		SectorHelper::UpdateObjectPosition(monsterId, nextX, nextY);
		const auto newList = SectorHelper::CollectVisiblePlayersAround(monsterId);
		SectorHelper::BroadcastMonsterVisibilityDelta(monsterId, oldList, newList);
	}

	void WakeUpMonster(ObjID& monsterId, ObjID& wakerId, bool forceWake)
	{
		auto monster = GGameObjectManager->Seek<Monster>(monsterId);
		auto waker   = GGameObjectManager->Seek<User>(wakerId);
		if (monster == nullptr || waker == nullptr)
			return;
		if (monster->GetStat()->IsDead()) return;

		// Aggro monsters only wake up when the player is within WAKE_RANGE tiles
		// (skipped on forced wake, e.g. player respawn)
		if (!forceWake && monster->GetType() == MONSTER_TYPE::AGGRO)
		{
			if (abs(monster->GetX() - waker->GetX()) > WAKE_RANGE) return;
			if (abs(monster->GetY() - waker->GetY()) > WAKE_RANGE) return;
		}

		// atomic exchange: 이미 활성화된 경우 중복 스케줄링 방지
		if (!monster->TryActivate()) return;

		switch (monster->GetType())
		{
		case MONSTER_TYPE::PASSIVE:
			GTimerThread->ScheduleAfter(monsterId, 1s, TIMER_EVENT_TYPE::EV_RANOM_MOVE);
			return;

		case MONSTER_TYPE::AGGRO:
			GTimerThread->ScheduleAfter(monsterId, 1s, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, wakerId);
			return;
		}
	}

	void HandleRandomMove(const ObjID& monsterId)
	{
		auto monster = GGameObjectManager->Seek<Monster>(monsterId);
		if (monster == nullptr || monster->GetStat()->IsDead())
			return;

		bool hasNearbyPlayer  = false;
		ObjID attackTargetId;

		GSector->ForEachNeighborObject(monster->GetSectorX(), monster->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;

			auto user = GGameObjectManager->Seek<User>(id);
			if (!user) return;
			auto session = user->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
			if (!SubjectHelper::CanSee(monsterId, id)) return;

			hasNearbyPlayer = true;
			if (SubjectHelper::CanAttack(monsterId, id))
				attackTargetId = id;
		});

		if (attackTargetId)
		{
			GTimerThread->ScheduleNow(monsterId, TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER, attackTargetId);
		}
		else if (hasNearbyPlayer)
		{
			ObjID mutableId = monsterId;
			RandomMove(mutableId);
			GTimerThread->ScheduleAfter(monsterId, 1s, TIMER_EVENT_TYPE::EV_RANOM_MOVE);
		}
		else
		{
			monster->SetActive(false);
		}
	}

	void HandleRespawn(const ObjID& monsterId)
	{
		GGameObjectManager->Delete(monsterId);

		ObjID newId(EnumCategory::eMonster, _nextMonsterId.fetch_add(1));
		auto newMonster = MakeNewSubject(newId, ObjID::npos);
		GGameObjectManager->Insert(newId, newMonster);
		SectorHelper::PlaceObjectAtRandomWalkablePosition(newId);

		auto monster = GGameObjectManager->Seek<Monster>(newId);
		if (monster == nullptr)
			return;

		monster->SetSpawn(monster->GetX(), monster->GetY());

		GSector->ForEachNeighborObject(monster->GetSectorX(), monster->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;

			auto user = GGameObjectManager->Seek<User>(id);
			if (!user) return;
			auto session = user->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
			if (!SubjectHelper::CanSee(id, newId)) return;

			UserHelper::SendRespawnMonsterPacket(user, newId);
		});
	}

	void HandleAggroMove(const ObjID& monsterId, const ObjID& targetId)
	{
		auto monster = GGameObjectManager->Seek<Monster>(monsterId);
		if (monster == nullptr || monster->GetStat()->IsDead())
			return;

		// ── Target validation ─────────────────────────────────────────────────
		auto target = GGameObjectManager->Seek<User>(targetId);
		if (!target)
		{
			monster->SetActive(false);
			monster->SetAttack(false);
			return;
		}
		auto targetSession = target->GetGameSession();
		if (!targetSession || targetSession->m_state != SOCKET_STATE::ST_INGAME)
		{
			monster->SetActive(false);
			monster->SetAttack(false);
			return;
		}
		// ─────────────────────────────────────────────────────────────────────

		// ── Leash check: stop chasing if > 7 tiles from spawn in any axis ──
		if (abs(monster->GetX() - monster->GetSpawnX()) > 7 ||
		    abs(monster->GetY() - monster->GetSpawnY()) > 7)
		{
			monster->SetAttack(false);

			vector<NODE>& path = monster->GetPath();
			path = FindPath(monster->GetX(), monster->GetY(), monster->GetSpawnX(), monster->GetSpawnY());

			if (!path.empty())
			{
				const short nx = path.back()._x;
				const short ny = path.back()._y;
				path.pop_back();
				monster->UpdateFacing(nx - monster->GetX());
				ObjID mutableId = monsterId;
				AStarMove(mutableId, nx, ny);
				GTimerThread->ScheduleAfter(monsterId, 1s, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
			}
			else
			{
				monster->SetActive(false);
			}
			return;
		}
		// ─────────────────────────────────────────────────────────────────────

		// ── Within attack range: stop pathfinding, attack only ────────────────
		if (SubjectHelper::CanAttack(monsterId, targetId))
		{
			if (!monster->IsAttacking())
				GTimerThread->ScheduleNow(monsterId, TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER, targetId);

			GTimerThread->ScheduleAfter(monsterId, 1s, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
			return;
		}
		// ─────────────────────────────────────────────────────────────────────

		// ── A* chase: target is > 1 tile away ────────────────────────────────
		vector<NODE>& path = monster->GetPath();
		path = FindPath(monster->GetX(), monster->GetY(), target->GetX(), target->GetY());

		if (!path.empty())
		{
			const short nx = path.back()._x;
			const short ny = path.back()._y;
			path.pop_back();
			monster->UpdateFacing(nx - monster->GetX());
			ObjID mutableId = monsterId;
			AStarMove(mutableId, nx, ny);
		}

		if (SubjectHelper::CanSee(monsterId, targetId))
		{
			monster->SetActive(true);
			GTimerThread->ScheduleAfter(monsterId, 1s, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
		}
		else
		{
			monster->SetActive(false);
		}
		// ─────────────────────────────────────────────────────────────────────
	}

	void HandleAttackToPlayer(const ObjID& monsterId, const ObjID& playerId)
	{
		auto monster = GGameObjectManager->Seek<Monster>(monsterId);
		auto victim  = GGameObjectManager->Seek<User>(playerId);
		if (monster == nullptr || victim == nullptr)
			return;

		monster->SetAttack(true);

		auto victimSession = victim->GetGameSession();
		if (!victimSession || victimSession->m_state != SOCKET_STATE::ST_INGAME
			|| victim->GetStat()->IsDead()
			|| !SubjectHelper::IsAdjacent(monsterId, playerId)
			|| monster->GetStat()->IsDead())
		{
			monster->SetAttack(false);
			return;
		}

		const uint16_t remaining = victim->GetStat()->TakeDamage(MONSTER_OFFENSIVE);

		if (!victim->GetStat()->IsDead())
		{
			GSector->ForEachNeighborObject(victim->GetSectorX(), victim->GetSectorY(), [&](const shared_ptr<Subject>& object)
			{
				ObjID id = object->GetObjID();
				if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;
				if (!SubjectHelper::CanSee(playerId, id)) return;
				auto viewer = GGameObjectManager->Seek<User>(id);
				if (!viewer) return;
				auto session = viewer->GetGameSession();
				if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
				UserHelper::SendMonsterAttackToPlayerPacket(viewer, monsterId);
			});

			if (SubjectHelper::CanAttack(monsterId, playerId))
				GTimerThread->ScheduleAfter(monsterId, 1s, TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER, playerId);
			else
				monster->SetAttack(false);
			return;
		}

		// 플레이어 사망 처리
		GSector->ForEachNeighborObject(victim->GetSectorX(), victim->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;
			if (!SubjectHelper::CanSee(playerId, id)) return;
			auto viewer = GGameObjectManager->Seek<User>(id);
			if (!viewer) return;
			auto session = viewer->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
			UserHelper::SendPlayerDiePacket(viewer, playerId);
		});

		monster->SetAttack(false);
		monster->SetActive(false);
		GSector->RemoveObject(const_cast<ObjID&>(playerId), victim->RefSectorX(), victim->RefSectorY());
		GTimerThread->ScheduleAfter(playerId, 30s, TIMER_EVENT_TYPE::EV_USER_RESPAWN);

		(void)remaining;
	}

	bool Init()
	{
		for (int32_t i = 0; i < MAX_MONSTER; ++i)
		{
			ObjID monsterId(EnumCategory::eMonster, MONSTER_ID_START + static_cast<uint64_t>(i));
			auto newMonster = MakeNewSubject(monsterId, ObjID::npos);
			ASSERT_CRASH(GGameObjectManager->Insert(monsterId, newMonster));
			SectorHelper::PlaceObjectAtRandomWalkablePosition(monsterId);

			auto monster = GGameObjectManager->Seek<Monster>(monsterId);
			if (monster) monster->SetSpawn(monster->GetX(), monster->GetY());
		}

		cout << "Monster Init Success" << endl;
		return true;
	}
}
