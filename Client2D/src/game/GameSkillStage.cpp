#include "game/Game.h"
#include "game/SkillView.h"
#include "fx/Effects.h"
#include "fx/SkillFx.h"
#include "fx/FxScale.h"
#include "fx/Vfx.h"
#include "fx/Trail.h"
#include "render/Draw.h"
#include "render/Actors.h"
#include "render/Sprites.h"
#include "core/Util.h"

#include "rlgl.h"
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// 기술 창 시연(Phase 2-D) — "마우스를 올리면 작은 화면에서 그 기술 시연이 반복 재생".
//
//  · 무대는 월드 밖의 먼 칸(kStage). 실제 연출 시스템(skillfx · vfx · trail · 입자)을 그대로 부르고,
//    그 칸만 비추는 카메라로 렌더 텍스처에 그린다 — 게임 화면의 카메라에는 무대가 들어오지 않는다.
//  · fxscale::StageScope: 연출은 내 것처럼 100%, 화면 효과(암전 · 번쩍임 · 흔들림)와 소리는 없다
//    (소리는 듣는 이(내 캐릭터)와 멀어서 줄어 없어진다).
//  · 시각표는 서버와 같은 표(갈래를 입힌 복사본 skillview::Def) — 콤보는 끝까지, 차지는 완벽 구간, 홀딩은 60%, 체인은 끝 단까지.
//  · 창이 닫히면 아무것도 하지 않는다(렌더 텍스처는 처음 열 때 만든다).
// ─────────────────────────────────────────────────────────────────────────────

using util::Rgb;

namespace
{
	constexpr float kStageX = -620.f, kStageY = -600.f;   // 무대 시전자 칸(월드 밖)
	constexpr int kStageW = 432, kStageH = 232;

	Vector2 TileIso(Vector2 t) { return gfx::IsoPx(t.x, t.y); }
	Vector2 ScreenDirOf(Vector2 dirTile) { return util::Norm(Vector2{ (dirTile.x - dirTile.y) * gfx::kHalfW, (dirTile.x + dirTile.y) * gfx::kHalfH }); }

	float Height(const Entity& e)
	{
		if (e.kind == EntityKind::Monster && e.row) return actors::MonsterHeight(*e.row);
		return actors::PlayerHeight();
	}

	// 서버 ScheduleHits와 같은 거르기(GameSkill RowActive와 같다)
	bool Active(const SkillDef& d, int i, uint8_t stage, bool perfect, bool releaseOnly)
	{
		const SkillHit& h = d.hits[i];
		if (h.flags & HF_Tick) return false;
		if (releaseOnly != ((h.flags & HF_Release) != 0)) return false;
		if ((h.flags & HF_Perfect) && !perfect) return false;
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
}

void Game::TickSkillStage(float dt)
{
	SkillStage& st = m_stage;
	// 보여 달라는 쪽이 없으면 멈춘다(그리는 쪽이 매 프레임 부른다 — 0.25초 여유)
	if (st.want == 0 || m_now - st.wantAt > 0.25)
	{
		st.skill = 0;
		return;
	}
	const uint16_t id = st.want;
	const uint8_t vbits = st.wantBits;
	const SkillDef* def = skillview::Def(id, vbits);
	const SkillRow* row = SkillTable::Find(id);
	if (!def || !row) { st.skill = 0; return; }

	const Vector2 casterTile{ kStageX, kStageY };
	const Vector2 dirTile{ 1.f, 0.f };
	// ── 새로 시작(기술 · 갈래가 바뀜 · 한 바퀴 끝) ──
	if (st.skill != id || st.flags != vbits || m_now >= st.t0 + st.endAt)
	{
		st.skill = id;
		st.flags = vbits;
		st.t0 = m_now;
		st.step = 0;
		st.evts.clear();
		st.beats.clear();
		// 무대 배우: 시전자(내 모습) + 허수아비 셋
		Entity me = Self() ? *Self() : Entity{};
		st.caster = Entity{};
		st.caster.id = ObjID(EnumCategory::eUser, 0x7FFFFF01u);
		st.caster.kind = EntityKind::Player;
		st.caster.classId = static_cast<uint8_t>(m_classId);
		st.caster.itemId = me.itemId;
		st.caster.gear = me.gear;
		st.caster.gearRarity = me.gearRarity;
		st.caster.gearKnown = me.gearKnown;
		st.caster.weaponGrade = me.weaponGrade;
		st.caster.enchant = me.enchant;
		st.caster.nameHash = me.nameHash;
		st.caster.pos = st.caster.logPos = casterTile;
		st.caster.tx = static_cast<int>(kStageX); st.caster.ty = static_cast<int>(kStageY);
		st.caster.faceRight = true;
		st.caster.spawnT = 1.f;
		st.caster.hp = st.caster.maxhp = 1;
		const MonsterTableRow* dummyRow = MonsterTable::Find(MonsterTable::Id::TrainingDummy);
		const Vector2 spots[3] = { { kStageX + 2.f, kStageY }, { kStageX + 3.f, kStageY - 1.f }, { kStageX + 3.f, kStageY + 1.f } };
		for (int k = 0; k < 3; ++k)
		{
			Entity& d = st.dummies[k];
			d = Entity{};
			d.id = ObjID(EnumCategory::eMonster, MONSTER_ID_START + 0x0FFFFFF0u + static_cast<uint32_t>(k));
			d.kind = EntityKind::Monster;
			d.row = dummyRow;
			d.species = MonsterTable::Id::TrainingDummy;
			d.pos = d.logPos = spots[k];
			d.tx = static_cast<int>(spots[k].x); d.ty = static_cast<int>(spots[k].y);
			d.faceRight = false;
			d.spawnT = 1.f;
			d.hp = d.maxhp = 1;
			d.nameHash = 17u + static_cast<uint32_t>(k) * 31u;
		}
		// ── 이번 바퀴의 시각표 ──
		auto addEvt = [&](float at, SkillExPhase phase, uint8_t stage, uint8_t flags, bool release, bool beats)
		{
			SkillStage::Evt e;
			e.at = at; e.phase = static_cast<uint8_t>(phase); e.stage = stage; e.flags = flags; e.release = release;
			st.evts.push_back(e);
			if (!beats) return;
			for (int i = 0; i < def->hitCount; ++i)
				if (Active(*def, i, stage, (flags & SKILLEX_PERFECT) != 0, release))
					st.beats.push_back(SkillStage::Beat{ at + def->hits[i].atMs / 1000.f, static_cast<uint8_t>(i), static_cast<uint8_t>(st.evts.size() - 1), false });
		};
		float end = 0.f;
		switch (def->kind)
		{
		case SkillKind::Basic:
			for (uint8_t s = 1; s <= std::max<uint8_t>(1, def->stages); ++s)
				addEvt((s - 1) * 0.42f, SkillExPhase::Start, s, 0, false, true);
			end = std::max<uint8_t>(1, def->stages) * 0.42f;
			break;
		case SkillKind::Charge:
		{
			const float rel = (def->stageMs * def->stages + def->perfectMs * 0.5f) / 1000.f;
			addEvt(0.f, SkillExPhase::ChargeBegin, 0, 0, false, false);
			addEvt(rel, SkillExPhase::ChargeRelease, def->stages, SKILLEX_PERFECT, false, true);
			st.holdFrom = 0.f; st.holdTo = rel;
			end = rel;
			break;
		}
		case SkillKind::Hold:
		{
			const float rel = def->holdMaxMs * 0.6f / 1000.f;
			addEvt(0.f, SkillExPhase::HoldBegin, 0, 0, false, false);
			for (float t = SkillDefTable::kHoldFirstTickMs / 1000.f; t < rel && def->tickMs > 0; t += def->tickMs / 1000.f)
				for (int i = 0; i < def->hitCount; ++i)
					if (def->hits[i].flags & HF_Tick) st.beats.push_back(SkillStage::Beat{ t, static_cast<uint8_t>(i), 0, true });
			addEvt(rel, SkillExPhase::HoldEnd, 0, 0, true, true);
			st.holdFrom = 0.f; st.holdTo = rel;
			end = rel;
			break;
		}
		case SkillKind::Chain:
			for (uint8_t s = 1; s <= std::max<uint8_t>(1, def->stages); ++s)
				addEvt((s - 1) * 0.8f, SkillExPhase::ChainStep, s, 0, false, true);
			end = std::max<uint8_t>(1, def->stages) * 0.8f;
			break;
		case SkillKind::Toggle:
			addEvt(0.f, SkillExPhase::ToggleOn, 0, 0, false, false);
			addEvt(2.2f, SkillExPhase::ToggleOff, 0, 0, false, false);
			end = 2.2f;
			break;
		case SkillKind::Identity:
			addEvt(0.f, SkillExPhase::Identity, 0, 0, false, true);
			break;
		default:
			addEvt(0.f, SkillExPhase::Start, 0, 0, false, true);
			break;
		}
		if (def->kind != SkillKind::Charge && def->kind != SkillKind::Hold) { st.holdFrom = st.holdTo = -1.f; }
		for (const auto& b : st.beats) end = std::max(end, b.at);
		end = std::max(end, def->castMs / 1000.f);
		st.endAt = end + (def->kind == SkillKind::Awaken ? 2.4f : 1.6f);
	}

	// ── 이번 프레임: 모든 연출은 무대 범위에서(화면 효과 · 소리 없이, 세기 100%) ──
	fxscale::StageScope stage;
	const float t = static_cast<float>(m_now - st.t0);
	auto ctxFor = [&](const SkillStage::Evt& e)
	{
		skillfx::ExCtx c;
		c.skillId = id;
		c.phase = e.phase;
		c.stage = e.stage;
		c.flags = static_cast<uint8_t>(e.flags | vbits);
		c.casterIso = TileIso(st.caster.pos);
		const Vector2 aim{ kStageX + 3.f, kStageY };
		c.aimIso = TileIso(aim);
		c.targetIso = TileIso(st.dummies[0].pos);
		c.dirTile = dirTile;
		c.dir = ScreenDirOf(dirTile);
		c.landIso = TileIso(Vector2{ kStageX + (row->dash > 0 ? std::min<float>(row->dash, 3.f) : 3.f), kStageY });
		c.casterHeight = actors::PlayerHeight();
		c.targetHeight = Height(st.dummies[0]);
		c.faceRight = true;
		c.mine = true;
		c.classId = static_cast<uint8_t>(m_classId);
		c.castSec = def->castMs / 1000.f;
		for (int k = 0; k < 3 && c.nPts < 8; ++k)
		{
			c.pts[c.nPts] = TileIso(st.dummies[k].pos);
			c.ptsZ[c.nPts] = Height(st.dummies[k]) * 0.5f;
			++c.nPts;
		}
		return c;
	};
	// 단계 알림
	while (st.step < static_cast<int>(st.evts.size()) && t >= st.evts[static_cast<size_t>(st.step)].at)
	{
		const SkillStage::Evt& e = st.evts[static_cast<size_t>(st.step)];
		skillfx::ExCtx c = ctxFor(e);
		skillfx::OnExPhase(c);
		const auto ph = static_cast<SkillExPhase>(e.phase);
		if (def->kind == SkillKind::Basic) { st.caster.attackT = 0.f; st.caster.animT = -1.f; }
		else if (ph == SkillExPhase::ChargeBegin)
		{
			st.caster.animSkill = id; st.caster.animT = 0.f;
			st.caster.animDur = SkillDefTable::ChargeMaxMs(*def) / 1000.f + 0.3f; st.caster.animCast = st.caster.animDur;
		}
		else if (ph != SkillExPhase::ToggleOff && ph != SkillExPhase::HoldEnd)
		{
			st.caster.animSkill = id; st.caster.animT = 0.f;
			st.caster.animDur = std::max(skillfx::AnimDuration(id), def->lockMs / 1000.f);
			if (ph == SkillExPhase::ChargeRelease) st.caster.animDur = std::max(0.35f, def->lockMs / 1000.f);
			st.caster.animCast = ph == SkillExPhase::ChargeRelease ? 0.f : def->castMs / 1000.f;
		}
		// 켜고 끄기(철벽 태세): 켜 둔 동안 몸에 방벽 모양
		if (ph == SkillExPhase::ToggleOn) st.caster.statuses[0] = Entity::StatusFx{ static_cast<uint8_t>(StatusId::Guard), 1, static_cast<float>(m_now) + 2.2f };
		// 이동 기술: 착지까지 미끄러진다(한 바퀴 끝에 제자리로)
		if ((def->moveMs > 0 || def->kind == SkillKind::Move || def->special == SkillSpecial::Blink || def->special == SkillSpecial::ShadowStep)
			&& (ph == SkillExPhase::Start || ph == SkillExPhase::ChargeRelease))
		{
			st.slideFrom = st.caster.pos;
			st.slideTo = def->special == SkillSpecial::ShadowStep ? Vector2{ st.dummies[0].pos.x + 1.f, st.dummies[0].pos.y } : Vector2{ kStageX + std::min<float>(row->dash > 0 ? row->dash : 3.f, 3.f), kStageY };
			st.slideAt = t;
			st.slideDur = def->moveMs > 0 ? def->moveMs * 3.f / 1000.f : 0.f;
		}
		++st.step;
	}
	// 모으기 · 돌기 동안
	if (st.holdTo > 0.f && t >= st.holdFrom && t < st.holdTo && !st.evts.empty())
	{
		skillfx::ExCtx c = ctxFor(st.evts.front());
		const uint32_t heldMs = static_cast<uint32_t>((t - st.holdFrom) * 1000.f);
		const uint8_t stg = def->kind == SkillKind::Charge ? SkillDefTable::ChargeStage(*def, heldMs) : 0;
		const bool perfect = def->kind == SkillKind::Charge && heldMs >= static_cast<uint32_t>(def->stageMs) * def->stages;
		skillfx::OnExHeld(c, t - st.holdFrom, stg, perfect, dt);
	}
	// 타격
	for (auto& b : st.beats)
	{
		if (b.fired || t < b.at) continue;
		b.fired = true;
		const SkillHit& h = def->hits[b.hit];
		const SkillStage::Evt& e = st.evts[std::min<size_t>(b.evt, st.evts.size() - 1)];
		skillfx::ExCtx c = ctxFor(e);
		Vector2 hdir = dirTile;
		SkillDefTable::TurnDir(h, hdir.x, hdir.y);
		Vector2 center = h.anchor == HitAnchor::Caster ? st.caster.pos : (h.anchor == HitAnchor::Target ? st.dummies[0].pos : Vector2{ kStageX + 3.f, kStageY });
		if (h.along != 0) center = Vector2{ center.x + std::round(hdir.x * h.along), center.y + std::round(hdir.y * h.along) };
		int n = 0;
		for (auto& d : st.dummies)
			if (SkillDefTable::InsideHit(h, center.x, center.y, hdir.x, hdir.y, d.pos.x, d.pos.y))
			{
				++n;
				d.hurtT = 1.f; d.flashT = 1.f;
				if (h.flags & HF_Heavy) d.knockT = 1.f;
				// 실제 게임과 같은 대상 연출: 예전 판정 기술은 예전 연출(OnHit), 나머지는 표의 타격 연출(OnExVictim)
				if (def->special == SkillSpecial::Legacy || def->kind == SkillKind::Move || def->special == SkillSpecial::Dash || def->special == SkillSpecial::ShadowStep)
					skillfx::OnHit(id, TileIso(d.pos), Height(d), false, 0, (h.flags & HF_Heavy) ? DAMAGE2_HEAVY : 0, false);
				else
					skillfx::OnExVictim(id, b.hit, TileIso(d.pos), Height(d), false, 0, (h.flags & HF_Heavy) ? DAMAGE2_HEAVY : 0);
			}
		if (def->special == SkillSpecial::DuskAwaken || def->special == SkillSpecial::MoonCleave || def->special == SkillSpecial::ShieldBounce)
			n = c.nPts;
		skillfx::OnExBeat(c, b.hit, TileIso(center), n);
		// 갈래 「장판」: 마지막 타격 자리에
		if (!b.tick)
		{
			bool last = true;
			for (const auto& o : st.beats) if (!o.tick && !o.fired && def->hits[o.hit].tag == HT_None) { last = false; break; }
			if (last && h.tag == HT_None)
				if (const auto* lv = skillview::Mod(id, vbits, SkillGrowth::Mod::Lasting))
					skillfx::Linger(TileIso(center), h.shape == HitShape::Target ? 1.f : std::clamp<float>(h.a, 1.f, 4.f), static_cast<float>(lv->value), lv->color, lv->status);
		}
	}

	// ── 배우 시간 ──
	auto age = [&](Entity& e)
	{
		if (e.attackT >= 0.f) { e.attackT += dt; if (e.attackT > 0.6f) e.attackT = -1.f; }
		if (e.animT >= 0.f) { e.animT += dt; if (e.animT > e.animDur) { e.animT = -1.f; e.animSkill = 0; } }
		e.hurtT = std::max(0.f, e.hurtT - dt * 5.f);
		e.flashT = std::max(0.f, e.flashT - dt * 7.f);
		e.knockT = std::max(0.f, e.knockT - dt * 4.f);
		e.stunShake = std::max(0.f, e.stunShake - dt * 3.f);
	};
	age(st.caster);
	for (auto& d : st.dummies) age(d);
	// 미끄러짐 · 한 바퀴 끝에 제자리
	if (st.slideDur >= 0.f && st.slideAt >= 0.f)
	{
		const float u = st.slideDur > 0.f ? util::Clamp01((t - st.slideAt) / st.slideDur) : 1.f;
		st.caster.pos = util::LerpV(st.slideFrom, st.slideTo, util::EaseOutCubic(u));
		st.caster.moving = u < 1.f ? 1.f : 0.f;
	}
	if (m_now + dt >= st.t0 + st.endAt)
	{
		st.caster.pos = casterTile;
		st.slideAt = -1.f;
	}
}

void Game::RenderSkillStage()
{
	SkillStage& st = m_stage;
	if (st.skill == 0) return;
	if (!st.ready)
	{
		st.rt = LoadRenderTexture(kStageW, kStageH);
		SetTextureFilter(st.rt.texture, TEXTURE_FILTER_BILINEAR);
		st.ready = st.rt.id != 0;
		if (!st.ready) return;
	}
	const Vector2 focus = TileIso(Vector2{ kStageX + 1.6f, kStageY });
	Camera2D cam{};
	cam.target = Vector2{ focus.x, focus.y - 26.f };
	cam.offset = Vector2{ kStageW * 0.5f, kStageH * 0.5f + 8.f };
	cam.zoom = 0.86f;
	BeginTextureMode(st.rt);
	ClearBackground(Color{ 17, 21, 25, 255 });
	BeginMode2D(cam);
	gfx::BeginWorld(kStageX + kStageY + 1.6f, cam.zoom);
	sprites::BeginWorld(0.35f);
	gfx::DepthTest(true);
	gfx::DepthWrite(false);
	gfx::SetDepthRaw(-0.97f);
	// 바닥: 훈련장 흙 마당(마름모 칸) — 가장자리로 갈수록 어두워진다
	for (int y = -4; y <= 4; ++y)
		for (int x = -4; x <= 7; ++x)
		{
			const float X = kStageX + x, Y = kStageY + y;
			const float d = std::sqrt((x - 1.5f) * (x - 1.5f) + y * y * 1.6f);
			const float k = util::Clamp01(1.f - d / 6.5f);
			const Color base = ((x + y) & 1) ? Color{ 58, 52, 42, 255 } : Color{ 64, 57, 46, 255 };
			const Color c = util::Mix(Color{ 17, 21, 25, 255 }, base, k);
			const Vector2 q[4] = { gfx::IsoPx(X - 0.5f, Y - 0.5f), gfx::IsoPx(X + 0.5f, Y - 0.5f), gfx::IsoPx(X + 0.5f, Y + 0.5f), gfx::IsoPx(X - 0.5f, Y + 0.5f) };
			gfx::Quad(q[0], q[1], q[2], q[3], c);
		}
	fx::Fx().DrawGround();
	skillfx::DrawGround();
	vfx::DrawGround();
	gfx::DepthWrite(true);
	for (auto& d : st.dummies) { gfx::ResetMod(); actors::DrawShadow(TileIso(d.pos), 14.f, 1.f); }
	gfx::ResetMod();
	actors::DrawShadow(TileIso(st.caster.pos), 16.f, 1.f);
	for (auto& d : st.dummies) DrawEntity(d, false);
	DrawEntity(st.caster, false);
	gfx::DepthWrite(false);
	gfx::ResetMod();
	skillfx::DrawStatus(st.caster, TileIso(st.caster.pos), actors::PlayerHeight(), static_cast<float>(m_now));
	gfx::ResetMod();
	fx::Fx().DrawWorld();
	skillfx::DrawWorld();
	trail::Draw();
	vfx::DrawWorld();
	sprites::EndWorld();
	gfx::Flush();
	gfx::DepthTest(false);
	gfx::DepthWrite(true);
	gfx::ResetMod();
	EndMode2D();
	EndTextureMode();
}

void Game::DrawSkillStage(Rectangle r, uint16_t skillId, uint8_t varBits, const char* caption)
{
	SkillStage& st = m_stage;
	st.want = skillId;
	st.wantBits = varBits;
	st.wantAt = m_now;
	ui::Plate(Rectangle{ r.x - 1, r.y - 1, r.width + 2, r.height + 2 }, Color{ 10, 12, 15, 255 }, ui::theme::kLineStrong, 3.f, 1.f);
	// 아직 이 기술 · 갈래로 한 바퀴를 시작하지 않았다(다음 프레임부터)
	if (!st.ready || st.skill != skillId || st.flags != varBits)
	{
		ui::Text("시연 준비 중", Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f - 7.f }, ui::type::kCaption, ui::theme::kTextMute, ui::Align::Center);
		return;
	}
	// 렌더 텍스처는 위아래가 뒤집혀 있다
	const float aspect = static_cast<float>(kStageW) / kStageH;
	float w = r.width, h = r.width / aspect;
	if (h > r.height) { h = r.height; w = h * aspect; }
	const Rectangle dst{ std::round(r.x + (r.width - w) * 0.5f), std::round(r.y + (r.height - h) * 0.5f), std::round(w), std::round(h) };
	DrawTexturePro(st.rt.texture, Rectangle{ 0, 0, static_cast<float>(kStageW), -static_cast<float>(kStageH) }, dst, Vector2{ 0, 0 }, 0.f, WHITE);
	// 시간 막대(한 바퀴)
	const float u = util::Clamp01(static_cast<float>(m_now - st.t0) / std::max(0.1f, st.endAt));
	DrawRectangleRec(Rectangle{ dst.x, dst.y + dst.height - 2.f, dst.width * u, 2.f }, util::WithA(ui::theme::kDawn, 0.8f));
	ui::Text(caption && caption[0] ? caption : "시연", Vector2{ dst.x + 8.f, dst.y + 6.f }, ui::type::kMicro, util::WithA(ui::theme::kTextDim, 0.9f));
}
