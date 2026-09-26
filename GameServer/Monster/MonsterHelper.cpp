#include "pch.h"
#include "Combat/CombatHelper.h"
#include "MonsterHelper.h"
#include "User.h"
#include "UserHelper.h"
#include "SubjectHelper.h"
#include "SectorHelper.h"
#include "Sector.h"
#include "Zone/ZoneLayout.h"
#include "Zone/ZoneManager.h"
#include "TimerThread.h"
#include "AStar.h"
#include "Collision.h"
#include "Transform.h"
#include "CoreTLS.h"
#include "Data/WorldData.h"
#include "Data/LevelTable.h"
#include "Content/ContentHelper.h"

namespace
{
	// 존 경계는 축 정렬(axis-aligned)이므로 4방향 극단 좌표만 검사한다.
	// 어느 한 방향이라도 ZONE_BOUNDARY_MARGIN 이내에 다른 존이 있으면 경계 근처로 판정.
	bool IsNearZoneBoundary(short x, short y)
	{
		const ZoneId curZone = ZoneLayout::GetZoneIdByWorld(x, y);
		if (curZone == InvalidZoneId)
			return false;

		const short x1 = static_cast<short>(std::max<int>(0, x - ZONE_BOUNDARY_MARGIN));
		const short x2 = static_cast<short>(std::min<int>(W_WIDTH  - 1, x + ZONE_BOUNDARY_MARGIN));
		const short y1 = static_cast<short>(std::max<int>(0, y - ZONE_BOUNDARY_MARGIN));
		const short y2 = static_cast<short>(std::min<int>(W_HEIGHT - 1, y + ZONE_BOUNDARY_MARGIN));

		return ZoneLayout::GetZoneIdByWorld(x1, y) != curZone
			|| ZoneLayout::GetZoneIdByWorld(x2, y) != curZone
			|| ZoneLayout::GetZoneIdByWorld(x,  y1) != curZone
			|| ZoneLayout::GetZoneIdByWorld(x,  y2) != curZone;
	}

	int ManhattanDistance(short x1, short y1, short x2, short y2)
	{
		return abs(x1 - x2) + abs(y1 - y2);
	}

	// 몬스터가 밟을 수 없는 칸: 장애물 + 마을(안전지대). 마을 규칙은 서버가 판정한다.
	bool IsMonsterBlocked(short x, short y)
	{
		return isCollision(x, y) || WorldData::IsSafeZone(x, y);
	}

	bool IsWalkableStep(short currentX, short currentY, short nextX, short nextY)
	{
		if (abs(nextX - currentX) + abs(nextY - currentY) != 1)
			return false;

		return IsMonsterBlocked(nextX, nextY) == false;
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
			if (IsMonsterBlocked(nextX, nextY))
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

	// 추격을 멈추고 잠든 상태로 되돌린다. 보스는 스폰 지점에서 쉬면 체력을 회복한다
	// (멀리 끌고 가서 조금씩 깎는 "리쉬 악용"을 막는 리니지식 처리).
	void Deactivate(const shared_ptr<Monster>& monster)
	{
		monster->ClearPath();
		monster->SetActive(false);
		monster->SetAttack(false);
		monster->ClearProvoke();
		if (monster->IsBoss())
		{
			monster->StopBossSkillLoop();
			if (monster->GetX() == monster->GetSpawnX() && monster->GetY() == monster->GetSpawnY())
				monster->GetStat()->SetHp(monster->GetStat()->GetMaxHp());
		}
	}

	std::chrono::milliseconds MoveDelay(const shared_ptr<Monster>& monster)
	{
		// V2: 둔화면 느려진다
		const uint32_t slow = monster->Status().SlowPercent(GetNowTime());
		return std::chrono::milliseconds(monster->GetMoveIntervalMs() * (100u + slow) / 100u);
	}

	// V2: 기절/빙결이면 행동 대신 잠깐 뒤에 다시 판단한다. true면 이번 행동을 건너뛴다.
	bool WaitIfDisabled(const shared_ptr<Monster>& monster, bool forMove, TIMER_EVENT_TYPE ev, const ObjID& target)
	{
		const uint32_t now = GetNowTime();
		const bool blocked = forMove ? monster->Status().BlocksMove(now) : monster->Status().BlocksAttack(now);
		if (!blocked)
			return false;
		uint32_t wait = 0;
		for (auto& slot : monster->Status().Slots())
			if (slot.stacks > 0 && now < slot.until)
			{
				const StatusRow& row = StatusTable::Get(slot.id);
				if ((forMove && row.blocksMove) || (!forMove && row.blocksAttack))
					wait = (std::max)(wait, slot.until - now);
			}
		wait = std::clamp<uint32_t>(wait + 30, 60, 1500);
		GTimerThread->ScheduleAfter(monster->GetObjID(), std::chrono::milliseconds(wait), ev, target);
		return true;
	}

	// ── V2 강공격 패턴 ────────────────────────────────────────────────────────
	void SendTelegraph(const shared_ptr<Monster>& monster, TelegraphShape shape, short x, short y, short fromX, short fromY, uint8_t radius, uint16_t durationMs, MonsterPattern pattern)
	{
		if (monster->GetSectorX() < 0)
			return;
		MONSTER_TELEGRAPH_NFY_PACKET packet{};
		packet.size = sizeof(packet);
		packet.type = static_cast<char>(PacketType::MONSTER_TELEGRAPH_NFY);
		packet.monsterId = monster->GetObjID();
		packet.shape = static_cast<uint8_t>(shape);
		packet.x = x; packet.y = y;
		packet.fromX = fromX; packet.fromY = fromY;
		packet.radius = radius;
		packet.durationMs = durationMs;
		packet.pattern = static_cast<uint8_t>(pattern);
		GSector->ForEachNeighborObject(monster->GetSectorX(), monster->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
				return;
			auto viewer = static_pointer_cast<User>(object);
			auto session = viewer->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				return;
			if (!SubjectHelper::CanSee(object, monster))
				return;
			session->PostSend(packet);
		});
	}

	int SignOf(int v) { return (v > 0) - (v < 0); }

	bool TryStartPattern(const shared_ptr<Monster>& monster, const shared_ptr<User>& target)
	{
		const MonsterTableRow* row = monster->GetSpecies();
		if (row == nullptr || monster->m_pattern.pending)
			return false;
		const MonsterPattern pattern = MonsterTable::PatternOf(*row);
		const uint32_t now = GetNowTime();
		if (pattern == MonsterPattern::None || now < monster->m_patternReadyAt)
			return false;
		if (monster->Status().BlocksAttack(now) || monster->Status().BlocksMove(now))
			return false;

		const int mx = monster->GetX(), my = monster->GetY();
		const int tx = target->GetX(), ty = target->GetY();
		const int dist = (std::max)(std::abs(tx - mx), std::abs(ty - my));
		Monster::PatternState st;
		st.type = pattern;
		st.fromX = static_cast<short>(mx);
		st.fromY = static_cast<short>(my);
		TelegraphShape shape = TelegraphShape::Circle;
		uint16_t windup = 800;

		switch (pattern)
		{
		case MonsterPattern::Charge:
		{
			if (dist < 2 || dist > 4) return false;
			st.dx = static_cast<int8_t>(SignOf(tx - mx));
			st.dy = static_cast<int8_t>(SignOf(ty - my));
			// 돌진 끝점: 그 방향으로 4칸(막히면 그 앞까지)
			int ex = mx, ey = my;
			for (int i = 0; i < 4; ++i)
			{
				const int nx = ex + st.dx, ny = ey + st.dy;
				if (!ZoneLayout::IsValidWorldPosition(static_cast<short>(nx), static_cast<short>(ny)) || isCollision(static_cast<short>(nx), static_cast<short>(ny))
					|| WorldData::IsSafeZone(nx, ny) || ZoneLayout::GetZoneIdByWorld(static_cast<short>(nx), static_cast<short>(ny)) != monster->GetZoneId())
					break;
				ex = nx; ey = ny;
			}
			if (ex == mx && ey == my) return false;
			st.cx = static_cast<short>(ex); st.cy = static_cast<short>(ey);
			shape = TelegraphShape::Line;
			windup = 650;
			break;
		}
		case MonsterPattern::Slam:
			if (dist > 1) return false;
			st.cx = static_cast<short>(mx); st.cy = static_cast<short>(my);
			st.radius = 1;
			windup = 850;
			break;
		case MonsterPattern::Ranged:
			if (dist < 2 || dist > 5) return false;
			st.cx = static_cast<short>(tx); st.cy = static_cast<short>(ty);
			st.radius = 0;
			windup = 750;
			break;
		case MonsterPattern::Leap:
			if (dist < 2 || dist > 4) return false;
			st.cx = static_cast<short>(tx); st.cy = static_cast<short>(ty);
			st.radius = 1;
			windup = 700;
			break;
		default:
			return false;
		}

		st.pending = true;
		monster->m_pattern = st;
		monster->m_countered = false;
		monster->m_busyUntil = now + windup + 350;
		monster->m_patternReadyAt = now + windup + 4500 + static_cast<uint32_t>(LRng() % 2500);
		monster->UpdateFacing(tx - mx);
		SendTelegraph(monster, shape, st.cx, st.cy, st.fromX, st.fromY, st.radius, windup, pattern);
		GTimerThread->ScheduleAfter(monster->GetObjID(), std::chrono::milliseconds(windup), TIMER_EVENT_TYPE::EV_MONSTER_PATTERN, target->GetObjID());
		return true;
	}

	void StartBossSkillLoopIfNeeded(const shared_ptr<Monster>& monster)
	{
		if (monster->IsBoss() && monster->TryStartBossSkillLoop())
			GTimerThread->ScheduleAfter(monster->GetObjID(), 2500ms, TIMER_EVENT_TYPE::EV_BOSS_SKILL_CAST);
	}

	// 지역 스폰 가중치로 종족을 하나 고른다.
	uint16_t PickSpecies(const WorldData::RegionRow& region)
	{
		int total = 0;
		for (const auto& spawn : region.spawns)
			total += spawn.weight;
		if (total <= 0)
			return 0;

		int roll = static_cast<int>(LRng() % static_cast<uint32_t>(total));
		for (const auto& spawn : region.spawns)
		{
			if (roll < spawn.weight)
				return spawn.species;
			roll -= spawn.weight;
		}
		return region.spawns[0].species;
	}

	// 피해 편차(±15%) — 몬스터는 치명타가 없다.
	uint16_t RollMonsterDamage(const shared_ptr<Monster>& monster)
	{
		bool crit = false;
		const uint32_t damage = CombatRule::RollDamage(monster->GetStat()->GetOffensive(), LRng() % 1001, 100, crit);
		return static_cast<uint16_t>(std::min<uint32_t>(damage, 65535));
	}

	void NotifyPlayerHitByMonster(const shared_ptr<User>& victim, const ObjID& monsterId, uint16_t damage, uint8_t damageFlags)
	{
		const ObjID  victimId = victim->GetObjID();
		const int32_t victimHp = static_cast<int32_t>(victim->GetStat()->GetHp());
		GSector->ForEachNeighborObject(victim->GetSectorX(), victim->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;
			if (!SubjectHelper::CanSee(victim, object)) return;
			auto viewer = static_pointer_cast<User>(object);
			auto session = viewer->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
			UserHelper::SendSUBJECT_ATTACK_NFY(viewer, victimId, monsterId, victimHp);
		});
		UserHelper::BroadcastDamage(victim, monsterId, damage, damageFlags);
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

		// Guard: new position must stay inside this zone and within world bounds.
		// Without this check a monster near y=499 can randomly step to y=500
		// (a different zone), causing UpdateObjectSectorAndPosition to return false
		// and ASSERT_CRASH to fire.
		// 마을(안전지대)에는 몬스터가 들어가지 않는다.
		if (!ZoneLayout::IsValidWorldPosition(x, y) ||
		    ZoneLayout::GetZoneIdByWorld(x, y) != monster->GetZoneId() ||
		    WorldData::IsSafeZone(x, y))
		{
			return;
		}

		monster->UpdateFacing(x - prevX);
		monster->SetLastStepMs(monster->GetMoveIntervalMs());
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

		monster->SetLastStepMs(monster->GetMoveIntervalMs());
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
		if (monster->IsDummy()) return;   // 허수아비는 깨어나지 않는다(AI 루프가 없다)

		// Aggro monsters only wake up when the player is within WAKE_RANGE tiles
		// (skipped on forced wake, e.g. player respawn)
		if (!forceWake && monster->GetType() == MONSTER_TYPE::AGGRO)
		{
			if (abs(monster->GetX() - waker->GetX()) > WAKE_RANGE) return;
			if (abs(monster->GetY() - waker->GetY()) > WAKE_RANGE) return;
		}

		// 마을 안의 플레이어는 선공 대상이 아니다.
		if (monster->GetType() == MONSTER_TYPE::AGGRO && WorldData::IsSafeZone(waker->GetX(), waker->GetY()))
			return;

		// atomic exchange: 이미 활성화된 경우 중복 스케줄링 방지
		if (!monster->TryActivate()) return;

		switch (monster->GetType())
		{
		case MONSTER_TYPE::PASSIVE:
			GTimerThread->ScheduleAfter(monsterId, MoveDelay(monster), TIMER_EVENT_TYPE::EV_RANOM_MOVE);
			return;

		case MONSTER_TYPE::AGGRO:
			GTimerThread->ScheduleAfter(monsterId, MoveDelay(monster), TIMER_EVENT_TYPE::EV_AGGRO_MOVE, wakerId);
			StartBossSkillLoopIfNeeded(monster);
			return;
		}
	}

	void Provoke(const shared_ptr<Monster>& monster, const shared_ptr<User>& attacker)
	{
		if (monster == nullptr || attacker == nullptr || monster->GetStat()->IsDead())
			return;
		if (monster->IsDummy())
			return;   // 허수아비는 반격하지 않는다

		// 이미 누군가를 쫓고 있으면(선공 몬스터의 추격 / 이미 도발됨) 대상을 바꾸지 않는다.
		if (monster->IsProvoked())
			return;
		if (monster->GetType() == MONSTER_TYPE::AGGRO && monster->IsActive())
			return;

		monster->SetProvoked(attacker->GetObjID());

		// 잠들어 있었다면 곧장 추격 루프를 시작한다. 이미 배회 루프(EV_RANOM_MOVE)가
		// 돌고 있다면 HandleRandomMove가 다음 틱에 추격 루프로 갈아탄다 — 루프가
		// 두 개 동시에 돌지 않도록 여기서 새로 예약하지 않는다.
		if (monster->TryActivate())
			GTimerThread->ScheduleAfter(monster->GetObjID(), MoveDelay(monster), TIMER_EVENT_TYPE::EV_AGGRO_MOVE, attacker->GetObjID());

		StartBossSkillLoopIfNeeded(monster);
	}

	void HandleRandomMove(const ObjID& monsterId)
	{
		auto monster = ::GetGameObject<Monster>(monsterId);
		if (monster == nullptr || monster->GetStat()->IsDead())
			return;
		if (WaitIfDisabled(monster, true, TIMER_EVENT_TYPE::EV_RANOM_MOVE, ObjID{}))
			return;

		// 맞아서 화가 난 상태면 배회 루프를 추격 루프로 갈아탄다.
		if (monster->IsProvoked())
		{
			GTimerThread->ScheduleAfter(monsterId, MoveDelay(monster), TIMER_EVENT_TYPE::EV_AGGRO_MOVE, monster->GetProvokedBy());
			return;
		}

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
			if (CombatHelper::MonsterCanReach(monster, user) && !WorldData::IsSafeZone(user->GetX(), user->GetY())
				&& !user->Status().Has(StatusId::Veil, GetNowTime()))
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
			GTimerThread->ScheduleAfter(monsterId, MoveDelay(monster), TIMER_EVENT_TYPE::EV_RANOM_MOVE);
		}
		else
		{
			Deactivate(monster);
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

		monster->GetStat()->SetHp(monster->GetStat()->GetMaxHp());
		monster->GetStat()->SetDead(false);
		monster->SetActive(false);
		monster->SetAttack(false);
		monster->ClearPath();
		monster->ClearProvoke();
		monster->StopBossSkillLoop();

		ObjID mutableId = monsterId;
		if (monster->IsBoss())
		{
			// 보스는 정해진 자리에 다시 나타난다(스폰 지점 = 레이드 장소).
			SectorHelper::UpdatePosition(mutableId, monster->GetSpawnX(), monster->GetSpawnY());
		}
		else
		{
			SectorHelper::GetRandomPosition(mutableId);
			monster->SetSpawn(monster->GetX(), monster->GetY());
		}

		GSector->ForEachNeighborObject(monster->GetSectorX(), monster->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;

			auto user = static_pointer_cast<User>(object);
			auto session = user->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
			if (!SubjectHelper::CanSee(object, monster)) return;

			UserHelper::SendSUBJECT_RESPAWN_NFY(user, monster);
		});

		if (monster->IsBoss())
		{
			UserHelper::BroadcastEffect(monster, monsterId, EffectId::BossSpawn, monster->GetX(), monster->GetY(), 0);
			UserHelper::BroadcastWorldNotice(WorldNoticeCode::BossSpawned, monster->GetSpecies()->id, monster->GetZoneId(), "");
		}
	}

	void HandleAggroMove(const ObjID& monsterId, const ObjID& targetId)
	{
		auto monster = ::GetGameObject<Monster>(monsterId);
		if (monster == nullptr || monster->GetStat()->IsDead())
			return;
		if (WaitIfDisabled(monster, true, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId))
			return;
		if (const uint32_t now = GetNowTime(); now < monster->m_busyUntil)
		{
			GTimerThread->ScheduleAfter(monsterId, std::chrono::milliseconds(monster->m_busyUntil - now + 10), TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
			return;
		}

		// ── Phase 2-B 도발: 도발한 사람을 쫓는다 ──────────────────────────────
		{
			const uint32_t now = GetNowTime();
			if (const StatusSlot* taunt = monster->Status().Get(StatusId::Taunt, now); taunt && taunt->source != targetId)
				if (auto taunter = ::GetGameObject<User>(taunt->source); taunter && !taunter->GetStat()->IsDead() && taunter->GetZoneId() == monster->GetZoneId())
				{
					monster->SetProvoked(taunter->GetObjID());
					GTimerThread->ScheduleAfter(monsterId, MoveDelay(monster), TIMER_EVENT_TYPE::EV_AGGRO_MOVE, taunter->GetObjID());
					return;
				}
		}

		// ── Target validation ─────────────────────────────────────────────────
		auto target = ::GetGameObject<User>(targetId);
		if (!target)
		{
			Deactivate(monster);
			return;
		}
		auto targetSession = target->GetGameSession();
		if (!targetSession || targetSession->m_state != SOCKET_STATE::ST_INGAME)
		{
			Deactivate(monster);
			return;
		}
		// Player left zone — deactivate immediately (no cross-zone chasing)
		if (target->GetZoneId() != monster->GetZoneId())
		{
			Deactivate(monster);
			return;
		}
		// ─────────────────────────────────────────────────────────────────────

		const auto returnToSpawn = [&]()
		{
			monster->SetAttack(false);
			monster->ClearProvoke();
			if (TryAdvanceTowardGoal(monster, monsterId, monster->GetSpawnX(), monster->GetSpawnY()))
				GTimerThread->ScheduleAfter(monsterId, MoveDelay(monster), TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
			else
				Deactivate(monster);
		};

		// ── Zone boundary guard ───────────────────────────────────────────────
		// 존 경계 ZONE_BOUNDARY_MARGIN 칸 이내에 도달하면 추적을 중단하고 스폰으로 복귀.
		// 몬스터가 Zone을 넘지 않으므로 Zone Transfer 로직이 불필요해진다.
		if (IsNearZoneBoundary(monster->GetX(), monster->GetY()))
		{
			// 스폰 위치도 경계 근처이면(배치 오류 등) 즉시 비활성화
			if (IsNearZoneBoundary(monster->GetSpawnX(), monster->GetSpawnY()))
			{
				Deactivate(monster);
				return;
			}
			returnToSpawn();
			return;
		}
		// ─────────────────────────────────────────────────────────────────────

		// ── 대상이 죽었거나 마을(안전지대)로 들어갔으면 추격 포기 ─────────────────
		// (마을 칸은 몬스터에게 막힌 칸이라, 그대로 A*를 돌리면 도달 불가능한 목표를
		//  찾느라 Zone 전체를 탐색하게 된다 — 경로 탐색 전에 먼저 걸러낸다.)
		// Phase 2-B: 밤 장막(은신) 중인 대상은 놓친다.
		if (target->GetStat()->IsDead() || WorldData::IsSafeZone(target->GetX(), target->GetY()) || target->Status().Has(StatusId::Veil, GetNowTime()))
		{
			returnToSpawn();
			return;
		}

		// ── Leash check: 스폰에서 종족별 한계(기본 7칸, 보스 12칸)를 넘으면 귀환 ──
		const int leash = monster->GetLeash();
		if (abs(monster->GetX() - monster->GetSpawnX()) > leash ||
		    abs(monster->GetY() - monster->GetSpawnY()) > leash)
		{
			returnToSpawn();
			return;
		}
		// ─────────────────────────────────────────────────────────────────────

		// 붙어 있는데 반대쪽을 보고 있으면 돌아서기만 한다(예전에는 등을 돌린 채로
		// 대상 칸으로 경로를 다시 찾다가 대상과 같은 칸에 올라서는 일이 있었다).
		if (SubjectHelper::IsAdjacent(monster, target))
			monster->UpdateFacing(target->GetX() - monster->GetX());

		// ── Within attack range: stop pathfinding, attack only ────────────────
		if (CombatHelper::MonsterCanReach(monster, target))
		{
			monster->UpdateFacing(target->GetX() - monster->GetX());
			// V2: 강공격 패턴(돌진·내려찍기·원거리…)을 쓸지 먼저 본다
			if (target->IsExtended() && TryStartPattern(monster, target))
			{
				GTimerThread->ScheduleAfter(monsterId, MoveDelay(monster), TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
				return;
			}
			if (!monster->IsAttacking())
				GTimerThread->ScheduleNow(monsterId, TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER, targetId);

			GTimerThread->ScheduleAfter(monsterId, MoveDelay(monster), TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
			return;
		}
		// ─────────────────────────────────────────────────────────────────────

		// ── V2: 거리가 있을 때 쓰는 패턴(돌진·원거리·도약) ────────────────────
		if (target->IsExtended() && TryStartPattern(monster, target))
		{
			GTimerThread->ScheduleAfter(monsterId, MoveDelay(monster), TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
			return;
		}

		// ── A* chase: target is > 1 tile away ────────────────────────────────
		(void)TryAdvanceTowardGoal(monster, monsterId, target->GetX(), target->GetY());

		if (SubjectHelper::CanSee(monster, target))
		{
			monster->SetActive(true);
			GTimerThread->ScheduleAfter(monsterId, MoveDelay(monster), TIMER_EVENT_TYPE::EV_AGGRO_MOVE, targetId);
		}
		else
		{
			Deactivate(monster);
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
		const bool extendedVictim = victimSession && victimSession->IsExtended();
		if (!victimSession || victimSession->m_state != SOCKET_STATE::ST_INGAME
			|| victim->GetStat()->IsDead()
			|| !(extendedVictim ? CombatHelper::MonsterCanReach(monster, victim) : SubjectHelper::IsAdjacent(monster, victim))
			|| monster->GetStat()->IsDead()
			|| victim->GetZoneId() != monster->GetZoneId()
			|| WorldData::IsSafeZone(victim->GetX(), victim->GetY()))
		{
			monster->ClearPath();
			monster->SetAttack(false);
			return;
		}

		// V2: 기절이면 치지 못한다(풀리면 다시 판단). 강공격 예고 중이면 평타를 쉬었다가.
		if (WaitIfDisabled(monster, false, TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER, playerId))
			return;
		if (const uint32_t now = GetNowTime(); now < monster->m_busyUntil)
		{
			GTimerThread->ScheduleAfter(monsterId, std::chrono::milliseconds(monster->m_busyUntil - now + 10), TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER, playerId);
			return;
		}

		if (extendedVictim)
		{
			// V2 대상: 회피·방어·철벽 태세·반격까지 CombatHelper가 처리
			const bool died = CombatHelper::HitPlayerFromMonster(monster, victim, RollMonsterDamage(monster), 0);
			if (!died)
			{
				// 빙결된 몬스터는 공격이 느리다
				const uint32_t interval = monster->GetAttackIntervalMs() * (monster->Status().Has(StatusId::Freeze, GetNowTime()) ? 2u : 1u);
				if (CombatHelper::MonsterCanReach(monster, victim))
					GTimerThread->ScheduleAfter(monsterId, std::chrono::milliseconds(interval), TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER, playerId);
				else
				{
					monster->ClearPath();
					monster->SetAttack(false);
				}
				return;
			}
			monster->SetAttack(false);
			Deactivate(monster);
			ContentHelper::HandlePlayerDeath(victim, monsterId, false);
			return;
		}

		const uint16_t damage = RollMonsterDamage(monster);
		const uint16_t remaining = victim->GetStat()->TakeDamage(damage);

		if (!victim->GetStat()->IsDead())
		{
			NotifyPlayerHitByMonster(victim, monsterId, damage, 0);

			if (SubjectHelper::CanAttack(monster, victim))
				GTimerThread->ScheduleAfter(monsterId, std::chrono::milliseconds(monster->GetAttackIntervalMs()), TIMER_EVENT_TYPE::EV_MONSTER_ATTACK_TO_USER, playerId);
			else
			{
				monster->ClearPath();
				monster->SetAttack(false);
			}
			return;
		}

		// 플레이어 사망 처리 — 사망 알림/섹터 제거/부활 예약/경험치 페널티는 몬스터·PK
		// 공통 경로(ContentHelper::HandlePlayerDeath)에서 한 번에 처리한다.
		UserHelper::BroadcastDamage(victim, monsterId, damage, DAMAGE_KILLED);
		monster->SetAttack(false);
		Deactivate(monster);
		ContentHelper::HandlePlayerDeath(victim, monsterId, false);

		(void)remaining;
	}

	void HandleBossSkillCast(const ObjID& bossId)
	{
		auto boss = ::GetGameObject<Monster>(bossId);
		if (boss == nullptr || boss->GetStat()->IsDead() || !boss->IsActive() || boss->GetSectorX() < 0)
		{
			if (boss) boss->StopBossSkillLoop();
			return;
		}

		// 3칸 안에 (마을 밖의) 살아 있는 플레이어가 있을 때만 시전한다.
		bool hasTarget = false;
		GSector->ForEachNeighborObject(boss->GetSectorX(), boss->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (hasTarget || object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
				return;
			if (object->GetStat()->IsDead() || WorldData::IsSafeZone(object->GetX(), object->GetY()))
				return;
			if (abs(object->GetX() - boss->GetX()) <= 3 && abs(object->GetY() - boss->GetY()) <= 3)
				hasTarget = true;
		});

		if (!hasTarget)
		{
			GTimerThread->ScheduleAfter(bossId, 2s, TIMER_EVENT_TYPE::EV_BOSS_SKILL_CAST);
			return;
		}

		// 시전 위치를 고정해 둔다 — 예고 원 밖으로 피하면 맞지 않는다(피할 수 있는 광역기).
		boss->SetSkillCenter(boss->GetX(), boss->GetY());
		UserHelper::BroadcastEffect(boss, bossId, EffectId::BossSlamCast, boss->GetSkillX(), boss->GetSkillY(), 2);
		GTimerThread->ScheduleAfter(bossId, 1200ms, TIMER_EVENT_TYPE::EV_BOSS_SKILL_IMPACT);
	}

	void HandleBossSkillImpact(const ObjID& bossId)
	{
		auto boss = ::GetGameObject<Monster>(bossId);
		if (boss == nullptr || boss->GetStat()->IsDead() || boss->GetSectorX() < 0)
		{
			if (boss) boss->StopBossSkillLoop();
			return;
		}

		constexpr int kRadius = 2;
		const short cx = boss->GetSkillX();
		const short cy = boss->GetSkillY();
		UserHelper::BroadcastEffect(boss, bossId, EffectId::BossSlamImpact, cx, cy, kRadius);

		std::vector<shared_ptr<User>> victims;
		GSector->ForEachNeighborObject(boss->GetSectorX(), boss->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
				return;
			if (abs(object->GetX() - cx) > kRadius || abs(object->GetY() - cy) > kRadius)
				return;
			auto user = static_pointer_cast<User>(object);
			if (user->GetStat()->IsDead() || WorldData::IsSafeZone(user->GetX(), user->GetY()))
				return;
			victims.push_back(std::move(user));
		});

		for (const auto& victim : victims)
		{
			// 광역기는 평타의 2배
			bool crit = false;
			const uint32_t damage32 = CombatRule::RollDamage(boss->GetStat()->GetOffensive() * 2u, LRng() % 1001, 100, crit);
			const uint16_t damage = static_cast<uint16_t>(std::min<uint32_t>(damage32, 65535));
			victim->GetStat()->TakeDamage(damage);
			if (!victim->GetStat()->IsDead())
			{
				NotifyPlayerHitByMonster(victim, bossId, damage, DAMAGE_BOSS_SKILL);
				continue;
			}
			UserHelper::BroadcastDamage(victim, bossId, damage, static_cast<uint8_t>(DAMAGE_BOSS_SKILL | DAMAGE_KILLED));
			ContentHelper::HandlePlayerDeath(victim, bossId, false);
		}

		if (boss->IsActive())
			GTimerThread->ScheduleAfter(bossId, 6s, TIMER_EVENT_TYPE::EV_BOSS_SKILL_CAST);
		else
			boss->StopBossSkillLoop();
	}

	void InitForZone(ZoneId zoneId)
	{
		const WorldData::RegionRow* region = WorldData::FindRegion(zoneId);

		// stride 분배: zoneId, zoneId+ZoneCount, zoneId+2*ZoneCount, ...
		// 각 존이 전체 ID 범위에서 균등한 간격으로 몬스터를 가져간다. 종족은 지역의 스폰
		// 가중치로 정하고, ObjID의 KIND에 실어 둔다 — 클라이언트는 KIND만 보고도 무엇을
		// 그릴지 안다(기존 패킷 구조 변경 없음).
		const int stride = static_cast<int>(ZoneLayout::ZoneCount);
		for (int32_t i = zoneId; i < MAX_MONSTER; i += stride)
		{
			ObjID monsterId(EnumCategory::eMonster, MONSTER_ID_START + static_cast<uint64_t>(i));
			if (region != nullptr)
				monsterId.SetKIND(PickSpecies(*region));
			auto newMonster = MakeNewSubject(monsterId, ObjID::npos);
			ASSERT_CRASH(GGameObjectManager->Insert(monsterId, newMonster));
			SectorHelper::GetRandomPosition(monsterId);  // GSector는 호출 전에 Zone::Run()이 설정

			auto monster = ::GetGameObject<Monster>(monsterId);
			if (monster) monster->SetSpawn(monster->GetX(), monster->GetY());
		}

		// 필드 보스 — 일반 몬스터 ID 구간 뒤쪽(MONSTER_ID_START + MAX_MONSTER + zoneId)을 쓴다.
		if (region != nullptr && region->bossSpecies != 0)
		{
			ObjID bossId(EnumCategory::eMonster, MONSTER_ID_START + static_cast<uint64_t>(MAX_MONSTER) + zoneId);
			bossId.SetKIND(region->bossSpecies);
			auto newBoss = MakeNewSubject(bossId, ObjID::npos);
			ASSERT_CRASH(GGameObjectManager->Insert(bossId, newBoss));
			SectorHelper::UpdatePosition(bossId, region->bossX, region->bossY);
			if (auto boss = ::GetGameObject<Monster>(bossId))
				boss->SetSpawn(region->bossX, region->bossY);
		}

		// Phase 2-E: 마을 훈련장 허수아비 — 보스 구간 뒤(MAX_MONSTER + 10000 + k)의 id. 봇 · 레거시 세션에도
		// 여느 몬스터처럼 보이지만(SUBJECT_ADD_NFY) 움직이지 않고, 마을 안이라 봇은 공격할 수 없다.
		if (zoneId == static_cast<ZoneId>(WorldData::kTownZoneId))
			for (int k = 0; k < WorldData::kDummyCount; ++k)
			{
				ObjID dummyId(EnumCategory::eMonster, MONSTER_ID_START + static_cast<uint64_t>(MAX_MONSTER) + 10000u + static_cast<uint64_t>(k));
				dummyId.SetKIND(MonsterTable::Id::TrainingDummy);
				auto dummy = MakeNewSubject(dummyId, ObjID::npos);
				ASSERT_CRASH(GGameObjectManager->Insert(dummyId, dummy));
				SectorHelper::UpdatePosition(dummyId, WorldData::kDummySpots[k].x, WorldData::kDummySpots[k].y);
				if (auto m = ::GetGameObject<Monster>(dummyId))
				{
					m->SetSpawn(WorldData::kDummySpots[k].x, WorldData::kDummySpots[k].y);
					m->UpdateFacing(-1);   // 광장 쪽(서쪽)을 본다
				}
			}

		cout << "[Zone " << zoneId << "] Monster Init Success ("
		     << (MAX_MONSTER / stride) << " monsters"
		     << ((region != nullptr && region->bossSpecies != 0) ? " + boss" : "") << ")\n";
	}

	void HandlePatternImpact(const ObjID& monsterId, const ObjID& targetId)
	{
		auto monster = ::GetGameObject<Monster>(monsterId);
		if (monster == nullptr || monster->GetStat()->IsDead() || !monster->m_pattern.pending || monster->GetSectorX() < 0)
		{
			if (monster) monster->m_pattern.pending = false;
			return;
		}
		Monster::PatternState st = monster->m_pattern;
		monster->m_pattern.pending = false;
		const uint32_t now = GetNowTime();
		// 예고 중에 기절/빙결시키면 끊긴다 — 방패 강타·서리 각인으로 받아칠 수 있다.
		// Phase 2-B: 반격 타격(HF_Counter)을 맞았어도 끊긴다.
		if (monster->Status().BlocksAttack(now) || monster->Status().BlocksMove(now) || monster->m_countered)
		{
			monster->m_countered = false;
			SendTelegraph(monster, TelegraphShape::Circle, st.cx, st.cy, st.fromX, st.fromY, 0, 0, st.type);   // 취소
			monster->m_busyUntil = now;
			return;
		}

		std::vector<shared_ptr<User>> victims;
		auto collect = [&](auto&& inside)
		{
			GSector->ForEachNeighborObject(monster->GetSectorX(), monster->GetSectorY(), [&](const shared_ptr<Subject>& object)
			{
				if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
					return;
				auto user = static_pointer_cast<User>(object);
				if (user->GetStat()->IsDead() || !user->IsExtended() || WorldData::IsSafeZone(user->GetX(), user->GetY()))
					return;
				if (inside(user->GetX(), user->GetY()))
					victims.push_back(user);
			});
		};

		uint32_t percent = 150;
		switch (st.type)
		{
		case MonsterPattern::Charge:
		{
			// 선 위의 칸들을 달려간다. 지나간 칸에 서 있던 대상이 맞는다.
			std::vector<std::pair<int, int>> line;
			int x = st.fromX, y = st.fromY;
			while (x != st.cx || y != st.cy)
			{
				x += st.dx; y += st.dy;
				line.emplace_back(x, y);
				if (line.size() > 6) break;
			}
			collect([&](int ux, int uy)
			{
				for (auto& [lx, ly] : line) if (lx == ux && ly == uy) return true;
				return false;
			});
			// 몬스터는 막히기 전 칸까지 이동(대상에 부딪히면 그 앞에서 멈춤)
			for (auto& [lx, ly] : line)
			{
				bool occupied = false;
				for (auto& v : victims) if (v->GetX() == lx && v->GetY() == ly) occupied = true;
				if (occupied) break;
				ObjID id = monsterId;
				monster->SetLastStepMs(70);
				MonsterHelper::AStarMove(id, static_cast<short>(lx), static_cast<short>(ly));
			}
			monster->ClearPath();
			percent = 160;
			break;
		}
		case MonsterPattern::Slam:
			collect([&](int ux, int uy) { return std::abs(ux - st.cx) <= st.radius && std::abs(uy - st.cy) <= st.radius; });
			percent = 180;
			break;
		case MonsterPattern::Ranged:
			collect([&](int ux, int uy) { return ux == st.cx && uy == st.cy; });
			percent = 130;
			break;
		case MonsterPattern::Leap:
		{
			// 목표 칸(또는 그 옆 빈 칸)으로 뛰어든다
			short lx = st.cx, ly = st.cy;
			if (isCollision(lx, ly) || WorldData::IsSafeZone(lx, ly) || ZoneLayout::GetZoneIdByWorld(lx, ly) != monster->GetZoneId())
			{
				lx = monster->GetX(); ly = monster->GetY();
			}
			collect([&](int ux, int uy) { return std::abs(ux - st.cx) <= st.radius && std::abs(uy - st.cy) <= st.radius; });
			if (lx != monster->GetX() || ly != monster->GetY())
			{
				ObjID id = monsterId;
				monster->SetLastStepMs(0);
				MonsterHelper::AStarMove(id, lx, ly);
				monster->ClearPath();
			}
			percent = 140;
			break;
		}
		default:
			return;
		}

		for (auto& v : victims)
		{
			const uint32_t raw = static_cast<uint32_t>(RollMonsterDamage(monster)) * percent / 100u;
			if (CombatHelper::HitPlayerFromMonster(monster, v, raw, DAMAGE_BOSS_SKILL))
				ContentHelper::HandlePlayerDeath(v, monsterId, false);
			else if (st.type == MonsterPattern::Charge)
				CombatHelper::ApplyStatus(v, StatusId::Slow, 1500, 1, monsterId);
		}
		(void)targetId;
	}
}
