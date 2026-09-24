#include "game/Game.h"
#include "game/Texts.h"
#include "fx/Effects.h"
#include "fx/SkillFx.h"
#include "render/Draw.h"
#include "render/Actors.h"
#include "render/Icons.h"
#include "render/Props.h"
#include "render/Assets.h"
#include "audio/Sfx.h"
#include "ui/Font.h"
#include "core/Util.h"
#include "Data/NpcTalk.h"

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

	// ── 아래 HUD 배치(한 곳에서 정해 두고 여러 함수가 같이 쓴다) ──
	struct HudLayout
	{
		Rectangle panel{}, hp{}, mp{};
		float skillX = 0, skillY = 0, skillS = 52.f, skillStep = 56.f;
		float quickX = 0, quickY = 0, quickS = 40.f, quickStep = 44.f;
	};
	HudLayout Layout(float W, float H)
	{
		HudLayout L;
		L.panel = Rectangle{ W * 0.5f - 390.f, H - 100.f, 780.f, 88.f };
		L.hp = Rectangle{ L.panel.x + 14.f, L.panel.y + 12.f, 206.f, 22.f };
		L.mp = Rectangle{ L.panel.x + 14.f, L.panel.y + 38.f, 206.f, 14.f };
		L.skillX = L.panel.x + 234.f;
		L.skillY = L.panel.y + 18.f;
		L.quickX = L.skillX + 6 * L.skillStep + 14.f;
		L.quickY = L.panel.y + 24.f;
		return L;
	}

	Color ClassColor(ClassId c) { return Rgb(ClassTable::Get(c).color); }

	// 직업 문장(원 + 상징) — 화면 공간
	void ClassEmblem(Vector2 c, float r, ClassId cls, float time)
	{
		const Color col = ClassColor(cls);
		DrawCircleV(Vector2{ c.x + 1, c.y + 2 }, r, Color{ 0, 0, 0, 120 });
		DrawCircleGradient(static_cast<int>(c.x), static_cast<int>(c.y), r, util::Shade(col, 0.55f), util::Shade(col, 0.18f));
		DrawRing(c, r - 2.5f, r, 0.f, 360.f, 40, util::Shade(col, 1.1f));
		const float k = r / 30.f;
		switch (cls)
		{
		case ClassId::Knight:   // 칼 + 방패
			DrawLineEx(Vector2{ c.x - 12 * k, c.y + 14 * k }, Vector2{ c.x + 12 * k, c.y - 14 * k }, 4.f * k, util::Shade(col, 1.5f));
			DrawLineEx(Vector2{ c.x - 6 * k, c.y + 2 * k }, Vector2{ c.x + 2 * k, c.y + 10 * k }, 3.f * k, util::Shade(col, 1.3f));
			DrawCircleV(Vector2{ c.x - 7 * k, c.y - 3 * k }, 9 * k, util::Shade(col, 0.7f));
			DrawRing(Vector2{ c.x - 7 * k, c.y - 3 * k }, 7.5f * k, 9.f * k, 0, 360, 24, util::Shade(col, 1.4f));
			break;
		case ClassId::RuneCaster:   // 도는 룬 삼각
		{
			for (int i = 0; i < 3; ++i)
			{
				const float a0 = time * 0.8f + i * util::kPi * 2.f / 3.f, a1 = a0 + util::kPi * 2.f / 3.f;
				DrawLineEx(Vector2{ c.x + std::cos(a0) * 15 * k, c.y + std::sin(a0) * 15 * k }, Vector2{ c.x + std::cos(a1) * 15 * k, c.y + std::sin(a1) * 15 * k }, 3.f * k, util::Shade(col, 1.4f));
			}
			DrawCircleV(c, 4.f * k, WHITE);
			DrawRing(c, 17 * k, 19 * k, 0, 360, 32, util::WithA(util::Shade(col, 1.3f), 0.7f));
			break;
		}
		case ClassId::ShadowHunter:   // 초승달 + 단검
		default:
			DrawCircleV(Vector2{ c.x - 3 * k, c.y - 2 * k }, 13 * k, util::Shade(col, 1.35f));
			DrawCircleV(Vector2{ c.x + 3 * k, c.y - 6 * k }, 12 * k, util::Shade(col, 0.28f));
			DrawLineEx(Vector2{ c.x + 2 * k, c.y + 14 * k }, Vector2{ c.x + 13 * k, c.y - 1 * k }, 3.f * k, Rgb(0xE8E0FF));
			break;
		}
	}

	const char* StatName(int i)
	{
		static const char* n[6] = { "힘 STR", "민첩 DEX", "지능 INT", "체력 VIT", "지혜 WIS", "행운 LUK" };
		return (i >= 0 && i < 6) ? n[i] : "";
	}
	const char* StatHelp(int i)
	{
		static const char* h[6] = {
			"물리 공격력과 방어력이 오른다. 기사의 주 능력치.",
			"공격 속도·명중·회피·치명타가 오르고, 그림자 사냥꾼은 공격력도 오른다.",
			"마법 공격력·마법 관통·최대 MP가 오른다. 룬술사의 주 능력치.",
			"최대 HP·방어력·상태이상 저항이 오른다. 오래 버티게 해 준다.",
			"최대 MP·MP 회복·마법 방어·재사용 대기 감소가 오른다.",
			"치명타 확률과 치명타 피해, 회피가 조금 오른다.",
		};
		return (i >= 0 && i < 6) ? h[i] : "";
	}
	bool IsMainStat(ClassId c, int i)
	{
		switch (c)
		{
		case ClassId::Knight: return i == 0 || i == 3;
		case ClassId::RuneCaster: return i == 2 || i == 4;
		default: return i == 1 || i == 5;
		}
	}
	const char* StatusHelp(StatusId id)
	{
		switch (id)
		{
		case StatusId::Stun: return "움직이거나 공격할 수 없다.";
		case StatusId::Freeze: return "움직일 수 없다. 화염에 맞으면 증발 폭발, 혜성에 맞으면 산산조각.";
		case StatusId::Burn: return "매초 불 피해를 입는다. 번개가 더 멀리 튄다.";
		case StatusId::Poison: return "매초 독 피해(중첩). 월영 난무가 한꺼번에 터뜨린다.";
		case StatusId::Slow: return "이동 속도가 느려진다.";
		case StatusId::Guard: return "받는 피해가 절반, 맞을 때마다 반격한다.";
		case StatusId::Ambush: return "다음 공격이 확정 치명타가 된다.";
		case StatusId::Evade: return "잠깐 모든 공격을 피한다.";
		case StatusId::Marked: return "번개 폭발이 이 대상을 거쳐 더 멀리 튄다.";
		default: return "";
		}
	}

	// 상태 아이콘 그림(화면 공간, 중심 c, 반지름 r)
	void StatusGlyph(StatusId id, Vector2 c, float r, Color col, float time)
	{
		switch (id)
		{
		case StatusId::Stun: DrawPoly(c, 5, r * 0.8f, time * 90.f, col); break;
		case StatusId::Freeze:
			for (int i = 0; i < 3; ++i) { const float a = i * util::kPi / 3.f; DrawLineEx(Vector2{ c.x - std::cos(a) * r, c.y - std::sin(a) * r }, Vector2{ c.x + std::cos(a) * r, c.y + std::sin(a) * r }, 2.f, col); }
			break;
		case StatusId::Burn:
			DrawTriangle(Vector2{ c.x, c.y - r }, Vector2{ c.x - r * 0.7f, c.y + r * 0.7f }, Vector2{ c.x + r * 0.7f, c.y + r * 0.7f }, col);
			DrawTriangle(Vector2{ c.x, c.y - r * 0.2f }, Vector2{ c.x - r * 0.35f, c.y + r * 0.7f }, Vector2{ c.x + r * 0.35f, c.y + r * 0.7f }, Rgb(0xFFE08A));
			break;
		case StatusId::Poison:
			DrawCircleV(Vector2{ c.x, c.y + r * 0.25f }, r * 0.6f, col);
			DrawTriangle(Vector2{ c.x, c.y - r }, Vector2{ c.x - r * 0.52f, c.y }, Vector2{ c.x + r * 0.52f, c.y }, col);
			break;
		case StatusId::Slow:
			DrawTriangle(Vector2{ c.x - r * 0.8f, c.y - r * 0.3f }, Vector2{ c.x, c.y + r * 0.7f }, Vector2{ c.x + r * 0.8f, c.y - r * 0.3f }, col);
			break;
		case StatusId::Guard:
			DrawPoly(c, 6, r * 0.85f, 30.f, util::Shade(col, 0.6f));
			DrawPolyLinesEx(c, 6, r * 0.85f, 30.f, 2.f, col);
			break;
		case StatusId::Ambush:
			DrawEllipse(static_cast<int>(c.x), static_cast<int>(c.y), r * 0.9f, r * 0.5f, col);
			DrawCircleV(c, r * 0.35f, Rgb(0x1A1024));
			break;
		case StatusId::Evade:
			for (int i = 0; i < 3; ++i) DrawCircleV(Vector2{ c.x - r * 0.6f + i * r * 0.6f, c.y }, r * 0.35f, util::WithA(col, 0.35f + i * 0.3f));
			break;
		case StatusId::Marked:
			DrawLineEx(Vector2{ c.x - r * 0.7f, c.y - r * 0.7f }, Vector2{ c.x + r * 0.7f, c.y + r * 0.7f }, 2.5f, col);
			DrawLineEx(Vector2{ c.x + r * 0.7f, c.y - r * 0.7f }, Vector2{ c.x - r * 0.7f, c.y + r * 0.7f }, 2.5f, col);
			break;
		default: DrawCircleV(c, r * 0.5f, col); break;
		}
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

	// 입력 판넬(왼쪽) + 직업 고르기(오른쪽)
	const Rectangle p{ W * 0.5f - 420.f, H * 0.34f, 400.f, 318.f };
	{
		const Rectangle cp{ W * 0.5f + 20.f, H * 0.34f, 420.f, 318.f };
		ui::Panel(cp);
		ui::TextShadow("직업 선택", Vector2{ cp.x + cp.width * 0.5f, cp.y + 16 }, 20.f, ui::theme::kTitle, Align::Center);
		ui::Divider(cp.x + 20, cp.x + cp.width - 20, cp.y + 46);
		DrawClassSelect(Rectangle{ cp.x + 14.f, cp.y + 56.f, cp.width - 28.f, cp.height - 70.f });
	}
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
		ui::Text("처음 쓰는 이름이면 고른 직업으로 새 캐릭터가 만들어집니다.", Vector2{ p.x + p.width * 0.5f, y }, 14.f, ui::theme::kTextDim, Align::Center);

	ui::Text("WASD 이동 · Space 공격 · 1~5 기술 · Tab 대상 · F 대화/줍기 · Q E R T 물약 · C 능력치 · I 소지품 · F1 도움말", Vector2{ W * 0.5f, H - 34 }, 14.f, Color{ 200, 190, 170, 200 }, Align::Center, true);
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
	const HudLayout L = Layout(W, H);

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

	// 아래 가운데 판넬: HP/MP · 기술 · 소모품
	ui::Panel(L.panel);
	{
		const float pulse = (m_maxhp > 0 && m_hp * 4 < m_maxhp) ? 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 8.f) : 0.f;
		ui::Bar(L.hp, m_hpShown, m_hpLag, util::Mix(Rgb(0xD8302A), Rgb(0xFF6A5A), pulse), Color{ 40, 12, 12, 255 },
			Fmt("HP  %d / %d", m_hp, m_maxhp), 14.f);
		const float mpFlash = static_cast<float>(std::max(0.0, 0.5 - (m_now - m_mpFlashAt)) * 2.0);
		ui::Bar(L.mp, m_mpShown, m_mpShown, util::Mix(Rgb(0x2A62D8), Rgb(0x9AC8FF), mpFlash), Color{ 12, 16, 40, 255 },
			Fmt("MP  %d / %d", m_mp, m_maxmp), 12.f);
		if (mpFlash > 0.f)
			DrawRectangleLinesEx(Rectangle{ L.mp.x - 2, L.mp.y - 2, L.mp.width + 4, L.mp.height + 4 }, 2.f, util::WithA(Rgb(0x9AC8FF), mpFlash));
		// 주요 능력치 한 줄
		const bool magic = m_classId == ClassId::RuneCaster;
		ui::Text(Fmt("%s %d  방어 %d  치명 %.0f%%", magic ? "마공" : "공격", magic ? m_stats.magicAttack : m_stats.attack, m_stats.defense, m_stats.critChance / 10.f),
			Vector2{ L.hp.x + 2.f, L.mp.y + 19.f }, 13.f, Rgb(0xE8D0A0));
	}

	// 왼쪽 위 캐릭터 정보(직업 문장 + 레벨)
	{
		const Rectangle r{ 12.f, 12.f, 262.f, 92.f };
		ui::Panel(r, 0.92f);
		const Vector2 ec{ r.x + 44.f, r.y + 46.f };
		ClassEmblem(ec, 30.f, m_classId, m_time);
		DrawRing(ec, 31.f, 34.f, -90.f, -90.f + 360.f * m_expShown, 48, Rgb(0xC08AFF));
		ui::Text(std::to_string(m_level), Vector2{ ec.x + 22.f, ec.y + 12.f }, 17.f, WHITE, Align::Center, true);
		Entity* me = Self();
		const Color nc = me ? NameColor(*me) : WHITE;
		ui::TextShadow(m_playerName, Vector2{ r.x + 86, r.y + 10 }, 19.f, nc);
		ui::Text(Fmt("Lv.%d  %s", m_level, ClassTable::Get(m_classId).name), Vector2{ r.x + 86, r.y + 36 }, 15.f, util::Shade(ClassColor(m_classId), 1.2f));
		const Color ac = m_alignment < 0 ? Rgb(0xFF6A5A) : (m_alignment >= AlignmentRule::kLawfulLine ? Rgb(0x7AB8FF) : ui::theme::kTextDim);
		ui::Text(Fmt("%s %+d", AlignmentLabel(m_alignment), m_alignment), Vector2{ r.x + r.width - 12, r.y + 38 }, 13.f, ac, Align::Right);
		CoinIcon(Vector2{ r.x + 94, r.y + 72 }, 7.f);
		ui::Text(texts::Number(m_inv.adena), Vector2{ r.x + 108, r.y + 63 }, 16.f, Rgb(0xFFD84A));
		if (m_pkCount > 0)
			ui::Text(Fmt("PK %d", m_pkCount), Vector2{ r.x + r.width - 12, r.y + 64 }, 13.f, Rgb(0xFF6A5A), Align::Right);
		// 남은 능력치 포인트 — 눌러서 캐릭터 창
		if (m_freePoints > 0)
		{
			const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 5.f);
			const Rectangle b{ r.x + r.width - 58.f, r.y + 8.f, 50.f, 22.f };
			DrawRectangleRounded(b, 0.5f, 8, util::Mix(Rgb(0x6A4A10), Rgb(0xC8962A), pulse));
			DrawRectangleRoundedLinesEx(b, 0.5f, 8, 1.5f, Rgb(0xFFE08A));
			ui::Text(Fmt("+%d", m_freePoints), Vector2{ b.x + b.width * 0.5f, b.y + 3.f }, 14.f, WHITE, Align::Center, true);
			ui::Block(b);
			if (ui::Hovered(b)) ui::Tooltip({ { "능력치 포인트", ui::theme::kGold, 16.f }, { "C를 누르거나 눌러서 캐릭터 창에서 찍으세요.", ui::theme::kText, 14.f } });
			if (ui::Clicked(b)) { m_charOpen = true; sfx::Play(sfx::Id::Open, 0.6f); }
		}
	}
	DrawStatusIcons(12.f, 112.f);

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
	const HudLayout L = Layout(W, H);
	static const char* keys[kQuickSlots] = { "Q", "E", "R", "T" };
	for (int i = 0; i < kQuickSlots; ++i)
	{
		const Rectangle r{ L.quickX + i * L.quickStep, L.quickY, L.quickS, L.quickS };
		const QuickSlot& q = m_quick[i];
		const bool hover = ui::Hovered(r);
		ui::SlotFrame(r, hover, false);
		ui::Block(r);
		double cdEnd = 0.0, cdLen = 1.0;
		int count = 0;
		if (q.kind == 1 && q.itemId)
		{
			count = m_inv.CountOf(q.itemId);
			icons::Draw(q.itemId, Rectangle{ r.x + 3, r.y + 3, r.width - 6, r.height - 6 }, count > 0 ? WHITE : Color{ 255, 255, 255, 90 });
			const ItemTableRow* row = ItemTableData::Find(q.itemId);
			if (row && row->effect == ItemEffect::eHeal) { cdEnd = m_lastPotionAt + CombatRule::kPotionCooldownMs / 1000.0; cdLen = CombatRule::kPotionCooldownMs / 1000.0; }
			else if (row && row->type == ItemType::eConsumable) { cdEnd = m_lastScrollAt + CombatRule::kScrollCooldownMs / 1000.0; cdLen = CombatRule::kScrollCooldownMs / 1000.0; }
			if (hover) DrawItemTooltip(q.itemId, 0, count, false, false);
		}
		if (cdEnd > m_now)
		{
			const float frac = static_cast<float>((cdEnd - m_now) / cdLen);
			BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width), static_cast<int>(r.height));
			DrawCircleSector(Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f }, r.width * 0.75f, -90.f, -90.f + 360.f * util::Clamp01(frac), 32, Color{ 0, 0, 0, 150 });
			EndScissorMode();
			ui::Text(Fmt("%.1f", cdEnd - m_now), Vector2{ r.x + r.width * 0.5f, r.y + 11 }, 13.f, WHITE, Align::Center, true);
		}
		if (count > 0)
			ui::Text(std::to_string(count), Vector2{ r.x + r.width - 3, r.y + r.height - 16 }, 12.f, WHITE, Align::Right, true);
		ui::Text(keys[i], Vector2{ r.x + 3, r.y + 1 }, 12.f, Rgb(0xE8D8A0), Align::Left, true);

		// 입력: 클릭 사용 / 오른쪽 클릭 비우기 / 끌어서 옮기기(소지품의 소모품을 끌어다 등록)
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
			SaveQuickSlots();
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
			else if (d.kind == 2 && d.index != i && d.index >= 0 && d.index < kQuickSlots)
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
	for (const auto& [nid, v] : m_npcView)
	{
		if (!v.row || WorldData::IsAnimal(v.row->kind)) continue;
		const Vector2 p = toMap(v.pos.x - m_predX, v.pos.y - m_predY);
		const bool service = v.row->kind == WorldData::NpcKind::eMerchant || v.row->kind == WorldData::NpcKind::eWeaponSmith
			|| v.row->kind == WorldData::NpcKind::eTeleporter || v.row->kind == WorldData::NpcKind::ePriest || v.row->kind == WorldData::NpcKind::eInnkeeper;
		DrawCircleV(p, service ? 3.2f : 2.4f, Color{ 0, 0, 0, 160 });
		DrawCircleV(p, service ? 2.4f : 1.7f, service ? Rgb(0xFFE08A) : Rgb(0x9AF06A));
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
	const float w = std::max(220.f, std::min(440.f, W * 0.5f - 390.f - 24.f));
	const Rectangle log{ 12.f, H - 292.f, w, 206.f };
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
		const Rectangle in{ 12.f, H - 82.f, w, 32.f };
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
// 아이템 툴팁 · 소지품 · 장비창은 GameItems.cpp

// ─────────────────────────────────────────────────────────────────────────────
// NPC · 상점 · 텔레포트
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawNpcDialog()
{
	const auto* npc = WorldData::FindNpc(m_dialogNpc);
	if (!npc) { m_dialogNpc = 0; return; }
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const Rectangle r{ W * 0.5f - 300.f, H - 350.f, 600.f, 226.f };
	bool open = true;
	ui::Window(r, npc->name, &open);
	if (!open) { CloseNpcWindows(); return; }

	// 초상화(NPC를 크게 다시 그린다 — 말하는 동안 살짝 움직인다)
	const Rectangle pr{ r.x + 16, r.y + 46, 120, 164 };
	DrawRectangleRounded(pr, 0.06f, 6, Color{ 30, 26, 34, 255 });
	DrawRectangleGradientV(static_cast<int>(pr.x), static_cast<int>(pr.y), static_cast<int>(pr.width), static_cast<int>(pr.height), Color{ 70, 60, 50, 120 }, Color{ 10, 8, 12, 0 });
	BeginScissorMode(static_cast<int>(pr.x), static_cast<int>(pr.y), static_cast<int>(pr.width), static_cast<int>(pr.height));
	{
		actors::Pose pose;
		const bool small = WorldData::IsAnimal(npc->kind) || npc->kind == WorldData::NpcKind::eChild;
		pose.base = Vector2{ pr.x + pr.width * 0.5f, pr.y + pr.height + (small ? -8.f : 18.f) };
		pose.scale = small ? 2.2f : 1.7f;
		pose.time = m_time;
		pose.faceRight = true;
		pose.seed = npc->id;
		gfx::BeginWorld(0.f, pose.scale);
		gfx::ResetMod();
		gfx::SetDepthRaw(-0.5f);
		actors::DrawNpc(pose, *npc);
		gfx::Flush();
	}
	EndScissorMode();
	DrawRectangleRoundedLinesEx(pr, 0.06f, 6, 1.5f, ui::theme::kBorder);

	// 대사 — 한 글자씩 나타난다(누르면 바로 다)
	const NpcTalk::Lines* lines = NpcTalk::Find(npc->id);
	const int talkCount = NpcTalk::TalkCount(npc->id);
	const char* text = npc->greeting;
	if (lines && m_dialogLine >= 0 && m_dialogLine < talkCount)
		text = lines->talk[m_dialogLine];
	std::vector<int> cps;
	ui::DecodeUtf8(text, cps);
	const int shown = std::min<int>(static_cast<int>(cps.size()), static_cast<int>((m_now - m_dialogTextAt) * 42.0));
	std::string visible;
	for (int i = 0; i < shown; ++i) ui::AppendUtf8(visible, cps[i]);
	const Rectangle tr{ r.x + 152, r.y + 50, r.width - 170, 100 };
	ui::TextWrapped(visible, tr, 17.f, ui::theme::kText, 6.f);
	if (shown < static_cast<int>(cps.size()) && ui::Clicked(tr))
		m_dialogTextAt = m_now - 60.0;
	if (shown >= static_cast<int>(cps.size()))
	{
		// 다 읽었으면 작은 화살표가 깜빡인다
		const float blink = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 6.f);
		DrawTriangle(Vector2{ r.x + r.width - 30, r.y + 142 }, Vector2{ r.x + r.width - 24, r.y + 150 }, Vector2{ r.x + r.width - 18, r.y + 142 }, util::WithA(ui::theme::kGold, blink));
	}

	float bx = r.x + 152;
	const float by = r.y + r.height - 56;
	auto button = [&](float w, const char* label, ButtonKind kind = ButtonKind::Normal)
	{
		const bool clicked = ui::Button(Rectangle{ bx, by, w, 40 }, label, kind);
		bx += w + 8;
		return clicked;
	};
	if (talkCount > 0 && button(92, m_dialogLine < 0 ? "이야기" : "더 듣기"))
	{
		m_dialogLine = (m_dialogLine + 1) % talkCount;
		m_dialogTextAt = m_now;
		if (m_now - m_lastVoiceAt > 0.3)
		{
			m_lastVoiceAt = m_now;
			sfx::PlayPitched(sfx::Id::Voice, 0.5f, npc->kind == WorldData::NpcKind::eChild ? 1.55f : (npc->kind == WorldData::NpcKind::eElder || npc->kind == WorldData::NpcKind::eVillager ? 0.8f : 1.05f));
		}
	}
	switch (npc->kind)
	{
	case WorldData::NpcKind::eMerchant:
	case WorldData::NpcKind::eWeaponSmith:
		if (button(92, "구매", ButtonKind::Primary)) { m_shopMode = ShopMode::Buy; m_buyItem = 0; m_buyCount = 1; sfx::Play(sfx::Id::Open, 0.6f); }
		if (button(92, "판매")) { m_shopMode = ShopMode::Sell; m_sellSlot = -1; m_invOpen = true; sfx::Play(sfx::Id::Open, 0.6f); }
		break;
	case WorldData::NpcKind::eTeleporter:
		if (button(120, "순간이동", ButtonKind::Primary)) { m_shopMode = ShopMode::Teleport; m_teleportSel = -1; sfx::Play(sfx::Id::Open, 0.6f); }
		break;
	case WorldData::NpcKind::ePriest:
	{
		const double remain = m_npcServiceReadyAt[static_cast<int>(WorldData::NpcService::Heal)] - m_now;
		const std::string label = remain > 0.0 ? Fmt("기도 (%.0f초)", remain) : std::string("치유의 기도");
		if (button(130, label.c_str(), ButtonKind::Primary)) RequestNpcService(npc->id, WorldData::NpcService::Heal);
		break;
	}
	case WorldData::NpcKind::eInnkeeper:
	{
		const double remain = m_npcServiceReadyAt[static_cast<int>(WorldData::NpcService::Soup)] - m_now;
		const std::string label = remain > 0.0 ? Fmt("수프 (%.0f초)", remain) : std::string("수프 (30 아데나)");
		if (button(140, label.c_str(), ButtonKind::Primary)) RequestNpcService(npc->id, WorldData::NpcService::Soup);
		break;
	}
	default:
		break;
	}
	if (button(80, "닫기"))
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

	// 물건이 많으면 두 줄로 — 한 칸에 아이콘 · 이름 · 한 줄 설명 · 가격
	const bool twoCols = items.size() > 7;
	const float cardW = twoCols ? (r.width - 24.f - 8.f) * 0.5f : r.width - 24.f;
	int idx = 0;
	for (uint16_t id : items)
	{
		const ItemTableRow* row = ItemTableData::Find(id);
		if (!row) continue;
		const int col = twoCols ? idx % 2 : 0, line = twoCols ? idx / 2 : idx;
		++idx;
		const Rectangle it{ r.x + 12 + col * (cardW + 8.f), r.y + 46 + line * 47.f, cardW, 44 };
		const bool hover = ui::Hovered(it), sel = m_buyItem == id;
		DrawRectangleRounded(it, 0.12f, 6, sel ? Color{ 90, 70, 40, 220 } : (hover ? Color{ 60, 50, 44, 200 } : Color{ 30, 26, 30, 200 }));
		if (sel) DrawRectangleRoundedLinesEx(it, 0.12f, 6, 1.5f, ui::theme::kBorderHi);
		ui::SlotFrame(Rectangle{ it.x + 6, it.y + 4, 36, 36 }, false, false);
		icons::Draw(id, Rectangle{ it.x + 8, it.y + 6, 32, 32 });
		ui::TextShadow(row->displayName, Vector2{ it.x + 50, it.y + 4 }, twoCols ? 14.f : 16.f, icons::ItemNameColor(id));
		std::string desc;
		if (row->type == ItemType::eEquipment)
		{
			const EquipSlot part = ItemTableData::SlotOf(*row);
			if (part == EquipSlot::Weapon) desc = Fmt(row->weaponKind == WeaponKind::Staff ? "마공 +%d" : "공격 +%d", row->attackBonus);
			else if (row->armorBonus > 0) desc = Fmt("%s · 방어 +%d", texts::SlotName(part), row->armorBonus);
			else desc = Fmt("%s · 마방 +%d", texts::SlotName(part), row->magicArmorBonus);
			if (row->classMask != 0)
				for (int c = 0; c < static_cast<int>(ClassId::Count); ++c)
					if (row->classMask & (1u << c)) { desc += std::string(" · ") + ClassTable::Get(static_cast<ClassId>(c)).name; break; }
		}
		else if (row->effect == ItemEffect::eHeal) desc = Fmt("HP %d 회복", row->healAmount);
		else if (row->effect == ItemEffect::eHaste) desc = Fmt("%d초 가속", row->effectValue);
		else if (row->effect == ItemEffect::eReturnTown) desc = "마을로 귀환";
		else if (row->effect == ItemEffect::eTeleportRandom) desc = "지역 내 순간이동";
		else if (row->effect == ItemEffect::eEnchantWeapon) desc = "무기 강화";
		ui::Text(desc, Vector2{ it.x + 50, it.y + 25 }, twoCols ? 12.f : 13.f, ui::theme::kTextDim);
		const bool affordable = m_inv.adena >= row->buyPrice;
		if (!twoCols) CoinIcon(Vector2{ it.x + it.width - 110, it.y + 22 }, 6.f);
		ui::Text(texts::Number(row->buyPrice), Vector2{ it.x + it.width - 8, it.y + (twoCols ? 4.f : 13.f) }, twoCols ? 14.f : 16.f, affordable ? Rgb(0xFFD84A) : ui::theme::kBad, Align::Right);
		ui::Block(it);
		if (ui::Clicked(it)) { m_buyItem = id; m_buyCount = 1; sfx::Play(sfx::Id::Click, 0.6f); }
		if (hover) DrawItemTooltip(id, 0, 1, false, false);
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
	if (ui::Button(Rectangle{ r.x + r.width - 236, fy + 48, 84, 42 }, "뒤로"))
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
		// 희귀도가 높을수록 비싸게 산다(서버와 같은 식 — ItemRules::SellPrice)
		const uint32_t unit = row ? ItemRules::SellPrice(*row, s.extra, s.enchant) : 0;
		const Rarity rar = SlotRarity(s);
		ui::SlotFrame(Rectangle{ r.x + 20, r.y + 52, 64, 64 }, false, true, rar != Rarity::Common ? icons::RarityColor(rar) : ui::theme::kBorder);
		icons::Draw(s.itemId, Rectangle{ r.x + 24, r.y + 56, 56, 56 });
		ui::TextShadow(texts::ItemNameFull(s.itemId, s.enchant, s.extra), Vector2{ r.x + 98, r.y + 58 }, 18.f, icons::ItemNameColor(s.itemId, s.extra));
		ui::Text("개당 " + texts::Number(unit) + " 아데나" + (row && row->type == ItemType::eEquipment && rar > Rarity::Common
			? Fmt("  (%s ×%.1f)", ItemRules::RarityName(rar), ItemRules::kSellPercent[ItemRules::Idx(rar)] / 100.f) : std::string()),
			Vector2{ r.x + 98, r.y + 86 }, 15.f, Rgb(0xFFD84A));
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
	ui::TextShadow(texts::ItemNameFull(s.itemId, s.enchant, s.extra), Vector2{ r.x + 80, r.y + 50 }, 17.f, icons::ItemNameColor(s.itemId, s.extra));
	if (s.equipped || SlotRarity(s) >= Rarity::Rare || s.enchant > 0)
		ui::Text(s.equipped ? "착용 중인 장비입니다. 정말 버릴까요?" : std::string(ItemRules::RarityName(SlotRarity(s))) + " 등급 장비입니다. 정말 버릴까요?",
			Vector2{ r.x + 80, r.y + 72 }, 13.f, Rgb(0xFF9A8A));
	m_discard.count = std::clamp(m_discard.count, 1, static_cast<int>(s.count));
	if (ui::Button(Rectangle{ r.x + 80, r.y + 96, 34, 30 }, "-")) m_discard.count = std::max(1, m_discard.count - 1);
	ui::Text(std::to_string(m_discard.count) + " / " + std::to_string(s.count), Vector2{ r.x + 160, r.y + 101 }, 16.f, WHITE, Align::Center);
	if (ui::Button(Rectangle{ r.x + 206, r.y + 96, 34, 30 }, "+")) m_discard.count = std::min(static_cast<int>(s.count), m_discard.count + 1);
	if (ui::Button(Rectangle{ r.x + 246, r.y + 96, 60, 30 }, "전부")) m_discard.count = s.count;
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
	const Rectangle r{ W * 0.5f - 330.f, H * 0.5f - 250.f, 660.f, 500.f };
	ui::Window(r, "도움말", &m_helpOpen);
	const std::pair<const char*, const char*> rows[] = {
		{ "W A S D / 방향키", "8방향 이동 (누른 쪽이 언제나 우선)" },
		{ "Space", "기본 공격 — 대상이 없으면 가까운 적. 누르고 있으면 다음 적으로" },
		{ "1 ~ 5", "직업 기술 (사거리 밖이면 다가가서 쓴다 · 쿨다운 직전에 눌러 두면 풀리는 순간)" },
		{ "Tab / Esc", "가까운 순서로 대상 바꾸기 / 대상 해제" },
		{ "Ctrl + 공격·기술", "다른 모험가 공격(PK) — 성향치와 보라돌이 규칙 주의" },
		{ "F", "가까운 NPC와 대화 · 발밑 아이템 줍기" },
		{ "Q E R T / F5~F8", "물약·주문서 칸 (소지품에서 끌어다 등록)" },
		{ "Z", "주변 아이템 줍기" },
		{ "C / I / M", "캐릭터(능력치) / 소지품 / 월드 지도" },
		{ "마우스", "창·상점·대화 · NPC/아이템 클릭 · 바닥 클릭 이동 (전투는 키보드)" },
		{ "Enter", "채팅 (&로 시작하면 전체 채팅)" },
		{ "Alt / 휠", "이름 모두 보기 / 확대·축소" },
	};
	float y = r.y + 50;
	for (const auto& [k, v] : rows)
	{
		ui::TextShadow(k, Vector2{ r.x + 24, y }, 15.f, ui::theme::kTitle);
		ui::Text(v, Vector2{ r.x + 184, y }, 14.f, ui::theme::kText);
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

// ─────────────────────────────────────────────────────────────────────────────
// 기술 막대 — 키보드(Space·1~5)로 쓴다. 마우스는 설명·범위 보기만(전투는 키보드).
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawSkillTooltip(const SkillRow& row)
{
	std::vector<ui::TooltipLine> lines;
	const Color cc = Rgb(row.color);
	lines.push_back({ row.name, util::Shade(cc, 1.15f), 18.f });
	lines.push_back({ row.slot == 0 ? "기본 공격 · Space (대상에게 계속)" : Fmt("기술 %d · 단축키 %d", row.slot, row.slot), ui::theme::kTextDim, 13.f });
	const bool magic = row.kind == DamageKind::Magic;
	const int power = magic ? m_stats.magicAttack : m_stats.attack;
	std::string cost;
	if (row.mp > 0) cost += Fmt("MP %d · ", row.mp);
	if (row.slot == 0) cost += Fmt("간격 %.2f초", SkillTable::BasicIntervalMs(m_classId, m_stats.attackSpeed) / 1000.f);
	else cost += Fmt("재사용 %.1f초", SkillTable::CooldownMs(row, m_stats.cooldownReduction) / 1000.f);
	if (row.range > 0) cost += Fmt(" · 사거리 %d칸", row.range);
	if (row.castMs > 0) cost += Fmt(" · 예고 %.1f초", row.castMs / 1000.f);
	lines.push_back({ cost, Rgb(0x9AC8FF), 14.f });
	const GearExtras gx = MyGearExtras();
	const int boost = row.id < sizeof(gx.skillBoost) ? gx.skillBoost[row.id] : 0;
	if (row.coeff > 0)
	{
		const int each = power * row.coeff / 100 * (100 + boost) / 100;
		lines.push_back({ row.hits > 1 ? Fmt("피해 약 %d × %d회 (%s)", each, row.hits, magic ? "마법" : "물리") : Fmt("피해 약 %d (%s)", each, magic ? "마법" : "물리"), Rgb(0xFFD8A0), 14.f });
	}
	if (boost > 0)
		lines.push_back({ Fmt("◈ 장비 기술 강화 +%d%%", boost), Rgb(0xFFD870), 14.f });
	lines.push_back({ row.desc, ui::theme::kText, 14.f });
	if (row.combo && row.combo[0])
		lines.push_back({ std::string("연계: ") + row.combo, Rgb(0xFFD25A), 14.f });
	if (m_level < row.unlockLevel)
		lines.push_back({ Fmt("Lv.%d에 배울 수 있습니다", row.unlockLevel), Rgb(0xFF7A6A), 14.f });
	ui::Tooltip(std::move(lines));
}

void Game::DrawSkillBar()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const HudLayout L = Layout(W, H);
	m_previewSkill = -1;
	for (int slot = 0; slot < 6; ++slot)
	{
		const SkillRow* row = SkillInSlot(slot);
		if (!row) continue;
		const float S = L.skillS;
		// 누른 순간 살짝 눌렸다가 튀어나온다
		const float press = static_cast<float>(std::max(0.0, 0.14 - (m_now - m_castSentAt[slot])) / 0.14);
		const float sc = 1.f - 0.1f * press;
		const Rectangle base{ L.skillX + slot * L.skillStep, L.skillY, S, S };
		const Rectangle r{ base.x + S * (1.f - sc) * 0.5f, base.y + S * (1.f - sc) * 0.5f, S * sc, S * sc };
		const bool hover = ui::Hovered(base);
		const bool locked = m_level < row->unlockLevel;
		const bool noMana = !locked && m_mp < static_cast<int>(row->mp);
		const double remain = m_skillReady[slot] - m_now;
		const bool cooling = remain > 0.02 && !locked;
		ui::Block(base);

		const Color cc = Rgb(row->color);
		DrawRectangleRounded(Rectangle{ r.x - 2, r.y - 2, r.width + 4, r.height + 4 }, 0.16f, 6, Color{ 10, 8, 12, 240 });
		skillfx::DrawSkillIcon(row->id, Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f }, r.width - 4.f, m_time, locked);
		// 쿨다운(시계 방향으로 걷히는 그늘)
		if (cooling)
		{
			const float frac = static_cast<float>(remain / std::max(0.05, m_skillCdTotal[slot]));
			BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width), static_cast<int>(r.height));
			DrawCircleSector(Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f }, r.width * 0.75f, -90.f, -90.f + 360.f * util::Clamp01(frac), 36, Color{ 0, 0, 0, 165 });
			EndScissorMode();
			if (slot != 0 && remain > 0.5)
				ui::Text(remain >= 10.0 ? Fmt("%.0f", remain) : Fmt("%.1f", remain), Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f - 10.f }, 17.f, WHITE, Align::Center, true);
		}
		if (noMana)
			DrawRectangleRounded(r, 0.12f, 6, Color{ 20, 40, 140, 120 });
		// 방금 쿨다운이 끝났다: 반짝 + 퍼지는 테
		const double flash = m_now - m_skillFlash[slot];
		if (flash < 0.4)
		{
			const float u = static_cast<float>(flash / 0.4);
			BeginBlendMode(BLEND_ADDITIVE);
			DrawRectangleRounded(r, 0.12f, 6, util::WithA(util::Shade(cc, 1.4f), 0.55f * (1.f - u)));
			EndBlendMode();
			DrawRectangleRoundedLinesEx(Rectangle{ r.x - 6 * u, r.y - 6 * u, r.width + 12 * u, r.height + 12 * u }, 0.2f, 6, 2.f, util::WithA(util::Shade(cc, 1.3f), 1.f - u));
		}
		// 예약·선입력 중이면 테두리가 맥동
		const bool queued = (m_queuedSkill == slot) || (m_bufferSlot == slot);
		const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 12.f);
		DrawRectangleRoundedLinesEx(r, 0.12f, 6, queued ? 2.5f : 1.5f,
			queued ? util::Mix(Rgb(0xFFD070), WHITE, pulse) : (hover ? ui::theme::kBorderHi : util::Shade(cc, locked ? 0.4f : 0.8f)));
		// 단축키 · MP · 잠금
		ui::Text(slot == 0 ? "Spc" : std::to_string(slot), Vector2{ r.x + 3, r.y + 1 }, 12.f, Rgb(0xFFF0C8), Align::Left, true);
		if (row->mp > 0 && !locked)
			ui::Text(std::to_string(row->mp), Vector2{ r.x + r.width - 3, r.y + r.height - 15 }, 11.f, noMana ? Rgb(0xFF8A8A) : Rgb(0x9AC8FF), Align::Right, true);
		if (locked)
			ui::Text(Fmt("Lv.%d", row->unlockLevel), Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f - 8.f }, 13.f, Rgb(0xE8D0B0), Align::Center, true);
		if (hover && !ui::Dragging())
		{
			DrawSkillTooltip(*row);
			m_previewSkill = slot;   // 월드에 사거리/범위를 보여 준다
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 상태 아이콘(왼쪽 위 캐릭터 정보 아래)
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawStatusIcons(float x, float y)
{
	Entity* me = Self();
	if (!me) return;
	const float nowf = static_cast<float>(m_now);
	const float S = 28.f;
	auto icon = [&](Color col, float remain, float total, int stacks, const std::string& title, const char* help, auto&& glyph)
	{
		const Rectangle b{ x, y, S, S };
		DrawRectangleRounded(b, 0.25f, 6, util::Shade(col, 0.22f));
		glyph(Vector2{ b.x + S * 0.5f, b.y + S * 0.5f }, S * 0.32f, col);
		if (total > 0.f && remain > 0.f)
		{
			const float frac = util::Clamp01(1.f - remain / total);
			BeginScissorMode(static_cast<int>(b.x), static_cast<int>(b.y), static_cast<int>(b.width), static_cast<int>(b.height));
			DrawCircleSector(Vector2{ b.x + S * 0.5f, b.y + S * 0.5f }, S * 0.75f, -90.f, -90.f + 360.f * frac, 24, Color{ 0, 0, 0, 120 });
			EndScissorMode();
		}
		// 곧 끝나면 깜빡인다
		const bool ending = remain > 0.f && remain < 2.f;
		const float blink = ending ? 0.5f + 0.5f * std::sin(nowf * 14.f) : 1.f;
		DrawRectangleRoundedLinesEx(b, 0.25f, 6, 1.5f, util::WithA(col, 0.4f + 0.6f * blink));
		if (stacks > 1) ui::Text(std::to_string(stacks), Vector2{ b.x + S - 2, b.y + S - 14 }, 12.f, WHITE, Align::Right, true);
		if (remain > 0.f) ui::Text(Fmt("%.0f", std::ceil(remain)), Vector2{ b.x + S * 0.5f, b.y + S + 1 }, 11.f, ui::theme::kTextDim, Align::Center, true);
		if (ui::Hovered(b))
			ui::Tooltip({ { title, col, 16.f }, { help, ui::theme::kText, 14.f } });
		x += S + 6.f;
	};
	for (const auto& st : me->statuses)
	{
		if (st.stacks == 0 || nowf >= st.until) continue;
		const auto id = static_cast<StatusId>(st.id);
		const StatusRow& row = StatusTable::Get(id);
		const Color col = Rgb(row.color);
		icon(col, st.until - nowf, 0.f, st.stacks, row.debuff ? std::string(row.name) + " (해로움)" : std::string(row.name), StatusHelp(id),
			[&](Vector2 c, float rr, Color cl) { StatusGlyph(id, c, rr, cl, nowf); });
	}
	if (m_hasteUntil > m_now)
		icon(Rgb(0x7AFF9A), static_cast<float>(m_hasteUntil - m_now), 0.f, 0, "가속", "이동 속도 증가",
			[&](Vector2 c, float rr, Color cl)
			{
				DrawTriangle(Vector2{ c.x - rr, c.y - rr }, Vector2{ c.x - rr, c.y + rr }, Vector2{ c.x, c.y }, cl);
				DrawTriangle(Vector2{ c.x, c.y - rr }, Vector2{ c.x, c.y + rr }, Vector2{ c.x + rr, c.y }, cl);
			});
	if (m_flags & SUBJECT_EX_PURPLE)
		icon(Rgb(0xD890FF), 0.f, 0.f, 0, "보라돌이", "먼저 공격한 상태입니다. 상대가 반격해도 벌칙이 없습니다.",
			[&](Vector2 c, float rr, Color cl) { ui::Text("!", Vector2{ c.x, c.y - rr * 1.1f }, rr * 2.2f, cl, Align::Center); });
}

// ─────────────────────────────────────────────────────────────────────────────
// 연속 타격 수 + 연계 이름(오른쪽)
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawComboCounter()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const double age = m_now - m_comboLastAt;
	const float cx = W - 150.f, cy = H * 0.42f;
	if (m_comboHits >= 2 && age < 3.0)
	{
		const float a = age > 2.2 ? static_cast<float>(1.0 - (age - 2.2) / 0.8) : 1.f;
		const float punch = age < 0.12 ? 1.f + 0.4f * static_cast<float>(1.0 - age / 0.12) : 1.f;
		const float heat = util::Clamp01(m_comboHits / 40.f);
		const Color col = util::Mix(Rgb(0xFFE8B0), Rgb(0xFF7A3A), heat);
		ui::TextStyle st;
		st.size = (38.f + 10.f * heat) * punch;
		st.color = util::MulA(col, a);
		st.outline = 3.f;
		st.outlineColor = util::MulA(Color{ 40, 14, 0, 230 }, a);
		st.shadow = 3.f;
		ui::Fonts().DrawCentered(std::to_string(m_comboHits), Vector2{ cx, cy }, st);
		st.size = 16.f;
		st.color = util::MulA(Rgb(0xFFF0D0), a);
		st.outline = 2.f;
		ui::Fonts().DrawCentered("HIT", Vector2{ cx, cy + 30.f }, st);
		// 남은 시간 막대(3초 안에 다시 때리면 이어진다)
		const float left = util::Clamp01(1.f - static_cast<float>(age / 3.0));
		DrawRectangle(static_cast<int>(cx - 40), static_cast<int>(cy + 44), 80, 3, util::MulA(Color{ 0, 0, 0, 160 }, a));
		DrawRectangle(static_cast<int>(cx - 40), static_cast<int>(cy + 44), static_cast<int>(80 * left), 3, util::MulA(col, a));
	}
	const double nameAge = m_now - m_comboNameAt;
	if (!m_comboName.empty() && nameAge < 1.6)
	{
		const float u = static_cast<float>(nameAge / 1.6);
		const float a = u > 0.7f ? 1.f - (u - 0.7f) / 0.3f : 1.f;
		const float pop = nameAge < 0.15 ? util::EaseOutBack(static_cast<float>(nameAge / 0.15), 3.f) : 1.f;
		ui::TextStyle st;
		st.size = 22.f * pop;
		st.color = util::MulA(Rgb(0xFFD25A), a);
		st.outline = 2.5f;
		st.outlineColor = util::MulA(Color{ 50, 20, 0, 230 }, a);
		ui::Fonts().DrawCentered("연계  " + m_comboName + "!", Vector2{ cx, cy + 70.f }, st);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 캐릭터 창(C) — 직업 · 능력치 분배 · 전투 능력치
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawCharacter()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const Rectangle r{ std::max(20.f, W * 0.5f - 470.f), std::max(12.f, H * 0.5f - 290.f), 600.f, std::min(H - 120.f, 560.f) };
	ui::Window(r, "캐릭터", &m_charOpen);
	const ClassRow& cls = ClassTable::Get(m_classId);
	const Color cc = ClassColor(m_classId);

	// 왼쪽: 직업 소개
	ClassEmblem(Vector2{ r.x + 50.f, r.y + 88.f }, 32.f, m_classId, m_time);
	ui::TextShadow(cls.name, Vector2{ r.x + 92.f, r.y + 60.f }, 22.f, util::Shade(cc, 1.2f));
	ui::Text(cls.role, Vector2{ r.x + 92.f, r.y + 90.f }, 14.f, ui::theme::kTextDim);
	ui::TextWrapped(cls.desc, Rectangle{ r.x + 20.f, r.y + 130.f, 260.f, 70.f }, 14.f, ui::theme::kText, 4.f);
	ui::Divider(r.x + 18.f, r.x + 282.f, r.y + 204.f);

	// 능력치 6개 + 분배
	const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
	ui::Text(m_freePoints > 0 ? Fmt("남은 포인트  %d", m_freePoints) : "레벨이 오르면 포인트를 얻습니다", Vector2{ r.x + 20.f, r.y + 214.f }, 15.f,
		m_freePoints > 0 ? ui::theme::kGold : ui::theme::kTextDim);
	for (int i = 0; i < 6; ++i)
	{
		const float y = r.y + 244.f + i * 34.f;
		const int base = m_baseStats.v[i], alloc = m_allocStats.v[i], gear = m_gearStats.v[i];
		const Rectangle row{ r.x + 16.f, y - 4.f, 268.f, 30.f };
		const bool hover = ui::Hovered(row);
		if (hover) DrawRectangleRounded(row, 0.2f, 6, Color{ 255, 230, 180, 18 });
		const bool main = IsMainStat(m_classId, i);
		ui::Text(StatName(i), Vector2{ r.x + 24.f, y }, 16.f, main ? util::Shade(cc, 1.2f) : ui::theme::kText);
		if (main) ui::Text("★", Vector2{ r.x + 10.f, y }, 12.f, util::Shade(cc, 1.2f));
		ui::Text(std::to_string(base + alloc + gear), Vector2{ r.x + 176.f, y }, 17.f, WHITE, Align::Right, true);
		if (alloc > 0 || gear != 0)
			ui::Text(Fmt("(%+d%s)", alloc + gear, gear != 0 ? " 장비" : ""), Vector2{ r.x + 184.f, y + 2.f }, 12.f, Rgb(0x9AE89A));
		if (m_freePoints > 0)
		{
			const Rectangle b{ r.x + 250.f, y - 2.f, 26.f, 24.f };
			if (ui::Button(b, "+", ButtonKind::Primary, true, 16.f))
			{
				m_net.SendStatAlloc(static_cast<uint8_t>(i), static_cast<uint8_t>(shift ? std::min<int>(5, m_freePoints) : 1));
				sfx::Play(sfx::Id::StatUp, 0.5f);
			}
		}
		if (hover)
			ui::Tooltip({ { StatName(i), util::Shade(cc, 1.2f), 16.f },
				{ StatHelp(i), ui::theme::kText, 14.f },
				{ Fmt("직업 %d + 분배 %d + 장비 %d", base, alloc, gear), ui::theme::kTextDim, 13.f },
				{ "Shift + 클릭: 5씩", ui::theme::kTextDim, 12.f } });
	}

	// 오른쪽: 전투 능력치
	const float x2 = r.x + 306.f;
	DrawLineEx(Vector2{ x2 - 12.f, r.y + 54.f }, Vector2{ x2 - 12.f, r.y + r.height - 18.f }, 1.f, Color{ 214, 176, 104, 60 });
	ui::TextShadow("전투 능력치", Vector2{ x2, r.y + 54.f }, 17.f, ui::theme::kTitle);
	struct Line { const char* name; std::string value; const char* help; };
	const DerivedStats& d = m_stats;
	const Line lines[] = {
		{ "최대 HP", std::to_string(d.maxHp), "VIT가 오를수록 커진다." },
		{ "최대 MP", std::to_string(d.maxMp), "WIS·INT가 오를수록 커진다." },
		{ "공격력", std::to_string(d.attack), "무기 + STR(기사)·DEX(사냥꾼)." },
		{ "마법 공격력", std::to_string(d.magicAttack), "INT·WIS와 지팡이." },
		{ "방어력", std::to_string(d.defense), "받는 물리 피해가 줄어든다." },
		{ "마법 방어력", std::to_string(d.magicDefense), "받는 마법 피해가 줄어든다." },
		{ "공격 속도", Fmt("%d%%", d.attackSpeed), "기본 공격 간격이 짧아진다(DEX)." },
		{ "이동 속도", Fmt("%d%%", d.moveSpeed), "걷는 속도(DEX 조금)." },
		{ "치명타 확률", Fmt("%.1f%%", d.critChance / 10.f), "LUK·DEX." },
		{ "치명타 피해", Fmt("%d%%", d.critDamage), "치명타가 나면 이만큼(LUK)." },
		{ "명중 / 회피", Fmt("%d / %d", d.accuracy, d.evasion), "회피는 몬스터 공격을 흘린다(최대 40%)." },
		{ "관통", std::to_string(d.penetration), "대상의 방어를 이만큼 무시한다." },
		{ "재사용 감소", Fmt("%d%%", d.cooldownReduction), "기술 쿨다운이 줄어든다(WIS, 최대 30%)." },
		{ "상태이상 저항", Fmt("%d%%", d.statusResist), "기절·빙결 등이 짧아진다(VIT·WIS)." },
		{ "회복(2.5초)", Fmt("HP %d · MP %d", d.hpRegen, d.mpRegen), "전투에서 벗어나면 세 배, 마을에선 네 배." },
	};
	float y = r.y + 84.f;
	for (const auto& ln : lines)
	{
		const Rectangle row{ x2 - 4.f, y - 3.f, r.x + r.width - x2 - 12.f, 24.f };
		const bool hover = ui::Hovered(row);
		if (hover) DrawRectangleRounded(row, 0.2f, 6, Color{ 255, 230, 180, 18 });
		ui::Text(ln.name, Vector2{ x2, y }, 14.f, ui::theme::kTextDim);
		ui::Text(ln.value, Vector2{ r.x + r.width - 20.f, y }, 15.f, ui::theme::kText, Align::Right);
		if (hover) ui::Tooltip({ { ln.name, ui::theme::kTitle, 15.f }, { ln.help, ui::theme::kText, 14.f } });
		y += 24.f;
	}

	// 왼쪽 아래: 장비 효과(능력치 표에 드러나지 않는 것 — 특수 효과 · 기술 강화 · 획득 보너스 · 세트)
	{
		const GearExtras ex = MyGearExtras();
		struct Fx { std::string text; Color color; };
		std::vector<Fx> list;
		for (int p = 1; p < static_cast<int>(ItemProc::Count); ++p)
			if (ex.procPower[p] > 0)
				list.push_back({ std::string("★ ") + ItemRules::Proc(static_cast<ItemProc>(p)).name + (ex.procPower[p] >= 2 ? " (강화)" : ""),
					Rgb(ItemRules::Proc(static_cast<ItemProc>(p)).color) });
		for (int sk = 1; sk < static_cast<int>(sizeof(ex.skillBoost)); ++sk)
			if (ex.skillBoost[sk] > 0)
				if (const SkillRow* row = SkillTable::Find(static_cast<uint16_t>(sk)))
					list.push_back({ Fmt("◈ %s +%d%%", row->name, ex.skillBoost[sk]), Rgb(0xFFD870) });
		if (ex.itemFind > 0) list.push_back({ Fmt("희귀품 발견 +%d%%", ex.itemFind), Rgb(0x8AC8FF) });
		if (ex.adenaFind > 0) list.push_back({ Fmt("아데나 획득 +%d%%", ex.adenaFind), Rgb(0xFFD84A) });
		for (const auto& set : ItemRules::kSets)
			if (ex.setCount[set.id] >= 2)
				list.push_back({ Fmt("%s %d/4%s", set.name, ex.setCount[set.id], ex.setCount[set.id] >= 4 ? " 완성" : ""), Rgb(set.color) });

		float yy = r.y + 454.f;
		if (yy + 40.f < r.y + r.height)
		{
			ui::Divider(r.x + 18.f, r.x + 282.f, yy);
			ui::TextShadow("장비 효과", Vector2{ r.x + 20.f, yy + 8.f }, 15.f, ui::theme::kTitle);
			yy += 32.f;
			if (list.empty())
				ui::TextWrapped("특수 효과는 전설·신화 장비에, 기술 강화는 전설 이상 무기·목걸이에 붙는다.", Rectangle{ r.x + 20.f, yy, 262.f, 60.f }, 13.f, ui::theme::kTextDim, 3.f);
			float xx = r.x + 20.f;
			for (const auto& f : list)
			{
				const float w = ui::Fonts().Measure(f.text, 13.f).x + 14.f;
				if (xx + w > r.x + 284.f) { xx = r.x + 20.f; yy += 20.f; }
				if (yy + 16.f > r.y + r.height - 6.f) break;
				ui::Text(f.text, Vector2{ xx, yy }, 13.f, f.color);
				xx += w;
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 직업 고르기(로그인 화면) — 새 캐릭터를 만들 때만 적용된다.
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawClassSelect(Rectangle area)
{
	const float cardH = (area.height - 16.f) / 3.f;
	for (int i = 0; i < 3; ++i)
	{
		const ClassId id = static_cast<ClassId>(i);
		const ClassRow& cls = ClassTable::Get(id);
		const Color cc = Rgb(cls.color);
		const Rectangle card{ area.x, area.y + i * (cardH + 8.f), area.width, cardH };
		const bool sel = m_newClassId == i;
		const bool hover = ui::Hovered(card);
		ui::Block(card);
		DrawRectangleRounded(card, 0.1f, 8, sel ? util::Shade(cc, 0.22f) : Color{ 20, 16, 24, 220 });
		if (sel)
		{
			const float pulse = 0.5f + 0.5f * std::sin(m_time * 3.f);
			DrawRectangleRoundedLinesEx(card, 0.1f, 8, 2.5f, util::Mix(cc, WHITE, 0.25f * pulse));
		}
		else
			DrawRectangleRoundedLinesEx(card, 0.1f, 8, 1.f, hover ? ui::theme::kBorderHi : ui::theme::kBorder);

		// 미리보기: 고른 직업은 기술을 차례로 보여 준다
		const Rectangle pv{ card.x + 6.f, card.y + 4.f, 86.f, card.height - 8.f };
		BeginScissorMode(static_cast<int>(pv.x), static_cast<int>(pv.y), static_cast<int>(pv.width), static_cast<int>(pv.height));
		{
			actors::Pose pose;
			pose.base = Vector2{ pv.x + pv.width * 0.5f, pv.y + pv.height - 8.f };
			pose.scale = 1.25f;
			pose.time = m_time + i * 1.3f;
			pose.faceRight = true;
			pose.seed = 7u + static_cast<uint32_t>(i);
			if (sel || hover)
			{
				const float cycle = 1.35f;
				const int k = static_cast<int>(m_time / cycle) % 5;
				pose.skill = cls.skills[1 + k];
				pose.skillT = std::fmod(m_time, cycle) / cycle * 1.25f;
				if (pose.skillT > 1.f) pose.skillT = -1.f;
				const SkillRow* sr = SkillTable::Find(pose.skill);
				pose.skillHold = sr ? (sr->castMs / 1000.f) / (cycle / 1.25f) : 0.f;
			}
			gfx::BeginWorld(0.f, 1.25f);
			gfx::ResetMod();
			gfx::SetDepthRaw(-0.5f);
			gfx::EllipseGrad(pose.base, 22.f, 8.f, Rgb(0x000000, 120), Rgb(0x000000, 0));
			actors::DrawPlayer(pose, actors::LookForPlayer(0x5EEDu * (i + 3), cls.starterWeapon, 0, static_cast<uint8_t>(i)));
			gfx::Flush();
		}
		EndScissorMode();

		ui::TextShadow(cls.name, Vector2{ card.x + 100.f, card.y + 10.f }, 19.f, sel ? util::Shade(cc, 1.3f) : util::Shade(cc, 1.05f));
		ui::Text(cls.role, Vector2{ card.x + 100.f, card.y + 36.f }, 13.f, ui::theme::kTextDim);
		ui::TextWrapped(cls.desc, Rectangle{ card.x + 100.f, card.y + 56.f, card.width - 110.f, card.height - 60.f }, 13.f, ui::theme::kText, 3.f);
		if (ui::Clicked(card) && !sel)
		{
			m_newClassId = static_cast<uint8_t>(i);
			sfx::Play(sfx::Id::Equip, 0.6f);
		}
	}
}
