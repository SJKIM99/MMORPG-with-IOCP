#include "pch.h"
#include "Combat/SkillHelper.h"
#include "Combat/CombatHelper.h"
#include "User.h"
#include "Monster.h"
#include "UserHelper.h"
#include "MonsterHelper.h"
#include "SubjectHelper.h"
#include "Sector.h"
#include "TimerThread.h"
#include "CoreTLS.h"
#include "Collision.h"
#include "Content/ContentHelper.h"
#include "Zone/ZoneLayout.h"
#include "Data/WorldData.h"
#include "Data/MoveRule.h"

namespace
{
	using namespace CombatHelper;

	constexpr uint32_t kCooldownGraceMs = 60;    // 네트워크 지터 여유
	constexpr uint32_t kRuneDelayMs = 1000;      // 공간 도약이 남긴 룬이 터지기까지

	int Cheb(int ax, int ay, int bx, int by) { return (std::max)(std::abs(ax - bx), std::abs(ay - by)); }
	int Sign(int v) { return (v > 0) - (v < 0); }

	void SendResult(const shared_ptr<User>& u, uint16_t seq, uint16_t skillId, SkillResult r, uint32_t ms)
	{
		auto session = u->GetGameSession();
		if (!session)
			return;
		SKILL_RESULT_ACK_PACKET packet{};
		packet.size = sizeof(packet);
		packet.type = static_cast<char>(PacketType::SKILL_RESULT_ACK);
		packet.seq = seq;
		packet.skillId = skillId;
		packet.result = static_cast<uint8_t>(r);
		packet.cooldownMs = ms;
		packet.mp = u->m_mp;
		session->PostSend(packet);
	}

	void BroadcastCast(const shared_ptr<User>& u, uint16_t skillId, const ObjID& targetId, short fromX, short fromY, short x, short y, uint16_t castMs)
	{
		if (u->GetSectorX() < 0)
			return;
		SKILL_CAST_NFY_PACKET packet{};
		packet.size = sizeof(packet);
		packet.type = static_cast<char>(PacketType::SKILL_CAST_NFY);
		packet.casterId = u->GetObjID();
		packet.skillId = skillId;
		packet.targetId = targetId;
		packet.fromX = fromX;
		packet.fromY = fromY;
		packet.x = x;
		packet.y = y;
		packet.castMs = castMs;
		GSector->ForEachNeighborObject(u->GetSectorX(), u->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
				return;
			auto viewer = static_pointer_cast<User>(object);
			auto session = viewer->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				return;
			if (!SubjectHelper::CanSee(object, u))
				return;
			session->PostSend(packet);
		});
	}

	// 대상 지정 스킬의 대상: 몬스터 또는(강제 공격한) 다른 플레이어
	shared_ptr<Subject> ResolveEnemy(const shared_ptr<User>& u, const ObjID& targetId)
	{
		if (targetId == ObjID{} || targetId == u->GetObjID())
			return nullptr;
		auto target = FindNear(u, targetId);
		if (!target)
			return nullptr;
		const auto cat = target->GetObjID().GetCategory<EnumCategory>();
		if (cat != EnumCategory::eMonster && cat != EnumCategory::eUser)
			return nullptr;
		if (target->GetStat()->IsDead())
			return nullptr;
		return target;
	}

	bool IsMonster(const shared_ptr<Subject>& s) { return s && s->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eMonster; }

	// 한 번의 적중 — 연계(대상 상태) 판정, 피해, 상태이상, 넉백까지.
	struct HitSpec
	{
		uint32_t coeff = 100;
		bool     forceCrit = false;
		uint32_t bonusPercent = 0;   // 추가 피해 %(연계 외)
		uint8_t  comboId = 0;        // 강제 연계 표시
		uint8_t  flags2 = 0;
		uint8_t  hitIndex = 0;
		bool     applyStatus = true;
		bool     applyKnockback = true;
		bool     evaluateCombo = true;
	};

	void DealHit(const shared_ptr<User>& u, const SkillRow& row, const shared_ptr<Subject>& target, HitSpec spec)
	{
		if (!target || target->GetStat()->IsDead())
			return;
		const uint32_t now = GetNowTime();
		uint8_t comboId = spec.comboId;
		uint32_t bonus = spec.bonusPercent;

		// 표의 연계: 대상이 comboStatus 상태면 추가 피해(+ 연계 표시)
		if (spec.evaluateCombo && row.comboStatus != StatusId::None && row.comboStatus != StatusId::Ambush)
		{
			const bool has = target->Status().Has(row.comboStatus, now)
				|| (row.id == 6 && target->GetStat()->GetHp() * 100u <= target->GetStat()->GetMaxHp() * 30u)
				|| (row.id == 11 && (target->Status().Has(StatusId::Marked, now) || target->Status().Has(StatusId::Freeze, now)));
			if (has)
			{
				bonus += row.comboBonus;
				if (row.comboId != 0) comboId = row.comboId;
				if (row.consumeCombo)
					RemoveStatus(target, row.comboStatus);
			}
		}

		bool crit = false;
		uint32_t damage = RollUserDamage(u, row.kind, spec.coeff, spec.forceCrit, crit);
		damage = damage * (100u + bonus) / 100u;

		HitCtx ctx;
		ctx.skillId = row.id;
		ctx.comboId = comboId;
		ctx.flags = static_cast<uint8_t>((crit ? DAMAGE_CRIT : 0) | (comboId != 0 ? DAMAGE_COMBO : 0));
		ctx.flags2 = spec.flags2;
		ctx.hitIndex = spec.hitIndex;

		// 넉백은 피해보다 먼저 계산해 표시 플래그를 싣는다(벽 충돌이면 추가 피해·기절).
		bool knocked = false, wall = false;
		if (spec.applyKnockback && row.knockback > 0 && IsMonster(target))
		{
			knocked = Knockback(static_pointer_cast<Monster>(target), u->GetX(), u->GetY(), row.knockback, wall);
			if (knocked) ctx.flags |= DAMAGE_KNOCKBACK;
			if (wall)
			{
				damage = damage * 130u / 100u;
				ctx.flags2 |= DAMAGE2_WALL;
				if (comboId == 0) { ctx.comboId = 9; ctx.flags |= DAMAGE_COMBO; }
				UserHelper::BroadcastEffect(u, target->GetObjID(), EffectId::WallSlam, target->GetX(), target->GetY(), 0);
			}
		}

		if (IsMonster(target))
			HitMonster(u, static_pointer_cast<Monster>(target), damage, ctx);
		else
			ContentHelper::ApplyPvpDamage(u, static_pointer_cast<User>(target), damage * 7u / 10u, ctx.flags, ctx.skillId, ctx.comboId, ctx.flags2, ctx.hitIndex);

		if (target->GetStat()->IsDead())
			return;
		if (spec.applyStatus && row.status != StatusId::None && row.status != StatusId::Ambush && row.status != StatusId::Guard && row.status != StatusId::Evade)
		{
			const StatusRow& st = StatusTable::Get(row.status);
			const uint16_t tick = st.tickMs > 0
				? static_cast<uint16_t>(std::max<uint32_t>(1, (row.kind == DamageKind::Magic ? u->m_derived.magicAttack : u->m_derived.attack) * st.tickPercent / 100u))
				: 0;
			ApplyStatus(target, row.status, row.statusMs, std::max<uint8_t>(1, row.statusStacks), u->GetObjID(), tick);
		}
		if (wall)
			ApplyStatus(target, StatusId::Stun, 900, 1, u->GetObjID());
	}

	// 방향 단위 벡터(8방향)
	void DirTo(int fromX, int fromY, int toX, int toY, int& dx, int& dy)
	{
		dx = Sign(toX - fromX);
		dy = Sign(toY - fromY);
	}

	bool Walkable(int x, int y)
	{
		return ZoneLayout::IsValidWorldPosition(static_cast<short>(x), static_cast<short>(y)) && !isCollision(static_cast<short>(x), static_cast<short>(y));
	}

	shared_ptr<Monster> MonsterAt(const shared_ptr<User>& u, int x, int y)
	{
		for (auto& m : MonstersAround(u, x, y, 0))
			if (m->GetX() == x && m->GetY() == y)
				return m;
		return nullptr;
	}

	// ── 판정 ────────────────────────────────────────────────────────────────
	void Execute(const shared_ptr<User>& u, const SkillRow& row, const ObjID& targetId, short tx, short ty)
	{
		const uint32_t now = GetNowTime();
		const int ux = u->GetX(), uy = u->GetY();
		switch (row.id)
		{
		// ── 기본 공격 ─────────────────────────────────────────────
		case 1:   // 검격
		case 7:   // 룬 탄환
		{
			auto target = ResolveEnemy(u, targetId);
			if (!target) return;
			HitSpec spec;
			spec.coeff = row.coeff;
			DealHit(u, row, target, spec);
			GainMp(u, row.id == 7 ? 3 : 2);
			return;
		}
		case 13:  // 연속 찌르기(2연타) — 기습 중이면 둘 다 확정 치명타
		{
			auto target = ResolveEnemy(u, targetId);
			if (!target) return;
			const bool ambush = u->Status().Has(StatusId::Ambush, now);
			for (uint8_t i = 0; i < row.hits && !target->GetStat()->IsDead(); ++i)
			{
				HitSpec spec;
				spec.coeff = row.coeff;
				spec.hitIndex = i;
				spec.forceCrit = ambush;
				if (ambush) { spec.comboId = 6; spec.flags2 = DAMAGE2_BACKSTAB; }
				DealHit(u, row, target, spec);
			}
			if (ambush) RemoveStatus(u, StatusId::Ambush);
			GainMp(u, 2);
			return;
		}

		// ── 황혼의 기사 ───────────────────────────────────────────
		case 2:   // 방패 강타
		case 15:  // 독 칼날
		case 6:   // 황혼의 처형(예고 뒤)
		{
			auto target = ResolveEnemy(u, targetId);
			if (!target) return;
			if (Cheb(ux, uy, target->GetX(), target->GetY()) > row.range + 1)
				return;   // 예고 사이 멀어졌다 — 헛침(피한 것)
			HitSpec spec;
			spec.coeff = row.coeff;
			if (row.id == 6) spec.flags2 = DAMAGE2_HEAVY;
			DealHit(u, row, target, spec);
			return;
		}
		case 3:   // 회전 베기
		case 9:   // 화염 고리
		case 18:  // 월영 난무
		{
			auto targets = MonstersAround(u, ux, uy, row.radius);
			if (row.id == 18)
			{
				// 다섯 번 벤다. 첫 적중에 대상의 중독 중첩을 터뜨린다(중첩당 +30%).
				std::vector<uint8_t> poison(targets.size());
				for (size_t k = 0; k < targets.size(); ++k)
				{
					poison[k] = targets[k]->Status().Stacks(StatusId::Poison, now);
					if (poison[k] > 0)
					{
						RemoveStatus(targets[k], StatusId::Poison);
						UserHelper::BroadcastEffect(u, targets[k]->GetObjID(), EffectId::PoisonBurst, targets[k]->GetX(), targets[k]->GetY(), poison[k]);
					}
				}
				for (uint8_t i = 0; i < row.hits; ++i)
					for (size_t k = 0; k < targets.size(); ++k)
					{
						HitSpec spec;
						spec.coeff = row.coeff;
						spec.hitIndex = i;
						spec.evaluateCombo = false;
						if (i == 0 && poison[k] > 0)
						{
							spec.bonusPercent = 30u * poison[k] * 2u;   // 첫 타에 몰아서
							spec.comboId = 7;
						}
						DealHit(u, row, targets[k], spec);
					}
				return;
			}
			for (auto& m : targets)
			{
				const bool frozen = row.id == 9 && m->Status().Has(StatusId::Freeze, now);
				HitSpec spec;
				spec.coeff = row.coeff;
				DealHit(u, row, m, spec);
				if (frozen)
				{
					// 증발 폭발 — 얼어 있던 적 주변으로 터진다
					UserHelper::BroadcastEffect(u, m->GetObjID(), EffectId::SteamBurst, m->GetX(), m->GetY(), 1);
					for (auto& n : MonstersAround(u, m->GetX(), m->GetY(), 1))
					{
						if (n == m) continue;
						HitSpec burst;
						burst.coeff = 60;
						burst.comboId = 3;
						burst.evaluateCombo = false;
						burst.applyStatus = false;
						burst.hitIndex = 1;
						DealHit(u, row, n, burst);
					}
				}
			}
			return;
		}
		case 4:   // 돌진 — 방향으로 최대 dash칸, 처음 부딪힌 적을 밀친다
		case 16:  // 잔상 회피 — 방향으로 dash칸 빠르게 이동 + 회피
		{
			int dx, dy;
			DirTo(ux, uy, tx, ty, dx, dy);
			if (dx == 0 && dy == 0) dx = u->IsFacingLeft() ? -1 : 1;
			int x = ux, y = uy;
			shared_ptr<Monster> hit;
			for (int i = 0; i < row.dash; ++i)
			{
				const int nx = x + dx, ny = y + dy;
				if (!Walkable(nx, ny) || (row.id == 4 && WorldData::IsSafeZone(nx, ny)))
					break;
				if (row.id == 4)
				{
					if (auto m = MonsterAt(u, nx, ny)) { hit = m; break; }
				}
				x = nx; y = ny;
			}
			if (x != ux || y != uy)
				MovePlayerTo(u, static_cast<short>(x), static_cast<short>(y), 60u * static_cast<uint32_t>(Cheb(ux, uy, x, y)));
			if (row.id == 16)
			{
				ApplyStatus(u, StatusId::Evade, row.statusMs, 1, u->GetObjID());
				return;
			}
			// 돌진 끝에서 한 칸 앞 적(또는 바로 옆)을 친다
			if (!hit)
				hit = MonsterAt(u, x + dx, y + dy);
			if (hit)
			{
				HitSpec spec;
				spec.coeff = row.coeff;
				spec.flags2 = DAMAGE2_HEAVY;
				DealHit(u, row, hit, spec);
			}
			return;
		}
		case 5:   // 철벽 태세
			ApplyStatus(u, StatusId::Guard, row.statusMs, 1, u->GetObjID());
			return;

		// ── 룬술사 ───────────────────────────────────────────────
		case 8:   // 빙결 인장
		case 12:  // 혜성 낙하(예고 뒤)
		{
			for (auto& m : MonstersAround(u, tx, ty, row.radius))
			{
				HitSpec spec;
				spec.coeff = row.coeff;
				if (row.id == 12) spec.flags2 = DAMAGE2_HEAVY;
				DealHit(u, row, m, spec);
			}
			return;
		}
		case 10:  // 공간 도약 — 도착은 시전 때 끝났다. 여기는 떠난 자리의 룬 폭발.
		{
			UserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::RuneExplosion, tx, ty, row.radius);
			for (auto& m : MonstersAround(u, tx, ty, row.radius))
			{
				HitSpec spec;
				spec.coeff = row.coeff;
				spec.applyKnockback = false;
				DealHit(u, row, m, spec);
			}
			return;
		}
		case 11:  // 번개 폭발 — 대상에서 주변으로 연쇄
		{
			auto first = ResolveEnemy(u, targetId);
			if (!first) return;
			std::vector<shared_ptr<Subject>> hitList{ first };
			int chains = 3;
			shared_ptr<Subject> cur = first;
			uint8_t index = 0;
			while (cur)
			{
				HitSpec spec;
				spec.coeff = row.coeff;
				spec.hitIndex = index++;
				const bool conduct = cur->Status().Has(StatusId::Burn, now) || cur->Status().Has(StatusId::Marked, now) || cur->Status().Has(StatusId::Freeze, now);
				if (conduct) chains += 1;   // 전도 대상이면 한 번 더 튄다
				DealHit(u, row, cur, spec);
				if (--chains < 0 || hitList.size() > 7)
					break;
				// 다음: 아직 안 맞은 가장 가까운 적(반경 radius)
				shared_ptr<Subject> next;
				int best = 99;
				for (auto& m : MonstersAround(u, cur->GetX(), cur->GetY(), row.radius))
				{
					if (std::find(hitList.begin(), hitList.end(), static_pointer_cast<Subject>(m)) != hitList.end())
						continue;
					const int d = Cheb(cur->GetX(), cur->GetY(), m->GetX(), m->GetY());
					if (d < best) { best = d; next = m; }
				}
				if (!next) break;
				hitList.push_back(next);
				cur = next;
			}
			return;
		}

		// ── 그림자 사냥꾼 ─────────────────────────────────────────
		case 14:  // 그림자 돌진 — 대상의 등 뒤로
		{
			auto target = ResolveEnemy(u, targetId);
			if (!target) return;
			// 대상 기준으로 나와 반대편 칸 → 막혔으면 대상 주변의 빈 칸
			int dx = Sign(target->GetX() - ux), dy = Sign(target->GetY() - uy);
			if (dx == 0 && dy == 0) dx = 1;
			int bx = target->GetX() + dx, by = target->GetY() + dy;
			if (!Walkable(bx, by) || WorldData::IsSafeZone(bx, by))
			{
				bool found = false;
				for (int oy = -1; oy <= 1 && !found; ++oy)
					for (int ox = -1; ox <= 1 && !found; ++ox)
					{
						if (ox == 0 && oy == 0) continue;
						const int cx = target->GetX() + ox, cy = target->GetY() + oy;
						if (Walkable(cx, cy) && !WorldData::IsSafeZone(cx, cy)) { bx = cx; by = cy; found = true; }
					}
				if (!found) return;
			}
			MovePlayerTo(u, static_cast<short>(bx), static_cast<short>(by), 0);
			u->UpdateFacing(target->GetX() - bx);
			ApplyStatus(u, StatusId::Ambush, row.statusMs, 1, u->GetObjID());
			HitSpec spec;
			spec.coeff = row.coeff;
			spec.applyStatus = false;
			DealHit(u, row, target, spec);
			return;
		}
		case 17:  // 후방 습격 — 등 뒤(또는 기습 중)면 확정 치명타 ×1.5
		{
			auto target = ResolveEnemy(u, targetId);
			if (!target) return;
			const bool ambush = u->Status().Has(StatusId::Ambush, now);
			// 몬스터는 좌우로만 바라본다: 바라보는 쪽의 반대편에 서 있으면 "등 뒤"
			const int side = u->GetX() - target->GetX();
			const bool behind = side != 0 && ((target->IsFacingLeft() && side > 0) || (!target->IsFacingLeft() && side < 0));
			HitSpec spec;
			spec.coeff = row.coeff;
			spec.evaluateCombo = false;
			if (ambush || behind)
			{
				spec.forceCrit = true;
				spec.bonusPercent = row.comboBonus;
				spec.comboId = row.comboId;
				spec.flags2 = DAMAGE2_BACKSTAB;
			}
			DealHit(u, row, target, spec);
			if (ambush) RemoveStatus(u, StatusId::Ambush);
			return;
		}
		default:
			return;
		}
	}
}

namespace SkillHelper
{
	void HandleCast(const shared_ptr<User>& u, const SKILL_CAST_REQ_PACKET& req)
	{
		if (!u)
			return;
		const SkillRow* row = SkillTable::Find(req.skillId);
		if (row == nullptr)
			return;
		const uint32_t now = GetNowTime();
		auto fail = [&](SkillResult r, uint32_t ms = 0) { SendResult(u, req.seq, req.skillId, r, ms); };

		if (u->GetStat()->IsDead() || u->IsTransferring() || u->GetSectorX() < 0) return fail(SkillResult::Busy);
		if (row->cls != u->m_classId) return fail(SkillResult::WrongClass);
		if (u->GetStat()->GetLevel() < row->unlockLevel) return fail(SkillResult::Locked, row->unlockLevel);
		if (WorldData::IsSafeZone(u->GetX(), u->GetY())) return fail(SkillResult::SafeZone);
		if (u->Status().BlocksAttack(now)) return fail(SkillResult::Stunned);
		if (row->dash > 0 && u->Status().BlocksMove(now)) return fail(SkillResult::Stunned);

		const uint8_t slot = row->slot;
		if (now + kCooldownGraceMs < u->m_skillReadyAt[slot])
			return fail(SkillResult::Cooldown, u->m_skillReadyAt[slot] - now);
		if (u->m_mp < row->mp)
			return fail(SkillResult::NoMana);

		const int ux = u->GetX(), uy = u->GetY();
		ObjID targetId = req.targetId;
		short tx = req.x, ty = req.y;

		// ── 대상/지점 확인 ──
		switch (row->target)
		{
		case SkillTarget::Enemy:
		{
			auto target = ResolveEnemy(u, targetId);
			if (!target) return fail(SkillResult::InvalidTarget);
			const int slack = row->range >= 3 ? 1 : 0;
			if (Cheb(ux, uy, target->GetX(), target->GetY()) > row->range + slack)
				return fail(SkillResult::OutOfRange);
			// 다른 플레이어는 PK 규칙이 있는 단일 대상 기술로만
			if (!IsMonster(target) && WorldData::IsSafeZone(target->GetX(), target->GetY()))
				return fail(SkillResult::SafeZone);
			tx = target->GetX();
			ty = target->GetY();
			u->UpdateFacing(tx - ux);
			break;
		}
		case SkillTarget::Ground:
		{
			if (Cheb(ux, uy, tx, ty) > row->range + 1)
				return fail(SkillResult::OutOfRange);
			if (row->id == 10)
			{
				// 공간 도약: 도착 칸이 걸을 수 있어야 한다. 막혔으면 가까운 쪽으로 당긴다.
				int dx = Sign(tx - ux), dy = Sign(ty - uy);
				int bx = ux, by = uy;
				for (int i = 0; i < row->dash; ++i)
				{
					const int nx = bx + dx * (std::abs(tx - bx) > 0 ? 1 : 0);
					const int ny = by + dy * (std::abs(ty - by) > 0 ? 1 : 0);
					if (nx == bx && ny == by) break;
					if (!Walkable(nx, ny)) break;
					bx = nx; by = ny;
				}
				if (bx == ux && by == uy) return fail(SkillResult::Blocked);
				tx = static_cast<short>(bx);
				ty = static_cast<short>(by);
			}
			u->UpdateFacing(tx - ux);
			break;
		}
		case SkillTarget::Direction:
			if (tx == ux && ty == uy) { tx = static_cast<short>(ux + (u->IsFacingLeft() ? -1 : 1)); ty = static_cast<short>(uy); }
			u->UpdateFacing(tx - ux);
			break;
		case SkillTarget::Self:
			tx = static_cast<short>(ux);
			ty = static_cast<short>(uy);
			break;
		}

		// ── 소모 + 쿨다운 ──
		const uint32_t cooldown = row->slot == 0
			? SkillTable::BasicIntervalMs(row->cls, u->m_derived.attackSpeed)
			: SkillTable::CooldownMs(*row, u->m_derived.cooldownReduction);
		u->m_skillReadyAt[slot] = now + cooldown;
		u->m_lastCombatAt = now;
		if (row->mp > 0)
		{
			u->m_mp = static_cast<uint16_t>(u->m_mp - row->mp);
			SendResource(u);
		}
		SendResult(u, req.seq, row->id, SkillResult::Ok, cooldown);

		// ── 알림(모션·예고) ──
		short landX = tx, landY = ty;
		if (row->id == 10)
		{
			// 공간 도약은 시전 즉시 이동, 떠난 자리에 룬(예약)
			BroadcastCast(u, row->id, targetId, static_cast<short>(ux), static_cast<short>(uy), landX, landY, 0);
			MovePlayerTo(u, landX, landY, 0);
			User::PendingImpact impact{ ++u->m_impactSeq, row->id, ObjID{}, static_cast<short>(ux), static_cast<short>(uy) };
			u->m_pendingImpacts.push_back(impact);
			GTimerThread->ScheduleAfter(u->GetObjID(), std::chrono::milliseconds(kRuneDelayMs), TIMER_EVENT_TYPE::EV_SKILL_IMPACT,
				ObjID(EnumCategory::eNone, static_cast<uint64_t>(impact.seq)));
			return;
		}
		BroadcastCast(u, row->id, targetId, static_cast<short>(ux), static_cast<short>(uy), landX, landY, row->castMs);

		if (row->castMs > 0)
		{
			User::PendingImpact impact{ ++u->m_impactSeq, row->id, targetId, tx, ty };
			u->m_pendingImpacts.push_back(impact);
			GTimerThread->ScheduleAfter(u->GetObjID(), std::chrono::milliseconds(row->castMs), TIMER_EVENT_TYPE::EV_SKILL_IMPACT,
				ObjID(EnumCategory::eNone, static_cast<uint64_t>(impact.seq)));
			return;
		}
		Execute(u, *row, targetId, tx, ty);
	}

	void HandleImpact(const ObjID& casterId, uint32_t impactSeq)
	{
		auto u = ::GetGameObject<User>(casterId);
		if (!u)
			return;
		auto& list = u->m_pendingImpacts;
		const auto it = std::find_if(list.begin(), list.end(), [&](const User::PendingImpact& p) { return p.seq == impactSeq; });
		if (it == list.end())
			return;
		const User::PendingImpact impact = *it;
		list.erase(it);
		if (u->GetStat()->IsDead() || u->IsTransferring() || u->GetSectorX() < 0)
			return;
		const SkillRow* row = SkillTable::Find(impact.skillId);
		if (!row)
			return;
		// 예고 중 기절하면 취소(처형) — 혜성·룬은 이미 떨어지는 중이라 그대로
		if (row->id == 6 && u->Status().BlocksAttack(GetNowTime()))
			return;
		Execute(u, *row, impact.target, impact.x, impact.y);
	}
}
