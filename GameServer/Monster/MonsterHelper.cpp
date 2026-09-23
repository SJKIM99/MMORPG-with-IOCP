#include "pch.h"
#include "MonsterHelper.h"
#include "User.h"
#include "UserHelper.h"
#include "SubjectHelper.h"
#include "SectorHelper.h"
#include "Sector.h"
#include "World/WorldRegistry.h"
#include "Zone/ZoneManager.h"
#include "TimerThread.h"
#include "AStar.h"
#include "Collision.h"
#include "Transform.h"
#include "CoreTLS.h"

namespace
{
	// 존 경계는 축 정렬(axis-aligned)이므로 4방향 극단 좌표만 검사한다.
	// 어느 한 방향이라도 ZONE_BOUNDARY_MARGIN 이내에 다른 존이 있으면 경계 근처로 판정.
	bool IsNearZoneBoundary(RegionIndex region, float x, float z)
	{
		const ZoneGrid& grid = GWorld->Grid(region);
		const ZoneId curZone = grid.LocalZoneAt(x, z);
		if (curZone == InvalidZoneId)
			return false;

		// 리전마다 크기가 다르므로 월드 상수가 아니라 그 리전의 크기로 자른다.
		const float x1 = std::max<float>(0.0f, x - ZONE_BOUNDARY_MARGIN);
		const float x2 = std::min<float>(grid.sizeX - 1.0f, x + ZONE_BOUNDARY_MARGIN);
		const float z1 = std::max<float>(0.0f, z - ZONE_BOUNDARY_MARGIN);
		const float z2 = std::min<float>(grid.sizeZ - 1.0f, z + ZONE_BOUNDARY_MARGIN);

		return grid.LocalZoneAt(x1, z) != curZone
			|| grid.LocalZoneAt(x2, z) != curZone
			|| grid.LocalZoneAt(x,  z1) != curZone
			|| grid.LocalZoneAt(x,  z2) != curZone;
	}

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
		monster->UpdateFacing(nextX - ToLegacyTile(monster->GetX()));
		ObjID mutableId = monsterId;
		MonsterHelper::AStarMove(mutableId, nextX, nextY);
	}

	bool TryMoveCachedPathStep(const shared_ptr<Monster>& monster, const ObjID& monsterId, short goalX, short goalY)
	{
		if (monster == nullptr || monster->HasCachedPathTo(goalX, goalY) == false)
			return false;

		auto& path = monster->GetPath();
		const NODE next = path.back();
		if (IsWalkableStep(ToLegacyTile(monster->GetX()), ToLegacyTile(monster->GetZ()), next._x, next._y) == false)
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

		const short currentX = ToLegacyTile(monster->GetX());
		const short currentY = ToLegacyTile(monster->GetZ());
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
		path = FindPath(ToLegacyTile(monster->GetX()), ToLegacyTile(monster->GetZ()), goalX, goalY);
		monster->CachePathTarget(goalX, goalY);

		if (path.empty())
		{
			monster->ClearPath();
			return false;
		}

		const NODE next = path.back();
		if (IsWalkableStep(ToLegacyTile(monster->GetX()), ToLegacyTile(monster->GetZ()), next._x, next._y) == false)
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

		float x = monster->GetX();
		float z = monster->GetZ();
		const float prevX = x;

		SubjectHelper::MovePositionByDirection(
			RegionOfZone(monster->GetZoneId()), x, z, static_cast<char>(LRng() % 4));

		// Guard: new position must stay inside this zone and within world bounds.
		// Without this check a monster near z=499 can randomly step to z=500
		// (a different zone), causing UpdateObjectSectorAndPosition to return false
		// and ASSERT_CRASH to fire.
		const RegionIndex region = RegionOfZone(monster->GetZoneId());
		if (!GWorld->Grid(region).Contains(x, z) ||
		    GWorld->ZoneAt(region, x, z) != monster->GetZoneId())
		{
			return;
		}

		monster->UpdateFacing(static_cast<int>(x - prevX));
		SectorHelper::UpdatePosition(monsterId, x, z);

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
			if (abs(monster->GetZ() - waker->GetZ()) > WAKE_RANGE) return;
		}

		// atomic exchange: 이미 활성화된 경우 중복 스케줄링 방지
		if (!monster->TryActivate()) return;

		switch (monster->GetType())
		{
		case MONSTER_TYPE::PASSIVE:
			GTimerThread->ScheduleAfter(monsterId, 500ms, TIMER_EVENT_TYPE::EV_RANOM_MOVE);
			return;

		case MONSTER_TYPE::AGGRO:
			GTimerThread->ScheduleAfter(monsterId, 500ms, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, wakerId);
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

		GSector->ForEachNeighborObjID(monster->GetSectorX(), monster->GetSectorZ(), [&](const ObjID& id)
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
			GTimerThread->ScheduleAfter(monsterId, 500ms, TIMER_EVENT_TYPE::EV_RANOM_MOVE);
		}
		else
		{
			monster->ClearPath();
			monster->SetActive(false);
		}
	}

	void HandleRespawn(const ObjID& monsterId)
	{
		// Reuse the existing monster object so the ID stays the same.
		// The stress-test client tracks dead monsters by ID: if we send
		// RESPAWN_NFY with the original ID it can decrement g_monster_dead.
		// Creating a brand-new ID (old behaviour) left the dead entry in
		// g_monsterMap forever and g_monster_dead would only go up.
		auto monster = ::GetGameObject<Monster>(monsterId);
		if (monster == nullptr) return;

		monster->GetStat()->SetHp(MONSTER_MAX_HP);
		monster->GetStat()->SetDead(false);
		monster->SetActive(false);
		monster->SetAttack(false);
		monster->ClearPath();

		ObjID mutableId = monsterId;
		SectorHelper::GetRandomPosition(mutableId);
		monster->SetSpawn(ToLegacyTile(monster->GetX()), ToLegacyTile(monster->GetZ()));

		GSector->ForEachNeighborObject(monster->GetSectorX(), monster->GetSectorZ(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;

			auto user = static_pointer_cast<User>(object);
			auto session = user->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
			if (!SubjectHelper::CanSee(object, monster)) return;

			UserHelper::SendSUBJECT_RESPAWN_NFY(user, monster);
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
		// Player left zone — deactivate immediately (no cross-zone chasing)
		if (target->GetZoneId() != monster->GetZoneId())
		{
			monster->ClearPath();
			monster->SetActive(false);
			monster->SetAttack(false);
			return;
		}
		// ─────────────────────────────────────────────────────────────────────

		// ── Zone boundary guard ───────────────────────────────────────────────
		// 존 경계 ZONE_BOUNDARY_MARGIN 칸 이내에 도달하면 추적을 중단하고 스폰으로 복귀.
		// 몬스터가 Zone을 넘지 않으므로 Zone Transfer 로직이 불필요해진다.
		if (IsNearZoneBoundary(RegionOfZone(monster->GetZoneId()), monster->GetX(), monster->GetZ()))
		{
			monster->SetAttack(false);

			const short spawnX = monster->GetSpawnX();
			const short spawnZ = monster->GetSpawnZ();

			// 스폰 위치도 경계 근처이면(배치 오류 등) 즉시 비활성화
			if (IsNearZoneBoundary(RegionOfZone(monster->GetZoneId()),
			                       static_cast<float>(spawnX), static_cast<float>(spawnZ)))
			{
				monster->ClearPath();
				monster->SetActive(false);
				return;
			}

			if (TryAdvanceTowardGoal(monster, monsterId, spawnX, spawnZ))
				GTimerThread->ScheduleAfter(monsterId, 500ms, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
			else
			{
				monster->ClearPath();
				monster->SetActive(false);
			}
			return;
		}
		// ─────────────────────────────────────────────────────────────────────

		// ── Leash check: stop chasing if > 7 tiles from spawn in any axis ──
		if (abs(monster->GetX() - monster->GetSpawnX()) > 7 ||
		    abs(monster->GetZ() - monster->GetSpawnZ()) > 7)
		{
			monster->SetAttack(false);

			if (TryAdvanceTowardGoal(monster, monsterId, monster->GetSpawnX(), monster->GetSpawnZ()))
			{
				GTimerThread->ScheduleAfter(monsterId, 500ms, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
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

			GTimerThread->ScheduleAfter(monsterId, 500ms, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
			return;
		}
		// ─────────────────────────────────────────────────────────────────────

		// ── A* chase: target is > 1 tile away ────────────────────────────────
		(void)TryAdvanceTowardGoal(monster, monsterId, ToLegacyTile(target->GetX()), ToLegacyTile(target->GetZ()));

		if (SubjectHelper::CanSee(monster, target))
		{
			monster->SetActive(true);
			GTimerThread->ScheduleAfter(monsterId, 500ms, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
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
			const ObjID  victimId = victim->GetObjID();
			const int32_t victimHp = static_cast<int32_t>(victim->GetStat()->GetHp());
			GSector->ForEachNeighborObject(victim->GetSectorX(), victim->GetSectorZ(), [&](const shared_ptr<Subject>& object)
			{
				ObjID id = object->GetObjID();
				if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;
				if (!SubjectHelper::CanSee(victim, object)) return;
				auto viewer = static_pointer_cast<User>(object);
				auto session = viewer->GetGameSession();
				if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
				UserHelper::SendSUBJECT_ATTACK_NFY(viewer, victimId, monsterId, victimHp);
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
		GSector->ForEachNeighborObject(victim->GetSectorX(), victim->GetSectorZ(), [&](const shared_ptr<Subject>& object)
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
		GSector->RemoveObject(const_cast<ObjID&>(playerId), victim->RefSectorX(), victim->RefSectorZ());
		GTimerThread->ScheduleAfter(playerId, 30s, TIMER_EVENT_TYPE::EV_USER_RESPAWN);

		(void)remaining;
	}

	void InitForZone(ZoneId zoneId)
	{
		// stride 분배: zoneId, zoneId+ZoneCount, zoneId+2*ZoneCount, ...
		// → 각 존이 전체 ID 범위에서 균등한 간격으로 몬스터를 가져가므로
		//   AGGRO(앞 25%)와 PASSIVE(나머지 75%)가 모든 존에 고르게 분포된다.
		// **몬스터가 생길 수 있는 Zone 들 안에서의 순번**으로 나눈다.
		//
		// ZoneId 를 그대로 시작값으로 쓰면 안 된다. 리전이 상위 바이트에 박혀
		// 있어(마을 0~3, 필드 256~271) 마을 Zone 0 의 수열 0,4,8,...,256 과
		// 필드 Zone 256 의 수열 256,272,... 가 부딪힌다 — 같은 ObjID 를 두 번
		// 넣으려다 ASSERT_CRASH 로 서버가 죽었다.
		const int slot = GWorld->SpawnSlotOf(zoneId);
		if (slot < 0)
		{
			// 평화지역(마을)이다. 리전 데이터의 flags.spawn 이 false 다 (9장).
			cout << "[Zone " << zoneId << "] 평화지역 - 몬스터 없음" << endl;
			return;
		}
		const int stride = static_cast<int>(GWorld->SpawnableZoneIds().size());
		for (int32_t i = slot; i < MAX_MONSTER; i += stride)
		{
			ObjID monsterId(EnumCategory::eMonster, MONSTER_ID_START + static_cast<uint64_t>(i));
			auto newMonster = MakeNewSubject(monsterId, ObjID::npos);
			ASSERT_CRASH(GGameObjectManager->Insert(monsterId, newMonster));
			SectorHelper::GetRandomPosition(monsterId);  // GSector는 호출 전에 Zone::Run()이 설정

			auto monster = ::GetGameObject<Monster>(monsterId);
			if (monster) monster->SetSpawn(ToLegacyTile(monster->GetX()), ToLegacyTile(monster->GetZ()));

		}

		// endl 로 반드시 flush 한다. 개행 문자만 넣으면 버퍼에 남아 있다가
		// 프로세스를 강제 종료할 때 통째로 버려진다. 실제로 그것 때문에
		// 필드 Zone 이 몬스터를 만들지 않는다고 오진했다 — 다 만들고 있었다.
		cout << "[Zone " << zoneId << "] Monster Init Success ("
		     << (MAX_MONSTER / stride) << " monsters)" << endl;
	}
}
