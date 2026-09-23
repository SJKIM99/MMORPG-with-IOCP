#include "game/Game.h"
#include "game/Texts.h"
#include "fx/Effects.h"
#include "render/Draw.h"
#include "render/Actors.h"
#include "render/Icons.h"
#include "render/Props.h"
#include "render/Assets.h"
#include "ui/Font.h"
#include "core/Util.h"

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
}

void Game::DrawGame()
{
	const Camera2D cam = m_cam.Get(m_screenW, m_screenH);
	m_cam2d = cam;
	Entity* me = Self();
	const Vector2 playerTile = me ? me->pos : Vector2{ static_cast<float>(m_predX), static_cast<float>(m_predY) };

	m_terrain.DrawGround(cam, m_screenW, m_screenH, playerTile, m_time, true, m_showGrid ? 0.07f : 0.f);
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
	fx::Fx().DrawScreen(m_screenW, m_screenH);

	DrawWorldOverlay(cam);
	fx::Fx().DrawOverlay(cam);

	if (m_scene != Scene::Game)
		return;
	DrawHud();
	DrawMinimap();
	DrawTargetFrame();
	DrawChat();
	DrawQuickBar();
	DrawBanners();
	DrawToasts();
	if (m_invOpen) DrawInventory();
	if (m_dialogNpc && m_shopMode == ShopMode::None) DrawNpcDialog();
	if (m_shopMode == ShopMode::Buy) DrawShop();
	if (m_shopMode == ShopMode::Sell) DrawSell();
	if (m_shopMode == ShopMode::Teleport) DrawTeleport();
	if (m_enchant.stage >= 2) DrawEnchant();
	if (m_discard.open) DrawDiscard();
	if (m_mapOpen) DrawWorldMap();
	if (m_helpOpen) DrawHelp();
	if (m_dead) DrawDeath();
}

// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawWorld(const Camera2D& cam)
{
	Entity* me = Self();
	const Vector2 pt = me ? me->pos : Vector2{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
	gfx::BeginWorld(pt.x + pt.y, cam.zoom);
	props::FrameLights().clear();
	const float t = m_time;

	// ── 1) 바닥 데칼: 깊이 테스트만(쓰기 X) ──
	gfx::DepthTest(true);
	gfx::DepthWrite(false);
	gfx::SetDepthRaw(-0.97f);

	// 마우스가 가리키는 칸
	if (m_hoverX >= 0 && m_hover == ObjID{} && m_hoverNpc == 0 && !ui::MouseOverUi() && !m_dead)
	{
		const bool blocked = WorldData::IsBlocked(m_hoverX, m_hoverY);
		const float X = static_cast<float>(m_hoverX), Y = static_cast<float>(m_hoverY);
		const Vector2 c[4] = { gfx::IsoPx(X - 0.5f, Y - 0.5f), gfx::IsoPx(X + 0.5f, Y - 0.5f), gfx::IsoPx(X + 0.5f, Y + 0.5f), gfx::IsoPx(X - 0.5f, Y + 0.5f) };
		const Color col = blocked ? Rgb(0xFF5A4A, 150) : Rgb(0xFFFFFF, 110);
		gfx::Quad(c[0], c[1], c[2], c[3], util::MulA(col, 0.25f));
		for (int i = 0; i < 4; ++i) gfx::Line(c[i], c[(i + 1) % 4], 1.5f, col);
	}
	// 이동 목표 표시
	if (m_goal.kind == GoalKind::Move && !m_mouseHeld)
	{
		const float pulse = 0.5f + 0.5f * std::sin(t * 8.f);
		gfx::Diamond(static_cast<float>(m_goal.x), static_cast<float>(m_goal.y), 0.55f + pulse * 0.15f, Rgb(0x8AF0A0, 70));
	}
	// 경로 점
	for (size_t i = 0; i < m_path.size(); ++i)
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
			const ItemTableRow* row = ItemTableData::Find(e.itemId);
			if (row && row->grade >= ItemGrade::eRare)
				gfx::EllipseGrad(base, 22.f, 11.f, util::MulA(icons::GradeColor(row->grade), 0.5f), util::WithA(icons::GradeColor(row->grade), 0.f));
		}
	}
	for (const auto& npc : WorldData::kNpcs)
	{
		actors::DrawShadow(gfx::IsoPx(npc.x, npc.y), 15.f, 1.f);
		if (npc.id == m_hoverNpc || npc.id == m_dialogNpc)
		{
			const Vector2 base = gfx::IsoPx(npc.x, npc.y);
			gfx::EllipseArc(base, 20.f, 10.f, 0.f, util::kPi * 2.f, 2.f, Rgb(0xFFE08A, 200));
		}
	}
	fx::Fx().DrawGround();

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

	// ── 3) 반투명: 깊이 쓰기 X ──
	for (auto& [id, e] : m_ents)
		if (IsTranslucent(e))
			DrawEntity(e, true);
	// 가속 잔상
	if (me && m_hasteUntil > m_now && me->moveT < 1.f)
	{
		for (int k = 1; k <= 2; ++k)
		{
			Entity ghost = *me;
			ghost.pos = util::LerpV(me->pos, me->from, 0.35f * k);
			m_ghostDraw = true;
			DrawEntity(ghost, true);
			m_ghostDraw = false;
		}
	}
	fx::Fx().DrawWorld();

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
	if (e.flashT > 0.f) gfx::SetFlash(e.flashT * 0.85f);
	else if (e.id == m_hover) gfx::SetFlash(0.12f);
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
		actors::DrawPlayer(pose, actors::LookForPlayer(e.nameHash, e.itemId, e.enchant));
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
		const ItemTableRow* row = ItemTableData::Find(e.itemId);
		if (row && row->grade >= ItemGrade::eRare)
		{
			gfx::Additive(true);
			gfx::TexQuad(assets::Get().glow, Vector2{ ground.x, ground.y - z }, Vector2{ size * 1.8f, size * 1.8f }, 0.f, util::MulA(icons::GradeColor(row->grade), 0.55f));
			gfx::TexQuad(assets::Get().beam, Vector2{ ground.x, ground.y - 60.f }, Vector2{ 18.f, 120.f }, 0.f, util::MulA(icons::GradeColor(row->grade), 0.4f + 0.2f * std::sin(m_time * 3.f)));
			gfx::Additive(false);
		}
		icons::DrawWorld(e.itemId, Vector2{ ground.x, ground.y - z - size * 0.3f }, size, rot, WHITE);
		break;
	}
	}
	gfx::ResetMod();
}

void Game::DrawNpcs()
{
	Entity* me = Self();
	for (const auto& npc : WorldData::kNpcs)
	{
		actors::Pose pose;
		pose.base = gfx::IsoPx(npc.x, npc.y);
		pose.time = m_time + npc.id * 1.7f;
		pose.seed = npc.id;
		pose.faceRight = npc.facing == 1 || npc.facing == 2;
		pose.faceBack = npc.facing >= 2;
		if (me && Cheb(static_cast<int>(me->pos.x + 0.5f), static_cast<int>(me->pos.y + 0.5f), npc.x, npc.y) <= 5)
		{
			// 가까이 오면 플레이어 쪽을 돌아본다
			const float dx = me->pos.x - npc.x, dy = me->pos.y - npc.y;
			const float sx = dx - dy, sy = dx + dy;
			if (std::fabs(sx) > 0.3f) pose.faceRight = sx > 0;
			pose.faceBack = sy < -0.3f;
		}
		gfx::ResetMod();
		gfx::SetDepth(static_cast<float>(npc.x + npc.y));
		if (npc.id == m_hoverNpc) gfx::SetFlash(0.12f);
		actors::DrawNpc(pose, npc);
	}
	gfx::ResetMod();
}

// ─────────────────────────────────────────────────────────────────────────────
// 이름표 · 체력바 · 말풍선
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawWorldOverlay(const Camera2D& cam)
{
	const bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
	ui::TextStyle st;
	st.outline = 1.6f;
	st.outlineColor = Color{ 0, 0, 0, 200 };

	// NPC
	for (const auto& npc : WorldData::kNpcs)
	{
		const Vector2 head = GetWorldToScreen2D(util::Add(gfx::IsoPx(npc.x, npc.y, 0.f), Vector2{ 0, -actors::NpcHeight(npc) - 8.f }), cam);
		if (head.x < -100 || head.y < -50 || head.x > m_screenW + 100 || head.y > m_screenH + 50) continue;
		st.size = 15.f;
		st.color = Rgb(0xB8F08A);
		ui::Fonts().DrawCentered(npc.name, head, st);
		const char* role = npc.kind == WorldData::NpcKind::eMerchant ? "잡화 상점" : npc.kind == WorldData::NpcKind::eWeaponSmith ? "무기 상점"
			: npc.kind == WorldData::NpcKind::eTeleporter ? "텔레포트" : "";
		if (role[0])
		{
			st.size = 12.f;
			st.color = Rgb(0xE8D8A0);
			ui::Fonts().DrawCentered(role, Vector2{ head.x, head.y - 15.f }, st);
			if (npc.id == m_hoverNpc)
			{
				const float bob = std::sin(m_time * 5.f) * 3.f;
				DrawTriangle(Vector2{ head.x - 6, head.y - 34 + bob }, Vector2{ head.x, head.y - 26 + bob }, Vector2{ head.x + 6, head.y - 34 + bob }, Rgb(0xFFE08A));
			}
		}
	}

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
		case EntityKind::Item: showName = hovered || alt; break;
		}
		if (e.dead && e.kind == EntityKind::Monster) showName = false;

		float y = head.y;
		if (showBar)
		{
			const float w = e.IsBoss() ? 90.f : 46.f;
			const Rectangle r{ head.x - w * 0.5f, y - 2.f, w, e.IsBoss() ? 7.f : 5.f };
			DrawRectangleRec(Rectangle{ r.x - 1, r.y - 1, r.width + 2, r.height + 2 }, Color{ 0, 0, 0, 180 });
			DrawRectangleRec(r, Color{ 40, 16, 16, 230 });
			if (e.hpLag > e.hpShown) DrawRectangleRec(Rectangle{ r.x, r.y, r.width * e.hpLag, r.height }, Color{ 255, 240, 220, 220 });
			const Color fill = e.kind == EntityKind::Player ? Rgb(0x4AD86A) : (e.IsBoss() ? Rgb(0xFF6A2A) : Rgb(0xE83A2A));
			DrawRectangleRec(Rectangle{ r.x, r.y, r.width * util::Clamp01(e.hpShown), r.height }, fill);
			DrawRectangleRec(Rectangle{ r.x, r.y, r.width * util::Clamp01(e.hpShown), r.height * 0.4f }, Color{ 255, 255, 255, 60 });
			y -= 8.f;
		}
		if (showName)
		{
			std::string label = e.name;
			if (e.kind == EntityKind::Player && e.enchant > 0 && hovered)
				label += "  (+" + std::to_string(e.enchant) + ")";
			if (e.kind == EntityKind::Monster && e.row && (hovered || targeted))
				label = "Lv." + std::to_string(e.row->level) + " " + label;
			st.size = e.IsBoss() ? 18.f : (e.kind == EntityKind::Item ? 13.f : 15.f);
			st.color = NameColor(e);
			if (id == m_selfId) st.color = util::Mix(st.color, Rgb(0xFFF2C0), 0.35f);
			ui::Fonts().DrawCentered(label, Vector2{ head.x, y - st.size * 0.5f - 2.f }, st);
			if (e.IsBoss())
			{
				// 보스 표식
				const Vector2 m = ui::Fonts().Measure(label, st.size);
				DrawPoly(Vector2{ head.x - m.x * 0.5f - 12.f, y - st.size * 0.5f - 2.f }, 4, 6.f, 45.f, Rgb(0xFF6A2A));
				DrawPoly(Vector2{ head.x + m.x * 0.5f + 12.f, y - st.size * 0.5f - 2.f }, 4, 6.f, 45.f, Rgb(0xFF6A2A));
			}
			y -= st.size + 4.f;
		}
		// 말풍선
		if (e.chatT > 0.f && !e.chat.empty())
		{
			const float a = util::Clamp01(e.chatT / 0.4f);
			const float size = 15.f;
			const float maxW = 220.f;
			float tw = ui::Fonts().Measure(e.chat, size).x;
			const int lines = static_cast<int>(std::ceil(tw / maxW));
			const float w = std::min(tw, maxW) + 20.f;
			const float bh = lines * (size + 4.f) + 12.f;
			const Rectangle b{ head.x - w * 0.5f, y - bh - 10.f, w, bh };
			DrawRectangleRounded(Rectangle{ b.x + 2, b.y + 3, b.width, b.height }, 0.3f, 6, util::MulA(Color{ 0, 0, 0, 90 }, a));
			DrawRectangleRounded(b, 0.3f, 6, util::MulA(Color{ 252, 248, 238, 240 }, a));
			DrawTriangle(Vector2{ head.x - 7, b.y + b.height - 1 }, Vector2{ head.x, b.y + b.height + 8 }, Vector2{ head.x + 7, b.y + b.height - 1 }, util::MulA(Color{ 252, 248, 238, 240 }, a));
			ui::TextWrapped(e.chat, Rectangle{ b.x + 10, b.y + 6, maxW, bh }, size, util::MulA(Color{ 40, 30, 20, 255 }, a), 4.f);
		}
	}
}

void Game::DrawCursor()
{
	const Vector2 m = GetMousePosition();
	enum { Arrow, Attack, Pickup, Talk, Enchant } kind = Arrow;
	if (m_scene == Scene::Game && !ui::MouseOverUi())
	{
		if (m_enchant.stage == 1) kind = Enchant;
		else if (Entity* e = Find(m_hover))
		{
			if (e->kind == EntityKind::Monster) kind = Attack;
			else if (e->kind == EntityKind::Item) kind = Pickup;
			else if (e->kind == EntityKind::Player && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) kind = Attack;
		}
		else if (m_hoverNpc) kind = Talk;
	}
	else if (m_enchant.stage == 1)
		kind = Enchant;

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
