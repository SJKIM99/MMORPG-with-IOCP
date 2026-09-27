#include "game/Game.h"
#include "game/KeyBinds.h"
#include "game/Texts.h"
#include "game/SkillView.h"
#include "fx/SkillFx.h"
#include "fx/Effects.h"
#include "fx/Impact.h"
#include "fx/FxGrade.h"
#include "fx/FxScale.h"
#include "render/Post.h"
#include "render/Draw.h"
#include "render/Actors.h"
#include "audio/Sfx.h"
#include "audio/Voice.h"
#include "render/Assets.h"
#include "render/Art.h"
#include "rlgl.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

// ─────────────────────────────────────────────────────────────────────────────
// 기술 구조 V3(Phase 2-B) — 클라이언트 쪽: 누름 · 뗌 · 타격 시각표 · 알림 · 피격 반응 · 각성기 연출 · 게이지.
//
//  · 판정은 서버(Combat/SkillEx). 클라는 같은 표(Data/SkillData.h)의 시각에 연출만 낸다 — 내 기술은 누르는 순간
//    예측해서, 남의 기술은 SKILL_EX_NFY를 받는 순간부터. 서버 알림이 다르면(차지 단계 · 콤보 타) 아직 안 나간 타격만 바로잡는다.
//  · 예측 타격(OnExBeat)에서 모양 안에 적이 보이면 섬광 · 소리 · 히트스톱을 바로 준다. 피해 숫자는 서버가 확정할 때.
//  · 이동기(Space)는 모든 기술의 후딜 · 모으기 · 돌기를 끊는다(TryCastSkill).
// ─────────────────────────────────────────────────────────────────────────────

using util::Rgb;

namespace
{
	int Cheb(int ax, int ay, int bx, int by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

	std::string Fmt(const char* f, ...)
	{
		char buf[256];
		va_list ap;
		va_start(ap, f);
		std::vsnprintf(buf, sizeof(buf), f, ap);
		va_end(ap);
		return buf;
	}

	float EntityHeight(const Entity& e)
	{
		if (e.kind == EntityKind::Monster && e.row) return actors::MonsterHeight(*e.row);
		if (e.kind == EntityKind::Item) return 16.f;
		return actors::PlayerHeight();
	}

	void FaceToward(Entity& e, float fromX, float fromY, float tx, float ty)
	{
		const float dx = tx - fromX, dy = ty - fromY;
		const float sx = dx - dy, sy = dx + dy;
		if (std::fabs(sx) > 0.05f) e.faceRight = sx > 0.f;
		if (std::fabs(sy) > 0.05f) e.faceBack = sy < 0.f;
	}

	Vector2 ScreenDir(Vector2 dirTile) { return util::Norm(Vector2{ (dirTile.x - dirTile.y) * gfx::kHalfW, (dirTile.x + dirTile.y) * gfx::kHalfH }); }

	// 서버 방향(화면 각 0~255) → 칸 방향
	Vector2 TileDirFrom256(uint8_t d)
	{
		const float a = d / 256.f * 2.f * util::kPi;
		const float c = std::cos(a), s = std::sin(a);
		return util::Norm(Vector2{ (c + 2.f * s) * 0.5f, (2.f * s - c) * 0.5f });
	}

	uint64_t Key(const ObjID& id) { return (static_cast<uint64_t>(id.GetSerial()) << 20) ^ static_cast<uint64_t>(id.GetDatabaseID()); }

	// 서버 ScheduleHits와 같은 거르기
	bool RowActive(const SkillDef& d, int i, uint8_t stage, uint8_t flags, bool releaseOnly)
	{
		const SkillHit& h = d.hits[i];
		if (h.flags & HF_Tick) return false;
		if (releaseOnly != ((h.flags & HF_Release) != 0)) return false;
		if ((h.flags & HF_Perfect) && !(flags & SKILLEX_PERFECT)) return false;
		switch (d.kind)
		{
		case SkillKind::Basic:
		case SkillKind::Chain:
			if (stage != 0 && h.stage != stage) return false;
			break;
		case SkillKind::Charge:
			if (h.stage != 0 && h.stage > stage) return false;
			break;
		default:
			break;
		}
		return true;
	}

	const char* GaugeName(ClassId c)
	{
		switch (c)
		{
		case ClassId::Knight: return "서약";
		case ClassId::Engraver: return "새김";
		default: return "그늘";
		}
	}

	Color GaugeColor(ClassId c)
	{
		switch (c)
		{
		case ClassId::Knight: return ui::theme::kDawnHi;
		case ClassId::Engraver: return Rgb(0x7FD0C4);
		default: return Rgb(0xC8D2E0);
		}
	}
}

bool Game::SkillTrace()
{
	static const bool on = std::getenv("HARVEN_SKILL_TRACE") != nullptr;
	return on;
}

// ─────────────────────────────────────────────────────────────────────────────
// 연출 문맥 · 예측 대상
// ─────────────────────────────────────────────────────────────────────────────
int Game::CountInShape(const SkillHit& h, Vector2 center, Vector2 dirTile) const
{
	int n = 0;
	for (const auto& [id, e] : m_ents)
	{
		if (e.kind != EntityKind::Monster || e.dead || e.removing) continue;
		if (SkillDefTable::InsideHit(h, center.x, center.y, dirTile.x, dirTile.y, static_cast<float>(e.tx), static_cast<float>(e.ty)))
			++n;
	}
	return n;
}

skillfx::ExCtx Game::MakeExCtx(const Entity& caster, uint16_t skillId, uint8_t phase, uint8_t stage, uint8_t flags, Vector2 originTile,
	Vector2 aimTile, Vector2 dirTile, const Entity* target, bool mine)
{
	skillfx::ExCtx c;
	// Phase 2-D: 켜진 갈래(비트 1~4) — 내 기술은 내 상태로, 남의 기술은 알림의 flags 그대로
	if (mine) flags = static_cast<uint8_t>((flags & ~SKILLEX_VARIANT_MASK) | MyVarBits(skillId));
	c.skillId = skillId;
	c.phase = phase;
	c.stage = stage;
	c.flags = flags;
	c.casterIso = TileToIso(caster.pos);
	c.aimIso = TileToIso(aimTile);
	c.targetIso = target ? TileToIso(target->pos) : c.aimIso;
	c.dirTile = util::Len(dirTile) > 0.01f ? util::Norm(dirTile) : Vector2{ 1.f, 0.f };
	c.dir = ScreenDir(c.dirTile);
	c.casterHeight = actors::PlayerHeight();
	c.targetHeight = target ? EntityHeight(*target) : 40.f;
	c.faceRight = caster.faceRight;
	c.mine = mine;
	c.classId = caster.kind == EntityKind::Player ? caster.classId : 0;
	const SkillDef* def = skillview::Def(skillId, flags);
	const SkillRow* row = skillview::Row(skillId, flags);
	c.castSec = def ? def->castMs / 1000.f : 0.f;
	c.landIso = c.aimIso;

	// 기술 이동의 착지(표의 이동량 — 막히면 그 앞)
	if (row && def && def->moveMs > 0 && row->dash > 0)
	{
		int x = static_cast<int>(std::lround(originTile.x)), y = static_cast<int>(std::lround(originTile.y));
		const int sdx = static_cast<int>(std::lround(c.dirTile.x)), sdy = static_cast<int>(std::lround(c.dirTile.y));
		for (int i = 0; i < row->dash && (sdx != 0 || sdy != 0); ++i)
		{
			const int nx = x + sdx, ny = y + sdy;
			if (WorldData::IsBlocked(nx, ny) || (skillId == 35 && WorldData::IsSafeZone(nx, ny))) break;
			x = nx; y = ny;
		}
		c.landIso = gfx::IsoPx(static_cast<float>(x), static_cast<float>(y));
	}

	// 예측 대상(연출용) — 서버와 같은 모양 · 가까운 순서
	auto push = [&](const Entity& e)
	{
		if (c.nPts >= 8) return;
		c.pts[c.nPts] = TileToIso(e.pos);
		c.ptsZ[c.nPts] = EntityHeight(e) * 0.5f;
		++c.nPts;
	};
	auto enemiesIn = [&](const SkillHit& h, Vector2 center, int maxN)
	{
		std::vector<std::pair<float, const Entity*>> list;
		for (const auto& [id, e] : m_ents)
		{
			if (e.kind != EntityKind::Monster || e.dead || e.removing) continue;
			if (!SkillDefTable::InsideHit(h, center.x, center.y, c.dirTile.x, c.dirTile.y, static_cast<float>(e.tx), static_cast<float>(e.ty))) continue;
			const float dx = e.tx - center.x, dy = e.ty - center.y;
			list.emplace_back(dx * dx + dy * dy, &e);
		}
		std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		for (int i = 0; i < static_cast<int>(list.size()) && i < maxN; ++i) push(*list[static_cast<size_t>(i)].second);
	};
	if (!def) return c;
	switch (def->special)
	{
	case SkillSpecial::DuskAwaken:
		if (def->hitCount > 0) enemiesIn(def->hits[0], originTile, 7);
		break;
	case SkillSpecial::MoonCleave:
		if (def->hitCount > 0) enemiesIn(def->hits[0], originTile, 8);
		break;
	case SkillSpecial::ShieldBounce:
	{
		auto& marks = m_bounceMarks[Key(caster.id)];
		if (stage <= 1)
		{
			marks.clear();
			const Entity* cur = target;
			// 갈래 「다섯 번 튕김」: 서버와 같은 수만큼 예측한다
			const auto* more = skillview::Mod(skillId, flags, SkillGrowth::Mod::Extra);
			const size_t bounces = 3u + (more ? more->value : 0u);
			while (cur && marks.size() < bounces)
			{
				marks.push_back(cur->id);
				push(*cur);
				const Entity* next = nullptr;
				int best = 99;
				for (const auto& [id, e] : m_ents)
				{
					if (e.kind != EntityKind::Monster || e.dead || e.removing) continue;
					if (std::find(marks.begin(), marks.end(), e.id) != marks.end()) continue;
					const int dd = Cheb(cur->tx, cur->ty, e.tx, e.ty);
					if (dd <= (row ? row->radius : 3) && dd < best) { best = dd; next = &e; }
				}
				cur = next;
			}
		}
		else
			for (const ObjID& id : marks)
				if (const auto it = m_ents.find(id); it != m_ents.end() && !it->second.dead) push(it->second);
		break;
	}
	default:
		// 투사체 · 창: 직선 위 첫 적
		if (skillId == 7 && def->hitCount > 0)
			enemiesIn(def->hits[0], originTile, 1);
		break;
	}
	return c;
}

void Game::StartCastTimeline(const skillfx::ExCtx& ctx, const ObjID& caster, Vector2 originTile, Vector2 aimTile, Vector2 dirTile,
	const ObjID& target, bool releaseOnly, double t0)
{
	const SkillDef* def = skillview::Def(ctx.skillId, ctx.flags);
	if (!def) return;
	CastTimeline t;
	t.caster = caster;
	t.skillId = ctx.skillId;
	t.stage = ctx.stage;
	t.flags = ctx.flags;
	t.origin = originTile;
	t.aim = aimTile;
	t.dir = util::Len(dirTile) > 0.01f ? util::Norm(dirTile) : Vector2{ 1.f, 0.f };
	t.target = target;
	t.t0 = t0;
	t.releaseOnly = releaseOnly;
	t.mine = ctx.mine;
	t.ctx = ctx;
	if (def->kind == SkillKind::Hold && !releaseOnly)
	{
		t.holdUntil = t0 + def->holdMaxMs / 1000.0;
		t.nextTick = t0 + SkillDefTable::kHoldFirstTickMs / 1000.0;   // 첫 0.12초 = 예고(서버와 같은 값)
	}
	// 타격 행이 하나도 없고 틱도 없으면 담을 필요가 없다(단죄 도약 같은 시각 연출은 각성기만)
	bool any = t.holdUntil > t0 || def->kind == SkillKind::Awaken;
	for (int i = 0; i < def->hitCount && !any; ++i)
		any = RowActive(*def, i, t.stage, t.flags, releaseOnly);
	if (!any) return;
	if (m_timelines.size() > 48) m_timelines.erase(m_timelines.begin());
	m_timelines.push_back(t);
}

// ─────────────────────────────────────────────────────────────────────────────
// 놓기 · 매 프레임
// ─────────────────────────────────────────────────────────────────────────────
void Game::ReleaseHeldSkill(bool cancel)
{
	if (m_heldSkill == 0) return;
	if (SkillTrace())
		std::fprintf(stderr, "[skill] t=%.3f release id=%u held=%.3f cancel=%d\n", m_now, m_heldSkill, m_now - m_heldAt, cancel ? 1 : 0);
	const uint16_t id = m_heldSkill;
	const SkillDef* def = MyDef(id);
	const SkillRow* row = SkillTable::Find(id);
	m_heldSkill = 0;
	m_heldHot = -1;
	Entity* me = Self();
	if (!def || !row || !me) return;

	int tx = m_aim.valid ? m_aim.tx : m_predX, ty = m_aim.valid ? m_aim.ty : m_predY;
	ObjID targetId{};
	Entity* target = nullptr;
	if (row->target == SkillTarget::Direction)
		if (Entity* t = AimEnemy(row); t && Cheb(m_predX, m_predY, t->tx, t->ty) <= row->range + 2) { target = t; targetId = t->id; }
	const uint16_t seq = ++m_castSeq;
	const bool force = kb::Down(kb::Act::ForceAttack);
	m_net.SendSkillEx(id, cancel ? SkillPhase::Cancel : SkillPhase::Release, force ? SKILLREQ_FORCE : 0, targetId, tx, ty, m_aim.dir256, seq);

	const Vector2 origin{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
	Vector2 aimTile{ static_cast<float>(tx), static_cast<float>(ty) };
	if (target) aimTile = target->pos;
	Vector2 dirTile = util::Sub(aimTile, origin);
	if (util::Len(dirTile) < 0.1f) dirTile = m_aim.dir;
	dirTile = util::Norm(dirTile);

	if (cancel)
	{
		m_timelines.erase(std::remove_if(m_timelines.begin(), m_timelines.end(), [&](const CastTimeline& t) { return t.mine && t.skillId == id; }), m_timelines.end());
		m_actionLockUntil = m_now;
		me->animT = -1.f; me->animSkill = 0;
		return;
	}

	const uint32_t heldMs = static_cast<uint32_t>(std::max(0.0, (m_now - m_heldAt) * 1000.0));
	if (def->kind == SkillKind::Charge)
	{
		const uint8_t stage = SkillDefTable::ChargeStage(*def, heldMs);
		const bool perfect = SkillDefTable::ChargePerfect(*def, heldMs, 40);
		FaceToward(*me, origin.x, origin.y, aimTile.x, aimTile.y);
		skillfx::ExCtx c = MakeExCtx(*me, id, static_cast<uint8_t>(SkillExPhase::ChargeRelease), stage, perfect ? SKILLEX_PERFECT : 0, origin, aimTile, dirTile, target, true);
		skillfx::OnExPhase(c);
		StartCastTimeline(c, m_selfId, origin, aimTile, dirTile, targetId, false, m_now);
		me->animSkill = id; me->animT = 0.f; me->animDur = std::max(0.35f, def->lockMs / 1000.f); me->animCast = 0.f;
		m_actionLockUntil = std::max(m_actionLockUntil, m_now + def->lockMs / 1000.0);
		if (def->moveMs > 0)
		{
			m_expectDashSkill = id;
			m_expectDashUntil = m_now + 0.8;
			m_path.clear();
		}
		const float kick = 0.02f + 0.01f * stage + (perfect ? 0.02f : 0.f);
		m_cam.Kick(kick);
		m_cam.AddTrauma(0.1f + 0.06f * stage);
		if (perfect)
		{
			fx::Fx().FloatText(TileToIso(me->pos), actors::PlayerHeight() + 26.f, "완벽", Rgb(0xFFF0C8), 22.f, 0.9f, ui::Face::Display);
			m_feelStop = std::max(m_feelStop, 0.05f);
		}
	}
	else
	{
		// 홀딩을 놓았다: 틱을 멈추고 마무리 타격
		for (auto& t : m_timelines)
			if (t.mine && t.skillId == id && !t.releaseOnly && t.holdUntil > m_now) t.holdUntil = m_now;
		skillfx::ExCtx c = MakeExCtx(*me, id, static_cast<uint8_t>(SkillExPhase::HoldEnd), 0, 0, origin, aimTile, dirTile, target, true);
		skillfx::OnExPhase(c);
		StartCastTimeline(c, m_selfId, origin, aimTile, dirTile, ObjID{}, true, m_now);
		m_actionLockUntil = std::max(m_actionLockUntil, m_now + 0.18);
	}
}

void Game::TickSkills(float dt)
{
	Entity* me = Self();
	if (me) skillfx::SetListener(TileToIso(me->pos));

	// ── 모으기 · 돌기(내 것): 키를 떼거나 시간이 차면 놓는다 ──
	if (m_heldSkill != 0)
	{
		const SkillDef* def = MyDef(m_heldSkill);
		const double held = m_now - m_heldAt;
		const bool down = m_heldHot >= 0 && kb::Down(kb::HotAct(m_heldHot));
		const double maxSec = !def ? 0.0 : (def->kind == SkillKind::Charge ? SkillDefTable::ChargeMaxMs(*def) / 1000.0 : def->holdMaxMs / 1000.0);
		if (!def || !me || m_dead)
		{
			m_heldSkill = 0;
			m_heldHot = -1;
		}
		else if (!down || held >= maxSec)
			ReleaseHeldSkill(false);
		else
		{
			if (!def->moveWhileCasting)
				m_actionLockUntil = std::max(m_actionLockUntil, m_now + 0.08);   // 모으는 동안은 서 있는다(서버: 걸으면 끊김)
			const uint32_t heldMs = static_cast<uint32_t>(held * 1000.0);
			const uint8_t stage = def->kind == SkillKind::Charge ? SkillDefTable::ChargeStage(*def, heldMs) : 0;
			const bool perfect = def->kind == SkillKind::Charge && def->perfectMs > 0 && heldMs >= static_cast<uint32_t>(def->stageMs) * def->stages;
			const Vector2 origin{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
			Vector2 aimTile{ static_cast<float>(m_aim.valid ? m_aim.tx : m_predX), static_cast<float>(m_aim.valid ? m_aim.ty : m_predY) };
			Vector2 dirTile = util::Sub(aimTile, origin);
			if (util::Len(dirTile) < 0.1f) dirTile = m_aim.dir;
			if (def->kind == SkillKind::Charge || !def->moveWhileCasting)
				FaceToward(*me, origin.x, origin.y, origin.x + dirTile.x, origin.y + dirTile.y);
			skillfx::ExCtx c = MakeExCtx(*me, m_heldSkill, 0, stage, 0, origin, aimTile, dirTile, nullptr, true);
			skillfx::OnExHeld(c, static_cast<float>(held), stage, perfect, dt);
			me->castT = def->kind == SkillKind::Charge ? std::min(1.f, static_cast<float>(held / std::max(0.1, maxSec))) : me->castT;
		}
	}
	if (m_chainSkill != 0 && m_now > m_chainUntil) m_chainSkill = 0;

	// ── 타격 시각표 ──
	for (auto& t : m_timelines)
	{
		const SkillDef* def = skillview::Def(t.skillId, t.flags);
		if (!def) continue;
		Entity* caster = Find(t.caster);
		if (caster)
		{
			t.ctx.casterIso = TileToIso(caster->pos);
			t.ctx.faceRight = caster->faceRight;
		}
		const Vector2 casterTile = t.mine ? Vector2{ static_cast<float>(m_predX), static_cast<float>(m_predY) }
			: (caster ? Vector2{ static_cast<float>(caster->tx), static_cast<float>(caster->ty) } : t.origin);

		// 남의 차지: 놓을 때까지 모으는 연출
		if (t.charging)
		{
			const float held = static_cast<float>(m_now - t.t0);
			const uint8_t stage = SkillDefTable::ChargeStage(*def, static_cast<uint32_t>(held * 1000.f));
			skillfx::OnExHeld(t.ctx, held, stage, false, dt);
			continue;
		}
		// 남의 홀딩: 도는 연출
		if (!t.mine && def->kind == SkillKind::Hold && !t.releaseOnly && m_now < t.holdUntil)
			skillfx::OnExHeld(t.ctx, static_cast<float>(m_now - t.t0), 0, false, dt);

		// 홀딩 틱
		while (t.holdUntil > t.t0 && t.nextTick <= std::min(m_now, t.holdUntil) && def->tickMs > 0)
		{
			for (int i = 0; i < def->hitCount; ++i)
			{
				const SkillHit& h = def->hits[i];
				if (!(h.flags & HF_Tick)) continue;
				const Vector2 center = h.anchor == HitAnchor::Caster ? casterTile : t.aim;
				Vector2 hdir = t.dir;
				SkillDefTable::TurnDir(h, hdir.x, hdir.y);
				const int n = CountInShape(h, center, hdir);
				skillfx::OnExBeat(t.ctx, i, TileToIso(center), n);
				if (SkillTrace() && t.mine)
					std::fprintf(stderr, "[skill] t=%.3f beat id=%u hit=%d stage=0 targets=%d f=%llu tick\n", m_now, t.skillId, i, n, static_cast<unsigned long long>(m_frameNo));
				if (t.mine && n > 0)
				{
					m_feelStop = std::max(m_feelStop, 0.02f);
					m_lastBeatAt = m_now;
					m_lastBeatSkill = t.skillId;
				}
			}
			t.nextTick += def->tickMs / 1000.0;
		}

		// 표의 타격 — 이 시전의 마지막 타격(덧붙은 것 빼고)에 갈래 「장판」이 남는다(서버 ScheduleHits의 kEvLast와 같다)
		int lastIdx = -1;
		for (int j = 0; j < def->hitCount; ++j)
			if (RowActive(*def, j, t.stage, t.flags, t.releaseOnly) && def->hits[j].tag == HT_None && (lastIdx < 0 || def->hits[j].atMs >= def->hits[lastIdx].atMs)) lastIdx = j;
		for (int i = 0; i < def->hitCount; ++i)
		{
			if (t.fired & (1u << i)) continue;
			if (!RowActive(*def, i, t.stage, t.flags, t.releaseOnly)) continue;
			const SkillHit& h = def->hits[i];
			if (m_now < t.t0 + h.atMs / 1000.0) continue;
			t.fired |= 1u << i;
			Vector2 center = t.aim;
			switch (h.anchor)
			{
			case HitAnchor::Caster: center = casterTile; break;
			case HitAnchor::Aim: center = t.aim; break;
			case HitAnchor::Target:
				if (Entity* te = Find(t.target)) center = Vector2{ static_cast<float>(te->tx), static_cast<float>(te->ty) };
				break;
			}
			// 세 줄기(갈래)로 덧붙은 타격은 방향을 돌린다(서버 ApplyHitRow와 같다)
			Vector2 hdir = t.dir;
			SkillDefTable::TurnDir(h, hdir.x, hdir.y);
			if (h.along != 0)
				center = Vector2{ center.x + std::round(hdir.x * h.along), center.y + std::round(hdir.y * h.along) };
			int n = CountInShape(h, center, hdir);
			if (def->special == SkillSpecial::DuskAwaken || def->special == SkillSpecial::MoonCleave || def->special == SkillSpecial::ShieldBounce)
				n = t.ctx.nPts;
			if (def->special == SkillSpecial::MoonCleave)
				t.ctx.casterIso = TileToIso(t.origin);   // 일섬: 지나온 선 그대로
			skillfx::OnExBeat(t.ctx, i, TileToIso(center), n);
			if (i == lastIdx)
				if (const auto* lv = skillview::Mod(t.skillId, t.flags, SkillGrowth::Mod::Lasting); lv && def->special != SkillSpecial::Dash && def->special != SkillSpecial::ShadowStep)
				{
					fxscale::Scope scope(t.mine);
					const float r = h.shape == HitShape::Target ? 1.f : std::clamp<float>(h.a, 1.f, 4.f);
					skillfx::Linger(TileToIso(center), r, static_cast<float>(lv->value), lv->color, lv->status);
				}
			if (SkillTrace() && t.mine)
				std::fprintf(stderr, "[skill] t=%.3f beat id=%u hit=%d stage=%u targets=%d f=%llu\n", m_now, t.skillId, i, t.stage, n, static_cast<unsigned long long>(m_frameNo));
			// 내 기술: 모양 안에 적이 보이면 히트스톱 · 흔들림을 예측으로(피해 숫자는 서버가 확정할 때)
			if (t.mine && n > 0)
			{
				// 주 타격(가장 센 줄)만 기술 등급, 나머지는 강타 T3 · 보통 T2까지(GameCombat 확정 타격과 같은 규칙) — 불꽃 각인의 약한 마지막 틱이 본 폭발보다 세게 멈추던 것
				const int src = h.tag != HT_None && h.src < def->hitCount ? h.src : i;
				int ti = src == SkillGrowth::MainHit(*def) ? def->tier : std::min<int>(def->tier, (h.flags & HF_Heavy) ? 3 : 2);
				if (h.tag != HT_None) ti = std::min(ti, 2);
				const impact::Feel f = impact::FeelOf(static_cast<fxgrade::Tier>(std::clamp(ti, 1, 4)), false, false, false, n);
				m_feelStop = std::max(m_feelStop, f.hitstop);
				m_feelTrauma = std::max(m_feelTrauma, f.trauma);
				m_feelKick = std::max(m_feelKick, f.kick);
				m_lastBeatAt = m_now;
				m_lastBeatSkill = t.skillId;
			}
		}

		// 단죄: 0.6초에 뛰어오른다(그림자는 땅에)
		if (t.skillId == 6 && !t.leapDone && m_now - t.t0 >= 0.6 && caster)
		{
			t.leapDone = true;
			caster->airT = 0.f;
			caster->airDur = 0.78f;
			caster->airH = 84.f;
		}
		// 그믐 베기: 0.4~1.2초에 대상 사이를 순간이동해 보인다
		if (t.skillId == 18 && caster && t.ctx.nPts > 0)
		{
			const double e = m_now - t.t0;
			if (e >= 0.4 && e < 1.3)
			{
				const int n = t.ctx.nPts;
				const int i = std::clamp(static_cast<int>((e - 0.4) / 0.8 * n), 0, n - 1);
				caster->visIso = util::Add(t.ctx.pts[i], Vector2{ (i % 2 ? -1.f : 1.f) * 22.f, 6.f });
				caster->visUntil = m_now + 0.05;
			}
		}
	}
	// 끝난 시각표 정리
	m_timelines.erase(std::remove_if(m_timelines.begin(), m_timelines.end(), [&](const CastTimeline& t)
	{
		if (m_now - t.t0 > 6.0) return true;
		if (t.charging || m_now < t.holdUntil) return false;
		const SkillDef* d = skillview::Def(t.skillId, t.flags);
		if (!d) return true;
		if (t.skillId == 6 && !t.leapDone) return false;
		if (t.skillId == 18 && m_now - t.t0 < 1.3) return false;
		for (int i = 0; i < d->hitCount; ++i)
			if (!(t.fired & (1u << i)) && RowActive(*d, i, t.stage, t.flags, t.releaseOnly)) return false;
		return true;
	}), m_timelines.end());

	// ── 각성기 총 피해: 마지막 타 0.45초 뒤 큰 숫자 ──
	if (m_awakeTotal.hits > 1 && m_now - m_awakeTotal.lastAt > 0.45)
	{
		fx::Fx().TotalDamage(m_awakeTotal.iso, 70.f, m_awakeTotal.sum);
		sfx::Play(sfx::Id::ComboSting, 0.8f, 0.01f);
		m_awakeTotal = AwakeTotal{};
	}
	else if (m_awakeTotal.hits <= 1 && m_awakeTotal.skillId != 0 && m_now - m_awakeTotal.lastAt > 1.0)
		m_awakeTotal = AwakeTotal{};
}

// ─────────────────────────────────────────────────────────────────────────────
// 서버 알림
// ─────────────────────────────────────────────────────────────────────────────
void Game::HandleSkillEx(const SKILL_EX_NFY_PACKET& p)
{
	const auto phase = static_cast<SkillExPhase>(p.phase);
	const bool self = p.casterId == m_selfId;
	if (SkillTrace())
		std::fprintf(stderr, "[skill] t=%.3f nfy %s id=%u phase=%u stage=%u flags=%u aim=%d,%d\n", m_now, self ? "self" : "other", p.skillId, p.phase, p.stage, p.flags, p.aimX, p.aimY);
	Entity* c = Find(p.casterId);

	if (phase == SkillExPhase::Cancel)
	{
		// 캐스팅 · 차지 · 홀딩이 끊겼다(skillId 0 = 캐스팅 기술 무엇이든)
		m_timelines.erase(std::remove_if(m_timelines.begin(), m_timelines.end(), [&](const CastTimeline& t)
		{
			if (t.caster != p.casterId) return false;
			const SkillDef* d = skillview::Def(t.skillId, t.flags);
			if (!d) return true;
			if (p.skillId != 0) return t.skillId == p.skillId;
			return d->kind == SkillKind::Cast && m_now - t.t0 < d->castMs / 1000.0 + 0.2;
		}), m_timelines.end());
		if (self)
		{
			if (m_heldSkill != 0 && (p.skillId == 0 || p.skillId == m_heldSkill)) { m_heldSkill = 0; m_heldHot = -1; }
			m_actionLockUntil = m_now;
			if (c) { c->animT = -1.f; c->animSkill = 0; }
			AddCenter("시전이 끊겼습니다.", ui::theme::kText);
		}
		if (c)
		{
			skillfx::ExCtx x;
			x.skillId = p.skillId; x.phase = p.phase; x.mine = self;
			x.casterIso = TileToIso(c->pos);
			skillfx::OnExPhase(x);
		}
		return;
	}

	const SkillRow* row = SkillTable::Find(p.skillId);
	const SkillDef* def = skillview::Def(p.skillId, p.flags);
	if (!row || !def)
		return;

	if (self)
	{
		// 내 예측을 서버 알림으로 바로잡는다(아직 안 나간 타격만)
		auto mineRecent = [&](bool releaseOnly) -> CastTimeline*
		{
			for (auto it = m_timelines.rbegin(); it != m_timelines.rend(); ++it)
				if (it->mine && it->skillId == p.skillId && it->releaseOnly == releaseOnly && m_now - it->t0 < 0.8) return &*it;
			return nullptr;
		};
		switch (phase)
		{
		case SkillExPhase::Start:
			if (def->kind == SkillKind::Basic)
			{
				if (CastTimeline* t = mineRecent(false); t && t->stage != p.stage && t->fired == 0) { t->stage = p.stage; t->ctx.stage = p.stage; }
				m_comboStage = p.stage;
			}
			break;
		case SkillExPhase::ChargeRelease:
			if (CastTimeline* t = mineRecent(false))
			{
				if ((p.flags & SKILLEX_PERFECT) && !(t->flags & SKILLEX_PERFECT))
					sfx::Play(sfx::Id::Perfect, 0.7f, 0.01f);
				t->stage = p.stage; t->flags = p.flags; t->ctx.stage = p.stage; t->ctx.flags = p.flags;
			}
			else if (m_heldSkill == p.skillId)
			{
				// 서버가 먼저 놓았다(최대 시간) — 지금부터 연출
				m_heldSkill = 0; m_heldHot = -1;
				if (Entity* me = Self())
				{
					const Vector2 origin{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
					const Vector2 aim{ static_cast<float>(p.aimX), static_cast<float>(p.aimY) };
					const Vector2 dir = TileDirFrom256(p.dir);
					skillfx::ExCtx x = MakeExCtx(*me, p.skillId, p.phase, p.stage, p.flags, origin, aim, dir, nullptr, true);
					skillfx::OnExPhase(x);
					StartCastTimeline(x, m_selfId, origin, aim, dir, ObjID{}, false, m_now);
				}
			}
			break;
		case SkillExPhase::HoldEnd:
			if (m_heldSkill == p.skillId)
			{
				// 서버가 먼저 끝냈다(최대 시간 · 기절)
				m_heldSkill = 0; m_heldHot = -1;
				for (auto& t : m_timelines)
					if (t.mine && t.skillId == p.skillId && !t.releaseOnly && t.holdUntil > m_now) t.holdUntil = m_now;
			}
			break;
		case SkillExPhase::ToggleOff:
			if (m_toggleSkill == p.skillId)
			{
				// 서버가 껐다(최대 시간 · MP) — 재사용 대기는 지금부터
				m_toggleSkill = 0;
				const int k = CdIndex(p.skillId);
				const double cd = SkillDefTable::CooldownMs(*def, *row, m_stats.cooldownReduction) / 1000.0;
				m_skillReady[k] = m_now + cd;
				m_skillCdTotal[k] = cd;
			}
			break;
		case SkillExPhase::ChainStep:
			if (def->kind == SkillKind::Chain) m_chainStage = std::max(m_chainStage, p.stage);
			break;
		default:
			break;
		}
		return;
	}

	// ── 남의 시전: 같은 시각표로 연출(세기는 fxscale 설정) ──
	if (!c)
		return;
	const Vector2 from{ static_cast<float>(p.fromX), static_cast<float>(p.fromY) };
	const Vector2 aim{ static_cast<float>(p.aimX), static_cast<float>(p.aimY) };
	Vector2 dir = TileDirFrom256(p.dir);
	Entity* target = Find(p.targetId);
	if (row->target != SkillTarget::Self)
		FaceToward(*c, c->pos.x, c->pos.y, c->pos.x + dir.x, c->pos.y + dir.y);
	skillfx::ExCtx x = MakeExCtx(*c, p.skillId, p.phase, p.stage, p.flags, from, target ? target->pos : aim, dir, target, false);
	x.castSec = p.castMs / 1000.f;

	switch (phase)
	{
	case SkillExPhase::ChargeBegin:
	{
		c->animSkill = p.skillId; c->animT = 0.f; c->animDur = SkillDefTable::ChargeMaxMs(*def) / 1000.f + 0.3f; c->animCast = c->animDur;
		skillfx::OnExPhase(x);
		CastTimeline t;
		t.caster = p.casterId; t.skillId = p.skillId; t.origin = from; t.aim = aim; t.dir = dir; t.target = p.targetId;
		t.t0 = m_now; t.charging = true; t.ctx = x;
		m_timelines.push_back(t);
		return;
	}
	case SkillExPhase::ChargeRelease:
	{
		m_timelines.erase(std::remove_if(m_timelines.begin(), m_timelines.end(), [&](const CastTimeline& t) { return t.caster == p.casterId && t.skillId == p.skillId && t.charging; }), m_timelines.end());
		c->animSkill = p.skillId; c->animT = 0.f; c->animDur = std::max(0.35f, def->lockMs / 1000.f); c->animCast = 0.f;
		skillfx::OnExPhase(x);
		StartCastTimeline(x, p.casterId, from, aim, dir, p.targetId, false, m_now);
		return;
	}
	case SkillExPhase::HoldEnd:
		for (auto& t : m_timelines)
			if (t.caster == p.casterId && t.skillId == p.skillId && !t.releaseOnly && t.holdUntil > m_now) t.holdUntil = m_now;
		skillfx::OnExPhase(x);
		StartCastTimeline(x, p.casterId, from, aim, dir, ObjID{}, true, m_now);
		return;
	case SkillExPhase::ToggleOff:
		skillfx::OnExPhase(x);
		return;
	default:
		break;
	}
	// 시작 · 체인 단 · 홀딩 시작 · 토글 켬 · 고유 기술
	if (def->kind == SkillKind::Basic)
	{
		c->attackT = 0.f;
		c->animT = -1.f;
	}
	else
	{
		c->animSkill = p.skillId;
		c->animT = 0.f;
		c->animDur = std::max(skillfx::AnimDuration(p.skillId), def->lockMs / 1000.f);
		if (skillview::Mod(p.skillId, p.flags, SkillGrowth::Mod::Swift)) c->animDur *= 0.75f;
		c->animCast = p.castMs / 1000.f;
		c->attackT = -1.f;
	}
	const float travel = skillfx::OnExPhase(x);
	if (travel > 0.f && target)
		target->projArriveAt = m_now + travel;
	if (def->kind != SkillKind::Toggle)
		StartCastTimeline(x, p.casterId, from, target ? target->pos : aim, dir, p.targetId, false, m_now);
	if (const int snd = CastSound(p.skillId); snd != static_cast<int>(sfx::Id::Count))
		PlayAt(snd, c->pos, def->kind == SkillKind::Basic ? 0.45f : 0.7f, 0.08f);
	if (def->kind == SkillKind::Awaken)
		voice::Play("pc_" + std::to_string(static_cast<int>(c->classId)) + "_ult", 0.55f, 2);
}

void Game::HandleForcedMove(const SUBJECT_FORCED_MOVE_NFY_PACKET& p)
{
	const auto kind = static_cast<ForcedMoveKind>(p.kind);
	const float dur = p.durationMs / 1000.f;
	const Vector2 fromTile{ static_cast<float>(p.fromX), static_cast<float>(p.fromY) };
	const Vector2 toTile{ static_cast<float>(p.toX), static_cast<float>(p.toY) };
	const Vector2 a = TileToIso(fromTile), b = TileToIso(toTile);
	const bool self = p.id == m_selfId;
	Entity* e = Find(p.id);
	if (self)
	{
		// 기술 이동의 착지(SUBJECT_MOVE_NFY가 먼저 왔으면 이미 미끄러지는 중)
		if (m_mover.SrvX() != p.toX || m_mover.SrvY() != p.toY)
		{
			m_mover.Dash(p.toX, p.toY, m_now, dur);
			SyncMover();
		}
		m_expectDashSkill = 0;
		m_dashWaitSeq = 0;
		m_actionLockUntil = std::max(m_actionLockUntil, m_now + dur * 0.85);
	}
	else if (e)
	{
		if (kind == ForcedMoveKind::Blink || dur <= 0.f)
			motion::RemoteSnap(*e, toTile);
		else
			motion::RemoteStep(*e, p.toX, p.toY, dur, true);
		e->tx = p.toX;
		e->ty = p.toY;
		if (kind == ForcedMoveKind::Knockback || kind == ForcedMoveKind::Pull)
			e->slideUntil = m_now + dur;
	}
	fxscale::Scope scope(self);
	switch (kind)
	{
	case ForcedMoveKind::Dash:
	{
		for (int i = 0; i < 6; ++i)
			fx::Fx().Dust(util::LerpV(a, b, i / 5.f), 0.6f);
		break;
	}
	case ForcedMoveKind::Roll:
		if (e) { e->airT = 0.f; e->airDur = std::max(0.2f, dur); e->airH = 12.f; }
		for (int i = 0; i < 4; ++i) fx::Fx().Dust(util::LerpV(a, b, i / 3.f), 0.8f);
		break;
	case ForcedMoveKind::Blink:
		if (self) { post::Warp(a, 70.f, 0.6f, 0.3f); post::Warp(b, 70.f, -0.6f, 0.35f); }
		break;
	case ForcedMoveKind::Pull:
		fx::Fx().Dust(b, 1.f);
		break;
	case ForcedMoveKind::Launch:
		if (e) { e->airT = 0.f; e->airDur = 0.6f; e->airH = 48.f; }
		break;
	default:
		break;
	}
}

void Game::HandleCombatGauge(const COMBAT_GAUGE_INF_PACKET& p)
{
	if (SkillTrace())
		std::fprintf(stderr, "[skill] t=%.3f gauge %u flags=%u active=%u\n", m_now, p.identity, p.flags, p.activeMs);
	const bool wasFull = m_identityFull;
	m_identity = p.identity;
	m_identityFull = (p.flags & GAUGE_FULL) != 0;
	m_identityActiveUntil = (p.flags & GAUGE_ACTIVE) ? m_now + p.activeMs / 1000.0 : 0.0;
	if (m_identityFull && !wasFull)
	{
		m_identityFullAt = m_now;
		sfx::Play(sfx::Id::SkillReady, 0.8f, 0.01f);
		AddCenter(Fmt("%s이 가득 찼습니다 — %s", GaugeName(m_classId), kb::Label(kb::HotAct(kHotIdentity)).c_str()), GaugeColor(m_classId));
	}
}

void Game::HandleMonsterGauge(const MONSTER_GAUGE_NFY_PACKET& p)
{
	Entity* m = Find(p.monsterId);
	if (!m) return;
	if (p.stagger < m->stagger || m->staggerAt < 0.0) m->staggerLag = std::max(m->staggerLag, m->stagger);
	m->stagger = p.stagger;
	m->staggerAt = m_now;
	if (p.state == 1)
	{
		m->brokenUntil = m_now + p.brokenMs / 1000.0;
		m->stunShake = 1.f;
		const Vector2 iso = TileToIso(m->pos);
		fx::Fx().FloatText(iso, EntityHeight(*m) + 40.f, "무력화!", ui::theme::kDawnHi, 30.f, 1.6f, ui::Face::Display);
		AddCenter("무력화 — 3초 동안 받는 피해 +30%", ui::theme::kDawnHi);
		{
			impact::Ctx k;
			k.iso = iso; k.z = EntityHeight(*m) * 0.5f; k.tier = fxgrade::Tier::T3; k.elem = impact::Element::Gold; k.heavy = true;
			impact::Shatter(k, 80.f);
			impact::Strike(k);
		}
		post::Shockwave(iso, 20.f, 260.f, 36.f, 0.8f, 0.5f);
		m_feelStop = std::max(m_feelStop, 0.1f);
		m_feelTrauma = std::max(m_feelTrauma, 0.45f);
		PlayAt(static_cast<int>(sfx::Id::MetalRing), m->pos, 1.f, 0.01f);
		PlayAt(static_cast<int>(sfx::Id::BoomLow), m->pos, 0.9f, 0.01f);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 피격 반응 — 기술 표의 반응(띄우기 · 넘어뜨리기 · 밀기 · 경직)
// ─────────────────────────────────────────────────────────────────────────────
void Game::ApplyHitReaction(Entity& v, uint16_t skillId, uint8_t hitIndex, uint8_t flags, uint8_t flags2)
{
	if (v.kind != EntityKind::Monster || v.dead) return;
	if (flags2 & DAMAGE2_PROC) return;   // 잔향 · 분신 · 장비 효과는 몸을 흔들지 않는다
	const SkillDef* d = SkillDefTable::Find(skillId);
	if (!d) return;
	HitReact react = HitReact::None;
	if (hitIndex & 0x80)
	{
		const int i = hitIndex & 0x7F;
		if (i < d->hitCount) react = d->hits[i].react;
	}
	else if (d->hitCount > 0 && d->special == SkillSpecial::Legacy)
		react = d->hits[0].react;
	if (v.IsBoss() || (v.row && v.row->dummy))
	{
		if (react == HitReact::Knockdown || react == HitReact::Launch) v.stunShake = std::max(v.stunShake, 0.5f);
		return;   // 큰 몸은 무력화 게이지로만 쓰러진다 · 허수아비는 기둥째 흔들리기만
	}
	switch (react)
	{
	case HitReact::Launch:
		v.airT = 0.f;
		v.airDur = 0.55f;
		v.airH = 46.f;
		v.downT = -1.f;
		break;
	case HitReact::Knockdown:
		if (v.airT < 0.f) { v.airT = 0.f; v.airDur = 0.22f; v.airH = 14.f; }
		break;
	case HitReact::Knockback:
	case HitReact::Pull:
		v.slideUntil = m_now + 0.2;
		v.knockT = 1.f;
		break;
	case HitReact::Flinch:
		v.hurtT = 1.f;
		break;
	default:
		break;
	}
	(void)flags;
}

// ─────────────────────────────────────────────────────────────────────────────
// 각성기 연출(시전자 화면) — 대사 · 암전 · 컷인 · 절정은 타격 시각표가
// ─────────────────────────────────────────────────────────────────────────────
void Game::StartAwakenCinematic(uint16_t skillId, Vector2 focusIso)
{
	const SkillDef* d = SkillDefTable::Find(skillId);
	if (!d) return;
	m_cine.skillId = skillId;
	m_cine.start = m_now;
	m_cine.focus = focusIso;
	m_cine.cls = static_cast<uint8_t>(m_classId);
	const float cast = d->castMs / 1000.f;
	fxscale::Scope scope(true);
	post::Darken(focusIso, skillId == 18 ? 0.72f : 0.55f, 0.1f, std::max(0.2f, cast - 0.25f), 0.45f);
	if (skillId == 18) post::Desaturate(0.6f, cast);
	sfx::Play(sfx::Id::Drone, 0.9f, 0.f);
	sfx::PlayAfter(sfx::Id::Cutin, 0.08f, 0.9f, 0.f);
	voice::Play("pc_" + std::to_string(static_cast<int>(m_classId)) + "_ult", 1.f, 2);
	m_cam.AddTrauma(0.12f);
}

void Game::DrawCinematic()
{
	const double e = m_now - m_cine.start;
	const SkillRow* row = SkillTable::Find(m_cine.skillId);
	// ── 컷인(0.08~0.7초): 직업 일러스트 띠가 화면을 비스듬히 가로지른다(그림이 없으면 직업 문장) ──
	if (row && e >= 0.08 && e < 0.72)
	{
		const float u = static_cast<float>((e - 0.08) / 0.64);
		const float in = util::EaseOutCubic(util::Clamp01(u * 3.2f));
		const float out = util::EaseInCubic(util::Clamp01((u - 0.74f) / 0.26f));
		const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
		const float bandH = std::round(H * 0.3f);
		const float cy = H * 0.42f;
		const float slide = (1.f - in) * W * 1.1f - out * W * 1.1f;
		const Color base = m_cine.cls == 0 ? ui::theme::kDawnHi : (m_cine.cls == 1 ? Rgb(0x7FD0C4) : Rgb(0xC8D2E0));
		const Color deep = m_cine.cls == 0 ? Rgb(0x1E1A16) : (m_cine.cls == 1 ? ui::theme::kSurface1 : ui::theme::kInk);
		const float skew = bandH * 0.18f;
		const float left = -40.f + slide, right = W + 40.f + slide;
		auto topY = [&](float x) { return cy - bandH * 0.5f + skew - 2.f * skew * (x - left) / (right - left); };
		const Vector2 p0{ left, topY(left) }, p1{ right, topY(right) };
		const Vector2 p2{ right, topY(right) + bandH }, p3{ left, topY(left) + bandH };
		DrawTriangle(p0, p3, p2, util::WithA(deep, 0.94f));
		DrawTriangle(p0, p2, p1, util::WithA(deep, 0.94f));
		float textX = W * 0.42f + slide;
		if (const Texture2D* art = art::Cutin(m_cine.cls))
		{
			// 그림: 띠 높이에 맞춰 왼쪽에 — 윗변 · 아랫변은 띠의 기울기를 따른다(텍스처 사각형을 비스듬히)
			const float imgW = bandH * static_cast<float>(art->width) / static_cast<float>(art->height);
			const float x0 = slide + W * 0.04f, x1 = x0 + imgW;
			rlSetTexture(art->id);
			rlBegin(RL_QUADS);
			rlColor4ub(255, 255, 255, 255);
			rlNormal3f(0.f, 0.f, 1.f);
			rlTexCoord2f(0.f, 0.f); rlVertex2f(x0, topY(x0));
			rlTexCoord2f(0.f, 1.f); rlVertex2f(x0, topY(x0) + bandH);
			rlTexCoord2f(1.f, 1.f); rlVertex2f(x1, topY(x1) + bandH);
			rlTexCoord2f(1.f, 0.f); rlVertex2f(x1, topY(x1));
			rlEnd();
			rlSetTexture(0);
			// 그림 오른쪽 끝을 띠 색에 녹인다
			const float f0 = x1 - imgW * 0.12f;
			rlBegin(RL_QUADS);
			rlColor4ub(deep.r, deep.g, deep.b, 0);   rlVertex2f(f0, topY(f0));
			rlColor4ub(deep.r, deep.g, deep.b, 0);   rlVertex2f(f0, topY(f0) + bandH);
			rlColor4ub(deep.r, deep.g, deep.b, 240); rlVertex2f(x1, topY(x1) + bandH);
			rlColor4ub(deep.r, deep.g, deep.b, 240); rlVertex2f(x1, topY(x1));
			rlEnd();
			textX = x0 + imgW * 0.56f;
		}
		else
		{
			// 그림이 없으면 직업 문장(절차적)
			const Vector2 em{ W * 0.3f + slide, cy };
			DrawCircleV(em, bandH * 0.36f, util::WithA(base, 0.18f));
			DrawRing(em, bandH * 0.3f, bandH * 0.33f, 0.f, 360.f, 48, util::WithA(base, 0.8f));
			for (int k = 0; k < 8; ++k)
			{
				const float a = k * util::kPi / 4.f + static_cast<float>(e) * 1.5f;
				DrawLineEx(Vector2{ em.x + std::cos(a) * bandH * 0.12f, em.y + std::sin(a) * bandH * 0.12f },
					Vector2{ em.x + std::cos(a) * bandH * 0.28f, em.y + std::sin(a) * bandH * 0.28f }, 3.f, util::WithA(base, 0.7f));
			}
		}
		// 흐름선(빠르게 지나가는 가는 빛) · 띠의 빛 테
		for (int i = 0; i < 12; ++i)
		{
			const float fy = bandH * (0.08f + 0.84f * util::Hash01(i, 3));
			const float len = 80.f + 220.f * util::Hash01(i, 7);
			const float x0 = std::fmod(static_cast<float>(e) * (1500.f + 600.f * util::Hash01(i, 9)) + i * 173.f, W + 400.f) - 200.f + slide;
			DrawLineEx(Vector2{ x0, topY(x0) + fy }, Vector2{ x0 + len, topY(x0 + len) + fy }, 1.5f, util::WithA(base, 0.22f));
		}
		DrawLineEx(p0, p1, 4.f, util::WithA(base, 0.95f));
		DrawLineEx(p3, p2, 4.f, util::WithA(base, 0.95f));
		ui::TextStyle ts;
		ts.size = std::round(bandH * 0.24f);
		ts.color = Rgb(0xFFF8EC);
		ts.outline = 2.f;
		ts.outlineColor = util::WithA(deep, 0.9f);
		ts.face = ui::Face::Display;
		const float ty = topY(textX) + bandH * 0.5f;
		ui::Fonts().Draw(row->name, Vector2{ textX, ty - ts.size * 0.72f }, ts);
		ui::TextStyle sub = ts;
		sub.size = std::round(bandH * 0.09f);
		sub.color = util::WithA(base, 0.95f);
		sub.face = ui::Face::Strong;
		ui::Fonts().Draw(m_cine.cls == 0 ? "서약기사 · 각성" : (m_cine.cls == 1 ? "각인술사 · 각성" : "밤사냥꾼 · 각성"), Vector2{ textX + 4.f, ty + ts.size * 0.38f }, sub);
	}

	// ── 차지 게이지(내 머리 위): 단계 눈금 + 완벽 구간 ──
	if (m_heldSkill != 0)
		if (const SkillDef* d = MyDef(m_heldSkill); d && d->kind == SkillKind::Charge)
			if (Entity* me = Self())
			{
				const Vector2 head = GetWorldToScreen2D(Vector2{ SurfaceIso(me->pos).x, SurfaceIso(me->pos).y - actors::PlayerHeight() - 30.f }, m_cam2d);
				const float w = 96.f, h = 9.f;
				const Rectangle r{ head.x - w * 0.5f, head.y, w, h };
				const float full = static_cast<float>(d->stageMs * d->stages + d->perfectMs);
				const float held = static_cast<float>((m_now - m_heldAt) * 1000.0);
				const float u = util::Clamp01(held / full);
				DrawRectangleRec(Rectangle{ r.x - 2, r.y - 2, r.width + 4, r.height + 4 }, util::A8(ui::theme::kInk, 220));
				const float pStart = static_cast<float>(d->stageMs * d->stages) / full;
				DrawRectangleRec(Rectangle{ r.x + r.width * pStart, r.y, r.width * (1.f - pStart), r.height }, util::WithA(Rgb(0xFFF0C8), 0.25f));
				const bool inPerfect = held >= d->stageMs * d->stages;
				const Color fill = inPerfect ? Rgb(0xFFF6DC) : GaugeColor(m_classId);
				DrawRectangleRec(Rectangle{ r.x, r.y, r.width * u, r.height }, fill);
				for (int s = 1; s < d->stages; ++s)
				{
					const float x = r.x + r.width * (static_cast<float>(d->stageMs * s) / full);
					DrawLineEx(Vector2{ x, r.y - 2 }, Vector2{ x, r.y + r.height + 2 }, 2.f, util::A8(ui::theme::kInk, 230));
				}
				if (inPerfect)
				{
					const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 40.f);
					DrawRectangleLinesEx(Rectangle{ r.x - 3, r.y - 3, r.width + 6, r.height + 6 }, 2.f, util::WithA(Rgb(0xFFF6DC), 0.5f + 0.5f * pulse));
				}
			}

	// ── 밤 장막: 내 화면 가장자리가 어두워진다 ──
	if (Entity* me = Self(); me && me->Status(static_cast<uint8_t>(StatusId::Veil), static_cast<float>(m_now)))
	{
		const auto& tex = assets::Get();
		DrawTexturePro(tex.vignette, Rectangle{ 0, 0, (float)tex.vignette.width, (float)tex.vignette.height },
			Rectangle{ -20, -20, (float)m_screenW + 40, (float)m_screenH + 40 }, Vector2{ 0, 0 }, 0.f, util::A8(ui::theme::kInk, 200));
	}
}

void Game::DrawIdentityGauge(Rectangle r)
{
	const Color col = GaugeColor(m_classId);
	const float u = util::Clamp01(m_identity / 1000.f);
	const bool active = m_identityActiveUntil > m_now;
	const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 5.f);
	ui::Plate(Rectangle{ r.x - 2, r.y - 2, r.width + 4, r.height + 4 }, util::A8(ui::theme::kInk, 220), BLANK, 3.f, 0.f);
	// 칸 나눔(직업마다): 서약 = 4마디 · 새김 = 5글자 · 그늘 = 3달
	const int seg = m_classId == ClassId::Knight ? 4 : (m_classId == ClassId::Engraver ? 5 : 3);
	const float gap = 3.f;
	const float sw = (r.width - gap * (seg - 1)) / seg;
	for (int i = 0; i < seg; ++i)
	{
		const Rectangle s{ r.x + i * (sw + gap), r.y, sw, r.height };
		DrawRectangleRec(s, ui::theme::kSurface2);
		const float fillU = util::Clamp01(u * seg - i);
		if (fillU > 0.f)
		{
			const Color c = active ? util::Mix(col, WHITE, 0.25f * pulse) : (u >= 1.f ? util::Mix(col, WHITE, 0.3f * pulse) : col);
			DrawRectangleRec(Rectangle{ s.x, s.y, s.width * fillU, s.height }, c);
		}
	}
	if (active)
	{
		const float remain = static_cast<float>(m_identityActiveUntil - m_now);
		ui::TextStyle ts; ts.size = 12.f; ts.color = ui::theme::kTextHi; ts.outline = 1.2f; ts.face = ui::Face::Number;
		ui::Fonts().DrawCentered(Fmt("%.1f", remain), Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f }, ts);
	}
	if (u >= 1.f && !active)
	{
		BeginBlendMode(BLEND_ADDITIVE);
		DrawRectangleLinesEx(Rectangle{ r.x - 3, r.y - 3, r.width + 6, r.height + 6 }, 2.f, util::WithA(col, 0.4f + 0.5f * pulse));
		EndBlendMode();
	}
	ui::TextStyle ls; ls.size = 12.f; ls.color = util::WithA(col, 0.9f); ls.outline = 1.f; ls.face = ui::Face::Strong;
	ui::Fonts().Draw(GaugeName(m_classId), Vector2{ r.x - 30.f, r.y + r.height * 0.5f - 7.f }, ls);
}
