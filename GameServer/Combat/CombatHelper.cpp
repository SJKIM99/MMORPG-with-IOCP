#include "pch.h"
#include "Combat/CombatHelper.h"
#include "Combat/SkillEx.h"
#include "User.h"
#include "Monster.h"
#include "UserHelper.h"
#include "MonsterHelper.h"
#include "SubjectHelper.h"
#include "SectorHelper.h"
#include "Sector.h"
#include "TimerThread.h"
#include "CoreTLS.h"
#include "Collision.h"
#include "Content/ContentHelper.h"
#include "Zone/ZoneLayout.h"
#include "Data/WorldData.h"
#include "Data/MoveRule.h"
#include "Data/MonsterTable.h"

namespace
{
	// 몬스터 → 확장 세션 플레이어 피해 배율(%). 플레이어 HP가 직업·능력치로 커진 만큼
	// 몬스터 위협을 맞춘다. 레거시 봇 경로(MonsterHelper 원래 식)에는 쓰지 않는다.
	constexpr uint32_t kMonsterDamageScaleV2 = 170;

	// 기본 공격으로 MP가 조금씩 찬다(스킬과 평타를 섞어 쓰게).
	constexpr uint32_t kCounterIntervalMs = 450;

	shared_ptr<GameSession> SessionOf(const shared_ptr<User>& u)
	{
		return u ? u->GetGameSession() : nullptr;
	}

	bool IsExtendedInGame(const shared_ptr<Subject>& s)
	{
		if (!s || s->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
			return false;
		auto user = static_pointer_cast<User>(s);
		auto session = user->GetGameSession();
		return session && session->m_state == SOCKET_STATE::ST_INGAME && session->IsExtended();
	}

	void BroadcastStatus(const shared_ptr<Subject>& target, StatusId id, uint8_t stacks, uint32_t remainMs, const ObjID& source)
	{
		if (target == nullptr || target->GetSectorX() < 0)
			return;
		STATUS_NFY_PACKET packet{};
		packet.size = sizeof(packet);
		packet.type = static_cast<char>(PacketType::STATUS_NFY);
		packet.subjectId = target->GetObjID();
		packet.status = static_cast<uint8_t>(id);
		packet.stacks = stacks;
		packet.durationMs = remainMs;
		packet.sourceId = source;
		GSector->ForEachNeighborObject(target->GetSectorX(), target->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (!IsExtendedInGame(object) || !SubjectHelper::CanSee(object, target))
				return;
			if (auto session = static_pointer_cast<User>(object)->GetGameSession())
				session->PostSend(packet);
		});
	}

	int Sign(int v) { return (v > 0) - (v < 0); }
}

namespace CombatHelper
{
	bool MonsterCanReach(const shared_ptr<Monster>& monster, const shared_ptr<User>& target)
	{
		if (!monster || !target)
			return false;
		if (!target->IsExtended())
			return SubjectHelper::CanAttack(monster, target);   // 레거시: 상하좌右 인접 + 바라보는 방향
		const int dx = std::abs(target->GetX() - monster->GetX());
		const int dy = std::abs(target->GetY() - monster->GetY());
		return (dx | dy) != 0 && dx <= 1 && dy <= 1;
	}

	shared_ptr<Subject> FindNear(const shared_ptr<Subject>& me, const ObjID& id)
	{
		shared_ptr<Subject> found;
		if (!me || me->GetSectorX() < 0)
			return found;
		GSector->ForEachNeighborObject(me->GetSectorX(), me->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (found == nullptr && object->GetObjID() == id)
				found = object;
		});
		return found;
	}

	std::vector<shared_ptr<Monster>> MonstersAround(const shared_ptr<Subject>& me, int cx, int cy, int radius)
	{
		std::vector<shared_ptr<Monster>> out;
		if (!me || me->GetSectorX() < 0)
			return out;
		// 중심 칸이 속한 섹터 기준으로 이웃을 훑는다(시전자와 같은 Zone 안일 때).
		short sx = static_cast<short>(cx / SECTOR_RANGE), sy = static_cast<short>(cy / SECTOR_RANGE);
		if (!GSector->IsValidSector(sx, sy)) { sx = me->GetSectorX(); sy = me->GetSectorY(); }
		GSector->ForEachNeighborObject(sx, sy, [&](const shared_ptr<Subject>& object)
		{
			if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eMonster)
				return;
			if (object->GetStat()->IsDead())
				return;
			if (std::abs(object->GetX() - cx) > radius || std::abs(object->GetY() - cy) > radius)
				return;
			out.push_back(static_pointer_cast<Monster>(object));
		});
		return out;
	}

	uint32_t RollUserDamage(const shared_ptr<User>& u, DamageKind kind, uint32_t coeffPercent, bool forceCrit, bool& outCrit)
	{
		const DerivedStats& d = u->m_derived;
		const uint32_t atk = kind == DamageKind::Magic ? d.magicAttack : d.attack;
		// ±10%
		const uint32_t roll = 900 + static_cast<uint32_t>(LRng() % 201);
		uint64_t dmg = static_cast<uint64_t>(atk) * coeffPercent * roll / 100000u;
		outCrit = forceCrit || (static_cast<uint32_t>(LRng() % 1000) < d.critChance);
		if (outCrit)
			dmg = dmg * d.critDamage / 100u;
		if (dmg < 1) dmg = 1;
		return static_cast<uint32_t>(std::min<uint64_t>(dmg, 60000));
	}

	uint32_t HitMonster(const shared_ptr<User>& attacker, const shared_ptr<Monster>& monster, uint32_t damage, const HitCtx& ctx)
	{
		if (!attacker || !monster || monster->GetStat()->IsDead())
			return 0;
		const int level = monster->GetSpecies() ? monster->GetSpecies()->level : monster->GetStat()->GetLevel();
		const uint32_t mitigated = StatRule::Mitigate(damage, StatRule::MonsterDefense(level), attacker->m_derived.penetration);
		ObjID monsterId = monster->GetObjID();
		ObjID attackerId = attacker->GetObjID();
		attacker->m_lastCombatAt = GetNowTime();
		uint8_t flags = ctx.flags;
		if (const SkillRow* row = SkillTable::Find(ctx.skillId); row != nullptr && row->slot != 0 && (flags & DAMAGE_DOT) == 0)
			flags |= DAMAGE_SKILL;
		UserHelper::AttackMonster(monsterId, attackerId, static_cast<int>(mitigated), flags,
			ctx.skillId, ctx.comboId, ctx.flags2, ctx.hitIndex);
		return mitigated;
	}

	void ApplyGearProcs(const shared_ptr<User>& u, uint16_t skillId, bool basic, DamageKind kind,
		const shared_ptr<Monster>& m, uint32_t dealt, uint8_t hitIndex)
	{
		if (!u || !m)
			return;
		const GearExtras& gx = u->m_gearEx;
		auto power = [&](ItemProc p) { return gx.procPower[static_cast<size_t>(p)]; };
		auto fires = [&](ItemProc p)
		{
			const uint8_t pw = power(p);
			return pw > 0 && static_cast<int>(LRng() % 100u) < ItemRules::ProcChance(p, pw);
		};
		const uint32_t now = GetNowTime();

		// 생명 흡수 — 준 피해의 일부를 HP로(쓰러뜨린 한 방도 포함)
		if (const uint8_t pw = power(ItemProc::Lifesteal); pw > 0 && dealt > 0 && u->GetStat()->GetHp() < u->GetStat()->GetMaxHp())
		{
			const uint32_t heal = std::max<uint32_t>(1, dealt * static_cast<uint32_t>(ItemRules::ProcPower(ItemProc::Lifesteal, pw)) / 100u);
			u->GetStat()->HealHp(static_cast<uint16_t>(std::min<uint32_t>(heal, 65535)), u->GetStat()->GetMaxHp());
			SendResource(u);
			if (now - u->m_lifestealFxAt >= 700)
			{
				u->m_lifestealFxAt = now;
				UserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::ProcLifesteal, u->GetX(), u->GetY(), static_cast<int32_t>(heal));
			}
		}
		if (m->GetStat()->IsDead())
			return;

		const DerivedStats& d = u->m_derived;
		const uint32_t atk = kind == DamageKind::Magic ? d.magicAttack : d.attack;

		// 천둥의 일격 — 벼락(마법 피해)
		if (fires(ItemProc::Thunder))
		{
			const uint32_t base = std::max<uint32_t>(d.attack, d.magicAttack);
			const uint32_t dmg = std::max<uint32_t>(1, base * static_cast<uint32_t>(ItemRules::ProcPower(ItemProc::Thunder, power(ItemProc::Thunder))) / 100u
				* (90u + static_cast<uint32_t>(LRng() % 21u)) / 100u);
			UserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::ProcThunder, m->GetX(), m->GetY(), 0);
			HitCtx ctx;
			ctx.flags = DAMAGE_SKILL;
			ctx.flags2 = DAMAGE2_PROC;
			ctx.hitIndex = static_cast<uint8_t>(hitIndex + 1);
			HitMonster(u, m, dmg, ctx);
			if (m->GetStat()->IsDead())
				return;
		}
		// 메아리 — 기술(기본 공격 제외)이 한 번 더
		if (!basic && dealt > 0 && fires(ItemProc::Echo))
		{
			const uint32_t dmg = std::max<uint32_t>(1, dealt * static_cast<uint32_t>(ItemRules::ProcPower(ItemProc::Echo, power(ItemProc::Echo))) / 100u);
			UserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::ProcEcho, m->GetX(), m->GetY(), 0);
			HitCtx ctx;
			ctx.skillId = skillId;
			ctx.flags = DAMAGE_SKILL;
			ctx.flags2 = DAMAGE2_PROC;
			ctx.hitIndex = static_cast<uint8_t>(hitIndex + 2);
			// 이미 방어가 적용된 값을 다시 방어에 넣지 않도록 관통 없이 그대로(조금 약해지는 건 의도)
			HitMonster(u, m, dmg, ctx);
			if (m->GetStat()->IsDead())
				return;
		}
		// 서리 손길 — 둔화(신화: 빙결)
		if (fires(ItemProc::FrostTouch))
		{
			if (power(ItemProc::FrostTouch) >= 2)
				ApplyStatus(m, StatusId::Freeze, 1200, 1, u->GetObjID());
			else
				ApplyStatus(m, StatusId::Slow, 2500, 1, u->GetObjID());
			UserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::ProcFrost, m->GetX(), m->GetY(), 0);
		}
		// 잔불 — 화상
		if (fires(ItemProc::Ember))
		{
			const uint16_t tick = static_cast<uint16_t>(std::max<uint32_t>(1, atk * StatusTable::Get(StatusId::Burn).tickPercent / 100u));
			ApplyStatus(m, StatusId::Burn, 3000, 1, u->GetObjID(), tick);
			UserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::ProcEmber, m->GetX(), m->GetY(), 0);
		}
		// 맹독 — 중독 1중첩
		if (fires(ItemProc::Venom))
		{
			const uint16_t tick = static_cast<uint16_t>(std::max<uint32_t>(1, atk * StatusTable::Get(StatusId::Poison).tickPercent / 100u));
			ApplyStatus(m, StatusId::Poison, 5000, 1, u->GetObjID(), tick);
			UserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::ProcVenom, m->GetX(), m->GetY(), 0);
		}
	}

	bool HitPlayerFromMonster(const shared_ptr<Monster>& monster, const shared_ptr<User>& victim, uint32_t raw, uint8_t flags)
	{
		const uint32_t now = GetNowTime();
		const ObjID monsterId = monster->GetObjID();
		victim->m_lastCombatAt = now;

		// 잔상 회피 · 무적(이동기 · 각성기) 중이면 전부 피한다. 잔상 회피 직후(0.3초 안)면 완벽 회피.
		if (victim->Status().Has(StatusId::Evade, now) || now < victim->m_invulnUntil)
		{
			UserHelper::BroadcastDamage(victim, monsterId, 0, flags, 0, 0, DAMAGE2_EVADE);
			SkillEx::OnEvaded(victim, now);
			return false;
		}
		const int monsterLevel = monster->GetSpecies() ? monster->GetSpecies()->level : 1;
		if (static_cast<uint32_t>(LRng() % 100) < StatRule::EvadeChance(victim->m_derived.evasion, monsterLevel))
		{
			UserHelper::BroadcastDamage(victim, monsterId, 0, flags, 0, 0, DAMAGE2_EVADE);
			return false;
		}

		uint32_t damage = raw * kMonsterDamageScaleV2 / 100u;
		damage = StatRule::Mitigate(damage, victim->m_derived.defense, 0);
		uint8_t flags2 = 0;
		const bool guarding = victim->Status().Has(StatusId::Guard, now);
		if (guarding)
		{
			damage = std::max<uint32_t>(1, damage / 2);
			flags2 |= DAMAGE2_GUARD;
			SkillEx::AddIdentity(victim, 60);   // 서약기사: 막으면 서약이 찬다
		}

		victim->GetStat()->TakeDamage(static_cast<uint16_t>(std::min<uint32_t>(damage, 65535)));
		const bool dead = victim->GetStat()->IsDead();
		UserHelper::BroadcastDamage(victim, monsterId, static_cast<int32_t>(damage), static_cast<uint8_t>(flags | (dead ? DAMAGE_KILLED : 0)), 0, 0, flags2);

		// 서약의 방패(장비 특수 효과) — 맞을 때 확률로 철벽 태세(재발동 대기 있음)
		if (!dead && !guarding)
		{
			const uint8_t pw = victim->m_gearEx.procPower[static_cast<size_t>(ItemProc::Guardian)];
			if (pw > 0 && now >= victim->m_guardianReadyAt && static_cast<int>(LRng() % 100u) < ItemRules::ProcChance(ItemProc::Guardian, pw))
			{
				victim->m_guardianReadyAt = now + ItemRules::kGuardianCooldownMs;
				ApplyStatus(victim, StatusId::Guard, pw >= 2 ? 4000u : 3000u, 1, victim->GetObjID());
				UserHelper::BroadcastEffect(victim, victim->GetObjID(), EffectId::ProcGuardian, victim->GetX(), victim->GetY(), 0);
			}
		}

		// 철벽 태세 반격 — 살아 있고, 간격이 지났으면 때린 몬스터에게 되돌려 준다.
		if (!dead && guarding && now - victim->m_lastCounterAt >= kCounterIntervalMs && !monster->GetStat()->IsDead())
		{
			victim->m_lastCounterAt = now;
			bool crit = false;
			// Phase 2-D 철벽 태세 갈래: 「날 선 반격」 피해 +30% · 「불꽃 반격」「울림 반격」 상태이상
			const SkillGrowth::State gs = victim->GrowState(5);
			const SkillGrowth::Variant* sharp = SkillGrowth::FindMod(5, gs, SkillGrowth::Mod::Heavy);
			const SkillGrowth::Variant* elem = SkillGrowth::FindMod(5, gs, SkillGrowth::Mod::Element);
			uint32_t counter = RollUserDamage(victim, DamageKind::Physical, sharp ? 70u * (100u + sharp->value) / 100u : 70u, false, crit);
			counter = counter * SkillGrowth::DamagePercent(gs.level) / 100u;
			UserHelper::BroadcastEffect(victim, victim->GetObjID(), EffectId::Counter, monster->GetX(), monster->GetY(), 0);
			HitCtx ctx;
			ctx.skillId = 5;
			ctx.comboId = 8;
			ctx.flags = static_cast<uint8_t>(DAMAGE_COMBO | (crit ? DAMAGE_CRIT : 0));
			ctx.flags2 = DAMAGE2_COUNTER;
			HitMonster(victim, monster, counter, ctx);
			if (elem && !monster->GetStat()->IsDead())
			{
				const StatusRow& st = StatusTable::Get(elem->status);
				const uint16_t tick = st.tickMs > 0 ? static_cast<uint16_t>(std::max<uint32_t>(1, victim->m_derived.attack * st.tickPercent / 100u)) : 0;
				ApplyStatus(monster, elem->status, SkillGrowth::DefaultStatusMs(elem->status), 1, victim->GetObjID(), tick);
			}
		}
		return dead;
	}

	void ApplyStatus(const shared_ptr<Subject>& target, StatusId id, uint32_t durationMs, uint8_t stacks, const ObjID& source, uint16_t tickDamage)
	{
		if (!target || target->GetStat()->IsDead() || id == StatusId::None || durationMs == 0)
			return;
		const uint32_t now = GetNowTime();
		// 상태이상 저항(플레이어) — 해로운 상태의 지속 시간을 줄인다.
		if (StatusTable::Get(id).debuff && target->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eUser)
		{
			const auto user = static_pointer_cast<User>(target);
			// Phase 2-B 슈퍼아머(홀딩 · 차지 · 서약 해방) · 무적: 몬스터가 거는 기절 · 빙결을 받지 않는다
			if ((id == StatusId::Stun || id == StatusId::Freeze) && source.GetCategory<EnumCategory>() == EnumCategory::eMonster
				&& (now < user->m_superArmorUntil || now < user->m_invulnUntil || user->Status().Has(StatusId::Oath, now)))
				return;
			durationMs = durationMs * (100u - std::min<uint32_t>(user->m_derived.statusResist, 60)) / 100u;
		}
		// 보스는 기절·빙결을 짧게 받는다(연계는 되지만 무한 제어는 안 되게).
		if (target->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eMonster && (id == StatusId::Stun || id == StatusId::Freeze))
		{
			const auto monster = static_pointer_cast<Monster>(target);
			if (monster->IsBoss())
				durationMs /= 3;
		}
		const bool hadTicks = [&]()
		{
			for (auto& s : target->Status().Slots())
				if (s.stacks > 0 && now < s.until && StatusTable::Get(s.id).tickMs > 0) return true;
			return false;
		}();
		StatusSlot& slot = target->Status().Apply(id, now, durationMs, stacks, source, tickDamage);
		BroadcastStatus(target, id, slot.stacks, durationMs, source);

		// 지속 피해가 새로 시작됐으면 틱 타이머를 건다(이미 돌고 있으면 그대로 둔다).
		if (StatusTable::Get(id).tickMs > 0 && !hadTicks)
			GTimerThread->ScheduleAfter(target->GetObjID(), std::chrono::milliseconds(StatusTable::Get(id).tickMs), TIMER_EVENT_TYPE::EV_STATUS_TICK);

		// 제어기에 걸린 몬스터는 경로를 다시 찾게 한다(밀려났거나 멈췄으므로).
		if (target->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eMonster && StatusTable::Get(id).blocksMove)
			static_pointer_cast<Monster>(target)->ClearPath();
	}

	void RemoveStatus(const shared_ptr<Subject>& target, StatusId id)
	{
		if (!target)
			return;
		const uint32_t now = GetNowTime();
		if (!target->Status().Has(id, now))
			return;
		target->Status().Remove(id);
		BroadcastStatus(target, id, 0, 0, ObjID{});
	}

	void HandleStatusTick(const ObjID& subjectId)
	{
		auto target = ::GetGameObject<Subject>(subjectId);
		if (!target || target->GetStat()->IsDead() || target->GetSectorX() < 0)
			return;
		const uint32_t now = GetNowTime();
		uint32_t nextAt = 0;

		// 틱 피해를 모은 뒤 적용(적용 중에 죽으면 슬롯이 지워지므로)
		struct Tick { ObjID source; uint32_t damage; StatusId id; };
		std::vector<Tick> ticks;
		for (auto& s : target->Status().Slots())
		{
			const StatusRow& row = StatusTable::Get(s.id);
			if (s.stacks == 0 || now >= s.until || row.tickMs == 0)
				continue;
			while (s.nextTick != 0 && s.nextTick <= now && s.nextTick <= s.until)
			{
				ticks.push_back(Tick{ s.source, static_cast<uint32_t>(s.tickDamage) * s.stacks, s.id });
				s.nextTick += row.tickMs;
			}
			if (s.nextTick <= s.until && (nextAt == 0 || s.nextTick < nextAt))
				nextAt = s.nextTick;
		}

		for (const Tick& t : ticks)
		{
			if (target->GetStat()->IsDead())
				break;
			if (target->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eMonster)
			{
				auto attacker = ::GetGameObject<User>(t.source);
				if (!attacker || attacker->GetZoneId() != target->GetZoneId())
					continue;
				HitCtx ctx;
				ctx.flags = DAMAGE_DOT;
				ctx.skillId = t.id == StatusId::Burn ? 9 : 15;
				HitMonster(attacker, static_pointer_cast<Monster>(target), std::max<uint32_t>(1, t.damage), ctx);
			}
		}

		if (!target->GetStat()->IsDead() && nextAt != 0)
		{
			const uint32_t wait = nextAt > now ? nextAt - now : 1;
			GTimerThread->ScheduleAfter(subjectId, std::chrono::milliseconds(wait), TIMER_EVENT_TYPE::EV_STATUS_TICK);
		}
	}

	bool Knockback(const shared_ptr<Monster>& target, int fromX, int fromY, int tiles, bool& outWall)
	{
		outWall = false;
		if (!target || target->GetStat()->IsDead() || tiles <= 0)
			return false;
		if (target->IsBoss() || target->IsDummy())
			return false;   // 보스 · 허수아비는 밀리지 않는다(허수아비는 안전지대 안이라 늘 "벽"이 된다)
		int dx = Sign(target->GetX() - fromX), dy = Sign(target->GetY() - fromY);
		if (dx == 0 && dy == 0)
			dx = target->IsFacingLeft() ? 1 : -1;
		bool moved = false;
		for (int i = 0; i < tiles; ++i)
		{
			const short nx = static_cast<short>(target->GetX() + dx);
			const short ny = static_cast<short>(target->GetY() + dy);
			// 대각이면 두 옆도 열려야 한다(Phase 4-A 모서리 규칙) — 막힌 두 칸 사이로 빠지지 않고 벽에 부딪힌다
			if (!ZoneLayout::IsValidWorldPosition(nx, ny) || !MoveRule::CleanStep(target->GetX(), target->GetY(), dx, dy, &CollisionAt)
				|| WorldData::IsSafeZone(nx, ny) || ZoneLayout::GetZoneIdByWorld(nx, ny) != target->GetZoneId())
			{
				outWall = true;
				break;
			}
			ObjID id = target->GetObjID();
			target->SetLastStepMs(95);
			MonsterHelper::AStarMove(id, nx, ny);
			moved = true;
		}
		target->ClearPath();
		return moved;
	}

	bool Pull(const shared_ptr<Monster>& target, int toX, int toY, int tiles)
	{
		if (!target || target->GetStat()->IsDead() || tiles <= 0 || target->IsBoss() || target->IsDummy())
			return false;
		bool moved = false;
		for (int i = 0; i < tiles; ++i)
		{
			const int dx = Sign(toX - target->GetX()), dy = Sign(toY - target->GetY());
			if (dx == 0 && dy == 0) break;
			const short nx = static_cast<short>(target->GetX() + dx);
			const short ny = static_cast<short>(target->GetY() + dy);
			if (nx == toX && ny == toY) break;   // 시전자 칸 바로 앞에서 멈춘다
			if (!ZoneLayout::IsValidWorldPosition(nx, ny) || !MoveRule::CleanStep(target->GetX(), target->GetY(), dx, dy, &CollisionAt)
				|| WorldData::IsSafeZone(nx, ny) || ZoneLayout::GetZoneIdByWorld(nx, ny) != target->GetZoneId())
				break;
			ObjID id = target->GetObjID();
			target->SetLastStepMs(90);
			MonsterHelper::AStarMove(id, nx, ny);
			moved = true;
		}
		target->ClearPath();
		return moved;
	}

	bool MovePlayerTo(const shared_ptr<User>& player, short x, short y, uint32_t stepMs)
	{
		if (!player || player->GetStat()->IsDead() || player->IsTransferring())
			return false;
		if (!ZoneLayout::IsValidWorldPosition(x, y) || isCollision(x, y))
			return false;
		if (stepMs > 0)   // 미끄러지는 이동(돌진 · 구르기)만 — 순간이동(stepMs 0)은 선을 긋지 않는다
			GuardTownMove("dash", player->GetX(), player->GetY(), x, y);
		player->SetLastStepMs(stepMs);
		player->m_moveClock = GetNowTime();   // 돌진 뒤 이동 예산을 새로 시작
		SectorHelper::HandlePlayerMove(player, x, y, false);
		return true;
	}

	void SendResource(const shared_ptr<User>& u)
	{
		auto session = SessionOf(u);
		if (!session || !session->IsExtended())
			return;
		USER_RESOURCE_INF_PACKET packet{};
		packet.size = sizeof(packet);
		packet.type = static_cast<char>(PacketType::USER_RESOURCE_INF);
		packet.hp = u->GetStat()->GetHp();
		packet.maxhp = u->GetStat()->GetMaxHp();
		packet.mp = u->m_mp;
		packet.maxmp = u->m_derived.maxMp;
		session->PostSend(packet);
	}

	void SendStats(const shared_ptr<User>& u)
	{
		auto session = SessionOf(u);
		if (!session || !session->IsExtended())
			return;
		USER_STATS_INF_PACKET packet{};
		packet.size = sizeof(packet);
		packet.type = static_cast<char>(PacketType::USER_STATS_INF);
		packet.classId = static_cast<uint8_t>(u->m_classId);
		packet.level = u->GetStat()->GetLevel();
		packet.base = ClassTable::BaseAt(u->m_classId, packet.level);
		packet.alloc = u->m_alloc;
		for (int i = 0; i < 6; ++i) packet.gear.v[i] = u->m_gear.stats[i];
		packet.freePoints = u->FreePoints();
		packet.stats = u->m_derived;
		packet.mp = u->m_mp;
		session->PostSend(packet);
	}

	void GainMp(const shared_ptr<User>& u, int amount)
	{
		if (!u || amount == 0)
			return;
		const int mp = std::clamp<int>(static_cast<int>(u->m_mp) + amount, 0, u->m_derived.maxMp);
		if (mp == u->m_mp)
			return;
		u->m_mp = static_cast<uint16_t>(mp);
		SendResource(u);
	}

	void RefreshStats(const shared_ptr<User>& u, bool fullHeal)
	{
		if (!u || !u->IsExtended())
			return;
		u->RecomputeStats(fullHeal);
		SendStats(u);
		SendResource(u);
		UserHelper::BroadcastInfoEx(u);
	}

	void HandleStatAlloc(const shared_ptr<User>& u, uint8_t stat, uint8_t amount)
	{
		if (!u || !u->IsExtended() || stat >= static_cast<uint8_t>(StatKind::Count) || amount == 0)
			return;
		const uint16_t free = u->FreePoints();
		const uint16_t use = std::min<uint16_t>(free, std::min<uint8_t>(amount, 10));
		if (use == 0)
		{
			SendStats(u);
			return;
		}
		u->m_alloc.v[stat] = static_cast<int16_t>(u->m_alloc.v[stat] + use);
		RefreshStats(u, false);
		UserHelper::SaveProgress(u);
	}
}
