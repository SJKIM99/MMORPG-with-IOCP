#include "game/Game.h"
#include "game/Texts.h"
#include "fx/Effects.h"
#include "fx/Particles.h"
#include "fx/SkillFx.h"
#include "render/Draw.h"
#include "render/Actors.h"
#include "audio/Sfx.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// 전투 V2 — 직업 기술 시전(입력 → 대상 → 즉시 연출 → 서버 판정), 적중 표시
// (투사체 도착·연타 간격에 맞춘 지연, 쌓이는 숫자, 연계 이름, 히트스톱·흔들림),
// 상태이상/몬스터 예고 표시.
//
// 원칙
//  · 누르면 바로 움직인다: 모션·소리·궤적은 요청과 동시에 재생하고, 피해 숫자와
//    체력은 서버가 알려 준 대로만 바꾼다(판정은 전부 서버).
//  · 사거리 밖에서 누르면 다가가서 쓴다(리니지식). 쿨다운이 거의 끝났을 때 누르면
//    풀리는 순간 나간다(선입력).
//  · 한 프레임에 여러 대상이 맞아도 흔들림은 곱으로 커지지 않는다(최댓값만).
// ─────────────────────────────────────────────────────────────────────────────

using util::Rgb;

namespace
{
	int Cheb(int ax, int ay, int bx, int by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }
	int Sign(int v) { return (v > 0) - (v < 0); }

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

	// 화면 기준 좌우/앞뒤로 몸을 돌린다(타일 → 화면: sx = X-Y, sy = X+Y)
	void FaceToward(Entity& e, int fromX, int fromY, int tx, int ty)
	{
		const int dx = tx - fromX, dy = ty - fromY;
		const int sx = dx - dy, sy = dx + dy;
		if (sx != 0) e.faceRight = sx > 0;
		if (sy != 0) e.faceBack = sy < 0;
	}

	// 바라보는 방향의 타일 한 칸(화면 오른쪽 아래 = +X, 오른쪽 위 = -Y ...)
	void FacingTile(const Entity& e, int& dx, int& dy)
	{
		if (e.faceRight) { dx = e.faceBack ? 0 : 1; dy = e.faceBack ? -1 : 0; }
		else { dx = e.faceBack ? -1 : 0; dy = e.faceBack ? 0 : 1; }
	}

	// 임의 방향 → 8방향(타일 공간 각도 기준 반올림)
	void Snap8(float fx, float fy, int& dx, int& dy)
	{
		if (std::fabs(fx) < 1e-4f && std::fabs(fy) < 1e-4f) { dx = dy = 0; return; }
		const float a = std::atan2(fy, fx);
		const int k = static_cast<int>(std::lround(a / (util::kPi * 0.25f)));
		dx = static_cast<int>(std::lround(std::cos(k * util::kPi * 0.25f)));
		dy = static_cast<int>(std::lround(std::sin(k * util::kPi * 0.25f)));
	}

	bool IsDashSkill(uint16_t id) { return id == 4 || id == 10 || id == 14 || id == 16; }

	// 기술 이동 시간(표시) — 서버가 칸당 60ms로 옮긴다
	double DashDuration(uint16_t id, int tiles)
	{
		switch (id)
		{
		case 4:  return 0.05 + 0.055 * tiles;
		case 16: return 0.05 + 0.05 * tiles;
		case 14: return 0.09;
		default: return 0.0;   // 10 공간 도약: 순간이동
		}
	}

	// 연타(hitIndex) 사이 표시 간격
	double HitInterval(uint16_t id)
	{
		switch (id)
		{
		case 13: return 0.11;   // 연속 찌르기
		case 18: return 0.13;   // 월영 난무
		case 11: return 0.075;  // 번개 연쇄
		case 9:  return 0.12;   // 증발 폭발(두 번째 폭발)
		default: return 0.09;
		}
	}

	// 기술 동작 중 걸음을 멈추는 시간(무게감). 짧게 — 조작이 굼떠지지 않게.
	double ActionLock(const SkillRow& row)
	{
		if (row.slot == 0) return 0.14;
		switch (row.id)
		{
		case 6:  return 0.46;   // 처형: 힘을 모으는 동안 서 있는다
		case 18: return 0.5;    // 월영 난무
		case 3: case 9: return 0.24;
		case 5:  return 0.12;
		default: return 0.2;
		}
	}

	sfx::Id CastSfx(uint16_t id)
	{
		switch (id)
		{
		case 3:  return sfx::Id::Whirl;
		case 4:  return sfx::Id::Dash;
		case 5:  return sfx::Id::GuardUp;
		case 6:  return sfx::Id::ChargeUp;
		case 7:  return sfx::Id::RuneBolt;
		case 8:  return sfx::Id::FrostSeal;
		case 9:  return sfx::Id::FireRing;
		case 10: return sfx::Id::Blink;
		case 11: return sfx::Id::Thunder;
		case 12: return sfx::Id::MeteorFall;
		case 13: return sfx::Id::Stab;
		case 14: return sfx::Id::ShadowStep;
		case 15: return sfx::Id::PoisonSlash;
		case 16: return sfx::Id::Evade;
		case 18: return sfx::Id::MoonDance;
		default: return sfx::Id::Swing;
		}
	}

	sfx::Id HitSfx(uint16_t id, bool crit, bool heavy)
	{
		switch (id)
		{
		case 2:  return sfx::Id::ShieldBash;
		case 6:  return sfx::Id::Execute;
		case 12: return sfx::Id::MeteorImpact;
		case 17: return sfx::Id::Backstab;
		case 8:  return sfx::Id::Freeze;
		default: return (crit || heavy) ? sfx::Id::HitHeavy : sfx::Id::Hit;
		}
	}

	bool InsideTelegraph(const Entity::Telegraph& t, int x, int y)
	{
		if (t.shape == static_cast<uint8_t>(TelegraphShape::Line))
		{
			int cx = static_cast<int>(t.from.x), cy = static_cast<int>(t.from.y);
			const int ex = static_cast<int>(t.c.x), ey = static_cast<int>(t.c.y);
			for (int i = 0; i < 8 && (cx != ex || cy != ey); ++i)
			{
				cx += Sign(ex - cx);
				cy += Sign(ey - cy);
				if (cx == x && cy == y) return true;
			}
			return false;
		}
		return Cheb(x, y, static_cast<int>(t.c.x), static_cast<int>(t.c.y)) <= static_cast<int>(t.radius);
	}

	Color StatusColor(StatusId id) { return Rgb(StatusTable::Get(id).color); }
}

// ─────────────────────────────────────────────────────────────────────────────
// 기술 표
// ─────────────────────────────────────────────────────────────────────────────
const SkillRow* Game::SkillInSlot(int slot) const
{
	if (slot < 0 || slot > 5)
		return nullptr;
	return SkillTable::Find(ClassTable::Get(m_classId).skills[slot]);
}

int Game::AttackRange() const
{
	if (m_queuedSkill >= 0)
		if (const SkillRow* r = SkillInSlot(m_queuedSkill))
			return std::max<int>(1, r->range);
	const SkillRow* basic = SkillInSlot(0);
	return basic ? std::max<int>(1, basic->range) : 1;
}

Entity* Game::PickSkillTarget(const SkillRow& row)
{
	// 전투는 키보드로만: 마우스가 가리키는 것은 보지 않는다.
	// 1) 지금 대상(Tab/자동) → 2) 바라보는 쪽의 가까운 적 → 3) 가까운 적.
	// Ctrl을 누르고 있으면 다른 모험가도 대상이 된다(강제 공격).
	const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
	auto usable = [&](const Entity* e)
	{
		if (!e || e->dead || e->removing || e->id == m_selfId) return false;
		if (e->kind == EntityKind::Monster) return true;
		if (e->kind == EntityKind::Player) return (ctrl || (m_goal.kind == GoalKind::Attack && m_goal.force && m_goal.target == e->id)) && !WorldData::IsSafeZone(e->tx, e->ty);
		return false;
	};
	if (Entity* t = Find(m_target); usable(t) && Cheb(m_predX, m_predY, t->tx, t->ty) <= row.range + 6)
		return t;
	Entity* me = Self();
	int fx = 0, fy = 0;
	if (me) FacingTile(*me, fx, fy);
	Entity* best = nullptr;
	float bestScore = 1e9f;
	for (auto& [id, e] : m_ents)
	{
		const bool candidate = ctrl ? (e.kind == EntityKind::Player || e.kind == EntityKind::Monster) : e.kind == EntityKind::Monster;
		if (!candidate || !usable(&e)) continue;
		const int d = Cheb(m_predX, m_predY, e.tx, e.ty);
		if (d > row.range + 4) continue;
		// 바라보는 쪽이면 조금 더 가깝게 친다(앞의 적을 먼저)
		const int dx = e.tx - m_predX, dy = e.ty - m_predY;
		const float front = static_cast<float>(dx * fx + dy * fy);
		float score = static_cast<float>(d) - (front > 0.f ? 0.6f : 0.f);
		if (ctrl && e.kind == EntityKind::Player) score -= 0.3f;   // Ctrl: 사람을 우선
		if (score < bestScore) { bestScore = score; best = &e; }
	}
	return best;
}

Vector2 Game::PredictLanding(const SkillRow& row, int tx, int ty, const Entity* target) const
{
	const Vector2 here{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
	switch (row.id)
	{
	case 4:
	case 16:
	{
		const int dx = Sign(tx - m_predX), dy = Sign(ty - m_predY);
		int x = m_predX, y = m_predY;
		for (int i = 0; i < row.dash; ++i)
		{
			const int nx = x + dx, ny = y + dy;
			if (WorldData::IsBlocked(nx, ny) || (row.id == 4 && WorldData::IsSafeZone(nx, ny)))
				break;
			if (row.id == 4)
			{
				bool monster = false;
				for (const auto& [id, e] : m_ents)
					if (e.kind == EntityKind::Monster && !e.dead && e.tx == nx && e.ty == ny) { monster = true; break; }
				if (monster) break;
			}
			x = nx; y = ny;
		}
		return Vector2{ static_cast<float>(x), static_cast<float>(y) };
	}
	case 14:
	{
		if (!target) return here;
		int dx = Sign(target->tx - m_predX), dy = Sign(target->ty - m_predY);
		if (dx == 0 && dy == 0) dx = 1;
		const int bx = target->tx + dx, by = target->ty + dy;
		if (!WorldData::IsBlocked(bx, by)) return Vector2{ static_cast<float>(bx), static_cast<float>(by) };
		return Vector2{ static_cast<float>(target->tx), static_cast<float>(target->ty) };
	}
	case 10:
	{
		const int dx = Sign(tx - m_predX), dy = Sign(ty - m_predY);
		int x = m_predX, y = m_predY;
		for (int i = 0; i < row.dash; ++i)
		{
			const int nx = x + (std::abs(tx - x) > 0 ? dx : 0);
			const int ny = y + (std::abs(ty - y) > 0 ? dy : 0);
			if ((nx == x && ny == y) || WorldData::IsBlocked(nx, ny)) break;
			x = nx; y = ny;
		}
		return Vector2{ static_cast<float>(x), static_cast<float>(y) };
	}
	default:
		return here;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 시전
// ─────────────────────────────────────────────────────────────────────────────
bool Game::TryCastSkill(int slot, bool quiet, const ObjID& forced)
{
	Entity* me = Self();
	if (!me || m_dead || m_scene != Scene::Game)
		return false;
	const SkillRow* row = SkillInSlot(slot);
	if (!row)
		return false;
	const bool basic = row->slot == 0;
	const float nowf = static_cast<float>(m_now);
	auto say = [&](const std::string& text, Color c)
	{
		if (quiet) return;
		AddCenter(text, c);
		sfx::Play(sfx::Id::Error, 0.3f);
	};

	if (m_level < row->unlockLevel)
	{
		say(Fmt("%s — Lv.%d에 배울 수 있습니다.", row->name, row->unlockLevel), Rgb(0xC8C0B0));
		return false;
	}
	if (InTown())
	{
		AddCenter("마을 안에서는 싸울 수 없습니다.", Rgb(0xFFD070));
		m_queuedSkill = -1;
		if (m_goal.kind == GoalKind::Attack) ClearGoal();
		return false;
	}
	if (me->Status(static_cast<uint8_t>(StatusId::Stun), nowf) || (row->dash > 0 && me->Status(static_cast<uint8_t>(StatusId::Freeze), nowf)))
	{
		say("몸이 움직이지 않습니다!", Rgb(0xFFE070));
		return false;
	}
	if (m_dashWaitSeq != 0 && m_now < m_dashWaitUntil)
		return false;   // 돌진 착지 전
	if (m_now < m_skillReady[slot] - 0.02)
	{
		if (!basic)
		{
			const double remain = m_skillReady[slot] - m_now;
			if (remain < 0.45)
			{
				// 선입력: 곧 풀리니 그때 나간다
				m_bufferSlot = slot;
				m_bufferAt = m_now;
				return false;
			}
			say(Fmt("%s 재사용 대기 중 (%.1f초)", row->name, remain), Rgb(0xC8C8C8));
		}
		return false;
	}
	if (m_mp < static_cast<int>(row->mp))
	{
		if (!quiet)
		{
			AddCenter("마나가 부족합니다.", Rgb(0x8AB8FF));
			sfx::Play(sfx::Id::NoMana, 0.7f);
		}
		m_mpFlashAt = m_now;
		return false;
	}

	// ── 대상/지점 ──
	ObjID targetId{};
	int tx = m_predX, ty = m_predY;
	Entity* target = nullptr;
	switch (row->target)
	{
	case SkillTarget::Enemy:
	{
		target = forced != ObjID{} ? Find(forced) : PickSkillTarget(*row);
		if (!target || target->dead || target->removing || target->kind == EntityKind::Item)
		{
			if (!basic) say("대상이 없습니다.", Rgb(0xC8C8C8));
			return false;
		}
		if (target->kind == EntityKind::Player)
		{
			const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
			if (!ctrl && !(m_goal.kind == GoalKind::Attack && m_goal.force && m_goal.target == target->id))
			{
				if (!basic) say("다른 모험가를 공격하려면 Ctrl을 누른 채로 쓰세요.", Rgb(0xFFB070));
				return false;
			}
			if (WorldData::IsSafeZone(target->tx, target->ty))
			{
				say("마을 안의 상대는 공격할 수 없습니다.", Rgb(0xFFD070));
				return false;
			}
		}
		const int d = Cheb(m_predX, m_predY, target->tx, target->ty);
		if (d > row->range)
		{
			// 사거리 밖 — 다가가서 쓴다
			m_queuedSkill = basic ? -1 : slot;
			m_queuedTarget = target->id;
			m_queuedAt = m_now;
			m_target = target->id;
			if (m_goal.kind != GoalKind::Attack || m_goal.target != target->id)
				SetGoal(Goal{ GoalKind::Attack, target->id, 0, 0, 0, target->kind == EntityKind::Player });
			return false;
		}
		targetId = target->id;
		tx = target->tx;
		ty = target->ty;
		m_target = target->id;
		break;
	}
	case SkillTarget::Ground:
	{
		int gx = m_predX, gy = m_predY;
		// 방향 키를 누른 채로 쓰면 그쪽으로
		int sx = (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT) ? 1 : 0) - (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT) ? 1 : 0);
		int sy = (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN) ? 1 : 0) - (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP) ? 1 : 0);
		if (ui::TextFocused()) sx = sy = 0;
		if (sx == 0 && sy == 0) { sx = m_scriptKeyX; sy = m_scriptKeyY; }
		if (row->id == 10)
		{
			// 공간 도약: 누르고 있는 방향(없으면 바라보는 방향)으로 dash칸
			int dx = 0, dy = 0;
			if (sx != 0 || sy != 0) { dx = std::clamp(sx + sy, -1, 1); dy = std::clamp(sy - sx, -1, 1); }
			else FacingTile(*me, dx, dy);
			gx = m_predX + dx * row->dash;
			gy = m_predY + dy * row->dash;
		}
		else if (Entity* t = PickSkillTarget(*row); t && Cheb(m_predX, m_predY, t->tx, t->ty) <= row->range + 2)
		{
			// 지점 기술은 대상 발밑에(없으면 앞쪽)
			gx = t->tx;
			gy = t->ty;
			m_target = t->id;
		}
		else
		{
			int dx = 0, dy = 0;
			FacingTile(*me, dx, dy);
			gx = m_predX + dx * std::min<int>(3, row->range);
			gy = m_predY + dy * std::min<int>(3, row->range);
		}
		const int d = Cheb(m_predX, m_predY, gx, gy);
		if (d > row->range && d > 0)
		{
			// 사거리보다 멀면 그 방향의 사거리 끝으로
			const float k = static_cast<float>(row->range) / static_cast<float>(d);
			gx = m_predX + static_cast<int>(std::lround((gx - m_predX) * k));
			gy = m_predY + static_cast<int>(std::lround((gy - m_predY) * k));
		}
		tx = gx;
		ty = gy;
		if (row->id == 10 && tx == m_predX && ty == m_predY)
		{
			say("도약할 방향이 막혀 있습니다.", Rgb(0xC8C0FF));
			return false;
		}
		break;
	}
	case SkillTarget::Direction:
	{
		// 방향: WASD를 누르고 있으면 그쪽(회피 = 가려는 곳으로), 아니면 대상 쪽
		// (잔상 회피는 대상 반대쪽 — 적을 본 채로 물러난다), 그것도 없으면 바라보는 쪽
		int sx = (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT) ? 1 : 0) - (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT) ? 1 : 0);
		int sy = (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN) ? 1 : 0) - (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP) ? 1 : 0);
		if (ui::TextFocused()) sx = sy = 0;
		if (sx == 0 && sy == 0) { sx = m_scriptKeyX; sy = m_scriptKeyY; }
		int dx = 0, dy = 0;
		if (sx != 0 || sy != 0)
		{
			dx = std::clamp(sx + sy, -1, 1);
			dy = std::clamp(sy - sx, -1, 1);
		}
		else if (Entity* t = Find(m_target); t && !t->dead && !t->removing && (t->tx != m_predX || t->ty != m_predY))
		{
			// 돌진은 대상 쪽으로, 잔상 회피는 대상 반대쪽으로
			Snap8(static_cast<float>(t->tx - m_predX), static_cast<float>(t->ty - m_predY), dx, dy);
			if (row->id == 16) { dx = -dx; dy = -dy; }
		}
		if (dx == 0 && dy == 0)
		{
			FacingTile(*me, dx, dy);
			if (row->id == 16) { dx = -dx; dy = -dy; }
		}
		const int reach = std::max<int>(1, row->dash);
		tx = m_predX + dx * reach;
		ty = m_predY + dy * reach;
		break;
	}
	case SkillTarget::Self:
		break;
	}

	// ── 요청 + 예측(쿨다운·MP는 응답이 바로잡는다) ──
	const uint16_t seq = ++m_castSeq;
	m_net.SendSkillCast(row->id, targetId, tx, ty, seq);
	m_castSentAt[slot] = m_now;
	const double cd = basic
		? SkillTable::BasicIntervalMs(m_classId, m_stats.attackSpeed) / 1000.0
		: SkillTable::CooldownMs(*row, m_stats.cooldownReduction) / 1000.0;
	m_skillReady[slot] = m_now + cd;
	m_skillCdTotal[slot] = cd;
	if (!basic)
	{
		m_skillReady[0] = std::max(m_skillReady[0], m_now + 0.22);   // 기술 뒤 평타는 한 박자 쉬고
		m_queuedSkill = -1;
	}
	if (row->mp > 0)
		m_mp = std::max(0, m_mp - static_cast<int>(row->mp));
	m_bufferSlot = -1;
	m_actionLockUntil = std::max(m_actionLockUntil, m_now + ActionLock(*row));
	if (IsDashSkill(row->id))
	{
		m_dashWaitSeq = seq;
		m_dashWaitUntil = m_now + 0.8;
		m_path.clear();
	}

	// ── 즉시 연출: 입력 → 준비 → 모션(판정은 서버) ──
	if (row->target != SkillTarget::Self)
		FaceToward(*me, m_predX, m_predY, tx, ty);
	me->animSkill = row->id;
	me->animT = 0.f;
	me->animDur = skillfx::AnimDuration(row->id);
	me->animCast = row->castMs / 1000.f;
	me->attackT = -1.f;
	me->castT = -1.f;

	skillfx::CastInfo ci;
	ci.casterIso = TileToIso(me->pos);
	ci.targetIso = target ? TileToIso(target->pos) : gfx::IsoPx(static_cast<float>(tx), static_cast<float>(ty));
	const Vector2 land = PredictLanding(*row, tx, ty, target);
	ci.landIso = gfx::IsoPx(land.x, land.y);
	ci.casterHeight = actors::PlayerHeight();
	ci.targetHeight = target ? EntityHeight(*target) : 40.f;
	ci.faceRight = me->faceRight;
	ci.castSec = row->castMs / 1000.f;
	ci.self = true;
	ci.classId = static_cast<uint8_t>(m_classId);
	const float travel = skillfx::OnCast(row->id, ci);
	if (travel > 0.f && target)
		target->projArriveAt = m_now + travel;

	sfx::Play(CastSfx(row->id), basic ? 0.6f : 0.85f, 0.08f);
	const skillfx::Feel feel = skillfx::CastFeel(row->id);
	if (feel.trauma > 0.f) m_cam.AddTrauma(feel.trauma);
	if (feel.kick > 0.f) m_cam.Kick(feel.kick);
	if (target && row->range <= 2)
		m_cam.Nudge(util::Sub(ci.targetIso, ci.casterIso), basic ? 1.5f : 2.5f);
	return true;
}

void Game::AddCombo(int hits)
{
	if (m_now - m_comboLastAt > 3.0)
		m_comboHits = 0;
	m_comboHits += hits;
	m_comboLastAt = m_now;
	m_comboBest = std::max(m_comboBest, m_comboHits);
	if (m_comboHits == 10 || m_comboHits == 30 || (m_comboHits > 0 && m_comboHits % 50 == 0))
		sfx::Play(sfx::Id::ComboSting, 0.35f, 0.03f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 서버 알림
// ─────────────────────────────────────────────────────────────────────────────
void Game::HandleStats(const USER_STATS_INF_PACKET& p)
{
	const bool first = !m_statsKnown;
	m_statsKnown = true;
	m_classId = ClassTable::IsValid(p.classId) ? static_cast<ClassId>(p.classId) : ClassId::Knight;
	m_level = p.level;
	m_baseStats = p.base;
	m_allocStats = p.alloc;
	m_gearStats = p.gear;
	m_stats = p.stats;
	m_mp = p.mp;
	m_maxmp = p.stats.maxMp;
	m_attack = m_classId == ClassId::RuneCaster ? p.stats.magicAttack : p.stats.attack;
	// 서버는 확장 세션의 이동속도를 능력치·장비로 계산한다(가속은 따로 더한다) — 예측도 같은 값으로
	if (p.stats.moveSpeed > 0)
		m_baseSpeedPercent = p.stats.moveSpeed;
	if (Entity* me = Self())
		me->classId = p.classId;
	if (!first && p.freePoints > m_prevFreePoints)
	{
		AddCenter(Fmt("능력치 포인트 +%d  (C 캐릭터 창)", p.freePoints - m_prevFreePoints), Rgb(0xFFE08A));
		sfx::Play(sfx::Id::StatUp, 0.6f);
	}
	m_freePoints = p.freePoints;
	m_prevFreePoints = p.freePoints;
}

void Game::HandleResource(const USER_RESOURCE_INF_PACKET& p)
{
	m_hp = p.hp;
	m_maxhp = std::max<int>(1, p.maxhp);
	m_mp = p.mp;
	m_maxmp = p.maxmp;
	if (Entity* me = Self()) { me->hp = m_hp; me->maxhp = m_maxhp; }
}

void Game::HandleSkillResult(const SKILL_RESULT_ACK_PACKET& p)
{
	m_mp = p.mp;
	const SkillRow* row = SkillTable::Find(p.skillId);
	if (!row)
		return;
	const int slot = row->slot;
	const auto r = static_cast<SkillResult>(p.result);
	if (p.seq == m_dashWaitSeq && r != SkillResult::Ok)
	{
		m_dashWaitSeq = 0;
		m_actionLockUntil = m_now;
	}
	if (r == SkillResult::Ok)
	{
		// 서버 쿨다운(능력치가 바뀌었으면 다를 수 있다)을 요청 시각 기준으로 맞춘다
		m_skillCdTotal[slot] = p.cooldownMs / 1000.0;
		m_skillReady[slot] = m_castSentAt[slot] + p.cooldownMs / 1000.0;
		return;
	}
	// 실패 — 쿨다운을 되돌리고 이유를 알린다(평타의 흔한 실패는 조용히)
	m_skillReady[slot] = r == SkillResult::Cooldown ? m_now + p.cooldownMs / 1000.0 : m_now;
	m_actionLockUntil = m_now;
	if (Entity* me = Self(); me && me->animSkill == p.skillId && r != SkillResult::Cooldown)
		me->animT = -1.f;
	const bool basic = slot == 0;
	std::string msg;
	Color c = Rgb(0xD8D0C0);
	switch (r)
	{
	case SkillResult::Cooldown: if (!basic) msg = Fmt("%s 재사용 대기 중", row->name); break;
	case SkillResult::NoMana: msg = "마나가 부족합니다."; c = Rgb(0x8AB8FF); m_mpFlashAt = m_now; break;
	case SkillResult::OutOfRange: if (!basic) msg = "대상이 너무 멉니다."; break;
	case SkillResult::InvalidTarget:
	{
		// 대부분은 누르는 사이 대상이 쓰러진 경우다 — 그때는 조용히 넘어간다
		const Entity* t = Find(m_target);
		if (!basic && t && !t->dead) msg = "대상이 없습니다.";
		break;
	}
	case SkillResult::Locked: msg = Fmt("%s — Lv.%u에 배울 수 있습니다.", row->name, static_cast<unsigned>(p.cooldownMs)); break;
	case SkillResult::SafeZone: msg = "마을 안에서는 싸울 수 없습니다."; c = Rgb(0xFFD070); break;
	case SkillResult::Stunned: msg = "몸이 움직이지 않습니다!"; c = Rgb(0xFFE070); break;
	case SkillResult::Blocked: msg = "그곳으로는 갈 수 없습니다."; break;
	case SkillResult::WrongClass: msg = "배우지 않은 기술입니다."; break;
	default: break;
	}
	if (!msg.empty())
	{
		AddCenter(msg, c);
		sfx::Play(r == SkillResult::NoMana ? sfx::Id::NoMana : sfx::Id::Error, 0.35f);
	}
}

void Game::HandleSkillCast(const SKILL_CAST_NFY_PACKET& p)
{
	const SkillRow* row = SkillTable::Find(p.skillId);
	if (!row)
		return;
	const bool dash = IsDashSkill(p.skillId);
	if (p.casterId == m_selfId)
	{
		// 내 시전 연출은 누를 때 이미 재생했다. 기술 이동만 서버가 정한 칸으로 맞춘다.
		if (dash)
		{
			const int tiles = Cheb(m_srvX, m_srvY, p.x, p.y);
			const double dur = DashDuration(p.skillId, tiles);
			if (p.x != m_srvX || p.y != m_srvY)
			{
				m_mover.Dash(p.x, p.y, m_now, dur);
				SyncMover();
			}
			m_dashWaitSeq = 0;
			m_actionLockUntil = std::max(m_actionLockUntil, m_now + dur * 0.85);
			if (p.skillId == 10)
				fx::Fx().Ring(gfx::IsoPx(p.x, p.y), 6.f, 70.f, 0.3f, Rgb(0xD8C8FF, 220), 3.f);
		}
		return;
	}
	Entity* c = Find(p.casterId);
	if (!c)
		return;
	if (row->target != SkillTarget::Self)
		FaceToward(*c, c->tx, c->ty, p.x, p.y);
	c->animSkill = row->id;
	c->animT = 0.f;
	c->animDur = skillfx::AnimDuration(row->id);
	c->animCast = p.castMs / 1000.f;
	c->attackT = -1.f;

	Entity* target = Find(p.targetId);
	skillfx::CastInfo ci;
	ci.casterIso = TileToIso(c->pos);
	ci.targetIso = target ? TileToIso(target->pos) : gfx::IsoPx(p.x, p.y);
	ci.landIso = gfx::IsoPx(p.x, p.y);
	ci.casterHeight = actors::PlayerHeight();
	ci.targetHeight = target ? EntityHeight(*target) : 40.f;
	ci.faceRight = c->faceRight;
	ci.castSec = p.castMs / 1000.f;
	ci.classId = c->classId;
	const float travel = skillfx::OnCast(row->id, ci);
	if (travel > 0.f && target)
		target->projArriveAt = m_now + travel;
	if (dash)
		motion::RemoteStep(*c, p.x, p.y, static_cast<float>(DashDuration(p.skillId, Cheb(c->tx, c->ty, p.x, p.y))), true);
	PlayAt(static_cast<int>(CastSfx(row->id)), c->pos, 0.7f, 0.08f);
}

void Game::HandleStatus(const STATUS_NFY_PACKET& p)
{
	Entity* e = Find(p.subjectId);
	if (!e)
		return;
	const float nowf = static_cast<float>(m_now);
	Entity::StatusFx* slot = nullptr;
	bool had = false;
	for (auto& s : e->statuses)
		if (s.id == p.status) { slot = &s; had = s.stacks > 0 && nowf < s.until; break; }
	if (p.stacks == 0)
	{
		if (slot) *slot = Entity::StatusFx{};
		return;
	}
	if (!slot)
		for (auto& s : e->statuses)
			if (s.stacks == 0 || nowf >= s.until) { slot = &s; break; }
	if (!slot)
		slot = &e->statuses[0];
	const uint8_t prevStacks = had ? slot->stacks : 0;
	slot->id = p.status;
	slot->stacks = p.stacks;
	slot->until = nowf + p.durationMs / 1000.f;

	const auto id = static_cast<StatusId>(p.status);
	const StatusRow& row = StatusTable::Get(id);
	const bool self = p.subjectId == m_selfId;
	const bool mine = p.sourceId == m_selfId;
	const Vector2 iso = TileToIso(e->pos);
	const float h = EntityHeight(*e);
	if (!had)
	{
		switch (id)
		{
		case StatusId::Stun:
		case StatusId::Freeze:
			e->stunShake = 1.f;
			fx::Fx().FloatText(iso, h + 22.f, std::string(row.name) + "!", StatusColor(id), self || mine ? 21.f : 16.f, 1.0f);
			PlayAt(static_cast<int>(id == StatusId::Stun ? sfx::Id::Stun : sfx::Id::Freeze), e->pos, self ? 0.9f : 0.6f, 0.05f);
			if (self)
			{
				AddCenter(id == StatusId::Stun ? "기절!" : "얼어붙었다!", StatusColor(id));
				m_cam.AddTrauma(0.2f);
			}
			break;
		case StatusId::Guard:
		case StatusId::Ambush:
		case StatusId::Evade:
			if (self || mine)
				fx::Fx().FloatText(iso, h + 18.f, row.name, StatusColor(id), 17.f, 1.0f);
			break;
		case StatusId::Slow:
			if (self) fx::Fx().FloatText(iso, h + 16.f, "둔화", StatusColor(id), 16.f, 0.9f);
			break;
		case StatusId::Marked:
			if (mine) fx::Fx().FloatText(iso, h + 18.f, "표식", StatusColor(id), 15.f, 0.8f);
			break;
		default:
			break;
		}
	}
	else if (id == StatusId::Poison && p.stacks > prevStacks && (mine || self))
		fx::Fx().FloatText(iso, h + 16.f, Fmt("중독 ×%d", p.stacks), StatusColor(id), 15.f + p.stacks, 0.8f);
}

void Game::HandleTelegraph(const MONSTER_TELEGRAPH_NFY_PACKET& p)
{
	Entity* m = Find(p.monsterId);
	if (!m)
		return;
	if (p.durationMs == 0)
	{
		// 예고 중에 기절/빙결 → 끊겼다(받아치기 성공)
		if (m->tele.active)
		{
			m->tele.active = false;
			m->windup = 0.f;
			const Vector2 iso = TileToIso(m->pos);
			fx::Fx().FloatText(iso, EntityHeight(*m) + 30.f, "끊김!", Rgb(0x9AE8FF), 23.f, 1.1f);
			fx::Fx().Ring(iso, 8.f, 80.f, 0.3f, Rgb(0xC8F0FF, 220), 4.f);
			PlayAt(static_cast<int>(sfx::Id::Interrupt), m->pos, 0.9f, 0.03f);
			m_feelStop = std::max(m_feelStop, 0.06f);
		}
		return;
	}
	auto& t = m->tele;
	t.active = true;
	t.shape = p.shape;
	t.pattern = p.pattern;
	t.c = Vector2{ static_cast<float>(p.x), static_cast<float>(p.y) };
	t.from = Vector2{ static_cast<float>(p.fromX), static_cast<float>(p.fromY) };
	t.radius = p.radius;
	t.t = 0.f;
	t.dur = p.durationMs / 1000.f;
	FaceToward(*m, m->tx, m->ty, p.x, p.y);
	if (InsideTelegraph(t, m_predX, m_predY) && !m_dead)
	{
		m_dangerAt = m_now;
		sfx::Play(sfx::Id::TeleWarn, 0.5f, 0.05f);
	}
	else
		PlayAt(static_cast<int>(sfx::Id::TeleWarn), m->pos, 0.3f, 0.05f);
}

void Game::OnTelegraphImpact(Entity& m)
{
	const auto& t = m.tele;
	const Vector2 fromIso = gfx::IsoPx(t.from.x, t.from.y);
	const Vector2 toIso = gfx::IsoPx(t.c.x, t.c.y);
	skillfx::MonsterImpact(t.pattern, fromIso, toIso, t.radius, m.row ? Rgb(m.row->color1) : Rgb(0xC8A878));
	m.attackT = 0.f;
	const int d = Cheb(m_predX, m_predY, static_cast<int>(t.c.x), static_cast<int>(t.c.y));
	switch (static_cast<MonsterPattern>(t.pattern))
	{
	case MonsterPattern::Slam:
		PlayAt(static_cast<int>(sfx::Id::Slam), t.c, 0.6f, 0.05f);
		if (d <= 5) m_cam.AddTrauma(d <= 1 ? 0.3f : 0.14f);
		break;
	case MonsterPattern::Leap:
		PlayAt(static_cast<int>(sfx::Id::WallSlam), t.c, 0.6f, 0.08f);
		if (d <= 4) m_cam.AddTrauma(0.12f);
		break;
	case MonsterPattern::Charge:
		PlayAt(static_cast<int>(sfx::Id::Dash), t.from, 0.7f, 0.08f);
		break;
	case MonsterPattern::Ranged:
		PlayAt(static_cast<int>(sfx::Id::HitHeavy), t.c, 0.5f, 0.1f);
		break;
	default:
		break;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 적중 표시 — 지연(투사체 도착·연타 간격) → HandleDamage
// ─────────────────────────────────────────────────────────────────────────────
bool Game::HasDelayedHitFor(const ObjID& victim) const
{
	for (const auto& d : m_delayedHits)
		if (d.p.victimId == victim) return true;
	return false;
}

void Game::QueueDamage(const SUBJECT_DAMAGE_NFY_PACKET& p)
{
	double at = m_now;
	if ((p.flags & DAMAGE_DOT) == 0 && p.skillId != 0)
	{
		const SkillRow* row = SkillTable::Find(p.skillId);
		Entity* v = Find(p.victimId);
		if (row && row->projectile > 0 && v && v->projArriveAt > m_now)
			at = v->projArriveAt;
		if (p.hitIndex > 0)
			at += p.hitIndex * HitInterval(p.skillId);
		if (p.skillId == 11 && p.hitIndex == 0)
			at += 0.06;   // 손끝에서 번쩍인 다음 떨어진다
	}
	// 같은 대상의 앞선 표시보다 먼저 보여 주면 체력바가 거꾸로 간다 — 순서를 지킨다
	for (const auto& d : m_delayedHits)
		if (d.p.victimId == p.victimId) at = std::max(at, d.at);
	if (at <= m_now + 0.004)
	{
		HandleDamage(p);
		return;
	}
	m_delayedHits.push_back(DelayedHit{ at, p });
}

void Game::HandleDamage(const SUBJECT_DAMAGE_NFY_PACKET& p)
{
	Entity* v = Find(p.victimId);
	Entity* a = Find(p.attackerId);
	const bool selfVictim = p.victimId == m_selfId;
	const bool selfAttacker = p.attackerId == m_selfId;
	const bool crit = (p.flags & DAMAGE_CRIT) != 0;
	const bool skill = (p.flags & DAMAGE_SKILL) != 0;
	const bool killed = (p.flags & DAMAGE_KILLED) != 0;
	const bool bossSkill = (p.flags & DAMAGE_BOSS_SKILL) != 0;
	const bool combo = (p.flags & DAMAGE_COMBO) != 0 && p.comboId != 0;
	const bool dot = (p.flags & DAMAGE_DOT) != 0;
	const bool knock = (p.flags & DAMAGE_KNOCKBACK) != 0;
	const bool evade = (p.flags2 & DAMAGE2_EVADE) != 0;
	const bool guard = (p.flags2 & DAMAGE2_GUARD) != 0;
	const bool heavy = (p.flags2 & (DAMAGE2_HEAVY | DAMAGE2_WALL)) != 0;

	if (v)
	{
		v->hp = std::max(0, p.hp);
		v->maxhp = std::max(1, p.maxhp);
		if (!evade)
		{
			v->lastHitAt = m_time;
			if (!dot) { v->hurtT = 1.f; v->flashT = 1.f; }
			else v->flashT = std::max(v->flashT, 0.35f);
			if (knock || heavy) v->knockT = 1.f;
		}
	}
	if (selfVictim)
	{
		m_hp = std::max(0, p.hp);
		m_maxhp = std::max(1, p.maxhp);
	}
	// 공격자 동작 — 기술(시전 알림으로 이미 재생)·지속 피해는 제외
	if (a && !selfAttacker && p.skillId == 0 && !bossSkill && !dot)
	{
		a->attackT = 0.f;
		if (v) FaceToward(*a, a->tx, a->ty, v->tx, v->ty);
	}
	if (!v)
		return;

	const Vector2 iso = TileToIso(v->pos);
	const float h = EntityHeight(*v);
	const bool throttleSfx = m_now - m_lastHitSfxAt < 0.045;

	// ── 회피: 숫자 대신 글자 ──
	if (evade)
	{
		fx::Fx().FloatText(iso, h + 12.f, selfVictim ? "회피!" : "빗나감", selfVictim ? Rgb(0xC8B8FF) : Rgb(0xB8B8B8), selfVictim ? 20.f : 16.f, 0.9f);
		if ((selfVictim || selfAttacker) && !throttleSfx)
		{
			PlayAt(static_cast<int>(sfx::Id::Miss), v->pos, 0.7f, 0.1f);
			m_lastHitSfxAt = m_now;
		}
		return;
	}

	// ── 숫자 ──
	fx::DamageStyle style = fx::DamageStyle::Other;
	if (dot) style = p.skillId == 9 ? fx::DamageStyle::DotBurn : fx::DamageStyle::DotPoison;
	else if (selfAttacker) style = crit ? fx::DamageStyle::Crit : ((skill || combo) ? fx::DamageStyle::Skill : fx::DamageStyle::Normal);
	else if (selfVictim) style = guard ? fx::DamageStyle::Guarded : (crit ? fx::DamageStyle::TakenCrit : fx::DamageStyle::Taken);
	uint64_t stackKey = 0;
	if (!dot && p.skillId != 0)
		if (const SkillRow* r = SkillTable::Find(p.skillId); r && r->hits > 1)
			stackKey = (static_cast<uint64_t>(p.victimId.GetSerial()) << 20) ^ (static_cast<uint64_t>(p.victimId.GetDatabaseID()) << 8) ^ p.skillId ^ (static_cast<uint64_t>(p.attackerId.GetSerial()) << 40);
	if (dot && selfAttacker)
		stackKey = (static_cast<uint64_t>(p.victimId.GetSerial()) << 20) ^ (static_cast<uint64_t>(p.victimId.GetDatabaseID()) << 8) ^ 0xD07u;
	fx::Fx().Damage(iso, h + 6.f, p.damage, style, stackKey);

	if (dot)
	{
		skillfx::OnHit(p.skillId, iso, h, false, 0, 0, true);
		if (selfAttacker && !throttleSfx) { PlayAt(static_cast<int>(sfx::Id::DotTick), v->pos, 0.45f, 0.2f); m_lastHitSfxAt = m_now; }
		return;
	}

	// ── 적중 연출 ──
	float dirSign = 1.f;
	Vector2 hitDir{ 0, 0 };
	if (a)
	{
		dirSign = ((v->pos.x - v->pos.y) - (a->pos.x - a->pos.y)) >= 0.f ? 1.f : -1.f;
		hitDir = util::Sub(iso, TileToIso(a->pos));
	}
	const Color bodyColor = (v->kind == EntityKind::Monster && v->row) ? Rgb(v->row->color1) : Rgb(0xC83A3A);
	fx::Fx().HitSpark(iso, h, bodyColor, crit, dirSign);
	if (p.skillId != 0)
		skillfx::OnHit(p.skillId, iso, h, crit, combo ? p.comboId : 0, p.flags2, false);
	if (guard)
		fx::Fx().Ring(util::Sub(iso, Vector2{ 0, h * 0.45f }), 10.f, 44.f, 0.25f, Rgb(0xFFD070, 230), 3.f);

	// 번개 폭발: 앞 대상(또는 시전자 손끝)에서 이 대상으로 줄기를 긋는다
	if (p.skillId == 11)
	{
		Vector2 from = a ? TileToIso(a->pos) : iso;
		float fromZ = actors::PlayerHeight() * 0.9f;
		for (auto it = m_chainHops.rbegin(); it != m_chainHops.rend(); ++it)
			if (it->caster == p.attackerId && m_now - it->at < 0.6 && p.hitIndex > 0) { from = it->iso; fromZ = it->z; break; }
		skillfx::Bolt(from, fromZ, iso, h * 0.55f, Rgb(0xCFE0FF), 0.24f);
		m_chainHops.push_back(ChainHop{ p.attackerId, iso, h * 0.55f, m_now });
		if (!throttleSfx) PlayAt(static_cast<int>(sfx::Id::Thunder), v->pos, 0.45f, 0.15f);
	}

	// ── 연계 이름 ──
	if (combo)
	{
		const uint8_t cid = p.comboId;
		if (m_now - m_comboShownAt[cid % m_comboShownAt.size()] > 0.45)
		{
			m_comboShownAt[cid % m_comboShownAt.size()] = m_now;
			const std::string name = std::string(ComboTable::Name(cid)) + "!";
			fx::Fx().FloatText(iso, h + 36.f, name, Rgb(0xFFD25A), (selfAttacker || selfVictim) ? 26.f : 18.f, 1.3f);
			if (selfAttacker)
			{
				m_comboName = ComboTable::Name(cid);
				m_comboNameAt = m_now;
				sfx::Play(sfx::Id::ComboSting, 0.7f, 0.02f);
				fx::Fx().Flash(Rgb(0xFFE0A0), 0.1f, 0.14f);
			}
			if (cid == 8) PlayAt(static_cast<int>(sfx::Id::Counter), v->pos, 0.8f, 0.05f);
			if (cid == 9) PlayAt(static_cast<int>(sfx::Id::WallSlam), v->pos, 0.8f, 0.05f);
		}
	}

	// ── 체감 ──
	if (selfAttacker)
	{
		const skillfx::Feel feel = skillfx::HitFeel(p.skillId, crit, combo, killed);
		m_feelTrauma = std::max(m_feelTrauma, feel.trauma);
		m_feelKick = std::max(m_feelKick, feel.kick);
		m_feelStop = std::max(m_feelStop, feel.hitstop);
		m_cam.Nudge(hitDir, heavy ? 6.f : (crit ? 4.f : 2.f));
		if (!throttleSfx)
		{
			PlayAt(static_cast<int>(HitSfx(p.skillId, crit, heavy)), v->pos, 1.f, 0.12f);
			m_lastHitSfxAt = m_now;
		}
		if (killed)
		{
			m_feelStop = std::max(m_feelStop, 0.08f);
			m_feelKick = std::max(m_feelKick, 0.03f);
		}
		AddCombo(1);
	}
	else if (selfVictim)
	{
		sfx::Play(guard ? sfx::Id::ShieldBash : sfx::Id::PlayerHurt, guard ? 0.45f : 0.8f, 0.1f);
		if (a && a->kind == EntityKind::Monster && !throttleSfx) PlayAt(static_cast<int>(sfx::Id::MonsterSwipe), a->pos, 0.6f);
		m_feelTrauma = std::max(m_feelTrauma, bossSkill ? 0.45f : (crit ? 0.3f : 0.16f));
		fx::Fx().Flash(Rgb(0xFF2A1A), bossSkill ? 0.3f : (guard ? 0.06f : 0.13f), 0.2f);
		m_cam.Nudge(hitDir, bossSkill ? 7.f : 3.f);
		m_lastHitSfxAt = m_now;
	}
	else if (!throttleSfx)
	{
		PlayAt(static_cast<int>(sfx::Id::Hit), v->pos, 0.4f, 0.15f);
		m_lastHitSfxAt = m_now;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 매 프레임
// ─────────────────────────────────────────────────────────────────────────────
void Game::TickCombat(float dt)
{
	const float sdt = dt * fx::Fx().TimeScale();
	const float nowf = static_cast<float>(m_now);

	// 1) 지연된 적중(도착 순서대로)
	if (!m_delayedHits.empty())
	{
		std::vector<DelayedHit> due, keep;
		for (auto& d : m_delayedHits) (d.at <= m_now ? due : keep).push_back(d);
		m_delayedHits.swap(keep);
		std::stable_sort(due.begin(), due.end(), [](const DelayedHit& x, const DelayedHit& y) { return x.at < y.at; });
		for (const auto& d : due)
			HandleDamage(d.p);
	}
	// 2) 투사체가 닿은 뒤로 미뤄 둔 쓰러짐
	for (size_t i = 0; i < m_deferredDies.size();)
	{
		if (!HasDelayedHitFor(m_deferredDies[i].id))
		{
			const SUBJECT_DIE_NFY_PACKET die = m_deferredDies[i];
			m_deferredDies.erase(m_deferredDies.begin() + static_cast<std::ptrdiff_t>(i));
			HandleSubjectDie(die);
		}
		else
			++i;
	}
	// 3) 이번 프레임의 적중 체감(여럿이 맞아도 최댓값 한 번)
	if (m_feelTrauma > 0.f) { m_cam.AddTrauma(m_feelTrauma); m_feelTrauma = 0.f; }
	if (m_feelKick > 0.f) { m_cam.Kick(m_feelKick); m_feelKick = 0.f; }
	if (m_feelStop > 0.f) { fx::Fx().HitStop(m_feelStop); m_feelStop = 0.f; }

	// 4) 개체별: 기술 모션 · 예고 · 도약 · 상태 파티클
	for (auto& [id, e] : m_ents)
	{
		if (e.animT >= 0.f)
		{
			e.animT += sdt;
			if (e.animT > e.animDur) { e.animT = -1.f; e.animSkill = 0; }
		}
		e.knockT = std::max(0.f, e.knockT - sdt * 4.f);
		e.stunShake = std::max(0.f, e.stunShake - sdt * 3.f);
		if (e.hopT >= 0.f)
		{
			e.hopT += sdt / 0.32f;
			if (e.hopT >= 1.f)
			{
				e.hopT = -1.f;
				fx::Fx().Dust(TileToIso(e.pos), 5.f);
			}
		}
		auto& t = e.tele;
		if (t.dur > 0.f && t.t < t.dur + 1.f)
		{
			const float before = t.t;
			t.t += dt;
			if (t.active)
			{
				e.windup = util::Clamp01(t.t / t.dur);
				if (t.pattern == static_cast<uint8_t>(MonsterPattern::Ranged) && before < t.dur * 0.62f && t.t >= t.dur * 0.62f)
				{
					// 던지기: 예고 끝에 딱 맞춰 떨어지도록 남은 시간 동안 날아간다
					const float h = EntityHeight(e);
					skillfx::Shot(TileToIso(e.pos), h * 0.7f, gfx::IsoPx(t.c.x, t.c.y), 10.f, t.dur * 0.38f, e.row ? Rgb(e.row->color2) : Rgb(0xE8D8B0));
				}
				if (t.t >= t.dur)
				{
					t.active = false;
					e.windup = 0.f;
					OnTelegraphImpact(e);
				}
			}
		}
		if (!e.dead && e.kind != EntityKind::Item)
			skillfx::EmitStatus(e, TileToIso(e.pos), EntityHeight(e), sdt, nowf);
	}

	// 5) 선입력
	if (m_bufferSlot >= 0)
	{
		if (m_now - m_bufferAt > 0.5)
			m_bufferSlot = -1;
		else if (m_now >= m_skillReady[m_bufferSlot] - 0.02)
		{
			const int s = m_bufferSlot;
			m_bufferSlot = -1;
			TryCastSkill(s, true);
		}
	}
	// 6) 돌진 착지를 너무 오래 기다리면 풀어 준다(응답 유실 대비)
	if (m_dashWaitSeq != 0 && m_now > m_dashWaitUntil)
		m_dashWaitSeq = 0;
	// 7) 쿨다운이 끝난 기술: 반짝 + 작은 소리(긴 쿨다운만)
	for (int s = 1; s < 6; ++s)
	{
		const bool readyNow = m_now >= m_skillReady[s];
		if (readyNow && m_readyPrev[s] > 0.5 && m_skillCdTotal[s] >= 2.5)
		{
			m_skillFlash[s] = m_now;
			sfx::Play(sfx::Id::SkillReady, 0.3f, 0.02f);
		}
		m_readyPrev[s] = readyNow ? 0.0 : 1.0;
	}
	// 8) 연속 타격 수
	if (m_comboHits > 0 && m_now - m_comboLastAt > 3.0)
		m_comboHits = 0;
	// 9) MP 표시값
	const float mpRatio = m_maxmp > 0 ? static_cast<float>(m_mp) / static_cast<float>(m_maxmp) : 0.f;
	m_mpShown = util::Damp(m_mpShown, mpRatio, 14.f, dt);
	// 10) 번개 연쇄 기록 정리
	m_chainHops.erase(std::remove_if(m_chainHops.begin(), m_chainHops.end(), [&](const ChainHop& c) { return m_now - c.at > 1.0; }), m_chainHops.end());
}
