#include "pch.h"
#include "MonsterHelper.h"
#include "User.h"
#include "UserHelper.h"
#include "SubjectHelper.h"
#include "SectorHelper.h"
#include "Sector.h"
#include "TimerThread.h"
#include "AStar.h"
#include "Collision.h"
#include "Transform.h"
#include "CoreTLS.h"

namespace
{
	std::atomic<uint64_t> _nextMonsterId{ MONSTER_ID_START + MAX_MONSTER };

	int ManhattanDistance(short x1, short y1, short x2, short y2)
	{
		return abs(x1 - x2) + abs(y1 - y2);
	}

	bool IsWalkableStep(short currentX, short currentY, short nextX, short nextY)
	{
		if (abs(nextX - currentX) + abs(nextY - currentY) != 1)
			return false;

		return isCollision(nextX, nextY) == false;
	}

	void MoveMonsterStep(const shared_ptr<Monster>& monster, const ObjID& monsterId, short nextX, short nextY)
	{
		monster->UpdateFacing(nextX - monster->GetX());
		ObjID mutableId = monsterId;
		MonsterHelper::AStarMove(mutableId, nextX, nextY);
	}

	bool TryMoveCachedPathStep(const shared_ptr<Monster>& monster, const ObjID& monsterId, short goalX, short goalY)
	{
		if (monster == nullptr || monster->HasCachedPathTo(goalX, goalY) == false)
			return false;

		auto& path = monster->GetPath();
		const NODE next = path.back();
		if (IsWalkableStep(monster->GetX(), monster->GetY(), next._x, next._y) == false)
		{
			monster->ClearPath();
			return false;
		}

		path.pop_back();
		MoveMonsterStep(monster, monsterId, next._x, next._y);
		return true;
	}

	bool TryMoveOneStepTowardGoal(const shared_ptr<Monster>& monster, const ObjID& monsterId, short goalX, short goalY)
	{
		if (monster == nullptr)
			return false;

		const short currentX = monster->GetX();
		const short currentY = monster->GetY();
		const int currentDistance = ManhattanDistance(currentX, currentY, goalX, goalY);
		if (currentDistance <= 1)
			return false;

		short bestX = currentX;
		short bestY = currentY;
		int bestDistance = currentDistance;

		const array<pair<short, short>, 4> directions{ {
			{ 0, -1 },
			{ 0, 1 },
			{ -1, 0 },
			{ 1, 0 }
		} };

		for (const auto [dx, dy] : directions)
		{
			const short nextX = static_cast<short>(currentX + dx);
			const short nextY = static_cast<short>(currentY + dy);
			if (isCollision(nextX, nextY))
				continue;

			const int nextDistance = ManhattanDistance(nextX, nextY, goalX, goalY);
			if (nextDistance >= bestDistance)
				continue;

			bestDistance = nextDistance;
			bestX = nextX;
			bestY = nextY;
		}

		if (bestDistance >= currentDistance)
			return false;

		monster->ClearPath();
		MoveMonsterStep(monster, monsterId, bestX, bestY);
		return true;
	}

	bool TryMoveUsingFreshPath(const shared_ptr<Monster>& monster, const ObjID& monsterId, short goalX, short goalY)
	{
		if (monster == nullptr)
			return false;

		auto& path = monster->GetPath();
		path = FindPath(monster->GetX(), monster->GetY(), goalX, goalY);
		monster->CachePathTarget(goalX, goalY);

		if (path.empty())
		{
			monster->ClearPath();
			return false;
		}

		const NODE next = path.back();
		if (IsWalkableStep(monster->GetX(), monster->GetY(), next._x, next._y) == false)
		{
			monster->ClearPath();
			return false;
		}

		path.pop_back();
		MoveMonsterStep(monster, monsterId, next._x, next._y);
		return true;
	}

	bool TryAdvanceTowardGoal(const shared_ptr<Monster>& monster, const ObjID& monsterId, short goalX, short goalY)
	{
		if (TryMoveCachedPathStep(monster, monsterId, goalX, goalY))
			return true;

		if (TryMoveOneStepTowardGoal(monster, monsterId, goalX, goalY))
			return true;

		return TryMoveUsingFreshPath(monster, monsterId, goalX, goalY);
	}
}

namespace MonsterHelper
{
	void RandomMove(ObjID& monsterId)
	{
		const auto monster = ::GetGameObject<Monster>(monsterId);
		if (monster == nullptr)
			return;

		// 캐시된 viewList를 oldList로 사용 — CollectUsers 첫 번째 호출 제거
		const vector<ObjID> oldList = monster->GetViewList();

		short x = monster->GetX();
		short y = monster->GetY();
		const short prevX = x;

		SubjectHelper::MovePositionByDirection(x, y, static_cast<char>(LRng() % 4));
		monster->UpdateFacing(x - prevX);
		SectorHelper::UpdatePosition(monsterId, x, y);

		auto newList = SectorHelper::CollectUsers(monsterId);
		SectorHelper::Replace(monsterId, oldList, newList);
		monster->SetViewList(std::move(newList));
	}

	void AStarMove(ObjID& monsterId, short nextX, short nextY)
	{
		const auto monster = ::GetGameObject<Monster>(monsterId);
		if (monster == nullptr)
			return;

		// 캐시된 viewList를 oldList로 사용 — CollectUsers 첫 번째 호출 제거
		const vector<ObjID> oldList = monster->GetViewList();

		SectorHelper::UpdatePosition(monsterId, nextX, nextY);

		auto newList = SectorHelper::CollectUsers(monsterId);
		SectorHelper::Replace(monsterId, oldList, newList);
		monster->SetViewList(std::move(newList));
	}

	void WakeUpMonster(ObjID& monsterId, ObjID& wakerId, bool forceWake)
	{
		auto monster = ::GetGameObject<Monster>(monsterId);
		auto waker   = ::GetGameObject<User>(wakerId);
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
		auto monster = ::GetGameObject<Monster>(monsterId);
		if (monster == nullptr || monster->GetStat()->IsDead())
			return;

		bool hasNearbyPlayer  = false;
		ObjID attackTargetId;

		GSector->ForEachNeighborObjID(monster->GetSectorX(), monster->GetSectorY(), [&](const ObjID& id)
		{
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;
			const auto user = ::GetGameObject<User>(id);
			if (!user) return;
			const auto session = user->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
			if (!SubjectHelper::CanSee(monster, user)) return;

			hasNearbyPlayer = true;
			if (SubjectHelper::CanAttack(monster, user))
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
			monster->ClearPath();
			monster->SetActive(false);
		}
	}

	void HandleRespawn(const ObjID& monsterId)
	{
		GGameObjectManager->Delete(monsterId);

		ObjID newId(EnumCategory::eMonster, _nextMonsterId.fetch_add(1));
		auto newMonster = MakeNewSubject(newId, ObjID::npos);
		GGameObjectManager->Insert(newId, newMonster);
		SectorHelper::GetRandomPosition(newId);

		dynamic_pointer_cast<Monster>(newMonster)->SetSpawn(newMonster->GetX(), newMonster->GetY());

		GSector->ForEachNeighborObject(newMonster->GetSectorX(), newMonster->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;

			auto user = static_pointer_cast<User>(object);
			auto session = user->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
			if (!SubjectHelper::CanSee(object, newMonster)) return;

			UserHelper::SendSUBJECT_RESPAWN_NFY(user, newMonster);
		});
	}

	void HandleAggroMove(const ObjID& monsterId, const ObjID& targetId)
	{
		auto monster = ::GetGameObject<Monster>(monsterId);
		if (monster == nullptr || monster->GetStat()->IsDead())
			return;

		// ── Target validation ─────────────────────────────────────────────────
		auto target = ::GetGameObject<User>(targetId);
		if (!target)
		{
			monster->ClearPath();
			monster->SetActive(false);
			monster->SetAttack(false);
			return;
		}
		auto targetSession = target->GetGameSession();
		if (!targetSession || targetSession->m_state != SOCKET_STATE::ST_INGAME)
		{
			monster->ClearPath();
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

			if (TryAdvanceTowardGoal(monster, monsterId, monster->GetSpawnX(), monster->GetSpawnY()))
			{
				GTimerThread->ScheduleAfter(monsterId, 1s, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
			}
			else
			{
				monster->ClearPath();
				monster->SetActive(false);
			}
			return;
		}
		// ─────────────────────────────────────────────────────────────────────

		// ── Within attack range: stop pathfinding, attack only ────────────────
		if (SubjectHelper::CanAttack(monster, target))
		{
			if (!monster->IsAttacking())
				GTimerThread->ScheduleNow(monsterId, TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER, targetId);

			GTimerThread->ScheduleAfter(monsterId, 1s, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
			return;
		}
		// ─────────────────────────────────────────────────────────────────────

		// ── A* chase: target is > 1 tile away ────────────────────────────────
		(void)TryAdvanceTowardGoal(monster, monsterId, target->GetX(), target->GetY());

		if (SubjectHelper::CanSee(monster, target))
		{
			monster->SetActive(true);
			GTimerThread->ScheduleAfter(monsterId, 1s, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
		}
		else
		{
			monster->ClearPath();
			monster->SetActive(false);
		}
		// ─────────────────────────────────────────────────────────────────────
	}

	void HandleAttackToPlayer(const ObjID& monsterId, const ObjID& playerId)
	{
		auto monster = ::GetGameObject<Monster>(monsterId);
		auto victim  = ::GetGameObject<User>(playerId);
		if (monster == nullptr || victim == nullptr)
			return;

		monster->SetAttack(true);

		auto victimSession = victim->GetGameSession();
		if (!victimSession || victimSession->m_state != SOCKET_STATE::ST_INGAME
			|| victim->GetStat()->IsDead()
			|| !SubjectHelper::IsAdjacent(monster, victim)
			|| monster->GetStat()->IsDead())
		{
			monster->ClearPath();
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
				if (!SubjectHelper::CanSee(victim, object)) return;
				auto viewer = static_pointer_cast<User>(object);
				auto session = viewer->GetGameSession();
				if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
				UserHelper::SendSUBJECT_ATTACK_NFY(viewer, monsterId);
			});

			if (SubjectHelper::CanAttack(monster, victim))
				GTimerThread->ScheduleAfter(monsterId, 1s, TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER, playerId);
			else
			{
				monster->ClearPath();
				monster->SetAttack(false);
			}
			return;
		}

		// 플레이어 사망 처리
		GSector->ForEachNeighborObject(victim->GetSectorX(), victim->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;
			if (!SubjectHelper::CanSee(victim, object)) return;
			auto viewer = static_pointer_cast<User>(object);
			auto session = viewer->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
			UserHelper::SendSUBJECT_DIE_NFY(viewer, victim);
		});

		monster->SetAttack(false);
		monster->SetActive(false);
		monster->ClearPath();
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
			SectorHelper::GetRandomPosition(monsterId);

			auto monster = ::GetGameObject<Monster>(monsterId);
			if (monster) monster->SetSpawn(monster->GetX(), monster->GetY());
		}

		cout << "Monster Init Success" << endl;
		return true;
	}
}
