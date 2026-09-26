#include "pch.h"
#include "Combat/SkillEx.h"
#include "Combat/SkillDetail.h"
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
#include "Data/SkillGrowth.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

// ─────────────────────────────────────────────────────────────────────────────
// 판정 규칙(칸 공간)
//   원(Circle) = 체비셰프 반경(예전 MonstersAround와 같다) · 부채꼴(Cone) = 유클리드 반경 + 0.5 안, 조준 방향과의 각 ≤ b°
//   직선(Line) = 조준 방향 투영 0 < t ≤ a + 0.5, 옆 거리 ≤ b/2 + 0.35 · 고리(Ring) = b ≤ 체비셰프 ≤ a
//   maxTargets가 있으면 모양 중심에서 가까운 순서로 자른다(각인탄 · 비수 투척: 먼저 닿는 적).
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	using namespace CombatHelper;
	using namespace SkillDetail;
	using SkillEx::ExEvent;

	constexpr uint32_t kGraceMs = 60;         // 재사용 대기 지터 여유
	constexpr uint32_t kChargeSlackMs = 80;   // 차지 완벽 구간 판정 여유(지연 흔들림)
	constexpr uint32_t kRuneDelayMs = 1000;   // 옛길 도약 각인이 터지기까지(예전과 같다)
	constexpr uint8_t  kEvLast = 0x40;        // PendingImpact.flags: 이 시전의 마지막 타격(갈래 「장판」이 여기서 남는다) — SkillReqFlag와 겹치지 않는 비트
	constexpr uint8_t  kEchoStage = 0xE0;     // Legacy 사건의 stage: 갈래 「메아리」로 한 번 더(피해 50%)
	constexpr uint8_t  kLingerHit = 0x7E;     // 장판 틱의 타격 번호(표 밖 — 클라는 반응 없이 숫자만)
	constexpr float kPiF = 3.14159265f;

	int Cheb(int ax, int ay, int bx, int by) { return (std::max)(std::abs(ax - bx), std::abs(ay - by)); }
	int Sign(int v) { return (v > 0) - (v < 0); }

	// 시험용 추적(HARVEN_SKILL_TRACE=1): 요청 · 타격 판정을 stderr로 — 샌드박스 녹화와 서버 시각을 맞춰 본다
	bool Trace()
	{
		static const bool on = std::getenv("HARVEN_SKILL_TRACE") != nullptr;
		return on;
	}

	struct Aim { short x = 0, y = 0; float dx = 1.f, dy = 0.f; };

	uint8_t Dir256(float dx, float dy)
	{
		const float sx = dx - dy, sy = (dx + dy) * 0.5f;
		float a = std::atan2(sy, sx);
		if (a < 0.f) a += 2.f * kPiF;
		return static_cast<uint8_t>(static_cast<int>(a / (2.f * kPiF) * 256.f) & 255);
	}

	void Normalize(float& dx, float& dy, const shared_ptr<User>& u)
	{
		const float l = std::sqrt(dx * dx + dy * dy);
		if (l < 1e-3f) { dx = u->IsFacingLeft() ? -1.f : 1.f; dy = 0.f; return; }
		dx /= l; dy /= l;
	}

	uint32_t CooldownOf(const shared_ptr<User>& u, const SkillDef& def, const SkillRow& row)
	{
		if (def.kind == SkillKind::Basic)
			return SkillTable::BasicIntervalMs(row.cls, u->m_derived.attackSpeed);
		return SkillDefTable::CooldownMs(def, row, u->m_derived.cooldownReduction);
	}

	uint32_t& ReadyAt(const shared_ptr<User>& u, const SkillDef& def) { return u->m_keyReadyAt[static_cast<size_t>(def.key)]; }

	// ── 알림 ────────────────────────────────────────────────────────────────
	template <typename Packet>
	void ToViewers(const shared_ptr<Subject>& center, const Packet& packet, bool includeSelf)
	{
		if (center->GetSectorX() < 0)
			return;
		GSector->ForEachNeighborObject(center->GetSectorX(), center->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
				return;
			if (!includeSelf && object->GetObjID() == center->GetObjID())
				return;
			auto viewer = static_pointer_cast<User>(object);
			auto session = viewer->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				return;
			if (object != center && !SubjectHelper::CanSee(object, center))
				return;
			session->PostSend(packet);
		});
	}

	void Notify(const shared_ptr<User>& u, const SkillDef& def, SkillExPhase phase, uint8_t stage, uint8_t flags, const Aim& aim, const ObjID& target, uint16_t castMs)
	{
		SKILL_EX_NFY_PACKET p{};
		p.size = sizeof(p);
		p.type = static_cast<char>(PacketType::SKILL_EX_NFY);
		p.casterId = u->GetObjID();
		p.skillId = def.id;
		p.phase = static_cast<uint8_t>(phase);
		p.stage = stage;
		// 켜진 갈래(비트 1~4) — 주변 클라가 같은 복사본(SkillGrowth::ApplyDef)으로 시각표 · 연출을 만든다
		p.flags = static_cast<uint8_t>((flags & ~SKILLEX_VARIANT_MASK) | SkillGrowth::NfyBits(u->GrowState(def.id)));
		p.dir = Dir256(aim.dx, aim.dy);
		p.fromX = u->GetX();
		p.fromY = u->GetY();
		p.aimX = aim.x;
		p.aimY = aim.y;
		p.targetId = target;
		p.castMs = castMs;
		p.serverTime = GetNowTime();
		ToViewers(u, p, true);
	}

	void NotifyForcedMove(const shared_ptr<Subject>& s, short fx, short fy, short tx, short ty, uint16_t ms, ForcedMoveKind kind)
	{
		SUBJECT_FORCED_MOVE_NFY_PACKET p{};
		p.size = sizeof(p);
		p.type = static_cast<char>(PacketType::SUBJECT_FORCED_MOVE_NFY);
		p.id = s->GetObjID();
		p.fromX = fx; p.fromY = fy;
		p.toX = tx; p.toY = ty;
		p.durationMs = ms;
		p.kind = static_cast<uint8_t>(kind);
		ToViewers(s, p, true);
	}

	// ── 예약 ────────────────────────────────────────────────────────────────
	void Schedule(const shared_ptr<User>& u, User::PendingImpact ev, uint32_t delayMs)
	{
		ev.seq = ++u->m_impactSeq;
		u->m_pendingImpacts.push_back(ev);
		GTimerThread->ScheduleAfter(u->GetObjID(), std::chrono::milliseconds(delayMs), TIMER_EVENT_TYPE::EV_SKILL_IMPACT,
			ObjID(EnumCategory::eNone, static_cast<uint64_t>(ev.seq)));
	}

	User::PendingImpact MakeEvent(const shared_ptr<User>& u, const SkillDef& def, ExEvent kind, const Aim& aim, const ObjID& target, uint8_t stage, uint8_t flags)
	{
		User::PendingImpact ev{};
		ev.skillId = def.id;
		ev.target = target;
		ev.x = aim.x;
		ev.y = aim.y;
		ev.kind = static_cast<uint8_t>(kind);
		ev.stage = stage;
		ev.flags = flags;
		ev.castSeq = u->m_skx.castSeq;
		ev.ox = u->GetX();
		ev.oy = u->GetY();
		ev.dx = aim.dx;
		ev.dy = aim.dy;
		ev.grow = def.id < SkillGrowth::kIdSpan ? u->m_skillGrow[def.id] : 0;
		return ev;
	}

	// ── 모양 ────────────────────────────────────────────────────────────────
	bool Inside(const SkillHit& h, int cx, int cy, float dx, float dy, int x, int y)
	{
		return SkillDefTable::InsideHit(h, static_cast<float>(cx), static_cast<float>(cy), dx, dy, static_cast<float>(x), static_cast<float>(y));
	}

	int Reach(const SkillHit& h) { return (h.shape == HitShape::Line || h.shape == HitShape::Cone) ? h.a + 1 : (std::max)(1, static_cast<int>(h.a)); }

	// 모양 안의 적(몬스터 + 강제 공격이면 마을 밖 다른 플레이어)
	std::vector<shared_ptr<Subject>> Collect(const shared_ptr<User>& u, const SkillHit& h, int cx, int cy, float dx, float dy, bool force)
	{
		std::vector<shared_ptr<Subject>> out;
		for (auto& m : MonstersAround(u, cx, cy, Reach(h)))
			if (Inside(h, cx, cy, dx, dy, m->GetX(), m->GetY()))
				out.push_back(m);
		if (force && u->GetSectorX() >= 0 && !WorldData::IsSafeZone(u->GetX(), u->GetY()))
		{
			GSector->ForEachNeighborObject(u->GetSectorX(), u->GetSectorY(), [&](const shared_ptr<Subject>& object)
			{
				if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser || object == u)
					return;
				if (object->GetStat()->IsDead() || WorldData::IsSafeZone(object->GetX(), object->GetY()))
					return;
				if (Inside(h, cx, cy, dx, dy, object->GetX(), object->GetY()))
					out.push_back(object);
			});
		}
		if (h.maxTargets > 0 && out.size() > h.maxTargets)
		{
			std::sort(out.begin(), out.end(), [&](const shared_ptr<Subject>& a, const shared_ptr<Subject>& b)
			{
				const int da = (a->GetX() - cx) * (a->GetX() - cx) + (a->GetY() - cy) * (a->GetY() - cy);
				const int db = (b->GetX() - cx) * (b->GetX() - cx) + (b->GetY() - cy) * (b->GetY() - cy);
				return da < db;
			});
			out.resize(h.maxTargets);
		}
		return out;
	}

	HitSpec SpecOf(const SkillHit& h, int hitIdx, uint32_t castSeq, int fromX, int fromY)
	{
		HitSpec s;
		s.coeff = h.coeff;
		s.hitIndex = static_cast<uint8_t>(0x80 | (hitIdx & 0x7F));
		s.applyStatus = (h.flags & HF_Status) != 0;
		s.applyKnockback = false;
		s.react = h.react;
		s.stagger = h.stagger;
		s.counter = (h.flags & HF_Counter) != 0;
		s.extras = true;
		s.castSeq = castSeq;
		s.fromX = static_cast<short>(fromX);
		s.fromY = static_cast<short>(fromY);
		if (h.flags & HF_Heavy) s.flags2 |= DAMAGE2_HEAVY;
		return s;
	}

	// 타격표 한 줄을 지금 판정한다. 돌려주는 값 = 모양 가운데 칸(갈래 「장판」이 남는 자리)
	std::pair<int, int> ApplyHitRow(const shared_ptr<User>& u, const SkillRow& row, const SkillDef& def, int hitIdx, const User::PendingImpact& ev)
	{
		const SkillHit& h = def.hits[hitIdx];
		// 세 줄기(갈래)로 덧붙은 타격은 조준 방향을 돌린다
		float dx = ev.dx, dy = ev.dy;
		SkillDefTable::TurnDir(h, dx, dy);
		int cx = ev.x, cy = ev.y;
		switch (h.anchor)
		{
		case HitAnchor::Caster: cx = u->GetX(); cy = u->GetY(); break;
		case HitAnchor::Aim: break;
		case HitAnchor::Target:
			if (auto t = ResolveEnemy(u, ev.target); t && Cheb(t->GetX(), t->GetY(), u->GetX(), u->GetY()) <= row.range + 3)
			{
				cx = t->GetX(); cy = t->GetY();
			}
			else if (h.shape == HitShape::Target)
				return { cx, cy };   // 대상이 없어졌다(멀어졌다) — 헛침
			break;
		}
		if (h.along != 0)
		{
			cx += static_cast<int>(std::lround(dx * h.along));
			cy += static_cast<int>(std::lround(dy * h.along));
		}
		const bool force = (ev.flags & SKILLREQ_FORCE) != 0;
		// 넉백 기준: 시전자 기준 타격은 시전자, 지점 타격은 지점 가운데
		const int fx = h.anchor == HitAnchor::Caster ? u->GetX() : cx;
		const int fy = h.anchor == HitAnchor::Caster ? u->GetY() : cy;
		const auto targets = Collect(u, h, cx, cy, dx, dy, force);
		if (Trace())
			std::fprintf(stderr, "[skillex] t=%u hit id=%u row=%d center=%d,%d dir=%.2f,%.2f targets=%zu tag=%u\n", GetNowTime(), def.id, hitIdx, cx, cy, dx, dy, targets.size(), h.tag);
		for (auto& t : targets)
			DealHit(u, row, t, SpecOf(h, hitIdx, ev.castSeq, fx, fy));
		return { cx, cy };
	}

	// ── 도움 ────────────────────────────────────────────────────────────────
	Aim ResolveAim(const shared_ptr<User>& u, const SkillRow& row, const SKILL_EX_REQ_PACKET& req, const shared_ptr<Subject>& target)
	{
		Aim a;
		const SkillDef* def = SkillDefTable::Find(row.id);
		const int ux = u->GetX(), uy = u->GetY();
		int ax = req.aimX, ay = req.aimY;
		if (target && (row.target == SkillTarget::Enemy || row.target == SkillTarget::Direction))
		{
			ax = target->GetX();
			ay = target->GetY();
		}
		if (!ZoneLayout::IsValidWorldPosition(static_cast<short>(ax), static_cast<short>(ay)))
		{
			ax = ux; ay = uy;
		}
		const int reach = (std::max)(1, static_cast<int>(def && def->range > 0 ? def->range : row.range));
		const int d = Cheb(ux, uy, ax, ay);
		if (row.target == SkillTarget::Self)
		{
			ax = ux; ay = uy;
		}
		else if (d > reach)
		{
			const float k = static_cast<float>(reach) / static_cast<float>(d);
			ax = ux + static_cast<int>(std::lround((ax - ux) * k));
			ay = uy + static_cast<int>(std::lround((ay - uy) * k));
		}
		a.x = static_cast<short>(ax);
		a.y = static_cast<short>(ay);
		a.dx = static_cast<float>(req.aimX - ux);
		a.dy = static_cast<float>(req.aimY - uy);
		if (target) { a.dx = static_cast<float>(target->GetX() - ux); a.dy = static_cast<float>(target->GetY() - uy); }
		Normalize(a.dx, a.dy, u);
		if (std::fabs(a.dx) > 0.2f) u->UpdateFacing(a.dx > 0.f ? 1 : -1);
		return a;
	}

	void ScheduleHits(const shared_ptr<User>& u, const SkillDef& def, const Aim& aim, const ObjID& target, uint8_t stageEq, uint8_t stageMax, bool perfect, uint8_t flags, bool releaseOnly, bool skipRelease, uint32_t offsetMs)
	{
		auto active = [&](const SkillHit& h)
		{
			if (h.flags & HF_Tick) return false;
			if (releaseOnly != ((h.flags & HF_Release) != 0)) return false;
			if (skipRelease && (h.flags & HF_Release)) return false;
			if ((h.flags & HF_Perfect) && !perfect) return false;
			if (stageEq != 0 && h.stage != stageEq) return false;
			if (stageMax != 0 && h.stage != 0 && h.stage > stageMax) return false;
			return true;
		};
		// 이 시전의 마지막 타격(덧붙은 메아리 · 세 줄기는 빼고) — 갈래 「장판」이 그 자리에 남는다
		int last = -1;
		for (int i = 0; i < def.hitCount; ++i)
			if (active(def.hits[i]) && def.hits[i].tag == HT_None && (last < 0 || def.hits[i].atMs >= def.hits[last].atMs)) last = i;
		for (int i = 0; i < def.hitCount; ++i)
		{
			const SkillHit& h = def.hits[i];
			if (!active(h)) continue;
			auto ev = MakeEvent(u, def, ExEvent::Hit, aim, target, stageEq ? stageEq : stageMax, flags);
			ev.hit = static_cast<uint8_t>(i);
			if (i == last) ev.flags = static_cast<uint8_t>(ev.flags | kEvLast);
			Schedule(u, ev, offsetMs + h.atMs);
		}
	}

	// 갈래 「장판」: 주 타격 자리에 value초 동안 0.5초마다(20% · 상태이상). 반경 = radius(최소 1)
	void StartLinger(const shared_ptr<User>& u, const SkillDef& def, uint8_t grow, int cx, int cy, int radius, uint32_t castSeq, uint8_t reqFlags)
	{
		const SkillGrowth::Variant* v = SkillGrowth::FindMod(def.id, SkillGrowth::Unpack(grow), SkillGrowth::Mod::Lasting);
		if (!v) return;
		User::PendingImpact ev{};
		ev.skillId = def.id;
		ev.x = static_cast<short>(cx);
		ev.y = static_cast<short>(cy);
		ev.kind = static_cast<uint8_t>(ExEvent::Linger);
		ev.stage = static_cast<uint8_t>((std::max)(1, (std::min)(4, radius)));
		ev.flags = static_cast<uint8_t>(reqFlags & SKILLREQ_FORCE);
		ev.castSeq = castSeq;
		ev.ox = u->GetX(); ev.oy = u->GetY();
		ev.grow = grow;
		ev.extra = static_cast<uint32_t>(v->value) * 2u;   // 0.5초 틱 수
		Schedule(u, ev, 500);
	}

	void ApplyWindows(const shared_ptr<User>& u, const SkillDef& def, uint32_t extraMs = 0)
	{
		const uint32_t now = GetNowTime();
		if (def.superArmorMs > 0) u->m_superArmorUntil = (std::max)(u->m_superArmorUntil, now + def.superArmorMs + extraMs);
		if (def.invulnMs > 0) u->m_invulnUntil = (std::max)(u->m_invulnUntil, now + def.invulnMs);
	}

	void EndCharge(const shared_ptr<User>& u) { u->m_skx.chargeSkill = 0; u->m_skx.chargeCast = 0; }
	void EndHold(const shared_ptr<User>& u) { u->m_skx.holdSkill = 0; u->m_skx.holdCast = 0; }

	void ToggleOff(const shared_ptr<User>& u, const SkillDef& def, const SkillRow& row)
	{
		u->m_skx.toggleSkill = 0;
		u->m_skx.toggleCast = 0;
		RemoveStatus(u, StatusId::Guard);
		const uint32_t cd = CooldownOf(u, def, row);
		ReadyAt(u, def) = GetNowTime() + cd;
		Aim a; a.x = u->GetX(); a.y = u->GetY();
		Notify(u, def, SkillExPhase::ToggleOff, 0, 0, a, ObjID{}, 0);
	}

	// 차지를 놓는다(요청 또는 최대 시간)
	void ReleaseCharge(const shared_ptr<User>& u, const SkillDef& def, const SkillRow& row, Aim aim, bool timedOut, uint8_t reqFlags)
	{
		const uint32_t now = GetNowTime();
		const uint32_t held = now - u->m_skx.chargeStart;
		const uint8_t stage = SkillDefTable::ChargeStage(def, held);
		const bool perfect = !timedOut && SkillDefTable::ChargePerfect(def, held, kChargeSlackMs);
		EndCharge(u);
		++u->m_skx.castSeq;
		ApplyWindows(u, def, 400);
		const uint8_t flags = static_cast<uint8_t>(perfect ? SKILLEX_PERFECT : 0);
		Notify(u, def, SkillExPhase::ChargeRelease, stage, flags, aim, ObjID{}, 0);
		if (def.special == SkillSpecial::MoonCleave)
		{
			// 달 가르기: 직선 위 적을 기억하고 끝까지 돌진 → 0.3초 뒤 한꺼번에
			u->m_skx.marks.clear();
			SkillHit line = def.hits[0];
			for (auto& t : Collect(u, line, u->GetX(), u->GetY(), aim.dx, aim.dy, (reqFlags & SKILLREQ_FORCE) != 0))
				u->m_skx.marks.push_back(t->GetObjID());
			const int ux = u->GetX(), uy = u->GetY();
			const int sdx = static_cast<int>(std::lround(aim.dx)), sdy = static_cast<int>(std::lround(aim.dy));
			int x = ux, y = uy;
			for (int i = 0; i < row.dash; ++i)
			{
				const int nx = x + sdx, ny = y + sdy;
				if (!Walkable(nx, ny) || (WorldData::IsSafeZone(nx, ny) && !WorldData::IsTrainingYard(nx, ny))) break;
				x = nx; y = ny;
			}
			if (x != ux || y != uy)
			{
				MovePlayerTo(u, static_cast<short>(x), static_cast<short>(y), def.moveMs * static_cast<uint32_t>(Cheb(ux, uy, x, y)));
				NotifyForcedMove(u, static_cast<short>(ux), static_cast<short>(uy), static_cast<short>(x), static_cast<short>(y),
					static_cast<uint16_t>(def.moveMs * Cheb(ux, uy, x, y)), ForcedMoveKind::Dash);
			}
			for (int i = 0; i < def.hitCount; ++i)
			{
				const SkillHit& h = def.hits[i];
				if ((h.flags & HF_Perfect) && !perfect) continue;
				if (h.stage != 0 && h.stage > stage) continue;
				auto ev = MakeEvent(u, def, ExEvent::MarkHit, aim, ObjID{}, stage, reqFlags);
				ev.hit = static_cast<uint8_t>(i);
				Schedule(u, ev, h.atMs);
			}
			return;
		}
		ScheduleHits(u, def, aim, ObjID{}, 0, stage, perfect, reqFlags, false, false, 0);
		(void)row;
	}

	void EndHoldWithRelease(const shared_ptr<User>& u, const SkillDef& def, bool release)
	{
		Aim a; a.x = u->GetX(); a.y = u->GetY();
		EndHold(u);
		++u->m_skx.castSeq;
		Notify(u, def, SkillExPhase::HoldEnd, 0, 0, a, ObjID{}, 0);
		if (!release) return;
		// 마무리 타격: 시전자 기준이면 지금 자리에서
		ScheduleHits(u, def, a, ObjID{}, 0, 0, false, 0, true, false, 0);
	}

	// ── 이동기 ──────────────────────────────────────────────────────────────
	bool DoMove(const shared_ptr<User>& u, const SkillDef& def, const SkillRow& row, Aim& aim, const shared_ptr<Subject>& target, const ObjID& targetId)
	{
		const int ux = u->GetX(), uy = u->GetY();
		switch (def.special)
		{
		case SkillSpecial::Roll:
		{
			int dx = static_cast<int>(std::lround(aim.dx)), dy = static_cast<int>(std::lround(aim.dy));
			if (dx == 0 && dy == 0) dx = u->IsFacingLeft() ? -1 : 1;
			int x = ux, y = uy;
			for (int i = 0; i < row.dash; ++i)
			{
				const int nx = x + dx, ny = y + dy;
				if (!Walkable(nx, ny)) break;
				x = nx; y = ny;
			}
			if (x != ux || y != uy)
			{
				MovePlayerTo(u, static_cast<short>(x), static_cast<short>(y), def.moveMs * static_cast<uint32_t>(Cheb(ux, uy, x, y)));
				NotifyForcedMove(u, static_cast<short>(ux), static_cast<short>(uy), static_cast<short>(x), static_cast<short>(y),
					static_cast<uint16_t>(def.moveMs * Cheb(ux, uy, x, y)), ForcedMoveKind::Roll);
			}
			aim.x = static_cast<short>(x); aim.y = static_cast<short>(y);
			ApplyStatus(u, StatusId::Evade, def.invulnMs, 1, u->GetObjID());
			return true;
		}
		case SkillSpecial::Blink:
		{
			short tx = aim.x, ty = aim.y;
			if (!BlinkLanding(u, row, tx, ty)) return false;
			aim.x = tx; aim.y = ty;
			MovePlayerTo(u, tx, ty, 0);
			NotifyForcedMove(u, static_cast<short>(ux), static_cast<short>(uy), tx, ty, 0, ForcedMoveKind::Blink);
			// 떠난 자리 각인 폭발(예전 판정 그대로)
			Aim from; from.x = static_cast<short>(ux); from.y = static_cast<short>(uy);
			auto ev = MakeEvent(u, def, ExEvent::Legacy, from, ObjID{}, 0, 0);
			Schedule(u, ev, kRuneDelayMs);
			return true;
		}
		case SkillSpecial::Dodge:
			Execute(u, row, targetId, aim.x, aim.y);
			u->m_skx.dodgeAt = GetNowTime();
			aim.x = u->GetX(); aim.y = u->GetY();
			NotifyForcedMove(u, static_cast<short>(ux), static_cast<short>(uy), aim.x, aim.y, static_cast<uint16_t>(def.moveMs * Cheb(ux, uy, aim.x, aim.y)), ForcedMoveKind::Dash);
			return true;
		default:
			(void)target;
			return false;
		}
	}

	// ── 고유 기술 ────────────────────────────────────────────────────────────
	void DoIdentity(const shared_ptr<User>& u, const SkillDef& def, const SkillRow& row, const Aim& aim, uint8_t flags)
	{
		const uint32_t now = GetNowTime();
		switch (def.special)
		{
		case SkillSpecial::OathRelease:
		{
			ApplyStatus(u, StatusId::Oath, row.statusMs, 1, u->GetObjID());
			u->m_superArmorUntil = (std::max)(u->m_superArmorUntil, now + row.statusMs);
			// 단죄 재사용 대기 절반
			uint32_t& v = u->m_keyReadyAt[static_cast<size_t>(SkillKey::V)];
			if (v > now) v = now + (v - now) / 2;
			UserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::OathRelease, u->GetX(), u->GetY(), 0);
			ScheduleHits(u, def, aim, ObjID{}, 0, 0, false, flags, false, false, 0);
			break;
		}
		case SkillSpecial::Resonance:
		{
			// 8칸 안 표식 대상이 0.05초 간격으로 터진다(없으면 가까운 셋)
			std::vector<shared_ptr<Monster>> marked, all = MonstersAround(u, u->GetX(), u->GetY(), row.radius);
			for (auto& m : all)
				if (m->Status().Has(StatusId::Marked, now)) marked.push_back(m);
			if (marked.empty())
			{
				std::sort(all.begin(), all.end(), [&](const shared_ptr<Monster>& a, const shared_ptr<Monster>& b)
				{
					return Cheb(a->GetX(), a->GetY(), u->GetX(), u->GetY()) < Cheb(b->GetX(), b->GetY(), u->GetX(), u->GetY());
				});
				for (size_t i = 0; i < all.size() && i < 3; ++i) marked.push_back(all[i]);
			}
			uint32_t delay = 250;
			uint8_t order = 0;
			for (auto& m : marked)
			{
				Aim at; at.x = m->GetX(); at.y = m->GetY();
				auto ev = MakeEvent(u, def, ExEvent::Resonance, at, m->GetObjID(), order++, flags);
				Schedule(u, ev, delay);
				delay += 50;
			}
			break;
		}
		case SkillSpecial::Veil:
		{
			ApplyStatus(u, StatusId::Veil, row.statusMs, 1, u->GetObjID());
			ApplyStatus(u, StatusId::Ambush, row.statusMs + 2000, 1, u->GetObjID());
			// 나를 쫓던 몬스터는 놓친다
			for (auto& m : MonstersAround(u, u->GetX(), u->GetY(), 8))
				if (m->IsProvoked() && m->GetProvokedBy() == u->GetObjID())
					m->ClearProvoke();
			CombatHelper::SendStats(u);   // 이동 속도가 바뀐다
			break;
		}
		default:
			break;
		}
	}

	// ── 특수 일반 기술 ────────────────────────────────────────────────────────
	void DoSpecialStart(const shared_ptr<User>& u, const SkillDef& def, const SkillRow& row, const Aim& aim, const ObjID& targetId, uint8_t flags)
	{
		const uint32_t now = GetNowTime();
		switch (def.special)
		{
		case SkillSpecial::Rally:
		{
			// 갈래 「긴 함성」: 8초 → 12초
			const SkillGrowth::Variant* longer = SkillGrowth::FindMod(def.id, u->GrowState(def.id), SkillGrowth::Mod::Longer);
			ApplyStatus(u, StatusId::Rally, longer ? 8000u * (100u + longer->value) / 100u : 8000u, 1, u->GetObjID());
			int n = 0;
			for (auto& m : MonstersAround(u, u->GetX(), u->GetY(), row.radius))
			{
				ApplyStatus(m, StatusId::Taunt, row.statusMs, 1, u->GetObjID());
				m->SetProvoked(u->GetObjID());
				MonsterHelper::Provoke(m, u);
				++n;
			}
			UserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::Taunted, u->GetX(), u->GetY(), n);
			ScheduleHits(u, def, aim, targetId, 0, 0, false, flags, false, false, 0);
			break;
		}
		case SkillSpecial::Clones:
			ApplyStatus(u, StatusId::Clone, row.statusMs, row.statusStacks, u->GetObjID());
			u->m_skx.lastCloneCast = u->m_skx.castSeq;   // 이번 시전은 세지 않는다
			break;
		case SkillSpecial::PoisonMist:
		{
			ScheduleHits(u, def, aim, targetId, 0, 0, false, flags, false, false, 0);
			auto ev = MakeEvent(u, def, ExEvent::MistTick, aim, ObjID{}, 0, flags);
			// 갈래 「긴 안개」: 틱 8번(약 4초) → 12번
			const SkillGrowth::Variant* longer = SkillGrowth::FindMod(def.id, u->GrowState(def.id), SkillGrowth::Mod::Longer);
			ev.extra = longer ? 8u * (100u + longer->value) / 100u : 8u;
			Schedule(u, ev, 700);
			break;
		}
		case SkillSpecial::DuskAwaken:
		{
			// 그믐 베기: 최대 7대상을 기억하고(원 4칸) 표의 시각에 한꺼번에
			u->m_skx.marks.clear();
			const SkillHit& first = def.hits[0];
			for (auto& t : Collect(u, first, u->GetX(), u->GetY(), aim.dx, aim.dy, (flags & SKILLREQ_FORCE) != 0))
				u->m_skx.marks.push_back(t->GetObjID());
			for (int i = 0; i < def.hitCount; ++i)
			{
				auto ev = MakeEvent(u, def, ExEvent::MarkHit, aim, ObjID{}, 0, flags);
				ev.hit = static_cast<uint8_t>(i);
				Schedule(u, ev, def.hits[i].atMs);
			}
			break;
		}
		case SkillSpecial::Barrier:
			ScheduleHits(u, def, aim, targetId, 0, 0, false, flags, false, false, 0);
			break;
		case SkillSpecial::Dash:
		case SkillSpecial::ShadowStep:
		{
			// 예전 판정이 곧바로 옮기고 때린다
			const int ux = u->GetX(), uy = u->GetY();
			Execute(u, row, targetId, aim.x, aim.y);
			if (u->GetX() != ux || u->GetY() != uy)
				NotifyForcedMove(u, static_cast<short>(ux), static_cast<short>(uy), u->GetX(), u->GetY(),
					static_cast<uint16_t>(def.moveMs * Cheb(ux, uy, u->GetX(), u->GetY())),
					def.special == SkillSpecial::Dash ? ForcedMoveKind::Dash : ForcedMoveKind::Blink);
			// 갈래 「불씨 자국」 · 「먹물 웅덩이」: 도착한 자리에
			StartLinger(u, def, def.id < SkillGrowth::kIdSpan ? u->m_skillGrow[def.id] : 0, u->GetX(), u->GetY(), 1, u->m_skx.castSeq, flags);
			break;
		}
		case SkillSpecial::Legacy:
		{
			auto ev = MakeEvent(u, def, ExEvent::Legacy, aim, targetId, 0, flags);
			const uint32_t at = def.hitCount > 0 ? def.hits[0].atMs : def.castMs;
			// 갈래 「메아리」(예전 판정 기술): 같은 판정을 value×10ms 뒤 한 번 더
			if (const SkillGrowth::Variant* echo = SkillGrowth::FindMod(def.id, u->GrowState(def.id), SkillGrowth::Mod::Echo))
			{
				auto again = ev;
				again.stage = kEchoStage;
				Schedule(u, again, at + echo->value * 10u);
			}
			if (at == 0) { u->m_pendingImpacts.push_back(ev); SkillEx::HandleEvent(u, u->m_pendingImpacts.back()); u->m_pendingImpacts.pop_back(); }
			else Schedule(u, ev, at);
			break;
		}
		default:
			ScheduleHits(u, def, aim, targetId, 0, 0, false, flags, false, false, 0);
			break;
		}
		(void)now;
	}
}

namespace SkillEx
{
	void HandleRequest(const shared_ptr<User>& u, const SKILL_EX_REQ_PACKET& req)
	{
		if (!u)
			return;
		const SkillRow* rowBase = SkillTable::Find(req.skillId);
		const SkillDef* defBase = SkillDefTable::Find(req.skillId);
		if (rowBase == nullptr || defBase == nullptr)
			return;
		// Phase 2-D: 켜진 갈래를 입힌 복사본으로 판정한다(클라 예측 · 연출도 같은 복사본 — SkillGrowth::ApplyDef)
		const SkillGrowth::State grow = u->GrowState(req.skillId);
		const SkillRow rowEff = SkillGrowth::ApplyRow(*rowBase, grow);
		const SkillDef defEff = SkillGrowth::ApplyDef(*defBase, *rowBase, grow);
		const SkillRow* row = &rowEff;
		const SkillDef* def = &defEff;
		const uint32_t now = GetNowTime();
		auto fail = [&](SkillResult r, uint32_t ms = 0) { SendResult(u, req.seq, req.skillId, r, ms); };
		const auto phase = static_cast<SkillPhase>(req.phase);
		if (Trace())
			std::fprintf(stderr, "[skillex] t=%u req id=%u phase=%u seq=%u at=%d,%d aim=%d,%d\n", now, req.skillId, req.phase, req.seq, u->GetX(), u->GetY(), req.aimX, req.aimY);

		// ── 놓음 · 취소(차지 · 홀딩) — 재사용 대기 · 자원은 누를 때 냈다 ──
		if (phase == SkillPhase::Release || phase == SkillPhase::Cancel)
		{
			if (u->m_skx.chargeSkill == def->id)
			{
				if (phase == SkillPhase::Cancel) { EndCharge(u); Aim a; a.x = u->GetX(); a.y = u->GetY(); Notify(u, *def, SkillExPhase::Cancel, 0, 0, a, ObjID{}, 0); return; }
				auto target = req.targetId != ObjID{} ? ResolveEnemy(u, req.targetId) : nullptr;
				ReleaseCharge(u, *def, *row, ResolveAim(u, *row, req, target), false, req.flags);
			}
			else if (u->m_skx.holdSkill == def->id)
				EndHoldWithRelease(u, *def, phase == SkillPhase::Release);
			return;
		}

		if (u->GetStat()->IsDead() || u->IsTransferring() || u->GetSectorX() < 0) return fail(SkillResult::Busy);
		if (row->cls != u->m_classId) return fail(SkillResult::WrongClass);
		if (u->GetStat()->GetLevel() < row->unlockLevel) return fail(SkillResult::Locked, row->unlockLevel);
		// 안전지대에서는 싸우지 않는다 — 훈련장(Phase 2-E)만 예외(맞는 것은 허수아비뿐)
		if (WorldData::IsSafeZone(u->GetX(), u->GetY()) && !WorldData::IsTrainingYard(u->GetX(), u->GetY())) return fail(SkillResult::SafeZone);
		if (u->Status().BlocksAttack(now)) return fail(SkillResult::Stunned);
		if ((row->dash > 0 || def->kind == SkillKind::Move) && u->Status().BlocksMove(now)) return fail(SkillResult::Stunned);

		// 토글 끄기 · 체인 다음 단은 재사용 대기와 상관없다
		if (def->kind == SkillKind::Toggle && u->m_skx.toggleSkill == def->id)
		{
			ToggleOff(u, *def, *row);
			SendResult(u, req.seq, def->id, SkillResult::Ok, CooldownOf(u, *def, *row));
			return;
		}
		auto target = req.targetId != ObjID{} ? ResolveEnemy(u, req.targetId) : nullptr;
		if (def->kind == SkillKind::Chain && u->m_skx.chainSkill == def->id && now < u->m_skx.chainUntil && u->m_skx.chainStage < def->stages)
		{
			const uint8_t stage = ++u->m_skx.chainStage;
			const Aim aim = ResolveAim(u, *row, req, target);
			++u->m_skx.castSeq;
			Notify(u, *def, SkillExPhase::ChainStep, stage, 0, aim, req.targetId, 0);
			if (def->special == SkillSpecial::ShieldBounce)
			{
				// 방패 투척 2단: 튕긴 적을 끌어당기며 되돌아온다
				for (size_t i = 0; i < u->m_skx.marks.size(); ++i)
				{
					auto ev = MakeEvent(u, *def, ExEvent::MarkHit, aim, u->m_skx.marks[i], stage, req.flags);
					ev.hit = 1;
					ev.extra = static_cast<uint32_t>(i);
					Schedule(u, ev, def->hits[1].atMs + static_cast<uint32_t>(i) * 60u);
				}
			}
			else
				ScheduleHits(u, *def, aim, req.targetId, stage, 0, false, req.flags, false, false, 0);
			if (stage >= def->stages) u->m_skx.chainSkill = 0;
			SendResult(u, req.seq, def->id, SkillResult::Ok, ReadyAt(u, *def) > now ? ReadyAt(u, *def) - now : 0);
			return;
		}
		// 이미 모으는 중 · 도는 중이면 누름은 무시(키 반복)
		if (u->m_skx.chargeSkill == def->id || u->m_skx.holdSkill == def->id)
			return;

		uint32_t& ready = ReadyAt(u, *def);
		if (now + kGraceMs < ready)
			return fail(SkillResult::Cooldown, ready - now);
		if (u->m_mp < row->mp)
			return fail(SkillResult::NoMana);
		if (def->kind == SkillKind::Identity && u->m_identity < 1000)
			return fail(SkillResult::NoGauge, u->m_identity);
		if (row->target == SkillTarget::Enemy && def->kind != SkillKind::Basic && def->kind != SkillKind::Awaken)
		{
			if (!target) return fail(SkillResult::InvalidTarget);
			const int range = def->range > 0 ? def->range : row->range;
			const int slack = range >= 3 ? 1 : 0;
			if (Cheb(u->GetX(), u->GetY(), target->GetX(), target->GetY()) > range + slack)
				return fail(SkillResult::OutOfRange);
			if (!IsMonster(target) && ((req.flags & SKILLREQ_FORCE) == 0 || WorldData::IsSafeZone(target->GetX(), target->GetY())))
				return fail(SkillResult::SafeZone);
		}

		Aim aim = ResolveAim(u, *row, req, target);

		// 이동기: 모으기 · 돌기 · 캐스팅을 끊고 움직인다
		if (def->kind == SkillKind::Move)
		{
			if (u->m_skx.chargeSkill != 0) EndCharge(u);
			if (u->m_skx.holdSkill != 0) { if (const SkillDef* hd = SkillDefTable::Find(u->m_skx.holdSkill)) EndHoldWithRelease(u, *hd, false); }
			u->m_skx.castingUntil = 0;
			u->m_skx.castingSeq = 0;
			if (!DoMove(u, *def, *row, aim, target, req.targetId))
				return fail(SkillResult::Blocked);
		}

		// 다른 기술을 누르면 모으던 · 돌던 기술은 끊긴다(새 기술이 받아들여질 때만 — 재사용 대기 · 자원 검사 뒤)
		if (def->kind != SkillKind::Move)
		{
			if (u->m_skx.chargeSkill != 0 && u->m_skx.chargeSkill != def->id)
				if (const SkillDef* cd = SkillDefTable::Find(u->m_skx.chargeSkill))
				{
					EndCharge(u);
					Aim a; a.x = u->GetX(); a.y = u->GetY();
					Notify(u, *cd, SkillExPhase::Cancel, 0, 0, a, ObjID{}, 0);
				}
			if (u->m_skx.holdSkill != 0 && u->m_skx.holdSkill != def->id)
				if (const SkillDef* hd = SkillDefTable::Find(u->m_skx.holdSkill))
					EndHoldWithRelease(u, *hd, false);
		}

		// ── 소모 + 재사용 대기 ──
		++u->m_skx.castSeq;
		const uint32_t cooldown = CooldownOf(u, *def, *row);
		// 토글 · 홀딩 · 차지는 누르는 순간 재사용 대기(토글은 끌 때 다시 센다)
		ready = now + cooldown;
		u->m_lastCombatAt = now;
		if (row->mp > 0)
		{
			u->m_mp = static_cast<uint16_t>(u->m_mp - row->mp);
			SendResource(u);
		}
		if (def->kind == SkillKind::Identity) { u->m_identity = 0; SendGauge(u, true); }
		SendResult(u, req.seq, row->id, SkillResult::Ok, cooldown);
		// 각인술사: 시전할수록 새김이 찬다
		if (u->m_classId == ClassId::Engraver && def->kind != SkillKind::Identity)
			AddIdentity(u, def->kind == SkillKind::Basic ? 12 : 55);
		ApplyWindows(u, *def);

		switch (def->kind)
		{
		case SkillKind::Basic:
		{
			const uint8_t stage = (now - u->m_skx.comboAt <= def->stageMs && u->m_skx.comboStage < def->stages) ? static_cast<uint8_t>(u->m_skx.comboStage + 1) : 1;
			u->m_skx.comboStage = stage;
			u->m_skx.comboAt = now;
			Notify(u, *def, SkillExPhase::Start, stage, 0, aim, req.targetId, 0);
			ScheduleHits(u, *def, aim, req.targetId, stage, 0, false, req.flags, false, false, 0);
			break;
		}
		case SkillKind::Charge:
			u->m_skx.chargeSkill = def->id;
			u->m_skx.chargeStart = now;
			u->m_skx.chargeCast = u->m_skx.castSeq;
			Notify(u, *def, SkillExPhase::ChargeBegin, 0, 0, aim, req.targetId, static_cast<uint16_t>(SkillDefTable::ChargeMaxMs(*def)));
			{
				auto ev = MakeEvent(u, *def, ExEvent::ChargeTimeout, aim, ObjID{}, 0, req.flags);
				Schedule(u, ev, SkillDefTable::ChargeMaxMs(*def) + 250);
			}
			break;
		case SkillKind::Hold:
		{
			u->m_skx.holdSkill = def->id;
			u->m_skx.holdStart = now;
			u->m_skx.holdCast = u->m_skx.castSeq;
			Notify(u, *def, SkillExPhase::HoldBegin, 0, 0, aim, req.targetId, def->holdMaxMs);
			auto tick = MakeEvent(u, *def, ExEvent::HoldTick, aim, ObjID{}, 0, req.flags);
			Schedule(u, tick, SkillDefTable::kHoldFirstTickMs);   // 첫 0.12초 = 예고(Phase 2 4박자)
			auto timeout = MakeEvent(u, *def, ExEvent::HoldTimeout, aim, ObjID{}, 0, req.flags);
			Schedule(u, timeout, def->holdMaxMs);
			break;
		}
		case SkillKind::Toggle:
			u->m_skx.toggleSkill = def->id;
			u->m_skx.toggleCast = u->m_skx.castSeq;
			ApplyStatus(u, StatusId::Guard, def->holdMaxMs, 1, u->GetObjID());
			ready = now;   // 재사용 대기는 끌 때부터
			Notify(u, *def, SkillExPhase::ToggleOn, 0, 0, aim, ObjID{}, def->holdMaxMs);
			{
				auto ev = MakeEvent(u, *def, ExEvent::ToggleTick, aim, ObjID{}, 0, 0);
				Schedule(u, ev, def->tickMs);
			}
			break;
		case SkillKind::Chain:
			u->m_skx.chainSkill = def->id;
			u->m_skx.chainStage = 1;
			u->m_skx.chainUntil = now + def->stageMs;
			Notify(u, *def, SkillExPhase::ChainStep, 1, 0, aim, req.targetId, 0);
			if (def->special == SkillSpecial::ShieldBounce)
			{
				u->m_skx.marks.clear();
				if (target)
				{
					auto ev = MakeEvent(u, *def, ExEvent::Bounce, aim, target->GetObjID(), 1, req.flags);
					ev.hit = 0;
					ev.extra = 0;
					Schedule(u, ev, def->hits[0].atMs);
				}
			}
			else
				ScheduleHits(u, *def, aim, req.targetId, 1, 0, false, req.flags, false, false, 0);
			break;
		case SkillKind::Move:
			Notify(u, *def, SkillExPhase::Start, 0, 0, aim, req.targetId, 0);
			break;
		case SkillKind::Identity:
			Notify(u, *def, SkillExPhase::Identity, 0, 0, aim, ObjID{}, 0);
			DoIdentity(u, *def, *row, aim, req.flags);
			break;
		case SkillKind::Cast:
			u->m_skx.castingUntil = now + def->castMs;
			u->m_skx.castingSeq = u->m_skx.castSeq;
			[[fallthrough]];
		case SkillKind::Normal:
		case SkillKind::Awaken:
			Notify(u, *def, SkillExPhase::Start, 0, 0, aim, req.targetId, def->castMs);
			DoSpecialStart(u, *def, *row, aim, req.targetId, req.flags);
			break;
		}
	}

	void HandleEvent(const shared_ptr<User>& u, const User::PendingImpact& ev)
	{
		const SkillRow* rowBase = SkillTable::Find(ev.skillId);
		const SkillDef* defBase = SkillDefTable::Find(ev.skillId);
		if (!rowBase || !defBase)
			return;
		// 시전 때의 갈래(ev.grow)로 — 도중에 바꿔도 이 시전은 그대로
		const SkillGrowth::State grow = SkillGrowth::Unpack(ev.grow);
		const SkillRow rowEff = SkillGrowth::ApplyRow(*rowBase, grow);
		const SkillDef defEff = SkillGrowth::ApplyDef(*defBase, *rowBase, grow);
		const SkillRow* row = &rowEff;
		const SkillDef* def = &defEff;
		const uint32_t now = GetNowTime();
		switch (static_cast<ExEvent>(ev.kind))
		{
		case ExEvent::Hit:
			if (ev.hit >= def->hitCount) return;
			// 캐스팅이 끊겼으면(이동) 캐스팅 기술의 예약은 버린다
			if (def->kind == SkillKind::Cast && u->m_skx.castingSeq != ev.castSeq && def->hits[ev.hit].atMs >= def->castMs) return;
			if (u->Status().BlocksAttack(now) && def->kind != SkillKind::Awaken) return;   // 기절하면 남은 타격도 끊긴다(각성기는 무적이라 그대로)
			{
				const auto [cx, cy] = ApplyHitRow(u, *row, *def, ev.hit, ev);
				// 갈래 「장판」: 이 시전의 마지막 타격 자리에 남는다
				if (ev.flags & kEvLast)
					StartLinger(u, *def, ev.grow, cx, cy, def->hits[ev.hit].shape == HitShape::Target ? 1 : def->hits[ev.hit].a, ev.castSeq, ev.flags);
			}
			if (def->special == SkillSpecial::Barrier)
			{
				// 결계 고리: 안의 적에게 표식
				SkillHit inner = def->hits[ev.hit];
				inner.shape = HitShape::Circle;
				const SkillGrowth::Variant* longer = SkillGrowth::FindMod(def->id, grow, SkillGrowth::Mod::Longer);
				for (auto& t : Collect(u, inner, ev.x, ev.y, ev.dx, ev.dy, false))
					ApplyStatus(t, StatusId::Marked, longer ? 5000u * (100u + longer->value) / 100u : 5000u, 1, u->GetObjID());
			}
			if (def->hits[ev.hit].flags & HF_Final)
				if (u->Status().Has(StatusId::Ambush, now) && ev.skillId != 17 && ev.skillId != 13) RemoveStatus(u, StatusId::Ambush);
			return;
		case ExEvent::Legacy:
		{
			// 예전 판정 — 표의 무력화 · 반격을 함께 싣는다(DealHit이 읽는 확장 문맥)
			SkillDetail::HitSpec ctx;
			if (def->hitCount > 0)
			{
				ctx.stagger = def->hits[0].stagger;
				ctx.counter = (def->hits[0].flags & HF_Counter) != 0;
				ctx.react = def->hits[0].react;
			}
			ctx.extras = true;
			ctx.castSeq = ev.castSeq;
			SkillDetail::LegacyScope scope(ctx);
			if (Trace())
				std::fprintf(stderr, "[skillex] t=%u legacy id=%u echo=%d at=%d,%d grow=0x%02X\n", GetNowTime(), ev.skillId, ev.stage == kEchoStage ? 1 : 0, ev.x, ev.y, ev.grow);
			if (ev.stage == kEchoStage)
			{
				// 갈래 「메아리」: 같은 판정을 한 번 더(피해 50% · 밀치기 없음)
				SkillRow echo = *row;
				echo.coeff = static_cast<uint16_t>(echo.coeff / 2u);
				echo.knockback = 0;
				Execute(u, echo, ev.target, ev.x, ev.y);
				return;
			}
			Execute(u, *row, ev.target, ev.x, ev.y);
			// 갈래 「장판」(서리 각인 · 화염 고리 — 예전 판정 기술은 한 번뿐이라 여기서)
			if (ev.skillId != 10)
				StartLinger(u, *def, ev.grow, ev.x, ev.y, row->radius, ev.castSeq, ev.flags);
			return;
		}
		case ExEvent::HoldTick:
		{
			if (u->m_skx.holdSkill != ev.skillId || u->m_skx.holdCast != ev.castSeq) return;
			if (u->Status().BlocksAttack(now)) { EndHoldWithRelease(u, *def, false); return; }
			for (int i = 0; i < def->hitCount; ++i)
				if (def->hits[i].flags & HF_Tick)
					ApplyHitRow(u, *row, *def, i, ev);
			if (now + def->tickMs <= u->m_skx.holdStart + def->holdMaxMs)
			{
				User::PendingImpact next = ev;
				Schedule(u, next, def->tickMs);
			}
			return;
		}
		case ExEvent::HoldTimeout:
			if (u->m_skx.holdSkill == ev.skillId && u->m_skx.holdCast == ev.castSeq)
				EndHoldWithRelease(u, *def, true);
			return;
		case ExEvent::ChargeTimeout:
			if (u->m_skx.chargeSkill == ev.skillId && u->m_skx.chargeCast == ev.castSeq)
			{
				Aim a; a.x = ev.x; a.y = ev.y; a.dx = ev.dx; a.dy = ev.dy;
				ReleaseCharge(u, *def, *row, a, true, ev.flags);
			}
			return;
		case ExEvent::ToggleTick:
		{
			if (u->m_skx.toggleSkill != ev.skillId || u->m_skx.toggleCast != ev.castSeq) return;
			if (!u->Status().Has(StatusId::Guard, now) || u->m_mp < def->tickMp) { ToggleOff(u, *def, *row); return; }
			u->m_mp = static_cast<uint16_t>(u->m_mp - def->tickMp);
			SendResource(u);
			Schedule(u, ev, def->tickMs);
			return;
		}
		case ExEvent::MistTick:
		{
			SkillHit h = def->hits[0];
			h.coeff = 12;
			h.stagger = 1;
			for (auto& t : Collect(u, h, ev.x, ev.y, ev.dx, ev.dy, (ev.flags & SKILLREQ_FORCE) != 0))
			{
				DealHit(u, *row, t, SpecOf(h, 0, ev.castSeq, ev.x, ev.y));
				ApplyStatus(t, StatusId::Slow, 1200, 1, u->GetObjID());
			}
			if (ev.extra > 1)
			{
				User::PendingImpact next = ev;
				next.extra = ev.extra - 1;
				Schedule(u, next, 450);
			}
			return;
		}
		case ExEvent::Resonance:
		{
			auto t = ResolveEnemy(u, ev.target);
			const int cx = t ? t->GetX() : ev.x, cy = t ? t->GetY() : ev.y;
			UserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::Resonance, static_cast<short>(cx), static_cast<short>(cy), ev.stage);
			SkillHit h{};
			h.coeff = row->coeff;
			h.shape = HitShape::Circle;
			h.a = 1;
			h.react = HitReact::Launch;
			h.stagger = 20;
			for (auto& v : Collect(u, h, cx, cy, ev.dx, ev.dy, false))
			{
				DealHit(u, *row, v, SpecOf(h, 0, ev.castSeq, cx, cy));
				RemoveStatus(v, StatusId::Marked);
			}
			return;
		}
		case ExEvent::Bounce:
		{
			// 방패 투척 1단: 맞히고 다음 적(3칸 안, 아직 안 맞은 가장 가까운)으로 튄다 — 최대 3번
			auto t = ResolveEnemy(u, ev.target);
			if (!t) return;
			u->m_skx.marks.push_back(t->GetObjID());
			DealHit(u, *row, t, SpecOf(def->hits[0], 0, ev.castSeq, u->GetX(), u->GetY()));
			// 갈래 「다섯 번 튕김」
			const SkillGrowth::Variant* more = SkillGrowth::FindMod(def->id, grow, SkillGrowth::Mod::Extra);
			if (ev.extra + 1 >= 3u + (more ? more->value : 0u)) return;
			shared_ptr<Subject> next;
			int best = 99;
			for (auto& m : MonstersAround(u, t->GetX(), t->GetY(), row->radius))
			{
				if (std::find(u->m_skx.marks.begin(), u->m_skx.marks.end(), m->GetObjID()) != u->m_skx.marks.end()) continue;
				const int d = Cheb(t->GetX(), t->GetY(), m->GetX(), m->GetY());
				if (d < best) { best = d; next = m; }
			}
			if (!next) return;
			User::PendingImpact n = ev;
			n.target = next->GetObjID();
			n.x = next->GetX();
			n.y = next->GetY();
			n.extra = ev.extra + 1;
			Schedule(u, n, 110);
			return;
		}
		case ExEvent::MarkHit:
		{
			if (ev.hit >= def->hitCount) return;
			const SkillHit& h = def->hits[ev.hit];
			// 그믐 베기 첫 타: 중독을 터뜨린다(중첩당 +60% — 예전 독 폭발 규칙)
			const bool dusk = def->special == SkillSpecial::DuskAwaken;
			auto hitOne = [&](const ObjID& id)
			{
				auto t = ResolveEnemy(u, id);
				if (!t || Cheb(t->GetX(), t->GetY(), u->GetX(), u->GetY()) > 7) return;
				HitSpec spec = SpecOf(h, ev.hit, ev.castSeq, u->GetX(), u->GetY());
				if (dusk && ev.hit == 0)
				{
					const uint8_t poison = t->Status().Stacks(StatusId::Poison, now);
					if (poison > 0)
					{
						RemoveStatus(t, StatusId::Poison);
						UserHelper::BroadcastEffect(u, t->GetObjID(), EffectId::PoisonBurst, t->GetX(), t->GetY(), poison);
						spec.bonusPercent = 60u * poison;
						spec.comboId = 7;
						spec.evaluateCombo = false;
					}
				}
				if (def->special == SkillSpecial::ShieldBounce && h.react == HitReact::Pull)
				{
					const short fx = t->GetX(), fy = t->GetY();
					DealHit(u, *row, t, spec);
					if (!t->GetStat()->IsDead() && (t->GetX() != fx || t->GetY() != fy))
						NotifyForcedMove(t, fx, fy, t->GetX(), t->GetY(), 180, ForcedMoveKind::Pull);
					return;
				}
				DealHit(u, *row, t, spec);
			};
			if (def->special == SkillSpecial::ShieldBounce)
				hitOne(ev.target);
			else
				for (const ObjID& id : std::vector<ObjID>(u->m_skx.marks)) hitOne(id);
			return;
		}
		case ExEvent::Linger:
		{
			// 갈래 「장판」 틱: 반경 ev.stage · 20% · 갈래의 상태이상(연계 없음 · 밀치기 없음)
			const SkillGrowth::Variant* v = SkillGrowth::FindMod(def->id, grow, SkillGrowth::Mod::Lasting);
			if (!v) return;
			SkillRow lr = *row;
			lr.status = v->status;
			lr.statusMs = v->status != StatusId::None ? SkillGrowth::DefaultStatusMs(v->status) : 0;
			lr.statusStacks = 1;
			lr.comboStatus = StatusId::None;
			lr.knockback = 0;
			SkillHit h{};
			h.shape = HitShape::Circle;
			h.a = ev.stage;
			h.coeff = 20;
			h.stagger = 1;
			h.flags = v->status != StatusId::None ? HF_Status : 0;
			const auto lingerTargets = Collect(u, h, ev.x, ev.y, ev.dx, ev.dy, (ev.flags & SKILLREQ_FORCE) != 0);
			if (Trace())
				std::fprintf(stderr, "[skillex] t=%u linger id=%u center=%d,%d r=%u left=%u targets=%zu\n", GetNowTime(), ev.skillId, ev.x, ev.y, ev.stage, ev.extra, lingerTargets.size());
			for (auto& t : lingerTargets)
			{
				HitSpec spec = SpecOf(h, kLingerHit, ev.castSeq, ev.x, ev.y);
				spec.evaluateCombo = false;
				spec.applyKnockback = false;
				DealHit(u, lr, t, spec);
			}
			if (ev.extra > 1)
			{
				User::PendingImpact next = ev;
				next.extra = ev.extra - 1;
				Schedule(u, next, 500);
			}
			return;
		}
		case ExEvent::None:
			return;
		}
	}

	void OnHitDealt(const shared_ptr<User>& u, const SkillRow& row, const shared_ptr<Subject>& target, uint32_t dealt, uint8_t flags2, uint32_t castSeq)
	{
		if (!u || !target || dealt == 0)
			return;
		const uint32_t now = GetNowTime();
		// 고유 기술 게이지: 서약기사 = 때릴수록, 밤사냥꾼 = 뒤를 잡을수록(각인술사는 시전할 때)
		if (u->m_classId == ClassId::Knight && row.id != 24)
			AddIdentity(u, row.slot == 0 ? 10 : 22);
		else if (u->m_classId == ClassId::NightHunter && row.id != 36)
			AddIdentity(u, (flags2 & (DAMAGE2_BACKSTAB | DAMAGE2_BACK)) ? 70 : 9);

		if (!IsMonster(target) || target->GetStat()->IsDead())
			return;
		const auto monster = static_pointer_cast<Monster>(target);
		// 서약 해방: 모든 타격에 금빛 잔향(30%)
		if (u->Status().Has(StatusId::Oath, now) && row.id != 24)
		{
			CombatHelper::HitCtx ctx;
			ctx.skillId = 24;
			ctx.flags = DAMAGE_SKILL;
			ctx.flags2 = DAMAGE2_PROC;
			ctx.hitIndex = 0x80;
			CombatHelper::HitMonster(u, monster, (std::max)(1u, dealt * 30u / 100u), ctx);
		}
		// 먹물 분신: 시전마다 한 번 줄이고, 그 시전의 타격을 분신 둘이 40%씩 따라 벤다
		if (row.id != 32 && !monster->GetStat()->IsDead())
			if (const uint8_t clones = u->Status().Stacks(StatusId::Clone, now); clones > 0)
			{
				if (u->m_skx.lastCloneCast != castSeq)
				{
					u->m_skx.lastCloneCast = castSeq;
					const uint32_t remain = u->Status().Remaining(StatusId::Clone, now);
					RemoveStatus(u, StatusId::Clone);
					if (clones > 1) ApplyStatus(u, StatusId::Clone, remain, static_cast<uint8_t>(clones - 1), u->GetObjID());
					UserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::CloneStrike, monster->GetX(), monster->GetY(), clones - 1);
				}
				// 갈래 「짙은 분신」 40% → 60%, 먹물 분신 레벨(+5%씩)
				const SkillGrowth::State cg = u->GrowState(32);
				const SkillGrowth::Variant* dense = SkillGrowth::FindMod(32, cg, SkillGrowth::Mod::Heavy);
				const uint32_t pct = (dense ? 40u * (100u + dense->value) / 100u : 40u) * SkillGrowth::DamagePercent(cg.level) / 100u;
				for (int k = 0; k < 2 && !monster->GetStat()->IsDead(); ++k)
				{
					CombatHelper::HitCtx ctx;
					ctx.skillId = 32;
					ctx.flags = DAMAGE_SKILL;
					ctx.flags2 = DAMAGE2_PROC;
					ctx.hitIndex = static_cast<uint8_t>(0x80 | k);
					CombatHelper::HitMonster(u, monster, (std::max)(1u, dealt * pct / 100u), ctx);
				}
			}
	}

	void ApplyStagger(const shared_ptr<User>& u, const shared_ptr<Monster>& m, uint8_t amount)
	{
		if (!m || !m->IsBoss() || m->GetStat()->IsDead() || amount == 0)
			return;
		const uint32_t now = GetNowTime();
		if (m->m_staggerMax == 0)
		{
			const int level = m->GetSpecies() ? m->GetSpecies()->level : 10;
			m->m_staggerMax = static_cast<uint16_t>(300 + level * 12);
			m->m_stagger = m->m_staggerMax;
		}
		if (m->Status().Has(StatusId::Broken, now))
			return;
		if (m->m_stagger == 0)
		{
			if (now < m->m_staggerRefillAt) return;
			m->m_stagger = m->m_staggerMax;
		}
		const uint16_t before = m->m_stagger;
		m->m_stagger = static_cast<uint16_t>(m->m_stagger > amount ? m->m_stagger - amount : 0);
		const bool broke = before > 0 && m->m_stagger == 0;
		if (broke)
		{
			ApplyStatus(m, StatusId::Broken, 3000, 1, u->GetObjID());
			m->m_staggerRefillAt = now + 3000;
			if (m->m_pattern.pending) m->m_countered = true;
			UserHelper::BroadcastEffect(m, m->GetObjID(), EffectId::StaggerBreak, m->GetX(), m->GetY(), 0);
		}
		if (broke || now - m->m_staggerSentAt >= 100)
		{
			m->m_staggerSentAt = now;
			MONSTER_GAUGE_NFY_PACKET p{};
			p.size = sizeof(p);
			p.type = static_cast<char>(PacketType::MONSTER_GAUGE_NFY);
			p.monsterId = m->GetObjID();
			p.stagger = static_cast<uint16_t>(static_cast<uint32_t>(m->m_stagger) * 1000u / (std::max<uint16_t>)(1, m->m_staggerMax));
			p.state = broke ? 1 : 0;
			p.brokenMs = broke ? 3000 : 0;
			ToViewers(m, p, false);
		}
	}

	void AddIdentity(const shared_ptr<User>& u, int amount)
	{
		if (!u || !u->IsExtended() || amount <= 0)
			return;
		const uint16_t before = u->m_identity;
		u->m_identity = static_cast<uint16_t>((std::min)(1000, u->m_identity + amount));
		// 가득 차는 순간 · 5% 단위로만 알린다(패킷을 아낀다)
		if (u->m_identity != before && (u->m_identity == 1000 || u->m_identity / 50 != before / 50))
			SendGauge(u, u->m_identity == 1000);
	}

	void SendGauge(const shared_ptr<User>& u, bool force)
	{
		if (!u) return;
		if (!force && u->m_identitySent == u->m_identity) return;
		auto session = u->GetGameSession();
		if (!session || !session->IsExtended()) return;
		u->m_identitySent = u->m_identity;
		const uint32_t now = GetNowTime();
		COMBAT_GAUGE_INF_PACKET p{};
		p.size = sizeof(p);
		p.type = static_cast<char>(PacketType::COMBAT_GAUGE_INF);
		p.identity = u->m_identity;
		uint32_t active = 0;
		if (u->Status().Has(StatusId::Oath, now)) active = u->Status().Remaining(StatusId::Oath, now);
		if (u->Status().Has(StatusId::Veil, now)) active = u->Status().Remaining(StatusId::Veil, now);
		p.flags = static_cast<uint8_t>((u->m_identity >= 1000 ? GAUGE_FULL : 0) | (active > 0 ? GAUGE_ACTIVE : 0));
		p.activeMs = active;
		session->PostSend(p);
	}

	void OnEvaded(const shared_ptr<User>& u, uint32_t now)
	{
		if (!u || u->m_classId != ClassId::NightHunter || u->m_skx.dodgeAt == 0 || now - u->m_skx.dodgeAt > 350)
			return;
		u->m_skx.dodgeAt = 0;
		ApplyStatus(u, StatusId::Ambush, 2500, 1, u->GetObjID());
		UserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::PerfectDodge, u->GetX(), u->GetY(), 0);
		AddIdentity(u, 120);
	}

	void OnMoved(const shared_ptr<User>& u)
	{
		if (!u) return;
		const uint32_t now = GetNowTime();
		Aim a; a.x = u->GetX(); a.y = u->GetY();
		if (u->m_skx.castingUntil > now)
		{
			u->m_skx.castingUntil = 0;
			u->m_skx.castingSeq = 0;
			SKILL_EX_NFY_PACKET p{};
			p.size = sizeof(p);
			p.type = static_cast<char>(PacketType::SKILL_EX_NFY);
			p.casterId = u->GetObjID();
			p.phase = static_cast<uint8_t>(SkillExPhase::Cancel);
			p.fromX = u->GetX(); p.fromY = u->GetY();
			p.aimX = a.x; p.aimY = a.y;
			p.serverTime = now;
			ToViewers(u, p, true);
		}
		if (u->m_skx.chargeSkill != 0)
			if (const SkillDef* d = SkillDefTable::Find(u->m_skx.chargeSkill); d && !d->moveWhileCasting)
			{
				EndCharge(u);
				Notify(u, *d, SkillExPhase::Cancel, 0, 0, a, ObjID{}, 0);
			}
		if (u->m_skx.holdSkill != 0)
			if (const SkillDef* d = SkillDefTable::Find(u->m_skx.holdSkill); d && !d->moveWhileCasting)
				EndHoldWithRelease(u, *d, false);
	}
}
