#include "game/Game.h"
#include "game/Texts.h"
#include "fx/Effects.h"
#include "render/Draw.h"
#include "render/Actors.h"
#include "render/Icons.h"
#include "render/Props.h"
#include "render/Assets.h"
#include "audio/Sfx.h"
#include "ui/Font.h"
#include "core/Util.h"

#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

using util::Rgb;
using ui::Align;
using ui::ButtonKind;

namespace
{
	std::string Fmt(const char* f, ...)
	{
		char buf[256];
		va_list ap;
		va_start(ap, f);
		std::vsnprintf(buf, sizeof(buf), f, ap);
		va_end(ap);
		return buf;
	}

	// 문장을 폭에 맞춰 여러 줄로 나눈다(글자 단위)
	std::vector<std::string> WrapLines(const std::string& text, float size, float maxW)
	{
		std::vector<std::string> out;
		std::vector<int> cps;
		ui::DecodeUtf8(text, cps);
		std::string line;
		for (int cp : cps)
		{
			std::string cand = line;
			ui::AppendUtf8(cand, cp);
			if (ui::Fonts().Measure(cand, size).x > maxW && !line.empty())
			{
				out.push_back(line);
				line.clear();
				if (cp == ' ') continue;
				ui::AppendUtf8(line, cp);
			}
			else
				line.swap(cand);
		}
		if (!line.empty()) out.push_back(line);
		return out;
	}

	void CoinIcon(Vector2 c, float r)
	{
		DrawCircleV(Vector2{ c.x + 1, c.y + 1 }, r, Color{ 120, 80, 10, 255 });
		DrawCircleV(c, r, Rgb(0xF2C23A));
		DrawCircleV(Vector2{ c.x - r * 0.2f, c.y - r * 0.2f }, r * 0.55f, Rgb(0xFFE88A));
	}

	void Stars(Vector2 pos, int tier, float size)
	{
		for (int i = 0; i < 5; ++i)
		{
			const Color c = i < tier ? (tier >= 5 ? Rgb(0xFF6A3A) : (tier >= 4 ? Rgb(0xFFA040) : Rgb(0xFFD66A))) : Color{ 90, 84, 76, 255 };
			DrawPoly(Vector2{ pos.x + i * (size + 3.f), pos.y }, 5, size * 0.55f, -90.f, c);
			DrawPoly(Vector2{ pos.x + i * (size + 3.f), pos.y }, 5, size * 0.28f, -54.f, util::Shade(c, 1.2f));
		}
	}

	const char* AlignmentLabel(int16_t a)
	{
		if (a < 0) return "카오틱";
		if (a >= AlignmentRule::kLawfulLine) return "라우풀";
		return "중립";
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 로그인 화면
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawLogin()
{
	const Camera2D cam = m_cam.Get(m_screenW, m_screenH);
	m_cam2d = cam;
	const Vector2 center = gfx::IsoToTile(cam.target);
	m_predX = static_cast<int>(center.x);
	m_predY = static_cast<int>(center.y);
	m_terrain.DrawGround(cam, m_screenW, m_screenH, center, m_time, false, 0.f);
	BeginMode2D(cam);
	DrawWorld(cam);
	EndMode2D();
	m_light.Apply(cam, m_screenW, m_screenH, props::FrameLights());
	fx::Fx().DrawOverlay(cam);

	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	DrawRectangleGradientV(0, 0, m_screenW, m_screenH / 2, Color{ 6, 6, 12, 150 }, Color{ 6, 6, 12, 60 });
	DrawRectangleGradientV(0, m_screenH / 2, m_screenW, m_screenH / 2, Color{ 6, 6, 12, 60 }, Color{ 6, 6, 12, 190 });

	// 제목
	const float t = m_time;
	ui::TextStyle title;
	title.size = 66.f;
	title.color = Rgb(0xFFE2A0);
	title.outline = 3.f;
	title.outlineColor = Color{ 60, 30, 0, 230 };
	title.shadow = 5.f;
	title.shadowColor = Color{ 0, 0, 0, 160 };
	const float ty = H * 0.16f + std::sin(t * 1.2f) * 3.f;
	ui::Fonts().DrawCentered("여명의 대지", Vector2{ W * 0.5f, ty }, title);
	ui::Text("IOCP MMORPG  ·  리니지 스타일 2.5D 클라이언트", Vector2{ W * 0.5f, ty + 44.f }, 18.f, Rgb(0xE8D8B0), Align::Center, true);
	// 제목 양옆 장식
	for (int s = -1; s <= 1; s += 2)
	{
		const float x = W * 0.5f + s * 210.f;
		DrawLineEx(Vector2{ x, ty }, Vector2{ x + s * 90.f, ty }, 2.f, Rgb(0xC8A060, 180));
		DrawPoly(Vector2{ x + s * 96.f, ty }, 4, 6.f, 45.f, Rgb(0xFFD070));
	}

	// 입력 판넬
	const Rectangle p{ W * 0.5f - 200.f, H * 0.38f, 400.f, 318.f };
	ui::Panel(p);
	ui::TextShadow("모험을 시작하세요", Vector2{ p.x + p.width * 0.5f, p.y + 16 }, 20.f, ui::theme::kTitle, Align::Center);
	ui::Divider(p.x + 20, p.x + p.width - 20, p.y + 46);

	float y = p.y + 60;
	const bool busy = m_scene == Scene::Connecting;
	auto label = [&](const char* s) { ui::Text(s, Vector2{ p.x + 24, y + 7 }, 15.f, ui::theme::kTextDim); };
	label("서버");
	ui::InputText(Rectangle{ p.x + 104, y, 190, 32 }, m_fHost, "127.0.0.1");
	ui::InputText(Rectangle{ p.x + 300, y, 76, 32 }, m_fPort, "4000");
	y += 42;
	label("캐릭터");
	const bool enterName = ui::InputText(Rectangle{ p.x + 104, y, 272, 32 }, m_fName, "영문/숫자 2~12자", !busy && m_fName.text.empty());
	y += 42;
	label("비밀번호");
	const bool enterPass = ui::InputText(Rectangle{ p.x + 104, y, 272, 32 }, m_fPass, "(선택)");
	y += 50;
	const bool go = ui::Button(Rectangle{ p.x + 24, y, p.width - 48, 42 }, busy ? "접속 중..." : "접속하기", ButtonKind::Primary, !busy, 19.f);
	if (!busy && (go || enterName || enterPass))
		BeginConnect();
	y += 54;
	if (busy)
	{
		const float a = t * 4.f;
		for (int i = 0; i < 8; ++i)
		{
			const float ang = a + i * util::kPi / 4.f;
			DrawCircleV(Vector2{ p.x + p.width * 0.5f + std::cos(ang) * 10.f - 70.f, y + 8 + std::sin(ang) * 10.f }, 2.5f, Color{ 255, 220, 150, static_cast<unsigned char>(60 + i * 24) });
		}
		ui::Text("서버에 연결하는 중...", Vector2{ p.x + p.width * 0.5f + 10.f, y }, 15.f, ui::theme::kText, Align::Center);
	}
	else if (!m_loginError.empty())
		ui::TextWrapped(m_loginError, Rectangle{ p.x + 24, y, p.width - 48, 40 }, 15.f, ui::theme::kBad);
	else
		ui::Text("처음 쓰는 이름이면 새 캐릭터가 만들어집니다.", Vector2{ p.x + p.width * 0.5f, y }, 14.f, ui::theme::kTextDim, Align::Center);

	ui::Text("클릭 이동 · 몬스터 클릭 공격 · Q 십자베기 · Z 줍기 · I 소지품 · Enter 채팅 · F1 도움말", Vector2{ W * 0.5f, H - 34 }, 14.f, Color{ 200, 190, 170, 200 }, Align::Center, true);
}

void Game::DrawDisconnected()
{
	DrawRectangle(0, 0, m_screenW, m_screenH, Color{ 0, 0, 0, 170 });
	const Rectangle r{ m_screenW * 0.5f - 200.f, m_screenH * 0.5f - 90.f, 400.f, 180.f };
	ui::Panel(r);
	ui::TextShadow("서버와의 연결이 끊어졌습니다", Vector2{ r.x + r.width * 0.5f, r.y + 30 }, 21.f, ui::theme::kTitle, Align::Center);
	ui::Text(m_net.LastError(), Vector2{ r.x + r.width * 0.5f, r.y + 68 }, 15.f, ui::theme::kTextDim, Align::Center);
	if (ui::Button(Rectangle{ r.x + 40, r.y + 112, 150, 40 }, "다시 접속", ButtonKind::Primary))
		BeginConnect();
	if (ui::Button(Rectangle{ r.x + 210, r.y + 112, 150, 40 }, "처음 화면"))
	{
		m_net.Disconnect();
		ResetWorld();
		m_scene = Scene::Login;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// HUD
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawHud()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const float cx = W * 0.5f;

	// 경험치 바(화면 맨 아래 전체)
	{
		const Rectangle r{ 0, H - 7, W, 7 };
		DrawRectangleRec(r, Color{ 12, 8, 18, 230 });
		DrawRectangleGradientH(0, static_cast<int>(H - 7), static_cast<int>(W * m_expShown), 7, Rgb(0x6A3AB0), Rgb(0xC08AFF));
		DrawRectangle(0, static_cast<int>(H - 7), static_cast<int>(W * m_expShown), 2, Color{ 255, 255, 255, 50 });
		for (int i = 1; i < 10; ++i)
			DrawLine(static_cast<int>(W * i / 10.f), static_cast<int>(H - 7), static_cast<int>(W * i / 10.f), static_cast<int>(H), Color{ 0, 0, 0, 150 });
		const Rectangle hit{ 0, H - 14, W, 14 };
		if (ui::Hovered(hit))
			ui::Tooltip({ { Fmt("경험치 %s / %s (%.1f%%)", texts::Number(m_exp).c_str(), texts::Number(m_expToNext).c_str(), m_expShown * 100.f), Rgb(0xD8B8FF), 15.f } });
	}

	// 아래 가운데 판넬
	const Rectangle panel{ cx - 330.f, H - 96.f, 660.f, 84.f };
	ui::Panel(panel);
	// 레벨 구슬
	{
		const Vector2 c{ panel.x + 44.f, panel.y + 42.f };
		DrawCircleV(c, 34.f, Color{ 20, 14, 10, 255 });
		DrawRing(c, 30.f, 34.f, 0.f, 360.f, 48, Rgb(0xC8A060));
		DrawCircleGradient(static_cast<int>(c.x), static_cast<int>(c.y), 29.f, Rgb(0x5A3A8A), Rgb(0x1A1028));
		// 경험치 원형 게이지
		DrawRing(c, 30.5f, 33.5f, -90.f, -90.f + 360.f * m_expShown, 48, Rgb(0xC08AFF));
		ui::Text("Lv", Vector2{ c.x, c.y - 20.f }, 12.f, Rgb(0xD8C8F0), Align::Center, true);
		ui::Text(std::to_string(m_level), Vector2{ c.x, c.y - 8.f }, 24.f, WHITE, Align::Center, true);
	}
	// HP
	{
		const Rectangle hp{ panel.x + 88.f, panel.y + 12.f, 224.f, 24.f };
		const float pulse = (m_maxhp > 0 && m_hp * 4 < m_maxhp) ? 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 8.f) : 0.f;
		ui::Bar(hp, m_hpShown, m_hpLag, util::Mix(Rgb(0xD8302A), Rgb(0xFF6A5A), pulse), Color{ 40, 12, 12, 255 },
			Fmt("HP  %d / %d", m_hp, m_maxhp), 15.f);
		// 공격력/성향
		const Color ac = m_alignment < 0 ? Rgb(0xFF6A5A) : (m_alignment >= AlignmentRule::kLawfulLine ? Rgb(0x7AB8FF) : ui::theme::kTextDim);
		ui::Text(Fmt("공격력 %d", m_attack), Vector2{ hp.x + 2.f, hp.y + 32.f }, 14.f, Rgb(0xF2D8A0));
		ui::Text(Fmt("%s %+d", AlignmentLabel(m_alignment), m_alignment), Vector2{ hp.x + 92.f, hp.y + 32.f }, 14.f, ac);
		// 버프 아이콘
		float bx = hp.x + 188.f;
		if (m_hasteUntil > m_now)
		{
			const Rectangle b{ bx, hp.y + 28.f, 26.f, 26.f };
			DrawRectangleRounded(b, 0.3f, 6, Color{ 20, 60, 30, 230 });
			DrawRectangleRoundedLinesEx(b, 0.3f, 6, 1.5f, Rgb(0x7AFF9A));
			DrawTriangle(Vector2{ b.x + 7, b.y + 6 }, Vector2{ b.x + 7, b.y + 20 }, Vector2{ b.x + 15, b.y + 13 }, Rgb(0x9AFFB0));
			DrawTriangle(Vector2{ b.x + 13, b.y + 6 }, Vector2{ b.x + 13, b.y + 20 }, Vector2{ b.x + 21, b.y + 13 }, Rgb(0x9AFFB0));
			if (ui::Hovered(b))
				ui::Tooltip({ { "가속", Rgb(0x9AFFB0), 16.f }, { Fmt("이동 속도 증가 · %.0f초 남음", m_hasteUntil - m_now), ui::theme::kText, 14.f } });
			bx -= 30.f;
		}
		if (m_flags & SUBJECT_EX_PURPLE)
		{
			const Rectangle b{ bx, hp.y + 28.f, 26.f, 26.f };
			DrawRectangleRounded(b, 0.3f, 6, Color{ 60, 20, 70, 230 });
			DrawRectangleRoundedLinesEx(b, 0.3f, 6, 1.5f, Rgb(0xD890FF));
			ui::Text("!", Vector2{ b.x + 13, b.y + 3 }, 18.f, Rgb(0xE8B0FF), Align::Center);
			if (ui::Hovered(b))
				ui::Tooltip({ { "보라돌이", Rgb(0xD890FF), 16.f }, { "먼저 공격한 상태입니다. 상대가 반격해도 벌칙이 없습니다.", ui::theme::kText, 14.f } });
		}
	}

	// 왼쪽 위 캐릭터 정보
	{
		const Rectangle r{ 12.f, 12.f, 250.f, 88.f };
		ui::Panel(r, 0.92f);
		Entity* me = Self();
		const Color nc = me ? NameColor(*me) : WHITE;
		ui::TextShadow(m_playerName, Vector2{ r.x + 14, r.y + 10 }, 20.f, nc);
		ui::Text(Fmt("Lv.%d  기사", m_level), Vector2{ r.x + 14, r.y + 38 }, 15.f, ui::theme::kText);
		if (m_pkCount > 0)
			ui::Text(Fmt("PK %d", m_pkCount), Vector2{ r.x + r.width - 14, r.y + 38 }, 14.f, Rgb(0xFF6A5A), Align::Right);
		CoinIcon(Vector2{ r.x + 22, r.y + 70 }, 7.f);
		ui::Text(texts::Number(m_inv.adena) + " 아데나", Vector2{ r.x + 36, r.y + 61 }, 16.f, Rgb(0xFFD84A));
	}

	// 가운데 알림
	float y = H * 0.27f;
	for (const auto& c : m_center)
	{
		const float age = static_cast<float>(m_now - c.start);
		const float a = age > 2.1f ? 1.f - (age - 2.1f) / 0.5f : 1.f;
		const float pop = age < 0.12f ? util::EaseOutBack(age / 0.12f) : 1.f;
		ui::TextStyle st;
		st.size = 20.f * pop;
		st.color = util::MulA(c.color, a);
		st.outline = 2.f;
		st.outlineColor = util::MulA(Color{ 0, 0, 0, 220 }, a);
		ui::Fonts().DrawCentered(c.text, Vector2{ W * 0.5f, y }, st);
		y += 28.f;
	}
}

void Game::DrawQuickBar()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const float x0 = W * 0.5f - 330.f + 318.f, y0 = H - 96.f + 18.f;
	for (int i = 0; i < 8; ++i)
	{
		const Rectangle r{ x0 + i * 43.f, y0, 40.f, 40.f };
		const QuickSlot& q = m_quick[i];
		const bool hover = ui::Hovered(r);
		ui::SlotFrame(r, hover, false);
		ui::Block(r);
		double cdEnd = 0.0, cdLen = 1.0;
		int count = 0;
		if (q.kind == 2)
		{
			// 십자베기 아이콘
			const Vector2 c{ r.x + 20, r.y + 20 };
			DrawCircleV(c, 16.f, Color{ 30, 60, 100, 255 });
			DrawLineEx(Vector2{ c.x - 11, c.y - 11 }, Vector2{ c.x + 11, c.y + 11 }, 4.f, Rgb(0xD8F0FF));
			DrawLineEx(Vector2{ c.x + 11, c.y - 11 }, Vector2{ c.x - 11, c.y + 11 }, 4.f, Rgb(0xD8F0FF));
			DrawCircleV(c, 4.f, Rgb(0x8AD8FF));
			cdEnd = m_lastSkillAt + CombatRule::kSkillCooldownMs / 1000.0;
			cdLen = CombatRule::kSkillCooldownMs / 1000.0;
			if (hover)
				ui::Tooltip({ { "십자베기", Rgb(0x9AD8FF), 17.f }, { "동서남북 인접한 적을 한꺼번에 벤다", ui::theme::kText, 14.f },
					{ Fmt("피해 %d · 재사용 5초", CombatRule::SkillDamage(m_attack)), ui::theme::kTextDim, 14.f } });
		}
		else if (q.kind == 1 && q.itemId)
		{
			count = m_inv.CountOf(q.itemId);
			icons::Draw(q.itemId, Rectangle{ r.x + 3, r.y + 3, 34, 34 }, count > 0 ? WHITE : Color{ 255, 255, 255, 90 });
			const ItemTableRow* row = ItemTableData::Find(q.itemId);
			if (row && row->effect == ItemEffect::eHeal) { cdEnd = m_lastPotionAt + CombatRule::kPotionCooldownMs / 1000.0; cdLen = CombatRule::kPotionCooldownMs / 1000.0; }
			else if (row && row->type == ItemType::eConsumable) { cdEnd = m_lastScrollAt + CombatRule::kScrollCooldownMs / 1000.0; cdLen = CombatRule::kScrollCooldownMs / 1000.0; }
			if (hover) DrawItemTooltip(q.itemId, 0, count, false, false);
		}
		// 쿨타임 부채꼴
		if (cdEnd > m_now)
		{
			const float frac = static_cast<float>((cdEnd - m_now) / cdLen);
			DrawCircleSector(Vector2{ r.x + 20, r.y + 20 }, 26.f, -90.f, -90.f + 360.f * util::Clamp01(frac), 32, Color{ 0, 0, 0, 150 });
			ui::Text(Fmt("%.1f", cdEnd - m_now), Vector2{ r.x + 20, r.y + 12 }, 14.f, WHITE, Align::Center, true);
		}
		if (count > 0)
			ui::Text(std::to_string(count), Vector2{ r.x + 38, r.y + 23 }, 13.f, WHITE, Align::Right, true);
		ui::Text(std::to_string(i + 1), Vector2{ r.x + 3, r.y + 1 }, 12.f, Rgb(0xE8D8A0), Align::Left, true);

		// 입력: 클릭 사용 / 오른쪽 클릭 비우기 / 끌어서 옮기기
		if (ui::Pressed(r) && q.kind != 0)
		{
			m_pressKind = 2; m_pressIndex = i; m_pressPos = ui::Mouse();
			ui::ConsumeMouse();
		}
		if (m_pressKind == 2 && m_pressIndex == i && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !ui::Dragging()
			&& util::Len(util::Sub(ui::Mouse(), m_pressPos)) > 6.f)
		{
			ui::BeginDrag(ui::DragPayload{ 2, i, q.itemId });
			m_pressKind = 0;
		}
		if (ui::Clicked(r) && m_pressKind == 2 && m_pressIndex == i && !ui::Dragging())
		{
			UseQuickSlot(i);
			m_pressKind = 0;
		}
		if (ui::Pressed(r, MOUSE_BUTTON_RIGHT))
		{
			m_quick[i] = QuickSlot{};
			sfx::Play(sfx::Id::Close, 0.4f);
			ui::ConsumeMouse();
		}
		if (ui::DragReleased() && hover)
		{
			const ui::DragPayload& d = ui::Drag();
			if (d.kind == 1 && d.itemId)
			{
				const ItemTableRow* row = ItemTableData::Find(d.itemId);
				if (row && row->type == ItemType::eConsumable)
				{
					m_quick[i] = QuickSlot{ 1, d.itemId };
					sfx::Play(sfx::Id::Equip, 0.5f);
				}
			}
			else if (d.kind == 2 && d.index != i)
				std::swap(m_quick[i], m_quick[d.index]);
			ui::EndDrag();
			SaveQuickSlots();
		}
	}
	if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && m_pressKind == 2)
		m_pressKind = 0;
}

void Game::DrawTargetFrame()
{
	Entity* t = Find(m_target);
	if (!t || t->kind == EntityKind::Item || t->removing)
		return;
	const float W = static_cast<float>(m_screenW);
	const Rectangle r{ W * 0.5f - 170.f, 12.f, 340.f, 58.f };
	ui::Panel(r, 0.95f, false);
	std::string title = t->name;
	if (t->kind == EntityKind::Monster && t->row)
		title = Fmt("Lv.%d  ", t->row->level) + t->name + (t->IsBoss() ? "  [보스]" : (t->aggressive ? "  (선공)" : ""));
	else if (t->kind == EntityKind::Player)
		title = Fmt("Lv.%d  ", t->level) + t->name + "  (" + AlignmentLabel(t->alignment) + ")";
	ui::TextShadow(title, Vector2{ r.x + r.width * 0.5f, r.y + 8 }, 16.f, NameColor(*t), Align::Center);
	const Rectangle bar{ r.x + 14, r.y + 32, r.width - 28, 16 };
	const Color fill = t->kind == EntityKind::Player ? Rgb(0x4AD86A) : (t->IsBoss() ? Rgb(0xFF6A2A) : Rgb(0xD8302A));
	ui::Bar(bar, t->hpShown, t->hpLag, fill, Color{ 30, 12, 12, 255 }, t->dead ? "쓰러짐" : Fmt("%d / %d", std::max(0, t->hp), t->maxhp), 13.f);
}

void Game::DrawMinimap()
{
	const float W = static_cast<float>(m_screenW);
	const float size = 184.f;
	const Rectangle r{ W - size - 14.f, 12.f, size, size + 50.f };
	ui::Panel(r, 0.95f);
	const Rectangle m{ r.x + 8, r.y + 8, size - 16, size - 16 };
	constexpr int kRange = 30;
	const float k = m.width / (kRange * 2.f + 1.f) * 0.72f;   // 타일 1칸 → 마름모 반폭(px)
	const Vector2 c{ m.x + m.width * 0.5f, m.y + m.height * 0.5f };
	auto toMap = [&](float dx, float dy) { return Vector2{ c.x + (dx - dy) * k, c.y + (dx + dy) * k * 0.5f }; };

	// 배경(지형) — 내 칸이 바뀔 때만 다시 그린다
	if (m_minimapRt.id == 0)
		m_minimapRt = LoadRenderTexture(static_cast<int>(m.width), static_cast<int>(m.height));
	if (m_minimapX != m_predX || m_minimapY != m_predY)
	{
		m_minimapX = m_predX;
		m_minimapY = m_predY;
		BeginTextureMode(m_minimapRt);
		ClearBackground(Color{ 10, 12, 14, 255 });
		const Vector2 lc{ m.width * 0.5f, m.height * 0.5f };
		for (int dy = -kRange; dy <= kRange; ++dy)
			for (int dx = -kRange; dx <= kRange; ++dx)
			{
				const int x = m_predX + dx, y = m_predY + dy;
				if (x < 0 || y < 0 || x >= WorldData::kWorldWidth || y >= WorldData::kWorldHeight) continue;
				const props::Palette pal = props::PaletteFor(WorldData::RegionIdAt(x, y));
				Color col{ static_cast<unsigned char>(pal.a[0] * 200), static_cast<unsigned char>(pal.a[1] * 200), static_cast<unsigned char>(pal.a[2] * 200), 255 };
				const char cell = WorldData::TownCell(x, y);
				if (cell != '\0')
				{
					if (cell == '=' || cell == ',' || cell == 'G' || cell == 'N' || cell == 'S' || cell == 'L') col = Color{ 190, 180, 160, 255 };
					if (cell == '#' || cell == 'O') col = Color{ 110, 104, 96, 255 };
					if (cell == 'B' || cell == 'H') col = Color{ 170, 90, 70, 255 };
					if (cell == 'F') col = Color{ 90, 160, 220, 255 };
				}
				else if (WorldData::LegacyPatternBlocked(x, y))
					col = util::Shade(col, 0.45f);
				const Vector2 p{ lc.x + (dx - dy) * k, lc.y + (dx + dy) * k * 0.5f };
				DrawRectangleV(Vector2{ p.x - k, p.y - k * 0.5f }, Vector2{ k * 2.f + 0.5f, k + 0.5f }, col);
			}
		EndTextureMode();
	}
	BeginScissorMode(static_cast<int>(m.x), static_cast<int>(m.y), static_cast<int>(m.width), static_cast<int>(m.height));
	DrawTextureRec(m_minimapRt.texture, Rectangle{ 0, 0, m.width, -m.height }, Vector2{ m.x, m.y }, WHITE);
	// 부드러운 이동 보정(내 실제 위치와 캐시 기준 칸의 차이)
	Entity* me = Self();
	const Vector2 myPos = me ? me->pos : Vector2{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
	for (auto& [id, e] : m_ents)
	{
		if (id == m_selfId || e.dead || e.removing) continue;
		const Vector2 p = toMap(e.pos.x - m_predX, e.pos.y - m_predY);
		Color col = Rgb(0xFF4A3A);
		float rad = 2.2f;
		if (e.kind == EntityKind::Player) { col = NameColor(e); rad = 2.6f; }
		else if (e.kind == EntityKind::Item) { col = Rgb(0xFFD84A); rad = 1.6f; }
		else if (e.IsBoss()) { col = Rgb(0xFF8A2A); rad = 4.5f + std::sin(static_cast<float>(m_now) * 6.f); }
		DrawCircleV(p, rad + 0.8f, Color{ 0, 0, 0, 160 });
		DrawCircleV(p, rad, col);
	}
	for (const auto& npc : WorldData::kNpcs)
	{
		const Vector2 p = toMap(static_cast<float>(npc.x - m_predX), static_cast<float>(npc.y - m_predY));
		DrawCircleV(p, 2.8f, Color{ 0, 0, 0, 160 });
		DrawCircleV(p, 2.f, Rgb(0x9AF06A));
	}
	// 나(바라보는 방향 화살표)
	{
		const Vector2 p = toMap(myPos.x - m_predX, myPos.y - m_predY);
		float ang = 0.f;
		if (me) ang = (me->faceBack ? -1.f : 1.f) * (me->faceRight ? 0.6f : 2.5f);
		const Vector2 d{ std::cos(ang), std::sin(ang) };
		const Vector2 n{ -d.y, d.x };
		DrawTriangle(Vector2{ p.x + d.x * 7, p.y + d.y * 7 }, Vector2{ p.x - d.x * 4 - n.x * 4, p.y - d.y * 4 - n.y * 4 }, Vector2{ p.x - d.x * 4 + n.x * 4, p.y - d.y * 4 + n.y * 4 }, WHITE);
		DrawTriangle(Vector2{ p.x + d.x * 7, p.y + d.y * 7 }, Vector2{ p.x - d.x * 4 + n.x * 4, p.y - d.y * 4 + n.y * 4 }, Vector2{ p.x - d.x * 4 - n.x * 4, p.y - d.y * 4 - n.y * 4 }, WHITE);
	}
	EndScissorMode();
	DrawRectangleLinesEx(m, 1.f, Color{ 214, 176, 104, 120 });

	// 지역/좌표/시계
	const int region = Region();
	const auto* row = WorldData::FindRegion(region);
	const std::string place = InTown() ? std::string(WorldData::kTownName) : (row ? row->name : "");
	ui::TextShadow(place, Vector2{ r.x + 12, r.y + size - 2 }, 16.f, InTown() ? Rgb(0x9AF06A) : ui::theme::kTitle);
	if (row && !InTown())
		Stars(Vector2{ r.x + r.width - 70.f, r.y + size + 7.f }, row->tier, 10.f);
	ui::Text(Fmt("(%d, %d)", m_predX, m_predY), Vector2{ r.x + 12, r.y + size + 20 }, 13.f, ui::theme::kTextDim);
	const float hour = m_light.Hour();
	const int hh = static_cast<int>(hour), mm = static_cast<int>((hour - hh) * 60.f);
	const bool night = m_light.Night() > 0.5f;
	const Vector2 ic{ r.x + r.width - 62.f, r.y + size + 28.f };
	if (night)
	{
		DrawCircleV(ic, 6.f, Rgb(0xF2E8C0));
		DrawCircleV(Vector2{ ic.x + 3, ic.y - 2 }, 5.f, Color{ 24, 20, 28, 255 });
	}
	else
	{
		DrawCircleV(ic, 5.f, Rgb(0xFFD84A));
		for (int i = 0; i < 8; ++i)
		{
			const float a = i * util::kPi / 4.f;
			DrawLineEx(Vector2{ ic.x + std::cos(a) * 7, ic.y + std::sin(a) * 7 }, Vector2{ ic.x + std::cos(a) * 9.5f, ic.y + std::sin(a) * 9.5f }, 1.5f, Rgb(0xFFD84A));
		}
	}
	ui::Text(Fmt("%02d:%02d", hh, mm), Vector2{ r.x + r.width - 12, r.y + size + 20 }, 13.f, ui::theme::kText, Align::Right);
}

// ─────────────────────────────────────────────────────────────────────────────
// 채팅
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawChat()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const float w = std::max(260.f, std::min(440.f, W * 0.5f - 346.f));
	const Rectangle log{ 12.f, H - 262.f, w, 206.f };
	const bool hover = ui::Hovered(log);
	const bool open = m_chatOpen && ui::Focused() == &m_chatField;
	if (open || hover)
	{
		DrawRectangleRounded(log, 0.04f, 6, Color{ 8, 6, 10, 150 });
		DrawRectangleRoundedLinesEx(log, 0.04f, 6, 1.f, Color{ 214, 176, 104, 70 });
	}
	// 최신 줄이 아래
	const float size = 15.f;
	float y = log.y + log.height - 6.f;
	BeginScissorMode(static_cast<int>(log.x), static_cast<int>(log.y), static_cast<int>(log.width), static_cast<int>(log.height));
	for (auto it = m_chat.rbegin(); it != m_chat.rend() && y > log.y; ++it)
	{
		const float age = static_cast<float>(m_now - it->time);
		float a = 1.f;
		if (!open && !hover) a = age > 14.f ? util::Clamp01(1.f - (age - 14.f) / 2.f) : 1.f;
		if (a <= 0.f) break;
		const auto lines = WrapLines(it->text, size, log.width - 16.f);
		for (auto ln = lines.rbegin(); ln != lines.rend(); ++ln)
		{
			y -= size + 4.f;
			ui::Text(*ln, Vector2{ log.x + 8.f, y }, size, util::MulA(it->color, a), Align::Left, true);
		}
	}
	EndScissorMode();

	if (open)
	{
		const Rectangle in{ 12.f, H - 52.f, w, 32.f };
		const bool submitted = ui::InputText(in, m_chatField, "메시지 (&로 시작하면 전체 채팅)");
		if (submitted && m_now - m_chatOpenedAt > 0.05)
		{
			SubmitChat();
			ui::Focus(nullptr);
			m_chatOpen = false;
		}
	}
	else
	{
		m_chatOpen = false;
		if (ui::Focused() == &m_chatField) ui::Focus(nullptr);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 소지품
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawItemTooltip(uint16_t itemId, uint8_t enchant, int count, bool equipped, bool forSell)
{
	const ItemTableRow* row = ItemTableData::Find(itemId);
	if (!row) return;
	std::vector<ui::TooltipLine> lines;
	lines.push_back({ texts::ItemNameEnchanted(itemId, enchant), icons::ItemNameColor(itemId), 18.f });
	const char* grade = "";
	switch (row->grade)
	{
	case ItemGrade::eRare: grade = " · 레어"; break;
	case ItemGrade::eEpic: grade = " · 에픽"; break;
	case ItemGrade::eUnique: grade = " · 유니크"; break;
	case ItemGrade::eLegendary: grade = " · 전설"; break;
	default: break;
	}
	switch (row->type)
	{
	case ItemType::eEquipment:
		lines.push_back({ std::string("한손검") + grade, ui::theme::kTextDim, 14.f });
		lines.push_back({ Fmt("공격력 +%d", row->attackBonus), Rgb(0xF2D8A0), 15.f });
		if (enchant > 0) lines.push_back({ Fmt("강화 보너스 +%d", EnchantRule::AttackBonus(enchant)), Rgb(0x9AD8FF), 15.f });
		break;
	case ItemType::eConsumable:
		lines.push_back({ "소모품", ui::theme::kTextDim, 14.f });
		switch (row->effect)
		{
		case ItemEffect::eHeal: lines.push_back({ Fmt("HP %d 회복 (재사용 5초)", row->healAmount), Rgb(0x8AFF9A), 15.f }); break;
		case ItemEffect::eReturnTown: lines.push_back({ std::string(WorldData::kTownName) + "(으)로 귀환", Rgb(0x9AD8FF), 15.f }); break;
		case ItemEffect::eTeleportRandom: lines.push_back({ "지금 있는 지역의 아무 곳으로 순간이동", Rgb(0xC8A0FF), 15.f }); break;
		case ItemEffect::eHaste: lines.push_back({ Fmt("%d초 동안 이동 속도 증가", row->effectValue), Rgb(0x8AFF9A), 15.f }); break;
		case ItemEffect::eEnchantWeapon:
			lines.push_back({ row->effectValue ? "무기 강화 (축복: 더 크게 오를 수 있음)" : "무기 강화 (+6까지 안전)", Rgb(0xFFE08A), 15.f });
			break;
		default: break;
		}
		break;
	case ItemType::eCurrency:
		lines.push_back({ "화폐", ui::theme::kTextDim, 14.f });
		break;
	default:
		break;
	}
	if (count > 1) lines.push_back({ Fmt("수량 %d", count), ui::theme::kText, 14.f });
	if (equipped) lines.push_back({ "착용 중", ui::theme::kGold, 14.f });
	if (row->sellPrice > 0)
	{
		const uint32_t unit = row->type == ItemType::eEquipment ? row->sellPrice * (10u + enchant) / 10u : row->sellPrice;
		lines.push_back({ Fmt("상점 매입가 %s 아데나", texts::Number(unit).c_str()), forSell ? Rgb(0xFFD84A) : ui::theme::kTextDim, 14.f });
	}
	if (!forSell)
	{
		if (row->type == ItemType::eEquipment) lines.push_back({ "더블클릭/우클릭: 착용·해제", Color{ 150, 140, 120, 255 }, 13.f });
		else if (row->type == ItemType::eConsumable) lines.push_back({ "더블클릭/우클릭: 사용 · 퀵슬롯으로 끌기", Color{ 150, 140, 120, 255 }, 13.f });
	}
	ui::Tooltip(std::move(lines), itemId);
}

void Game::DrawInventory()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const float slot = 54.f, gap = 4.f;
	const float w = 6 * (slot + gap) + 20.f, h = 5 * (slot + gap) + 110.f;
	Rectangle r{ W - w - 14.f, 258.f, w, h };
	if (r.y + r.height > H - 110.f) r.y = std::max(12.f, H - 110.f - r.height);
	m_invRect = r;
	ui::Window(r, "소지품", &m_invOpen);
	if (m_enchant.stage == 1)
	{
		const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 6.f);
		DrawRectangleRoundedLinesEx(Rectangle{ r.x - 3, r.y - 3, r.width + 6, r.height + 6 }, 0.03f, 6, 2.f, Color{ 255, 220, 120, static_cast<unsigned char>(120 + 120 * pulse) });
		ui::Text("강화할 무기를 고르세요 (Esc 취소)", Vector2{ r.x + r.width * 0.5f, r.y + r.height - 22 }, 14.f, Rgb(0xFFE08A), Align::Center, true);
	}

	const float x0 = r.x + 12.f, y0 = r.y + 44.f;
	m_invHover = -1;
	for (int i = 0; i < MAX_INVENTORY_SLOTS; ++i)
	{
		const Rectangle s{ x0 + (i % 6) * (slot + gap), y0 + (i / 6) * (slot + gap), slot, slot };
		const InvSlot& it = m_inv.slots[i];
		const bool hover = ui::Hovered(s);
		if (hover) m_invHover = i;
		bool selectable = true;
		if (m_enchant.stage == 1)
			selectable = !it.Empty() && ItemTableData::IsWeapon(it.itemId);
		ui::SlotFrame(s, hover && selectable, m_sellSlot == i || m_enchant.targetSlot == i, it.equipped ? ui::theme::kGold : ui::theme::kBorder);
		if (!it.Empty())
		{
			const bool dragged = ui::Dragging() && ui::Drag().kind == 1 && ui::Drag().index == i;
			const Color tint = (!selectable || dragged) ? Color{ 255, 255, 255, 80 } : WHITE;
			if (it.equipped)
			{
				DrawRectangleRounded(Rectangle{ s.x + 2, s.y + 2, s.width - 4, s.height - 4 }, 0.14f, 4, Color{ 255, 200, 80, 30 });
				DrawRectangleRoundedLinesEx(s, 0.14f, 4, 2.f, ui::theme::kGold);
			}
			const Color glow = icons::EnchantGlow(it.enchant);
			if (glow.a > 0)
				DrawCircleGradient(static_cast<int>(s.x + slot * 0.5f), static_cast<int>(s.y + slot * 0.5f), slot * 0.5f, util::MulA(glow, 0.8f), util::WithA(glow, 0.f));
			icons::Draw(it.itemId, Rectangle{ s.x + 4, s.y + 4, slot - 8, slot - 8 }, tint);
			if (it.count > 1)
				ui::Text(std::to_string(it.count), Vector2{ s.x + slot - 4, s.y + slot - 18 }, 14.f, WHITE, Align::Right, true);
			if (it.enchant > 0)
				ui::Text("+" + std::to_string(it.enchant), Vector2{ s.x + 4, s.y + 2 }, 14.f, it.enchant >= 7 ? Rgb(0xE08AFF) : Rgb(0x9AD8FF), Align::Left, true);
			if (it.equipped)
				ui::Text("E", Vector2{ s.x + slot - 5, s.y + 2 }, 13.f, ui::theme::kGold, Align::Right, true);
			if (hover && !ui::Dragging())
				DrawItemTooltip(it.itemId, it.enchant, it.count, it.equipped, m_shopMode == ShopMode::Sell);
		}

		// ── 입력 ──
		if (m_enchant.stage == 1)
		{
			if (ui::Clicked(s) && selectable)
			{
				m_enchant.targetSlot = i;
				m_enchant.itemId = it.itemId;
				m_enchant.before = it.enchant;
				m_enchant.stage = 2;
				sfx::Play(sfx::Id::Click);
			}
			continue;
		}
		if (it.Empty())
		{
			if (ui::DragReleased() && hover && ui::Drag().kind == 1 && ui::Drag().index != i)
			{
				m_net.SendSwap(static_cast<uint16_t>(ui::Drag().index), static_cast<uint16_t>(i));
				ui::EndDrag();
			}
			continue;
		}
		if (m_shopMode == ShopMode::Sell)
		{
			if (ui::Clicked(s))
			{
				const ItemTableRow* row = ItemTableData::Find(it.itemId);
				if (row && row->sellPrice > 0 && !it.equipped) { m_sellSlot = i; m_sellCount = it.count; sfx::Play(sfx::Id::Click, 0.6f); }
				else { AddCenter(it.equipped ? "착용 중인 장비는 팔 수 없습니다." : "그 물건은 팔 수 없습니다.", Rgb(0xFF8A7A)); sfx::Play(sfx::Id::Error, 0.5f); }
			}
			continue;
		}
		if (ui::DoubleClicked(s) || ui::Pressed(s, MOUSE_BUTTON_RIGHT))
		{
			UseSlot(i);
			ui::ConsumeMouse();
			m_pressKind = 0;
			continue;
		}
		if (ui::Pressed(s))
		{
			m_pressKind = 1; m_pressIndex = i; m_pressPos = ui::Mouse();
			ui::ConsumeMouse();
		}
		if (m_pressKind == 1 && m_pressIndex == i && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !ui::Dragging()
			&& util::Len(util::Sub(ui::Mouse(), m_pressPos)) > 6.f)
		{
			ui::BeginDrag(ui::DragPayload{ 1, i, it.itemId });
			m_pressKind = 0;
		}
		if (ui::DragReleased() && hover && ui::Drag().kind == 1 && ui::Drag().index != i)
		{
			m_net.SendSwap(static_cast<uint16_t>(ui::Drag().index), static_cast<uint16_t>(i));
			ui::EndDrag();
		}
	}
	// 창 안(슬롯 밖)에 떨어뜨리면 취소
	if (ui::DragReleased() && ui::Hovered(r))
		ui::EndDrag();
	if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && m_pressKind == 1)
		m_pressKind = 0;

	// 아래 정보
	const float fy = r.y + r.height - (m_enchant.stage == 1 ? 58.f : 48.f);
	ui::Divider(r.x + 12, r.x + r.width - 12, fy - 6);
	CoinIcon(Vector2{ r.x + 22, fy + 12 }, 8.f);
	ui::Text(texts::Number(m_inv.adena), Vector2{ r.x + 36, fy + 3 }, 17.f, Rgb(0xFFD84A));
	ui::Text(Fmt("빈 칸 %d/%d", m_inv.FreeSlots(), MAX_INVENTORY_SLOTS), Vector2{ r.x + r.width - 14, fy + 4 }, 14.f, ui::theme::kTextDim, Align::Right);
	ui::Text(Fmt("공격력 %d", m_attack), Vector2{ r.x + r.width * 0.5f + 20.f, fy + 4 }, 14.f, Rgb(0xF2D8A0), Align::Center);
}

// ─────────────────────────────────────────────────────────────────────────────
// NPC · 상점 · 텔레포트
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawNpcDialog()
{
	const auto* npc = WorldData::FindNpc(m_dialogNpc);
	if (!npc) { m_dialogNpc = 0; return; }
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const Rectangle r{ W * 0.5f - 290.f, H - 330.f, 580.f, 210.f };
	bool open = true;
	ui::Window(r, npc->name, &open);
	if (!open) { CloseNpcWindows(); return; }

	// 초상화(NPC를 크게 다시 그린다)
	const Rectangle pr{ r.x + 16, r.y + 46, 120, 148 };
	DrawRectangleRounded(pr, 0.06f, 6, Color{ 30, 26, 34, 255 });
	DrawRectangleGradientV(static_cast<int>(pr.x), static_cast<int>(pr.y), static_cast<int>(pr.width), static_cast<int>(pr.height), Color{ 70, 60, 50, 120 }, Color{ 10, 8, 12, 0 });
	BeginScissorMode(static_cast<int>(pr.x), static_cast<int>(pr.y), static_cast<int>(pr.width), static_cast<int>(pr.height));
	{
		actors::Pose pose;
		pose.base = Vector2{ pr.x + pr.width * 0.5f, pr.y + pr.height + 18.f };
		pose.scale = 1.7f;
		pose.time = m_time;
		pose.faceRight = true;
		gfx::BeginWorld(0.f, 1.7f);
		gfx::SetDepthRaw(-0.5f);
		actors::DrawNpc(pose, *npc);
		gfx::Flush();
	}
	EndScissorMode();
	DrawRectangleRoundedLinesEx(pr, 0.06f, 6, 1.5f, ui::theme::kBorder);

	// 대사(한 글자씩 나타나는 타자 효과)
	const Rectangle tr{ r.x + 152, r.y + 50, r.width - 170, 90 };
	ui::TextWrapped(npc->greeting, tr, 17.f, ui::theme::kText, 6.f);

	float bx = r.x + 152;
	const float by = r.y + r.height - 56;
	switch (npc->kind)
	{
	case WorldData::NpcKind::eMerchant:
	case WorldData::NpcKind::eWeaponSmith:
		if (ui::Button(Rectangle{ bx, by, 120, 40 }, "구매", ButtonKind::Primary)) { m_shopMode = ShopMode::Buy; m_buyItem = 0; m_buyCount = 1; sfx::Play(sfx::Id::Open, 0.6f); }
		bx += 130;
		if (ui::Button(Rectangle{ bx, by, 120, 40 }, "판매")) { m_shopMode = ShopMode::Sell; m_sellSlot = -1; m_invOpen = true; sfx::Play(sfx::Id::Open, 0.6f); }
		bx += 130;
		break;
	case WorldData::NpcKind::eTeleporter:
		if (ui::Button(Rectangle{ bx, by, 150, 40 }, "순간이동", ButtonKind::Primary)) { m_shopMode = ShopMode::Teleport; m_teleportSel = -1; sfx::Play(sfx::Id::Open, 0.6f); }
		bx += 160;
		break;
	default:
		break;
	}
	if (ui::Button(Rectangle{ bx, by, 100, 40 }, "닫기"))
		CloseNpcWindows();
}

void Game::DrawShop()
{
	const auto* npc = WorldData::FindNpc(m_dialogNpc);
	if (!npc) { m_shopMode = ShopMode::None; return; }
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const Rectangle r{ std::max(12.f, W * 0.5f - 470.f), std::max(12.f, H * 0.5f - 262.f), 440.f, std::min(H - 124.f, 504.f) };
	bool open = true;
	ui::Window(r, std::string(npc->name) + " — 구매", &open);
	if (!open) { CloseNpcWindows(); return; }

	std::vector<uint16_t> items;
	for (const auto& shop : WorldData::kShops)
		if (shop.npcId == npc->id)
			for (uint16_t id : shop.items) if (id) items.push_back(id);

	float y = r.y + 46;
	for (uint16_t id : items)
	{
		const ItemTableRow* row = ItemTableData::Find(id);
		if (!row) continue;
		const Rectangle it{ r.x + 12, y, r.width - 24, 44 };
		const bool hover = ui::Hovered(it), sel = m_buyItem == id;
		DrawRectangleRounded(it, 0.12f, 6, sel ? Color{ 90, 70, 40, 220 } : (hover ? Color{ 60, 50, 44, 200 } : Color{ 30, 26, 30, 200 }));
		if (sel) DrawRectangleRoundedLinesEx(it, 0.12f, 6, 1.5f, ui::theme::kBorderHi);
		ui::SlotFrame(Rectangle{ it.x + 6, it.y + 4, 36, 36 }, false, false);
		icons::Draw(id, Rectangle{ it.x + 8, it.y + 6, 32, 32 });
		ui::TextShadow(row->displayName, Vector2{ it.x + 54, it.y + 4 }, 16.f, icons::ItemNameColor(id));
		std::string desc;
		if (row->type == ItemType::eEquipment) desc = Fmt("공격력 +%d", row->attackBonus);
		else if (row->effect == ItemEffect::eHeal) desc = Fmt("HP %d 회복", row->healAmount);
		else if (row->effect == ItemEffect::eHaste) desc = Fmt("%d초 가속", row->effectValue);
		else if (row->effect == ItemEffect::eReturnTown) desc = "마을로 귀환";
		else if (row->effect == ItemEffect::eTeleportRandom) desc = "지역 내 순간이동";
		else if (row->effect == ItemEffect::eEnchantWeapon) desc = "무기 강화";
		ui::Text(desc, Vector2{ it.x + 54, it.y + 25 }, 13.f, ui::theme::kTextDim);
		const bool affordable = m_inv.adena >= row->buyPrice;
		CoinIcon(Vector2{ it.x + it.width - 110, it.y + 22 }, 6.f);
		ui::Text(texts::Number(row->buyPrice), Vector2{ it.x + it.width - 10, it.y + 13 }, 16.f, affordable ? Rgb(0xFFD84A) : ui::theme::kBad, Align::Right);
		ui::Block(it);
		if (ui::Clicked(it)) { m_buyItem = id; m_buyCount = 1; sfx::Play(sfx::Id::Click, 0.6f); }
		if (hover) DrawItemTooltip(id, 0, 1, false, false);
		y += 47;
	}

	// 수량/합계/구매
	const float fy = r.y + r.height - 104;
	ui::Divider(r.x + 12, r.x + r.width - 12, fy - 8);
	const ItemTableRow* sel = ItemTableData::Find(m_buyItem);
	const int maxStack = sel ? (sel->type == ItemType::eEquipment ? 1 : sel->maxStack) : 1;
	const int affordableMax = sel && sel->buyPrice > 0 ? static_cast<int>(std::min<uint32_t>(m_inv.adena / sel->buyPrice, 999u)) : 0;
	const int maxCount = std::max(1, std::min(maxStack, std::max(1, affordableMax)));
	m_buyCount = std::clamp(m_buyCount, 1, maxCount);
	ui::Text("수량", Vector2{ r.x + 16, fy + 10 }, 15.f, ui::theme::kTextDim);
	if (ui::Button(Rectangle{ r.x + 60, fy + 4, 34, 32 }, "-", ButtonKind::Normal, sel != nullptr)) m_buyCount = std::max(1, m_buyCount - 1);
	DrawRectangleRounded(Rectangle{ r.x + 98, fy + 4, 60, 32 }, 0.2f, 6, Color{ 10, 9, 12, 220 });
	ui::Text(std::to_string(m_buyCount), Vector2{ r.x + 128, fy + 10 }, 17.f, WHITE, Align::Center);
	if (ui::Button(Rectangle{ r.x + 162, fy + 4, 34, 32 }, "+", ButtonKind::Normal, sel != nullptr)) m_buyCount = std::min(maxCount, m_buyCount + 1);
	if (ui::Button(Rectangle{ r.x + 200, fy + 4, 48, 32 }, "×10", ButtonKind::Normal, sel != nullptr)) m_buyCount = std::min(maxCount, m_buyCount + 10);
	if (ui::Button(Rectangle{ r.x + 252, fy + 4, 52, 32 }, "최대", ButtonKind::Normal, sel != nullptr)) m_buyCount = maxCount;
	const uint64_t total = sel ? static_cast<uint64_t>(sel->buyPrice) * m_buyCount : 0;
	CoinIcon(Vector2{ r.x + 22, fy + 58 }, 7.f);
	ui::Text("합계 " + texts::Number(static_cast<long long>(total)), Vector2{ r.x + 36, fy + 49 }, 17.f, total <= m_inv.adena ? Rgb(0xFFD84A) : ui::theme::kBad);
	ui::Text("보유 " + texts::Number(m_inv.adena), Vector2{ r.x + 36, fy + 72 }, 13.f, ui::theme::kTextDim);
	const bool can = sel && total <= m_inv.adena;
	if (ui::Button(Rectangle{ r.x + r.width - 144, fy + 48, 130, 42 }, "구매하기", ButtonKind::Primary, can, 18.f))
		m_lastShopSeq = m_net.SendShopBuy(npc->id, m_buyItem, static_cast<uint16_t>(m_buyCount));
	if (ui::Button(Rectangle{ r.x + r.width - 144, fy + 4, 130, 32 }, "뒤로"))
		m_shopMode = ShopMode::None;
}

void Game::DrawSell()
{
	const auto* npc = WorldData::FindNpc(m_dialogNpc);
	if (!npc) { m_shopMode = ShopMode::None; return; }
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	m_invOpen = true;
	const Rectangle r{ std::max(12.f, W * 0.5f - 420.f), std::max(12.f, H * 0.5f - 180.f), 400.f, 300.f };
	bool open = true;
	ui::Window(r, std::string(npc->name) + " — 판매", &open);
	if (!open) { CloseNpcWindows(); return; }

	if (m_sellSlot < 0 || m_inv.slots[m_sellSlot].Empty())
	{
		m_sellSlot = -1;
		ui::TextWrapped("오른쪽 소지품 창에서 팔 물건을 클릭하세요.\n착용 중인 장비와 아데나는 팔 수 없습니다.", Rectangle{ r.x + 24, r.y + 70, r.width - 48, 120 }, 16.f, ui::theme::kText, 8.f);
	}
	else
	{
		const InvSlot& s = m_inv.slots[m_sellSlot];
		const ItemTableRow* row = ItemTableData::Find(s.itemId);
		const uint32_t unit = row ? (row->type == ItemType::eEquipment ? row->sellPrice * (10u + s.enchant) / 10u : row->sellPrice) : 0;
		ui::SlotFrame(Rectangle{ r.x + 20, r.y + 52, 64, 64 }, false, true);
		icons::Draw(s.itemId, Rectangle{ r.x + 24, r.y + 56, 56, 56 });
		ui::TextShadow(texts::ItemNameEnchanted(s.itemId, s.enchant), Vector2{ r.x + 98, r.y + 58 }, 18.f, icons::ItemNameColor(s.itemId));
		ui::Text("개당 " + texts::Number(unit) + " 아데나", Vector2{ r.x + 98, r.y + 86 }, 15.f, Rgb(0xFFD84A));
		m_sellCount = std::clamp(m_sellCount, 1, static_cast<int>(s.count));
		const float fy = r.y + 136;
		ui::Text("수량", Vector2{ r.x + 20, fy + 8 }, 15.f, ui::theme::kTextDim);
		if (ui::Button(Rectangle{ r.x + 64, fy, 34, 32 }, "-")) m_sellCount = std::max(1, m_sellCount - 1);
		DrawRectangleRounded(Rectangle{ r.x + 102, fy, 60, 32 }, 0.2f, 6, Color{ 10, 9, 12, 220 });
		ui::Text(std::to_string(m_sellCount), Vector2{ r.x + 132, fy + 6 }, 17.f, WHITE, Align::Center);
		if (ui::Button(Rectangle{ r.x + 166, fy, 34, 32 }, "+")) m_sellCount = std::min(static_cast<int>(s.count), m_sellCount + 1);
		if (ui::Button(Rectangle{ r.x + 204, fy, 52, 32 }, "전부")) m_sellCount = s.count;
		const uint64_t total = static_cast<uint64_t>(unit) * m_sellCount;
		CoinIcon(Vector2{ r.x + 26, fy + 62 }, 7.f);
		ui::Text("받을 금액 " + texts::Number(static_cast<long long>(total)), Vector2{ r.x + 40, fy + 53 }, 18.f, Rgb(0xFFD84A));
		if (ui::Button(Rectangle{ r.x + r.width - 144, r.y + r.height - 58, 130, 42 }, "판매하기", ButtonKind::Primary, true, 18.f))
			m_lastShopSeq = m_net.SendShopSell(npc->id, static_cast<uint16_t>(m_sellSlot), static_cast<uint16_t>(m_sellCount));
	}
	if (ui::Button(Rectangle{ r.x + 16, r.y + r.height - 58, 100, 42 }, "뒤로"))
		m_shopMode = ShopMode::None;
}

void Game::DrawTeleport()
{
	const auto* npc = WorldData::FindNpc(m_dialogNpc);
	if (!npc) { m_shopMode = ShopMode::None; return; }
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const Rectangle r{ W * 0.5f - 280.f, std::max(12.f, H * 0.5f - 300.f), 560.f, std::min(H - 130.f, 600.f) };
	bool open = true;
	ui::Window(r, "텔레포트 — 목적지 선택", &open);
	if (!open) { CloseNpcWindows(); return; }

	float y = r.y + 44;
	const float rowH = std::min(32.f, (r.height - 150.f) / 15.f);
	for (const auto& reg : WorldData::kRegions)
	{
		if (reg.teleportFee == 0) continue;   // 마을 지역
		const Rectangle it{ r.x + 12, y, r.width - 24, rowH - 3 };
		const bool hover = ui::Hovered(it), sel = m_teleportSel == reg.zoneId;
		DrawRectangleRounded(it, 0.15f, 6, sel ? Color{ 90, 70, 40, 220 } : (hover ? Color{ 60, 50, 44, 200 } : Color{ 30, 26, 30, 180 }));
		const float ty = it.y + (it.height - 15.f) * 0.5f - 1.f;
		ui::TextShadow(reg.name, Vector2{ it.x + 12, ty }, 15.f, sel ? ui::theme::kTitle : ui::theme::kText);
		Stars(Vector2{ it.x + 150, it.y + it.height * 0.5f }, reg.tier, 9.f);
		if (reg.bossSpecies)
			ui::Text("보스: " + texts::MonsterName(reg.bossSpecies), Vector2{ it.x + 222, ty + 1 }, 13.f, Rgb(0xFF8A6A));
		const bool affordable = m_inv.adena >= reg.teleportFee;
		ui::Text(texts::Number(reg.teleportFee), Vector2{ it.x + it.width - 12, ty }, 15.f, affordable ? Rgb(0xFFD84A) : ui::theme::kBad, Align::Right);
		ui::Block(it);
		if (ui::Clicked(it)) { m_teleportSel = reg.zoneId; sfx::Play(sfx::Id::Click, 0.6f); }
		y += rowH;
	}
	const float fy = r.y + r.height - 60;
	ui::Divider(r.x + 12, r.x + r.width - 12, fy - 8);
	CoinIcon(Vector2{ r.x + 24, fy + 20 }, 7.f);
	ui::Text("보유 " + texts::Number(m_inv.adena), Vector2{ r.x + 38, fy + 11 }, 16.f, Rgb(0xFFD84A));
	const auto* dest = WorldData::FindRegion(m_teleportSel);
	const bool can = dest && dest->teleportFee > 0 && m_inv.adena >= dest->teleportFee;
	if (ui::Button(Rectangle{ r.x + r.width - 154, fy, 140, 44 }, dest ? "이동하기" : "목적지 선택", ButtonKind::Primary, can, 18.f))
		m_lastShopSeq = m_net.SendNpcTeleport(npc->id, static_cast<uint16_t>(m_teleportSel));
	if (ui::Button(Rectangle{ r.x + r.width - 264, fy, 100, 44 }, "뒤로"))
		m_shopMode = ShopMode::None;
}

// ─────────────────────────────────────────────────────────────────────────────
// 강화 · 버리기 · 사망
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawEnchant()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	DrawRectangle(0, 0, m_screenW, m_screenH, Color{ 0, 0, 0, 120 });
	const Rectangle r{ W * 0.5f - 230.f, H * 0.5f - 190.f, 460.f, 360.f };
	ui::Panel(r);
	ui::TextShadow("무기 강화", Vector2{ r.x + r.width * 0.5f, r.y + 14 }, 21.f, ui::theme::kTitle, Align::Center);
	const Vector2 c{ r.x + r.width * 0.5f, r.y + 118 };
	const float t = static_cast<float>(m_now - m_enchant.start);
	const ItemTableRow* scroll = ItemTableData::Find(m_enchant.scrollId);
	const bool blessed = scroll && scroll->effectValue == 1;

	if (m_enchant.stage == 2)
	{
		const int before = m_enchant.before;
		const uint32_t chance = EnchantRule::SuccessPercent(static_cast<uint8_t>(before));
		DrawCircleGradient(static_cast<int>(c.x), static_cast<int>(c.y), 70.f, Color{ 255, 220, 140, 60 }, Color{ 255, 220, 140, 0 });
		icons::Draw(m_enchant.itemId, Rectangle{ c.x - 48, c.y - 48, 96, 96 });
		ui::TextShadow(texts::ItemNameEnchanted(m_enchant.itemId, before) + "  →  +" + std::to_string(before + 1), Vector2{ c.x, c.y + 60 }, 18.f, icons::ItemNameColor(m_enchant.itemId), Align::Center);
		const Color cc = chance >= 100 ? ui::theme::kGood : (chance >= 30 ? Rgb(0xFFD070) : ui::theme::kBad);
		ui::TextShadow(Fmt("성공 확률 %u%%", chance), Vector2{ c.x, c.y + 88 }, 19.f, cc, Align::Center);
		std::string note = chance >= 100 ? "안전 강화 구간입니다(+6까지 실패 없음)." : "실패하면 무기가 증발합니다!";
		if (blessed) note += "  축복: 크게 오를 수 있음";
		ui::Text(note, Vector2{ c.x, c.y + 116 }, 14.f, chance >= 100 ? ui::theme::kTextDim : ui::theme::kBad, Align::Center);
		if (ui::Button(Rectangle{ r.x + 40, r.y + r.height - 60, 180, 44 }, "강화하기", chance >= 100 ? ButtonKind::Primary : ButtonKind::Danger, true, 18.f))
		{
			m_net.SendEnchant(static_cast<uint16_t>(m_enchant.scrollSlot), static_cast<uint16_t>(m_enchant.targetSlot));
			m_enchant.stage = 3;
			m_enchant.start = m_now;
			m_enchant.resultKnown = false;
			sfx::Play(sfx::Id::EnchantCharge);
		}
		if (ui::Button(Rectangle{ r.x + r.width - 220, r.y + r.height - 60, 180, 44 }, "취소"))
			m_enchant = EnchantFlow{};
		return;
	}
	if (m_enchant.stage == 3)
	{
		// 충전: 떨림 + 빛이 모인다
		const float k = util::Clamp01(t / 1.25f);
		const float shake = k * 4.f;
		const Vector2 sc{ c.x + util::RandF(-shake, shake), c.y + util::RandF(-shake, shake) };
		BeginBlendMode(BLEND_ADDITIVE);
		DrawCircleGradient(static_cast<int>(sc.x), static_cast<int>(sc.y), 60.f + 40.f * k, Color{ 255, 230, 160, static_cast<unsigned char>(80 + 140 * k) }, Color{ 255, 200, 80, 0 });
		for (int i = 0; i < 10; ++i)
		{
			const float a = static_cast<float>(m_now) * 3.f + i * 0.628f;
			const float rr = 110.f * (1.f - std::fmod(t * 0.9f + i * 0.1f, 1.f));
			DrawCircleV(Vector2{ c.x + std::cos(a) * rr, c.y + std::sin(a) * rr }, 3.f, Color{ 255, 240, 200, 200 });
		}
		EndBlendMode();
		icons::Draw(m_enchant.itemId, Rectangle{ sc.x - 48, sc.y - 48, 96, 96 });
		ui::TextShadow("주문서의 힘이 무기에 스며듭니다...", Vector2{ c.x, c.y + 80 }, 17.f, Rgb(0xFFE8B0), Align::Center);
		return;
	}
	// 결과
	const bool ok = m_enchant.result == EnchantResult::Success;
	if (ok)
	{
		BeginBlendMode(BLEND_ADDITIVE);
		for (int i = 0; i < 12; ++i)
		{
			const float a = static_cast<float>(m_now) * 0.6f + i * util::kPi / 6.f;
			DrawTriangle(c, Vector2{ c.x + std::cos(a + 0.08f) * 160.f, c.y + std::sin(a + 0.08f) * 160.f }, Vector2{ c.x + std::cos(a - 0.08f) * 160.f, c.y + std::sin(a - 0.08f) * 160.f }, Color{ 200, 230, 255, 40 });
		}
		DrawCircleGradient(static_cast<int>(c.x), static_cast<int>(c.y), 90.f, Color{ 180, 220, 255, 150 }, Color{ 180, 220, 255, 0 });
		EndBlendMode();
		const float pop = util::EaseOutBack(util::Clamp01(t / 0.35f));
		icons::Draw(m_enchant.itemId, Rectangle{ c.x - 48 * pop, c.y - 48 * pop, 96 * pop, 96 * pop });
		ui::TextStyle st;
		st.size = 30.f * pop;
		st.color = Rgb(0x9AD8FF);
		st.outline = 2.5f;
		st.outlineColor = Color{ 10, 20, 50, 230 };
		ui::Fonts().DrawCentered("강화 성공!", Vector2{ c.x, c.y + 78 }, st);
		ui::TextShadow(texts::ItemNameEnchanted(m_enchant.itemId, m_enchant.after), Vector2{ c.x, c.y + 104 }, 18.f, icons::ItemNameColor(m_enchant.itemId), Align::Center);
	}
	else
	{
		// 부서져 흩어지는 조각
		for (int i = 0; i < 12; ++i)
		{
			const float a = i * 0.52f;
			const float d = t * 90.f;
			const Vector2 p{ c.x + std::cos(a) * d, c.y + std::sin(a) * d + t * t * 120.f };
			const unsigned char al = static_cast<unsigned char>(255 * util::Clamp01(1.f - t / 1.5f));
			DrawPoly(p, 3, 6.f + (i % 3) * 2.f, a * 57.f + t * 200.f, Color{ 190, 196, 210, al });
		}
		ui::TextStyle st;
		st.size = 26.f;
		st.color = Rgb(0xC08AFF);
		st.outline = 2.5f;
		st.outlineColor = Color{ 30, 10, 40, 230 };
		ui::Fonts().DrawCentered("강화 실패...", Vector2{ c.x, c.y + 60 }, st);
		ui::TextShadow(texts::Josa(texts::ItemNameEnchanted(m_enchant.itemId, m_enchant.before), "이", "가") + " 증발했습니다.", Vector2{ c.x, c.y + 94 }, 16.f, ui::theme::kTextDim, Align::Center);
	}
	if (ui::Button(Rectangle{ r.x + r.width * 0.5f - 80, r.y + r.height - 60, 160, 44 }, "확인", ButtonKind::Primary) || t > 4.f)
		m_enchant = EnchantFlow{};
}

void Game::DrawDiscard()
{
	const InvSlot& s = m_inv.slots[m_discard.slot];
	if (s.Empty()) { m_discard.open = false; return; }
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const Rectangle r{ W * 0.5f - 180.f, H * 0.5f - 100.f, 360.f, 200.f };
	ui::Panel(r);
	ui::TextShadow("버리기", Vector2{ r.x + r.width * 0.5f, r.y + 14 }, 19.f, ui::theme::kTitle, Align::Center);
	icons::Draw(s.itemId, Rectangle{ r.x + 20, r.y + 50, 48, 48 });
	ui::TextShadow(texts::ItemNameEnchanted(s.itemId, s.enchant), Vector2{ r.x + 80, r.y + 56 }, 17.f, icons::ItemNameColor(s.itemId));
	m_discard.count = std::clamp(m_discard.count, 1, static_cast<int>(s.count));
	if (ui::Button(Rectangle{ r.x + 80, r.y + 84, 34, 30 }, "-")) m_discard.count = std::max(1, m_discard.count - 1);
	ui::Text(std::to_string(m_discard.count) + " / " + std::to_string(s.count), Vector2{ r.x + 160, r.y + 89 }, 16.f, WHITE, Align::Center);
	if (ui::Button(Rectangle{ r.x + 206, r.y + 84, 34, 30 }, "+")) m_discard.count = std::min(static_cast<int>(s.count), m_discard.count + 1);
	if (ui::Button(Rectangle{ r.x + 246, r.y + 84, 60, 30 }, "전부")) m_discard.count = s.count;
	if (ui::Button(Rectangle{ r.x + 30, r.y + r.height - 56, 140, 42 }, "버리기", ButtonKind::Danger))
	{
		m_net.SendDiscard(static_cast<uint16_t>(m_discard.slot), static_cast<uint16_t>(m_discard.count));
		m_discard.open = false;
	}
	if (ui::Button(Rectangle{ r.x + r.width - 170, r.y + r.height - 56, 140, 42 }, "취소"))
		m_discard.open = false;
}

void Game::DrawDeath()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const float t = static_cast<float>(m_now - m_deadAt);
	const float a = util::Clamp01(t / 1.2f);
	DrawRectangle(0, 0, m_screenW, m_screenH, Color{ 20, 0, 0, static_cast<unsigned char>(150 * a) });
	const unsigned char band = static_cast<unsigned char>(120 * a);
	DrawRectangleGradientV(0, static_cast<int>(H * 0.2f), m_screenW, static_cast<int>(H * 0.16f), Color{ 60, 0, 0, 0 }, Color{ 60, 0, 0, band });
	DrawRectangleGradientV(0, static_cast<int>(H * 0.36f), m_screenW, static_cast<int>(H * 0.16f), Color{ 60, 0, 0, band }, Color{ 60, 0, 0, 0 });
	if (t < 0.6f) return;
	const float k = util::EaseOutCubic(util::Clamp01((t - 0.6f) / 0.6f));
	ui::TextStyle st;
	st.size = 52.f;
	st.color = util::MulA(Rgb(0xFF5A4A), k);
	st.outline = 3.f;
	st.outlineColor = util::MulA(Color{ 30, 0, 0, 255 }, k);
	st.shadow = 5.f;
	ui::Fonts().DrawCentered("사망하였습니다", Vector2{ W * 0.5f, H * 0.36f - (1.f - k) * 20.f }, st);
	const int remain = std::max(0, 30 - static_cast<int>(t));
	ui::Text(Fmt("%d초 뒤 자동으로 %s에서 다시 시작합니다.", remain, WorldData::kTownName), Vector2{ W * 0.5f, H * 0.36f + 44.f }, 16.f, util::MulA(ui::theme::kText, k), Align::Center, true);
	if (t > 1.2f)
	{
		const Rectangle b{ W * 0.5f - 130.f, H * 0.36f + 84.f, 260.f, 50.f };
		if (ui::Button(b, m_restartSent ? "부활 중..." : "마을에서 다시 시작", ButtonKind::Primary, !m_restartSent, 19.f))
		{
			m_net.SendRestart();
			m_restartSent = true;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 알림 · 도움말 · 지도
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawBanners()
{
	const float W = static_cast<float>(m_screenW);
	float y = 84.f;
	for (const auto& b : m_banners)
	{
		const float age = static_cast<float>(m_now - b.start);
		const float in = util::EaseOutBack(util::Clamp01(age / 0.35f));
		const float out = age > b.life - 0.5f ? 1.f - (age - (b.life - 0.5f)) / 0.5f : 1.f;
		const float a = util::Clamp01(out);
		const float yy = y - (1.f - in) * 30.f;
		const float w = std::min(W - 40.f, ui::Fonts().Measure(b.text, 20.f).x + 160.f);
		DrawRectangleGradientH(static_cast<int>(W * 0.5f - w * 0.5f), static_cast<int>(yy), static_cast<int>(w * 0.5f), 38, util::MulA(b.color, 0.f), util::MulA(Color{ 20, 12, 8, 220 }, a));
		DrawRectangleGradientH(static_cast<int>(W * 0.5f), static_cast<int>(yy), static_cast<int>(w * 0.5f), 38, util::MulA(Color{ 20, 12, 8, 220 }, a), util::MulA(b.color, 0.f));
		DrawRectangleGradientH(static_cast<int>(W * 0.5f - w * 0.4f), static_cast<int>(yy), static_cast<int>(w * 0.4f), 2, util::MulA(b.color, 0.f), util::MulA(b.color, a));
		DrawRectangleGradientH(static_cast<int>(W * 0.5f), static_cast<int>(yy), static_cast<int>(w * 0.4f), 2, util::MulA(b.color, a), util::MulA(b.color, 0.f));
		DrawRectangleGradientH(static_cast<int>(W * 0.5f - w * 0.4f), static_cast<int>(yy + 36), static_cast<int>(w * 0.4f), 2, util::MulA(b.color, 0.f), util::MulA(b.color, a));
		DrawRectangleGradientH(static_cast<int>(W * 0.5f), static_cast<int>(yy + 36), static_cast<int>(w * 0.4f), 2, util::MulA(b.color, a), util::MulA(b.color, 0.f));
		ui::TextStyle st;
		st.size = 20.f;
		st.color = util::MulA(util::Shade(b.color, 1.15f), a);
		st.outline = 1.8f;
		st.outlineColor = util::MulA(Color{ 0, 0, 0, 220 }, a);
		ui::Fonts().DrawCentered(b.text, Vector2{ W * 0.5f, yy + 19.f }, st);
		y += 44.f;
	}
}

void Game::DrawToasts()
{
	const float W = static_cast<float>(m_screenW);
	float y = 262.f;
	const float x1 = m_invOpen ? m_invRect.x - 12.f : W - 14.f;
	for (const auto& t : m_toasts)
	{
		const float age = static_cast<float>(m_now - t.start);
		const float in = util::EaseOutCubic(util::Clamp01(age / 0.25f));
		const float a = age > 3.f ? 1.f - (age - 3.f) / 0.5f : 1.f;
		const float w = ui::Fonts().Measure(t.text, 15.f).x + 64.f;
		const Rectangle r{ x1 - w + (1.f - in) * 60.f, y, w, 38.f };
		DrawRectangleRounded(r, 0.25f, 6, util::MulA(Color{ 16, 12, 18, 220 }, a));
		DrawRectangleRoundedLinesEx(r, 0.25f, 6, 1.f, util::MulA(t.color, a * 0.7f));
		icons::Draw(t.itemId, Rectangle{ r.x + 6, r.y + 3, 32, 32 }, util::MulA(WHITE, a));
		ui::Text(t.text, Vector2{ r.x + 46, r.y + 10 }, 15.f, util::MulA(t.color, a), Align::Left, true);
		y += 42.f;
	}
}

void Game::DrawHelp()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const Rectangle r{ W * 0.5f - 300.f, H * 0.5f - 250.f, 600.f, 500.f };
	ui::Window(r, "도움말", &m_helpOpen);
	const std::pair<const char*, const char*> rows[] = {
		{ "마우스 왼쪽", "이동 · 몬스터 공격 · 아이템 줍기 · NPC 대화 (누르고 있으면 계속 이동)" },
		{ "Ctrl + 클릭", "플레이어 공격(PK) — 성향치와 보라돌이 규칙 주의" },
		{ "W A S D / 방향키", "화면 방향으로 한 칸씩 이동" },
		{ "Space", "가장 가까운 몬스터 자동 공격" },
		{ "Q", "십자베기 (동서남북 인접한 적 동시 공격, 5초)" },
		{ "1 ~ 8 / F5 ~ F12", "퀵슬롯 사용 (소지품에서 끌어다 등록)" },
		{ "Z", "발밑/주변 아이템 줍기" },
		{ "I / Tab", "소지품 — 더블클릭/우클릭 사용, 끌어서 교환·버리기" },
		{ "M", "월드 지도" },
		{ "Enter", "채팅 (&로 시작하면 전체 채팅)" },
		{ "Alt", "바닥 아이템/몬스터 이름 모두 보기" },
		{ "휠 / + -", "화면 확대·축소" },
		{ "G", "바닥 격자 표시 전환" },
		{ "Esc", "창 닫기 · 대상 해제" },
	};
	float y = r.y + 50;
	for (const auto& [k, v] : rows)
	{
		ui::TextShadow(k, Vector2{ r.x + 24, y }, 15.f, ui::theme::kTitle);
		ui::Text(v, Vector2{ r.x + 190, y }, 15.f, ui::theme::kText);
		y += 29;
	}
	ui::Divider(r.x + 20, r.x + r.width - 20, y + 4);
	ui::TextWrapped("마을(여명의 마을)은 안전지대입니다. 텔레포터 루시엔에게 가면 다른 지역으로 이동할 수 있고, "
		"잡화상인 그레타와 대장장이 브론에게서 물약·주문서·무기를 사고팔 수 있습니다.",
		Rectangle{ r.x + 24, y + 14, r.width - 48, 80 }, 14.f, ui::theme::kTextDim, 5.f);
}

void Game::DrawWorldMap()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const float cell = std::min(132.f, (std::min(W, H) - 140.f) / 4.f);
	const Rectangle r{ W * 0.5f - (cell * 4 + 40) * 0.5f, H * 0.5f - (cell * 4 + 90) * 0.5f, cell * 4 + 40, cell * 4 + 90 };
	ui::Window(r, "월드 지도", &m_mapOpen);
	const float x0 = r.x + 20, y0 = r.y + 46;
	for (const auto& reg : WorldData::kRegions)
	{
		const int gx = reg.zoneId % 4, gy = reg.zoneId / 4;
		const Rectangle c{ x0 + gx * cell, y0 + gy * cell, cell - 3, cell - 3 };
		const props::Palette pal = props::PaletteFor(reg.zoneId);
		const Color base{ static_cast<unsigned char>(pal.a[0] * 255), static_cast<unsigned char>(pal.a[1] * 255), static_cast<unsigned char>(pal.a[2] * 255), 255 };
		const Color dark{ static_cast<unsigned char>(pal.b[0] * 200), static_cast<unsigned char>(pal.b[1] * 200), static_cast<unsigned char>(pal.b[2] * 200), 255 };
		DrawRectangleGradientEx(c, base, dark, util::Shade(dark, 0.8f), util::Shade(base, 0.9f));
		const bool hover = ui::Hovered(c);
		DrawRectangleLinesEx(c, hover ? 2.f : 1.f, hover ? ui::theme::kBorderHi : Color{ 0, 0, 0, 120 });
		ui::TextShadow(reg.name, Vector2{ c.x + 8, c.y + 8 }, 15.f, WHITE);
		Stars(Vector2{ c.x + 14, c.y + 36 }, reg.tier, 9.f);
		if (reg.bossSpecies)
		{
			DrawCircleV(Vector2{ c.x + c.width - 16, c.y + c.height - 16 }, 9.f, Rgb(0x8A1A1A));
			ui::Text("보", Vector2{ c.x + c.width - 16, c.y + c.height - 25 }, 12.f, WHITE, Align::Center);
			if (hover)
				ui::Tooltip({ { reg.name, ui::theme::kTitle, 17.f }, { "필드 보스: " + texts::MonsterName(reg.bossSpecies), Rgb(0xFF8A6A), 15.f },
					{ "텔레포트 " + texts::Number(reg.teleportFee) + " 아데나", Rgb(0xFFD84A), 14.f } });
		}
		else if (hover && reg.teleportFee > 0)
			ui::Tooltip({ { reg.name, ui::theme::kTitle, 17.f }, { "텔레포트 " + texts::Number(reg.teleportFee) + " 아데나", Rgb(0xFFD84A), 14.f } });
		if (reg.zoneId == WorldData::kTownZoneId)
		{
			const Vector2 tp{ c.x + (WorldData::kTownX0 + 22 - 500.f) / 500.f * cell, c.y + (WorldData::kTownY0 + 22 - 500.f) / 500.f * cell };
			DrawRectangleV(Vector2{ tp.x - 7, tp.y - 5 }, Vector2{ 14, 10 }, Rgb(0xC8B8A0));
			DrawTriangle(Vector2{ tp.x - 8, tp.y - 5 }, Vector2{ tp.x + 8, tp.y - 5 }, Vector2{ tp.x, tp.y - 12 }, Rgb(0xB8483A));
			ui::Text(WorldData::kTownName, Vector2{ tp.x, tp.y + 8 }, 12.f, Rgb(0x9AF06A), Align::Center, true);
		}
	}
	// 내 위치
	const float px = x0 + m_predX / 500.f * cell, py = y0 + m_predY / 500.f * cell;
	const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 5.f);
	DrawCircleV(Vector2{ px, py }, 8.f + pulse * 4.f, Color{ 255, 255, 255, static_cast<unsigned char>(80 * (1.f - pulse)) });
	DrawCircleV(Vector2{ px, py }, 5.f, WHITE);
	DrawCircleV(Vector2{ px, py }, 3.f, Rgb(0xFF4A3A));
	ui::Text("현재 위치: " + (InTown() ? std::string(WorldData::kTownName) : texts::RegionName(Region())), Vector2{ r.x + r.width * 0.5f, r.y + r.height - 34 }, 15.f, ui::theme::kText, Align::Center);
}
