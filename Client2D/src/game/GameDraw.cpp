#include "game/Game.h"
#include "game/Texts.h"
#include "fx/Effects.h"
#include "fx/SkillFx.h"
#include "render/Draw.h"
#include "render/Actors.h"
#include "render/Sprites.h"
#include "ui/UiArt.h"
#include "render/Icons.h"
#include "render/Props.h"
#include "render/Assets.h"
#include "ui/Font.h"
#include "core/Util.h"
#include "game/Input.h"
#include "game/KeyBinds.h"

#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

using util::Rgb;

namespace
{
	float EntityHeight(const Entity& e)
	{
		if (e.kind == EntityKind::Monster && e.row) return actors::MonsterHeight(*e.row);
		if (e.kind == EntityKind::Item) return 30.f;
		return actors::PlayerHeight();
	}

	bool IsTranslucent(const Entity& e)
	{
		if (e.kind == EntityKind::Item) return true;
		if (e.removing || e.dead || e.spawnT < 1.f) return true;
		if (e.row && e.row->archetype == MonsterArchetype::eGhost) return true;
		return false;
	}

	int Cheb(int ax, int ay, int bx, int by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

	// 바닥 장비의 희귀도: 서버가 알려 준 값(SUBJECT_INFO_EX_NFY)과 표의 바닥 중 높은 쪽
	Rarity FieldRarity(const Entity& e)
	{
		const ItemTableRow* row = ItemTableData::Find(e.itemId);
		if (!row || row->type != ItemType::eEquipment) return Rarity::Common;
		const int sent = std::min<int>(e.weaponGrade, static_cast<int>(Rarity::Count) - 1);
		const int floor = static_cast<int>(ItemRules::FloorRarity(*row));
		return static_cast<Rarity>(std::max(sent, floor));
	}
}

void Game::DrawGame()
{
	const Camera2D cam = m_cam.Get(m_screenW, m_screenH);
	m_cam2d = cam;
	Entity* me = Self();
	const Vector2 playerTile = me ? me->pos : Vector2{ static_cast<float>(m_predX), static_cast<float>(m_predY) };

	m_terrain.DrawGround(cam, m_screenW, m_screenH, playerTile, m_time, true, kb::Opt().showGrid ? 0.07f : 0.f);
	BeginMode2D(cam);
	DrawWorld(cam);
	EndMode2D();
	m_light.Apply(cam, m_screenW, m_screenH, props::FrameLights());

	// 화면 가장자리 어둡게 + 체력이 낮으면 붉게 맥동
	const auto& tex = assets::Get();
	DrawTexturePro(tex.vignette, Rectangle{ 0, 0, (float)tex.vignette.width, (float)tex.vignette.height },
		Rectangle{ 0, 0, (float)m_screenW, (float)m_screenH }, Vector2{ 0, 0 }, 0.f, Color{ 255, 255, 255, 150 });
	const float hpRatio = m_maxhp > 0 ? static_cast<float>(m_hp) / m_maxhp : 1.f;
	if (!m_dead && hpRatio < 0.3f)
	{
		const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 6.f);
		const float k = (0.3f - hpRatio) / 0.3f;
		DrawTexturePro(tex.vignette, Rectangle{ 0, 0, (float)tex.vignette.width, (float)tex.vignette.height },
			Rectangle{ -40, -40, (float)m_screenW + 80, (float)m_screenH + 80 }, Vector2{ 0, 0 }, 0.f,
			Color{ 255, 30, 20, static_cast<unsigned char>((90 + 110 * pulse) * k) });
	}
	// 내 발밑에 몬스터 강공격 예고가 깔렸다 — 화면 가장자리가 붉게 두 번 깜빡인다
	if (!m_dead && m_now - m_dangerAt < 0.9)
	{
		const float u = static_cast<float>((m_now - m_dangerAt) / 0.9);
		const float a = (1.f - u) * (0.55f + 0.45f * std::sin(u * util::kPi * 4.f));
		DrawTexturePro(tex.vignette, Rectangle{ 0, 0, (float)tex.vignette.width, (float)tex.vignette.height },
			Rectangle{ -30, -30, (float)m_screenW + 60, (float)m_screenH + 60 }, Vector2{ 0, 0 }, 0.f,
			Color{ 255, 60, 30, static_cast<unsigned char>(std::clamp(a, 0.f, 1.f) * 150) });
	}
	fx::Fx().DrawScreen(m_screenW, m_screenH);

	DrawWorldOverlay(cam);
	fx::Fx().DrawOverlay(cam);
	DrawQuestOverlay(cam);

	if (m_scene != Scene::Game)
		return;
	DrawQuestCompass(cam);
	if (m_bigMapOpen) DrawBigMap();   // 창이 아니다 — HUD 아래, 달리면서 본다
	DrawHud();
	DrawMinimap();
	DrawQuestTracker();
	DrawTargetFrame();
	DrawChat();
	DrawSkillBar();
	DrawQuickBar();
	DrawComboCounter();
	DrawBanners();
	DrawToasts();
	if (m_invOpen) DrawInventory();
	if (m_charOpen) DrawCharacter();
	if (m_questLogOpen) DrawQuestLog();
	if (m_dialogNpc && m_shopMode == ShopMode::None) DrawNpcDialog();
	if (m_shopMode == ShopMode::Buy) DrawShop();
	if (m_shopMode == ShopMode::Sell) DrawSell();
	if (m_shopMode == ShopMode::Teleport) DrawTeleport();
	if (m_enchant.stage >= 2) DrawEnchant();
	if (m_discard.open) DrawDiscard();
	if (m_mapOpen) DrawWorldMap();
	if (m_skillWinOpen) DrawSkillWindow();
	if (m_helpOpen) DrawHelp();
	DrawQuestScreenFx();
	if (m_dead) DrawDeath();
	// 맨 위: 시스템 메뉴 · 조작 설정(Esc)
	if (m_menuOpen) DrawSystemMenu();
	if (m_keysOpen) DrawKeySettings();
}

// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawWorld(const Camera2D& cam)
{
	Entity* me = Self();
	const Vector2 pt = me ? me->pos : Vector2{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
	gfx::BeginWorld(pt.x + pt.y, cam.zoom);
	props::FrameLights().clear();
	const float t = m_time;
	// 그린 스프라이트용 월드 셰이더(알파 테스트 · 번쩍임). 스프라이트가 아닌 도형에는 아무 일도 하지 않는다.
	sprites::BeginWorld(0.35f);

	// ── 1) 바닥 데칼: 깊이 테스트만(쓰기 X) ──
	gfx::DepthTest(true);
	gfx::DepthWrite(false);
	gfx::SetDepthRaw(-0.97f);

	// 마우스가 가리키는 칸(바닥 격자를 켰을 때만 — 설정 창)
	if (kb::Opt().showGrid && m_hoverX >= 0 && m_hover == ObjID{} && m_hoverNpc == 0 && !ui::MouseOverUi() && !m_dead)
	{
		const bool blocked = WorldData::IsBlocked(m_hoverX, m_hoverY);
		const float X = static_cast<float>(m_hoverX), Y = static_cast<float>(m_hoverY);
		const Vector2 c[4] = { gfx::IsoPx(X - 0.5f, Y - 0.5f), gfx::IsoPx(X + 0.5f, Y - 0.5f), gfx::IsoPx(X + 0.5f, Y + 0.5f), gfx::IsoPx(X - 0.5f, Y + 0.5f) };
		const Color col = blocked ? Rgb(0xFF5A4A, 150) : Rgb(0xFFFFFF, 110);
		gfx::Quad(c[0], c[1], c[2], c[3], util::MulA(col, 0.25f));
		for (int i = 0; i < 4; ++i) gfx::Line(c[i], c[(i + 1) % 4], 1.5f, col);
	}
	// 이동 목표 · 경로 점은 F3(계측)에서만 — 평소에는 클릭 고리(0.3초)가 알려 준다
	if (m_debugOverlay && m_goal.kind == GoalKind::Move)
	{
		const float pulse = 0.5f + 0.5f * std::sin(t * 8.f);
		gfx::Diamond(static_cast<float>(m_goal.x), static_cast<float>(m_goal.y), 0.55f + pulse * 0.15f, Rgb(0x8AF0A0, 70));
	}
	// 경로 점
	for (size_t i = 0; m_debugOverlay && i < m_path.size(); ++i)
	{
		const Vector2 p = gfx::IsoPx(static_cast<float>(m_path[i].x), static_cast<float>(m_path[i].y));
		gfx::Ellipse(p, 3.f, 1.5f, Rgb(0xFFFFFF, static_cast<uint8_t>(std::max(30, 120 - static_cast<int>(i) * 10))));
	}

	// 대상/마우스 고리 + 그림자
	for (auto& [id, e] : m_ents)
	{
		if (e.flyT >= 0.f) continue;
		const Vector2 base = TileToIso(e.pos);
		float alpha = util::EaseOutCubic(e.spawnT) * (e.removing ? (1.f - e.removeT) : 1.f);
		if (e.dead) alpha *= 1.f - util::Clamp01((e.deadT - (e.kind == EntityKind::Player ? 4.5f : 0.9f)) / 0.6f);
		float r = 15.f;
		if (e.kind == EntityKind::Monster && e.row) r = actors::MonsterShadow(*e.row);
		if (e.kind == EntityKind::Item) r = 10.f;
		if (e.row && e.row->archetype == MonsterArchetype::eGhost)
			alpha *= 0.5f;
		// 몸이 뜨면(도약 · 띄우기) 그림자는 땅에 남아 작고 옅어진다
		if (e.hopT >= 0.f)
		{
			const float h = std::sin(util::kPi * e.hopT);
			r *= 1.f - 0.35f * h;
			alpha *= 1.f - 0.45f * h;
		}
		actors::DrawShadow(base, r, alpha);

		if (id == m_target || id == m_hover)
		{
			const bool hostile = e.kind == EntityKind::Monster || (e.kind == EntityKind::Player && m_goal.force);
			const Color c = (id == m_target) ? (hostile ? Rgb(0xFF4A3A) : Rgb(0x7AC8FF)) : Rgb(0xFFFFFF, 160);
			const float pulse = 0.5f + 0.5f * std::sin(t * 7.f);
			const float rr = r * 1.35f + (id == m_target ? pulse * 3.f : 0.f);
			gfx::EllipseArc(base, rr, rr * 0.5f, 0.f, util::kPi * 2.f, id == m_target ? 2.5f : 1.5f, util::MulA(c, alpha));
			if (id == m_target)
				for (int k = 0; k < 4; ++k)
				{
					const float a = t * 1.5f + k * util::kPi * 0.5f;
					gfx::EllipseArc(base, rr + 5.f, (rr + 5.f) * 0.5f, a, a + 0.5f, 2.f, util::MulA(c, alpha * 0.8f));
				}
		}
		if (e.IsBoss() && !e.dead)
		{
			const float pulse = 0.6f + 0.4f * std::sin(t * 2.f);
			gfx::EllipseGrad(base, r * 3.f, r * 1.5f, Rgb(0xFF2A1A, static_cast<uint8_t>(70 * pulse)), Rgb(0xFF2A1A, 0));
		}
		if (e.kind == EntityKind::Item)
		{
			// 희귀도 바닥 빛: 높을수록 넓고 진하게, 영웅부터는 도는 고리
			const Rarity rar = FieldRarity(e);
			if (rar >= Rarity::Uncommon)
			{
				const int k = static_cast<int>(rar);
				const Color rc = icons::RarityColor(rar);
				gfx::EllipseGrad(base, 16.f + 4.f * k, 8.f + 2.f * k, util::MulA(rc, (0.3f + 0.08f * k) * alpha), util::WithA(rc, 0.f));
				if (rar >= Rarity::Heroic)
				{
					const float a0 = t * (1.f + 0.3f * (k - 3));
					for (int q = 0; q < 3; ++q)
					{
						const float a = a0 + q * 2.094f;
						gfx::EllipseArc(base, 18.f + 2.f * k, 9.f + k, a, a + 1.1f, 2.f, util::MulA(rc, 0.75f * alpha));
					}
				}
			}
			else if (const ItemTableRow* row = ItemTableData::Find(e.itemId); row && row->grade >= ItemGrade::eRare && row->type != ItemType::eEquipment)
				gfx::EllipseGrad(base, 22.f, 11.f, util::MulA(icons::GradeColor(row->grade), 0.5f), util::WithA(icons::GradeColor(row->grade), 0.f));
		}
	}
	for (const auto& [nid, v] : m_npcView)
	{
		if (!v.row) continue;
		const Vector2 base = TileToIso(v.pos);
		const bool animal = WorldData::IsAnimal(v.row->kind);
		const float sr = animal ? 10.f : (v.row->kind == WorldData::NpcKind::eChild ? 11.f : 15.f);
		actors::DrawShadow(base, sr * (v.hopT >= 0.f ? 0.8f : 1.f), 1.f);
		if (nid == m_hoverNpc || nid == m_dialogNpc)
			gfx::EllipseArc(base, sr + 5.f, (sr + 5.f) * 0.5f, 0.f, util::kPi * 2.f, 2.f, Rgb(0xFFE08A, 200));
	}
	fx::Fx().DrawGround();
	skillfx::DrawGround();
	gfx::SetDepthRaw(-0.97f);
	DrawQuestWorld(true);
	// 몬스터 강공격 예고(바닥이 차오른다) · 내 기술 사거리 미리보기
	for (auto& [id, e] : m_ents)
		if (e.tele.active && !e.dead)
			skillfx::DrawTelegraph(e, static_cast<float>(m_now));
	DrawSkillPreview();

	// ── 2) 불투명: 깊이 쓰기 ──
	gfx::DepthWrite(true);
	m_terrain.DrawProps(cam, m_screenW, m_screenH, pt, t, true);
	DrawNpcs();
	for (auto& [id, e] : m_ents)
		if (!IsTranslucent(e))
			DrawEntity(e, false);

	// ── 2.5) 가려진 내 캐릭터 실루엣(나무/건물/큰 보스 뒤에서도 위치가 보이게) ──
	// 깊이 쓰기를 켠 채로 GREATER 테스트: 먼저 그린 부위가 깊이를 차지하므로
	// 팔·몸통처럼 겹치는 부위가 두 번 섞이지 않고 균일한 한 겹 실루엣이 된다.
	if (me && !m_dead && me->spawnT >= 1.f)
	{
		gfx::DepthGreater(true);
		m_silhouetteDraw = true;
		DrawEntity(*me, true);
		m_silhouetteDraw = false;
		gfx::DepthGreater(false);
	}
	gfx::DepthWrite(false);
	sprites::SetAlphaTest(0.02f);   // 깊이를 안 쓰니 가장자리를 부드럽게(완전히 빈 곳만 버린다)

	// ── 3) 반투명: 깊이 쓰기 X ──
	for (auto& [id, e] : m_ents)
		if (IsTranslucent(e))
			DrawEntity(e, true);
	// 가속 잔상
	if (me && m_hasteUntil > m_now && me->speed > 1.f)
	{
		const Vector2 v = m_mover.Velocity();
		for (int k = 1; k <= 2; ++k)
		{
			Entity ghost = *me;
			ghost.pos = Vector2{ me->pos.x - v.x * 0.07f * k, me->pos.y - v.y * 0.07f * k };
			m_ghostDraw = true;
			DrawEntity(ghost, true);
			m_ghostDraw = false;
		}
	}
	// 상태이상 모양(기절 별·빙결 얼음·철벽 막·표식·중독 점)
	for (auto& [id, e] : m_ents)
	{
		if (e.kind == EntityKind::Item || e.dead || e.removing) continue;
		Vector2 base = TileToIso(e.pos);
		if (e.hopT >= 0.f) base.y -= std::sin(util::kPi * e.hopT) * e.hopH;
		gfx::ResetMod();
		skillfx::DrawStatus(e, base, EntityHeight(e), static_cast<float>(m_now));
	}
	gfx::ResetMod();
	DrawQuestWorld(false);
	fx::Fx().DrawWorld();
	skillfx::DrawWorld();

	sprites::EndWorld();
	gfx::Flush();
	gfx::DepthTest(false);
	gfx::DepthWrite(true);
	gfx::ResetMod();
}

void Game::DrawEntity(Entity& e, bool translucentPass)
{
	const bool isGhostCopy = m_ghostDraw;
	actors::Pose pose;
	pose.base = TileToIso(e.pos);
	pose.faceRight = e.faceRight;
	pose.faceBack = e.faceBack;
	pose.turn = e.body.turn;
	pose.lean = e.dead ? 0.f : e.body.lean;
	pose.bank = e.dead ? 0.f : e.body.bank;
	pose.settle = e.body.settle;
	pose.walk = e.walkPhase;
	pose.moving = e.moving;
	pose.hurt = e.hurtT;
	pose.time = m_time;
	pose.seed = e.nameHash ? e.nameHash : e.id.GetSerial() + static_cast<uint32_t>(e.id.GetDatabaseID());
	const float atkDur = e.kind == EntityKind::Player ? 0.42f : 0.46f;
	pose.attack = e.attackT >= 0.f ? util::Clamp01(e.attackT / atkDur) : -1.f;
	if (pose.attack >= 1.f) pose.attack = -1.f;
	pose.cast = e.castT >= 0.f ? util::Clamp01(e.castT / 0.7f) : 0.f;
	pose.dead = e.dead ? util::Clamp01(e.deadT / 0.7f) : 0.f;
	if (e.animT >= 0.f && e.animSkill != 0 && !e.dead)
	{
		pose.skill = e.animSkill;
		pose.skillT = util::Clamp01(e.animT / std::max(0.05f, e.animDur));
		pose.skillHold = e.animDur > 0.f ? util::Clamp01(e.animCast / e.animDur) : 0.f;
		pose.attack = -1.f;
	}
	pose.windup = e.dead ? 0.f : e.windup;
	pose.knock = e.knockT;
	if (e.hopT >= 0.f) pose.base.y -= std::sin(util::kPi * e.hopT) * e.hopH;
	if (e.stunShake > 0.f) pose.base.x += std::sin(m_time * 70.f) * 3.f * e.stunShake;

	float alpha = util::EaseOutCubic(e.spawnT);
	if (e.removing) alpha *= 1.f - util::Clamp01(e.removeT);
	if (e.dead) alpha *= 1.f - util::Clamp01((e.deadT - (e.kind == EntityKind::Player ? 4.5f : 0.9f)) / 0.6f);
	if (isGhostCopy) alpha *= 0.28f;
	if (alpha <= 0.01f) return;

	gfx::ResetMod();
	// 같은 칸의 여러 개체가 깜빡이지 않도록 id로 아주 작은 깊이 차이를 준다
	const float jitter = static_cast<float>((e.id.GetSerial() * 2654435761u + static_cast<uint32_t>(e.id.GetDatabaseID()) * 40503u) % 97) * 0.0006f;
	gfx::SetDepth(e.pos.x + e.pos.y + jitter);
	gfx::SetAlpha(alpha);
	const float nowf = static_cast<float>(m_now);
	if (e.flashT > 0.f) gfx::SetFlash(e.flashT * 0.85f);
	else if (e.windup > 0.f && !e.dead)
	{
		// 강공격 예고: 몸이 붉게 달아오르며 깜빡인다
		gfx::SetFlashColor(Rgb(0xFF3A1A));
		gfx::SetFlash(0.12f + 0.38f * e.windup * (0.5f + 0.5f * std::sin(m_time * (18.f + 20.f * e.windup))));
	}
	else if (e.id == m_hover) gfx::SetFlash(0.12f);
	// 커서에 가장 가까운 적(대상형 기술 · 기본 공격이 향할 적): 외곽선 — 같은 그림을 네 방향으로 1.5px 밀어
	// 단색으로 조금 뒤 깊이에 먼저 그린다. 셰이더 없이 되고, 한 마리에만 쓰므로 싸다.
	if (e.id == m_aim.enemy && !e.dead && !m_silhouetteDraw && !isGhostCopy && !m_ghostDraw
		&& (e.kind == EntityKind::Monster || e.kind == EntityKind::Player))
	{
		const float flashNow = gfx::Flash();
		const Color flashColorNow = gfx::FlashColor();
		const Color oc = e.kind == EntityKind::Player ? Rgb(0xFF8A5A) : Rgb(0xFF6A4A);
		const float pulse = 0.75f + 0.25f * std::sin(m_time * 6.f);
		constexpr float kOff[4][2] = { { -1.6f, 0.f }, { 1.6f, 0.f }, { 0.f, -1.3f }, { 0.f, 1.3f } };
		for (const auto& o : kOff)
		{
			actors::Pose p2 = pose;
			p2.base.x += o[0];
			p2.base.y += o[1];
			gfx::SetDepth(e.pos.x + e.pos.y + jitter - 0.003f);
			gfx::SetFlashColor(oc);
			gfx::SetFlash(1.f);
			gfx::SetAlpha(alpha * pulse);
			if (e.kind == EntityKind::Monster && e.row) actors::DrawMonster(p2, *e.row);
			else if (e.kind == EntityKind::Player) actors::DrawPlayer(p2, LookOf(e));
		}
		gfx::ResetMod();
		gfx::SetDepth(e.pos.x + e.pos.y + jitter);
		gfx::SetAlpha(alpha);
		gfx::SetFlashColor(flashColorNow);
		if (flashNow > 0.f) gfx::SetFlash(flashNow);
	}
	if (e.Status(static_cast<uint8_t>(StatusId::Freeze), nowf)) gfx::SetTint(Rgb(0xA8D8FF));
	else if (e.Status(static_cast<uint8_t>(StatusId::Poison), nowf)) gfx::SetTint(Rgb(0xC8F0B0));
	if (e.Status(static_cast<uint8_t>(StatusId::Evade), nowf) && !m_silhouetteDraw) gfx::SetAlpha(alpha * 0.55f);
	if (isGhostCopy) { gfx::SetFlashColor(Rgb(0x6AFF8A)); gfx::SetFlash(0.6f); }
	if (m_silhouetteDraw)
	{
		const float pulse = 0.5f + 0.5f * std::sin(m_time * 3.f);
		gfx::SetFlashColor(Rgb(0xFFE08A));
		gfx::SetFlash(1.f);
		gfx::SetAlpha(0.40f + 0.08f * pulse);
	}

	switch (e.kind)
	{
	case EntityKind::Player:
		actors::DrawPlayer(pose, LookOf(e));
		if (e.id == m_selfId && m_light.Night() > 0.05f && !m_silhouetteDraw && !m_ghostDraw)
			props::FrameLights().push_back({ pose.base, 170.f, Rgb(0xFFD8A0), 0.75f });
		break;
	case EntityKind::Monster:
		if (e.row) actors::DrawMonster(pose, *e.row);
		break;
	case EntityKind::Item:
	{
		const float seed = static_cast<float>(e.id.GetDatabaseID() % 100);
		Vector2 ground = pose.base;
		float z = 8.f + std::sin(m_time * 3.f + seed) * 2.5f;
		float rot = std::sin(m_time * 1.7f + seed) * 0.12f;
		float size = 30.f;
		if (e.dropT < 1.f)
		{
			const float u = e.dropT;
			ground = util::LerpV(TileToIso(e.dropFrom), pose.base, util::EaseOutCubic(u));
			z = std::sin(u * util::kPi) * 60.f + (1.f - u) * 20.f + 8.f * util::EaseOutBounce(u);
			rot = (1.f - u) * 9.f;
		}
		if (e.flyT >= 0.f)
		{
			const float u = util::Clamp01(e.flyT);
			ground = util::LerpV(TileToIso(e.dropFrom), TileToIso(e.flyTo), util::EaseInCubic(u));
			z = 8.f + std::sin(u * util::kPi) * 40.f + u * 30.f;
			size *= 1.f - u * 0.6f;
		}
		// 희귀도 빛기둥: 고급은 은은한 빛, 희귀부터 기둥 — 전설·신화는 멀리서도 보이게 높고 반짝인다
		const Rarity rar = FieldRarity(e);
		if (rar >= Rarity::Uncommon && e.flyT < 0.f)
		{
			const int k = static_cast<int>(rar);
			const Color rc = icons::RarityColor(rar);
			const float pulse = 0.75f + 0.25f * std::sin(m_time * 3.f + seed);
			gfx::Additive(true);
			gfx::TexQuad(assets::Get().glow, Vector2{ ground.x, ground.y - z }, Vector2{ size * (1.4f + 0.2f * k), size * (1.4f + 0.2f * k) }, 0.f, util::MulA(rc, 0.45f + 0.05f * k));
			if (rar >= Rarity::Rare)
			{
				const float bh = 70.f + 50.f * (k - 2);
				const float bw = 14.f + 4.f * (k - 2);
				const float land = e.dropT < 1.f ? util::EaseInCubic(e.dropT) : 1.f;   // 떨어지는 동안은 기둥이 자라난다
				gfx::TexQuad(assets::Get().beam, Vector2{ ground.x, ground.y - bh * 0.5f * land }, Vector2{ bw, bh * land }, 0.f, util::MulA(rc, (0.35f + 0.05f * k) * pulse));
				if (rar >= Rarity::Legendary)
				{
					gfx::TexQuad(assets::Get().beam, Vector2{ ground.x, ground.y - bh * 0.55f * land }, Vector2{ bw * 0.4f, bh * 1.1f * land }, 0.f, util::MulA(util::Shade(rc, 1.5f), 0.55f * pulse));
					for (int q = 0; q < (rar == Rarity::Mythic ? 6 : 4); ++q)
					{
						const float u = std::fmod(m_time * 0.45f + q * 0.23f + seed * 0.01f, 1.f);
						const float sx = ground.x + std::sin(u * 9.f + q * 2.1f) * bw * 0.8f;
						gfx::Star(Vector2{ sx, ground.y - 10.f - u * bh * land }, 0.8f, 4.f * (1.f - u * 0.6f), 4, m_time * 2.f + q, util::MulA(util::Shade(rc, 1.4f), 1.f - u));
					}
				}
			}
			gfx::Additive(false);
		}
		icons::DrawWorld(e.itemId, Vector2{ ground.x, ground.y - z - size * 0.3f }, size, rot, WHITE);
		break;
	}
	}
	gfx::ResetMod();
}

// 기술 칸에 마우스를 올리거나 "표시 후 시전"으로 누르고 있으면 사거리(마름모 = 서버의 체비셰프 거리)와
// 실제로 떨어질 곳을 바닥에 보여 준다. 떨어질 곳은 시전과 같은 계산(ResolveSkillAim)이다.
void Game::DrawSkillPreview()
{
	if (m_dead) return;
	const SkillRow* row = nullptr;
	bool aiming = false;
	if (m_aimingHot >= 0 && (row = HotSkill(m_aimingHot)) != nullptr)
		aiming = true;
	else if (m_previewSkill >= 0)
		row = SkillInSlot(m_previewSkill);
	Entity* me = Self();
	if (!row || !me) return;
	const Color col = Rgb(row->color);
	const float pulse = 0.5f + 0.5f * std::sin(m_time * 4.f);
	const float k = aiming ? 1.f : 0.8f;
	auto diamond = [&](Vector2 center, float R, Color fill, Color line, float th)
	{
		const Vector2 q[4] = { gfx::IsoPx(center.x - R, center.y - R), gfx::IsoPx(center.x + R, center.y - R),
			gfx::IsoPx(center.x + R, center.y + R), gfx::IsoPx(center.x - R, center.y + R) };
		if (fill.a > 0) gfx::Quad(q[0], q[1], q[2], q[3], fill);
		for (int i = 0; i < 4; ++i) gfx::Line(q[i], q[(i + 1) % 4], th, line);
	};
	gfx::SetDepthRaw(-0.966f);
	const Vector2 c{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
	const float reach = static_cast<float>(row->id == 10 ? row->dash : row->range);
	if (reach > 1.f)
		diamond(c, reach + 0.5f, util::MulA(col, 0.05f * k), util::MulA(col, (0.45f + 0.25f * pulse) * k), 2.f);
	const SkillAim a = ResolveSkillAim(*row, ObjID{});
	switch (row->target)
	{
	case SkillTarget::Self:
		if (row->radius > 0)
			diamond(c, row->radius + 0.5f, util::MulA(col, 0.16f * k), util::MulA(col, 0.85f * k), 2.5f);
		break;
	case SkillTarget::Enemy:
		if (a.target)
		{
			const Vector2 tc{ static_cast<float>(a.target->tx), static_cast<float>(a.target->ty) };
			const bool far = Cheb(m_predX, m_predY, a.target->tx, a.target->ty) > row->range;
			const float R = (row->shape == SkillShape::Circle || row->shape == SkillShape::Chain ? row->radius : 0) + 0.5f;
			diamond(tc, R, util::MulA(col, (far ? 0.10f : 0.22f) * k), util::MulA(col, (far ? 0.5f : 0.9f) * k), 2.5f);
			if (aiming)
				gfx::Line(gfx::IsoPx(c.x, c.y), gfx::IsoPx(tc.x, tc.y), 2.f, util::MulA(col, 0.45f));
		}
		break;
	case SkillTarget::Ground:
	{
		const Vector2 tc = row->id == 10 ? PredictLanding(*row, a.tx, a.ty, nullptr) : Vector2{ static_cast<float>(a.tx), static_cast<float>(a.ty) };
		diamond(tc, (row->shape == SkillShape::Circle ? row->radius : 0) + 0.5f, util::MulA(col, (a.ok ? 0.22f : 0.08f) * k), util::MulA(col, (a.ok ? 0.9f : 0.35f) * k), 2.5f);
		if (aiming)
			gfx::Line(gfx::IsoPx(c.x, c.y), gfx::IsoPx(tc.x, tc.y), 2.f, util::MulA(col, 0.45f));
		break;
	}
	case SkillTarget::Direction:
	{
		const int n = std::max<int>(1, row->dash);
		for (int i = 1; i <= n; ++i)
			gfx::Diamond(c.x + a.dx * i, c.y + a.dy * i, 0.8f, util::MulA(col, (0.12f + 0.06f * i) * (aiming ? 1.6f : 1.f)));
		break;
	}
	}
}

void Game::DrawNpcs()
{
	for (auto& [nid, v] : m_npcView)
	{
		if (!v.row) continue;
		actors::Pose pose;
		pose.base = TileToIso(v.pos);
		if (v.hopT >= 0.f) pose.base.y -= std::sin(util::kPi * v.hopT) * (WorldData::IsAnimal(v.row->kind) ? 10.f : 6.f);
		pose.time = m_time + nid * 1.7f;
		pose.seed = nid;
		pose.walk = v.walkPhase;
		pose.moving = v.moving;
		pose.faceRight = v.faceRight;
		pose.turn = v.body.turn;
		pose.lean = v.body.lean * 0.6f;
		pose.settle = v.body.settle;
		pose.faceBack = v.faceBack;
		// 말하는 중(대화창 · 혼잣말): 손짓하는 그림으로
		if (nid == m_dialogNpc || (v.barkT > 0.f && !v.bark.empty()))
			pose.cast = 0.6f;
		gfx::ResetMod();
		gfx::SetDepth(v.pos.x + v.pos.y + 0.003f * (nid % 7));
		if (nid == m_hoverNpc) gfx::SetFlash(0.12f);
		actors::DrawNpc(pose, *v.row);
	}
	gfx::ResetMod();
}

// ─────────────────────────────────────────────────────────────────────────────
// 이름표 · 체력바 · 말풍선
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawWorldOverlay(const Camera2D& cam)
{
	const bool alt = kb::Down(kb::Act::ShowNames);
	ui::TextStyle st;
	st.outline = 1.4f;
	st.outlineColor = Color{ 0, 0, 0, 190 };
	st.face = ui::Face::Strong;

	// 말풍선: 먹빛 반투명 판 + 밝은 글씨(세계 위에 뜨는 모든 말이 같은 모양)
	auto bubble = [&](const std::string& text, float cx, float bottom, float size, float maxW, float a, bool player)
	{
		const float tw = ui::Fonts().Measure(text, size, 0.f, ui::Face::Body).x;
		const int lines = std::max(1, static_cast<int>(std::ceil(tw / maxW)));
		const float w = std::round(std::min(tw, maxW) + 20.f);
		const float bh = std::round(lines * (size + 5.f) + 11.f);
		const Rectangle b{ std::round(cx - w * 0.5f), std::round(bottom - bh), w, bh };
		const Color fill = util::MulA(player ? Color{ 24, 28, 33, 232 } : Color{ 16, 19, 23, 228 }, a);
		ui::Plate(Rectangle{ b.x + 1, b.y + 3, b.width, b.height }, util::MulA(Color{ 0, 0, 0, 70 }, a), BLANK, 4.f, 0.f);
		ui::Plate(b, fill, util::MulA(player ? ui::theme::kLineStrong : ui::theme::kLine, a), 4.f, 1.f);
		DrawTriangle(Vector2{ cx - 5, b.y + b.height - 1 }, Vector2{ cx, b.y + b.height + 6 }, Vector2{ cx + 5, b.y + b.height - 1 }, fill);
		ui::FaceScope body(ui::Face::Body);
		ui::TextWrapped(text, Rectangle{ b.x + 10, b.y + 5, maxW, bh }, size, util::MulA(ui::theme::kTextHi, a), 5.f);
	};

	// NPC — 이름(가까이·가리킬 때), 역할, 말풍선. [G]는 G를 누르면 상호작용할 대상 하나에만(NPC 또는 물건).
	uint16_t interactNpc = 0;
	ObjID interactItem{};
	InteractTarget(interactNpc, interactItem);
	const std::string gKey = kb::Label(kb::Act::Interact);
	auto gMark = [&](float cx, float bottom)
	{
		const float bob = std::sin(m_time * 5.f) * 2.f;
		const float kw = std::max(20.f, std::round(ui::Fonts().Measure(gKey, ui::type::kMicro, 0.f, ui::Face::Strong).x + 10.f));
		ui::Keycap(Rectangle{ std::round(cx - kw * 0.5f), std::round(bottom - 20.f + bob), kw, 18.f }, gKey, true);
	};
	std::vector<std::pair<Vector2, QuestMark>> questMarks;   // 이름표보다 위에 그린다(겹친 NPC 이름에 가리지 않게)
	for (const auto& [nid, v] : m_npcView)
	{
		if (!v.row) continue;
		const auto& npc = *v.row;
		const bool animal = WorldData::IsAnimal(npc.kind);
		const float lift = v.hopT >= 0.f ? std::sin(util::kPi * v.hopT) * 8.f : 0.f;
		const Vector2 head = GetWorldToScreen2D(util::Add(TileToIso(v.pos), Vector2{ 0, -actors::NpcHeight(npc) - 8.f - lift }), cam);
		if (head.x < -120 || head.y < -80 || head.x > m_screenW + 120 || head.y > m_screenH + 60) continue;
		const int d = Cheb(m_predX, m_predY, v.tx, v.ty);
		const bool hovered = nid == m_hoverNpc;
		const QuestMark qm = NpcQuestMark(nid);
		float y = head.y;
		const char* role = npc.kind == WorldData::NpcKind::eMerchant ? "잡화 상점" : npc.kind == WorldData::NpcKind::eWeaponSmith ? "무기 상점"
			: npc.kind == WorldData::NpcKind::eTeleporter ? "옛길 이동" : npc.kind == WorldData::NpcKind::ePriest ? "치유의 기도"
			: npc.kind == WorldData::NpcKind::eInnkeeper ? "여관 · 수프" : "";
		if (!animal || hovered || alt || d <= 3 || qm == QuestMark::Talk)
		{
			st.size = animal ? 13.f : 14.5f;
			st.color = animal ? Rgb(0xD6CCB8) : (hovered ? ui::theme::kTextHi : Rgb(0xE8DDC4));
			st.face = ui::Face::Strong;
			ui::Fonts().DrawCentered(npc.name, Vector2{ head.x, y }, st);
			y -= 15.f;
		}
		if (role[0])
		{
			st.size = 11.5f;
			st.color = Rgb(0xB4AA94);
			st.face = ui::Face::Body;
			ui::Fonts().DrawCentered(role, Vector2{ head.x, y }, st);
			y -= 14.f;
		}
		// 말을 걸 수 있을 만큼 가까우면 [G] 표시
		if (nid == interactNpc && !m_dead && m_dialogNpc == 0)
		{
			gMark(head.x, y);
			y -= 22.f;
		}
		else if (hovered && !animal && qm == QuestMark::None)
		{
			const float bob = std::sin(m_time * 5.f) * 3.f;
			DrawTriangle(Vector2{ head.x - 6, y - 12 + bob }, Vector2{ head.x, y - 4 + bob }, Vector2{ head.x + 6, y - 12 + bob }, ui::theme::kDawn);
			y -= 12.f;
		}
		// 퀘스트 표식(! 새 이야기 · ? 보고 · 말풍선 = 말을 걸어야 할 대상)
		if (qm != QuestMark::None && (d <= 26 || qm == QuestMark::Ready || qm == QuestMark::Available))
		{
			questMarks.emplace_back(Vector2{ head.x, y - 6.f }, qm);
			y -= 34.f;
		}
		// 말풍선(혼잣말·인사)
		if (v.barkT > 0.f && !v.bark.empty())
		{
			const float a = util::Clamp01(v.barkT / 0.4f) * util::Clamp01((v.barkLife - v.barkT) / 0.12f + 0.3f);
			bubble(v.bark, head.x, y - 8.f, 13.5f, 200.f, a, false);
		}
		// 쓰다듬은 동물: 하트가 떠오른다
		if (v.heartT >= 0.f)
		{
			const float u = v.heartT / 1.6f;
			ui::TextStyle hs;
			hs.size = 18.f + 6.f * util::Pulse(std::min(1.f, u * 3.f));
			hs.color = util::MulA(Rgb(0xE07A8A), 1.f - u);
			hs.outline = 1.5f;
			hs.outlineColor = util::MulA(Color{ 90, 10, 30, 200 }, 1.f - u);
			ui::Fonts().DrawCentered("♥", Vector2{ head.x + std::sin(u * 9.f) * 4.f, y - 10.f - u * 30.f }, hs);
		}
	}

	for (const auto& [at, mark] : questMarks)
		DrawQuestMark(at, mark, mark == QuestMark::Low || mark == QuestMark::Active ? 0.8f : 1.f);

	for (auto& [id, e] : m_ents)
	{
		if (e.flyT >= 0.f || (e.removing && e.removeT > 0.5f)) continue;
		const float h = EntityHeight(e) * ((e.kind == EntityKind::Monster && e.row) ? 1.f : 1.f);
		Vector2 head = GetWorldToScreen2D(util::Add(TileToIso(e.pos), Vector2{ 0, -h - 10.f }), cam);
		if (head.x < -100 || head.y < -60 || head.x > m_screenW + 100 || head.y > m_screenH + 80) continue;
		const bool hovered = id == m_hover, targeted = id == m_target;
		const bool recentHit = (m_time - e.lastHitAt) < 4.f;

		bool showName = false, showBar = false;
		switch (e.kind)
		{
		case EntityKind::Player: showName = true; showBar = id != m_selfId && (e.hp < e.maxhp || targeted) && !e.dead; break;
		case EntityKind::Monster: showName = e.IsBoss() || hovered || targeted || recentHit || alt; showBar = !e.dead && (e.hp < e.maxhp || hovered || targeted); break;
		case EntityKind::Item: showName = hovered || alt || FieldRarity(e) >= Rarity::Rare; break;
		}
		if (e.dead && e.kind == EntityKind::Monster) showName = false;

		float y = head.y;
		if (showBar)
		{
			const float w = e.IsBoss() ? 90.f : 46.f;
			const Rectangle r{ std::round(head.x - w * 0.5f), std::round(y - 2.f), w, e.IsBoss() ? 6.f : 4.f };
			DrawRectangleRec(Rectangle{ r.x - 1, r.y - 1, r.width + 2, r.height + 2 }, Color{ 0, 0, 0, 190 });
			DrawRectangleRec(r, Color{ 36, 16, 15, 235 });
			if (e.hpLag > e.hpShown) DrawRectangleRec(Rectangle{ r.x, r.y, std::round(r.width * e.hpLag), r.height }, Color{ 236, 222, 200, 200 });
			const Color fill = e.kind == EntityKind::Player ? ui::theme::kGood : (e.IsBoss() ? ui::theme::kEmber : Rgb(0xC8453A));
			DrawRectangleRec(Rectangle{ r.x, r.y, std::round(r.width * util::Clamp01(e.hpShown)), r.height }, fill);
			DrawRectangleRec(Rectangle{ r.x, r.y, std::round(r.width * util::Clamp01(e.hpShown)), 1.f }, util::WithA(util::Shade(fill, 1.4f), 0.8f));
			y -= 8.f;
		}
		if (showName)
		{
			std::string label = e.name;
			if (e.kind == EntityKind::Player && e.enchant > 0 && hovered)
				label += "  (+" + std::to_string(e.enchant) + ")";
			if (e.kind == EntityKind::Monster && e.row && (hovered || targeted))
				label = "Lv." + std::to_string(e.row->level) + " " + label;
			st.size = e.IsBoss() ? 18.f : (e.kind == EntityKind::Item ? 13.f : 14.5f);
			st.color = NameColor(e);
			st.face = e.IsBoss() ? ui::Face::Display : ui::Face::Strong;
			if (id == m_selfId) st.color = util::Mix(st.color, ui::theme::kDawnHi, 0.25f);
			ui::Fonts().DrawCentered(label, Vector2{ head.x, y - st.size * 0.5f - 2.f }, st);
			if (e.IsBoss())
			{
				// 보스 표식: 이름 양옆 작은 마름모(적대의 불씨색)
				const Vector2 m = ui::Fonts().Measure(label, st.size, 0.f, ui::Face::Display);
				DrawPoly(Vector2{ head.x - m.x * 0.5f - 11.f, y - st.size * 0.5f - 2.f }, 4, 4.5f, 45.f, ui::theme::kEmber);
				DrawPoly(Vector2{ head.x + m.x * 0.5f + 11.f, y - st.size * 0.5f - 2.f }, 4, 4.5f, 45.f, ui::theme::kEmber);
			}
			y -= st.size + 4.f;
		}
		// G로 주울 물건
		if (id == interactItem && !m_dead)
		{
			gMark(head.x, y - 2.f);
			y -= 22.f;
		}
		// 퀘스트 대상 몬스터: 머리 위 작은 금빛 마름모
		if (e.kind == EntityKind::Monster && !e.dead && IsQuestTarget(e))
		{
			const float bob = std::sin(m_time * 4.f + static_cast<float>(e.tx)) * 1.5f;
			const Vector2 q{ head.x, y - 8.f + bob };
			DrawPoly(q, 4, 6.5f, 0.f, Color{ 20, 14, 6, 220 });
			DrawPoly(q, 4, 4.5f, 0.f, ui::theme::kDawn);
			DrawPoly(Vector2{ q.x - 1.f, q.y - 1.5f }, 4, 1.6f, 0.f, ui::theme::kDawnHi);
			y -= 16.f;
		}
		// 말풍선
		if (e.chatT > 0.f && !e.chat.empty())
		{
			const float a = util::Clamp01(e.chatT / 0.4f);
			bubble(e.chat, head.x, y - 10.f, 14.f, 220.f, a, true);
		}
	}
}

void Game::DrawCursor()
{
	const Vector2 m = in::MousePos();
	enum { Arrow, Attack, Pickup, Talk, Enchant } kind = Arrow;
	if (m_scene == Scene::Game && !ui::MouseOverUi())
	{
		if (m_enchant.stage == 1) kind = Enchant;
		else if (Entity* e = Find(m_hover))
		{
			// 좌클릭 = 다가가 공격(몬스터 · 강제 공격 중인 모험가), 물건 = 줍기
			if (e->kind == EntityKind::Item) kind = Pickup;
			else if (!e->dead && IsEnemy(*e)) kind = Attack;
		}
		else if (m_hoverNpc) kind = Talk;
	}
	else if (m_enchant.stage == 1)
		kind = Enchant;

	// 그린 커서(있으면): 끝이 왼쪽 위(마우스 위치)
	{
		const uiart::Id id = kind == Attack ? uiart::CursorAttack : (kind == Pickup ? uiart::CursorPick : (kind == Talk ? uiart::CursorTalk : uiart::CursorArrow));
		if (kind != Enchant)
			if (const Texture2D* t = uiart::Get(id))
			{
				const float s = 30.f;
				const float k = s / static_cast<float>(std::max(t->width, t->height));
				DrawTexturePro(*t, Rectangle{ 0, 0, static_cast<float>(t->width), static_cast<float>(t->height) },
					Rectangle{ std::round(m.x - 1.f), std::round(m.y - 1.f), t->width * k, t->height * k }, Vector2{ 0, 0 }, 0.f, WHITE);
				return;
			}
	}
	auto outlineTri = [](Vector2 a, Vector2 b, Vector2 c, Color fill)
	{
		DrawTriangle(a, b, c, fill);
		DrawLineEx(a, b, 1.5f, Color{ 20, 14, 8, 255 });
		DrawLineEx(b, c, 1.5f, Color{ 20, 14, 8, 255 });
		DrawLineEx(c, a, 1.5f, Color{ 20, 14, 8, 255 });
	};
	switch (kind)
	{
	case Arrow:
		outlineTri(m, Vector2{ m.x + 4, m.y + 20 }, Vector2{ m.x + 15, m.y + 13 }, Rgb(0xF2E0B0));
		DrawTriangle(Vector2{ m.x + 2, m.y + 3 }, Vector2{ m.x + 5, m.y + 15 }, Vector2{ m.x + 11, m.y + 11 }, Rgb(0xFFF8E0));
		break;
	case Attack:
		DrawLineEx(m, Vector2{ m.x + 16, m.y + 16 }, 4.f, Color{ 20, 14, 8, 255 });
		DrawLineEx(m, Vector2{ m.x + 16, m.y + 16 }, 2.f, Rgb(0xE8ECF2));
		DrawLineEx(Vector2{ m.x + 10, m.y + 16 }, Vector2{ m.x + 16, m.y + 10 }, 3.f, Rgb(0xC8A040));
		DrawCircleV(Vector2{ m.x + 18, m.y + 18 }, 2.5f, Rgb(0xC83A2A));
		break;
	case Pickup:
		DrawCircleV(Vector2{ m.x + 8, m.y + 10 }, 8.f, Color{ 20, 14, 8, 255 });
		DrawCircleV(Vector2{ m.x + 8, m.y + 10 }, 6.5f, Rgb(0xFFD84A));
		DrawCircleV(Vector2{ m.x + 6, m.y + 8 }, 2.f, Rgb(0xFFF4C0));
		break;
	case Talk:
		DrawRectangleRounded(Rectangle{ m.x, m.y, 20, 14 }, 0.4f, 4, Color{ 20, 14, 8, 255 });
		DrawRectangleRounded(Rectangle{ m.x + 1.5f, m.y + 1.5f, 17, 11 }, 0.4f, 4, Rgb(0xFFF8E8));
		DrawTriangle(Vector2{ m.x + 5, m.y + 13 }, Vector2{ m.x + 4, m.y + 19 }, Vector2{ m.x + 10, m.y + 13 }, Rgb(0xFFF8E8));
		break;
	case Enchant:
		DrawCircleV(m, 10.f, Color{ 255, 220, 120, 60 });
		DrawPoly(m, 4, 7.f, static_cast<float>(m_now) * 90.f, Rgb(0xFFE08A));
		DrawPoly(m, 4, 3.f, static_cast<float>(m_now) * 90.f, Rgb(0xFFFFFF));
		break;
	}
}
