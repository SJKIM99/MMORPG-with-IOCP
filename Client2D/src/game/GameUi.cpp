#include "game/Game.h"
#include "ui/Wm.h"
#include "game/Texts.h"
#include "game/KeyBinds.h"
#include "fx/Effects.h"
#include "fx/SkillFx.h"
#include "render/Draw.h"
#include "render/Actors.h"
#include "render/Icons.h"
#include "render/Props.h"
#include "render/Assets.h"
#include "render/Art.h"
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

	// 그로시(두툼한 은화)
	void CoinIcon(Vector2 c, float r)
	{
		DrawCircleV(Vector2{ c.x + 0.5f, c.y + 1.2f }, r, Color{ 0, 0, 0, 140 });
		DrawCircleV(c, r, Color{ 128, 132, 138, 255 });
		DrawCircleV(c, r - 1.2f, Color{ 196, 199, 202, 255 });
		DrawCircleV(Vector2{ c.x - r * 0.18f, c.y - r * 0.2f }, r * 0.5f, Color{ 232, 232, 226, 255 });
		DrawRing(c, r * 0.55f, r * 0.68f, 20.f, 160.f, 10, Color{ 140, 144, 150, 200 });
	}

	// 지역 위험도: 샛별(◆) 다섯 개 — 채운 개수 = 단계
	void Stars(Vector2 pos, int tier, float size)
	{
		for (int i = 0; i < 5; ++i)
		{
			const Vector2 c{ pos.x + i * (size + 3.f), pos.y };
			const float h = size * 0.5f;
			if (i < tier)
			{
				const Color col = tier >= 5 ? ui::theme::kEmber : (tier >= 4 ? Rgb(0xD98A4A) : ui::theme::kDawn);
				DrawTriangle(Vector2{ c.x, c.y - h }, Vector2{ c.x - h * 0.7f, c.y }, Vector2{ c.x + h * 0.7f, c.y }, col);
				DrawTriangle(Vector2{ c.x - h * 0.7f, c.y }, Vector2{ c.x, c.y + h }, Vector2{ c.x + h * 0.7f, c.y }, col);
			}
			else
			{
				const Color col = ui::theme::kTextMute;
				DrawLineEx(Vector2{ c.x, c.y - h }, Vector2{ c.x + h * 0.7f, c.y }, 1.f, col);
				DrawLineEx(Vector2{ c.x + h * 0.7f, c.y }, Vector2{ c.x, c.y + h }, 1.f, col);
				DrawLineEx(Vector2{ c.x, c.y + h }, Vector2{ c.x - h * 0.7f, c.y }, 1.f, col);
				DrawLineEx(Vector2{ c.x - h * 0.7f, c.y }, Vector2{ c.x, c.y - h }, 1.f, col);
			}
		}
	}

	const char* AlignmentLabel(int16_t a)
	{
		if (a < 0) return "악명";
		if (a >= AlignmentRule::kLawfulLine) return "명예";
		return "중립";
	}

	Color ClassColor(ClassId c) { return Rgb(ClassTable::Get(c).color); }

	// 직업 문장(원 + 상징) — 화면 공간. 바탕은 먹빛, 선과 상징만 직업 색.
	void ClassEmblem(Vector2 c, float r, ClassId cls, float time)
	{
		const Color col = ClassColor(cls);
		DrawCircleV(Vector2{ c.x, c.y + 2 }, r, Color{ 0, 0, 0, 110 });
		DrawCircleV(c, r, ui::theme::kSurface1);
		DrawCircleGradient(static_cast<int>(c.x), static_cast<int>(c.y - r * 0.2f), r, util::WithA(util::Shade(col, 0.5f), 0.35f), Color{ 0, 0, 0, 0 });
		DrawRing(c, r - 1.5f, r, 0.f, 360.f, 48, util::WithA(col, 0.75f));
		const float k = r / 30.f;
		const Color ink = util::Shade(col, 1.15f);
		switch (cls)
		{
		case ClassId::Knight:   // 칼 + 방패
			DrawLineEx(Vector2{ c.x - 12 * k, c.y + 14 * k }, Vector2{ c.x + 12 * k, c.y - 14 * k }, 3.5f * k, ink);
			DrawLineEx(Vector2{ c.x - 6 * k, c.y + 2 * k }, Vector2{ c.x + 2 * k, c.y + 10 * k }, 2.6f * k, util::Shade(col, 0.9f));
			DrawCircleV(Vector2{ c.x - 7 * k, c.y - 3 * k }, 8.5f * k, ui::theme::kSurface1);
			DrawRing(Vector2{ c.x - 7 * k, c.y - 3 * k }, 7.f * k, 8.5f * k, 0, 360, 24, ink);
			break;
		case ClassId::Engraver:   // 도는 각인 삼각
		{
			for (int i = 0; i < 3; ++i)
			{
				const float a0 = time * 0.6f + i * util::kPi * 2.f / 3.f, a1 = a0 + util::kPi * 2.f / 3.f;
				DrawLineEx(Vector2{ c.x + std::cos(a0) * 14 * k, c.y + std::sin(a0) * 14 * k }, Vector2{ c.x + std::cos(a1) * 14 * k, c.y + std::sin(a1) * 14 * k }, 2.6f * k, ink);
			}
			DrawCircleV(c, 3.2f * k, ui::theme::kTextHi);
			DrawRing(c, 17 * k, 18.2f * k, 0, 360, 32, util::WithA(ink, 0.55f));
			break;
		}
		case ClassId::NightHunter:   // 그믐달 + 단검
		default:
			DrawCircleV(Vector2{ c.x - 3 * k, c.y - 2 * k }, 12 * k, ink);
			DrawCircleV(Vector2{ c.x + 3 * k, c.y - 6 * k }, 11.5f * k, ui::theme::kSurface1);
			DrawLineEx(Vector2{ c.x + 2 * k, c.y + 14 * k }, Vector2{ c.x + 13 * k, c.y - 1 * k }, 2.6f * k, ui::theme::kTextHi);
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
			"물리 공격력과 방어력이 오른다. 서약기사의 주 능력치.",
			"공격 속도·명중·회피·치명타가 오르고, 밤사냥꾼은 공격력도 오른다.",
			"마법 공격력·마법 관통·최대 MP가 오른다. 각인술사의 주 능력치.",
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
		case ClassId::Engraver: return i == 2 || i == 4;
		default: return i == 1 || i == 5;
		}
	}
	const char* StatusHelp(StatusId id)
	{
		switch (id)
		{
		case StatusId::Stun: return "움직이거나 공격할 수 없다.";
		case StatusId::Freeze: return "움직일 수 없다. 화염에 맞으면 증발 폭발, 낙성에 맞으면 산산조각.";
		case StatusId::Burn: return "매초 불 피해를 입는다. 벼락 사슬이 더 멀리 튄다.";
		case StatusId::Poison: return "매초 독 피해(중첩). 그믐 베기가 한꺼번에 터뜨린다.";
		case StatusId::Slow: return "이동 속도가 느려진다.";
		case StatusId::Guard: return "받는 피해가 절반, 맞을 때마다 반격한다.";
		case StatusId::Ambush: return "다음 공격이 확정 치명타가 된다.";
		case StatusId::Evade: return "잠깐 모든 공격을 피한다.";
		case StatusId::Marked: return "벼락 사슬이 이 대상을 거쳐 더 멀리 튄다. 새김 울림이 터뜨린다.";
		case StatusId::Oath: return "흔들리지 않는다(경직 면역). 모든 타격에 금빛 잔향(30%)이 따른다.";
		case StatusId::Rally: return "공격 +15%.";
		case StatusId::Veil: return "몬스터가 놓친다. 이동 +30%, 다음 기술은 급소를 찌른다.";
		case StatusId::Taunt: return "도발한 기사를 쫓는다.";
		case StatusId::Broken: return "무력화 — 움직이지 못하고 받는 피해 +30%.";
		case StatusId::Clone: return "분신 둘이 다음 공격을 40%로 따라 한다(남은 횟수).";
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
		case StatusId::Oath:   // 새김 원
			DrawRing(c, r * 0.55f, r * 0.8f, time * 90.f, time * 90.f + 270.f, 24, col);
			DrawCircleV(c, r * 0.25f, col);
			break;
		case StatusId::Rally:  // 위로 솟는 빛살 셋
			for (int i = -1; i <= 1; ++i) DrawLineEx(Vector2{ c.x + i * r * 0.5f, c.y + r * 0.7f }, Vector2{ c.x + i * r * 0.5f, c.y - r * (0.5f + 0.3f * (i == 0)) }, 2.5f, col);
			break;
		case StatusId::Veil:   // 초승달
			DrawCircleV(c, r * 0.8f, col);
			DrawCircleV(Vector2{ c.x + r * 0.35f, c.y - r * 0.2f }, r * 0.7f, ui::theme::kSurface1);
			break;
		case StatusId::Clone:  // 겹친 두 몸
			DrawCircleV(Vector2{ c.x - r * 0.35f, c.y }, r * 0.45f, util::WithA(col, 0.5f));
			DrawCircleV(Vector2{ c.x + r * 0.35f, c.y }, r * 0.45f, col);
			break;
		default: DrawCircleV(c, r * 0.5f, col); break;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 로그인 화면 — 대표 그림(심장나무 하르벤과 데니차 마을) 위에 로고와 접속 판.
//   왼쪽 기둥: 로고 → 접속 판(접속 정보 · 새 캐릭터의 직업 · 접속하기).
//   가운데와 오른쪽은 비워 둔다 — 심장나무 · 마을 · 샛별이 보이도록.
//   그림 파일이 없으면 예전처럼 마을 위를 천천히 도는 카메라로 대신한다.
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawLogin()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const float t = m_time;
	const Color ink = ui::theme::kInk;

	if (const Texture2D* key = art::KeyArt())
	{
		// 아주 느린 밀고 당기기(켄 번스) — 한 번 숨 쉬는 데 50초 남짓
		const Rectangle full{ 0, 0, W, H };
		const float zoom = 1.07f + 0.035f * std::sin(t * 0.12f);
		const Vector2 focus{ 0.5f + 0.03f * std::sin(t * 0.07f + 1.f), 0.42f };
		art::DrawCover(*key, full, WHITE, focus, zoom);
		// 샛별(데니차): 그림 속 별이 숨 쉬듯 반짝인다 — 로고의 별과 같은 표식
		const Vector2 star = art::CoverPoint(*key, full, focus, zoom, Vector2{ 0.7125f, 0.0543f });
		art::Glint(star, 5.f * H / 720.f, Color{ 255, 226, 170, 255 }, art::StarPulse(t));
		// 반딧불: 마을 쪽 아래에서 천천히 떠오르는 작은 빛(더하기 혼합, 아주 옅게)
		BeginBlendMode(BLEND_ADDITIVE);
		for (int i = 0; i < 26; ++i)
		{
			const float r0 = util::Hash01(i, 1, 77u), r1 = util::Hash01(i, 2, 77u), r2 = util::Hash01(i, 3, 77u);
			const float period = 14.f + r0 * 12.f;
			const float u = std::fmod(t / period + r1, 1.f);
			const float x = W * (0.34f + 0.62f * r2) + std::sin(t * (0.35f + r0 * 0.5f) + i) * 16.f;
			const float y = H * (0.98f - 0.42f * u);
			const float a = std::sin(u * util::kPi) * (0.35f + 0.45f * r1) * (0.65f + 0.35f * std::sin(t * 2.6f + i * 1.7f));
			const float rad = (1.3f + r0 * 1.3f) * H / 720.f;
			DrawCircleGradient(static_cast<int>(x), static_cast<int>(y), rad * 5.f, util::WithA(ui::theme::kDawn, 0.2f * a), util::WithA(ui::theme::kDawn, 0.f));
			DrawCircleV(Vector2{ x, y }, rad, util::WithA(ui::theme::kDawnHi, 0.95f * a));
		}
		EndBlendMode();
	}
	else
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
	}

	// 읽기 좋게 가라앉히기: 왼쪽 기둥(로고 · 판) · 아래(조작 안내) · 위 가장자리
	DrawRectangleGradientH(0, 0, static_cast<int>(W * 0.5f), m_screenH, util::WithA(ink, 0.74f), util::WithA(ink, 0.f));
	DrawRectangleGradientV(0, static_cast<int>(H * 0.7f), m_screenW, static_cast<int>(H * 0.3f) + 1, util::WithA(ink, 0.f), util::WithA(ink, 0.72f));
	DrawRectangleGradientV(0, 0, m_screenW, static_cast<int>(H * 0.14f), util::WithA(ink, 0.45f), util::WithA(ink, 0.f));

	// ── 배치: 로고와 판을 한 덩어리로 세로 가운데(아래 안내 줄 몫은 뺀다) ──
	const bool compact = H < 700.f;
	const float k = compact ? 0.78f : (H >= 860.f ? 1.f : 0.9f);
	const float PW = 392.f;
	const float colX = std::round(std::max(32.f, W * 0.04f));
	const float chipH = compact ? 82.f : 96.f;
	const float classH = chipH + 10.f + 62.f;
	const float PH = 52.f + 40.f * 3.f + 8.f + 22.f + classH + 54.f + 34.f + 6.f;
	const float logoH = (31.f + (compact ? 70.f : 98.f)) * k;
	const float gap = compact ? 18.f : 28.f;
	const float top = std::max(14.f, std::round((H - 48.f - (logoH + gap + PH)) * 0.5f));

	// 들어오는 연출: 화면이 밝아지고 → 로고 → 판이 조금 떠오르며 자리 잡는다
	ui::Logo(Vector2{ colX + PW * 0.5f, top + 31.f * k }, k, util::Clamp01((t - 0.35f) / 1.1f), !compact);
	const float slide = (1.f - util::EaseOutCubic(util::Clamp01((t - 0.5f) / 0.8f))) * 14.f;
	const Rectangle p{ colX, std::round(top + logoH + gap + slide), PW, PH };
	ui::Window(p, "접속", nullptr);

	const float ix = p.x + 22.f, iw = PW - 44.f;
	const float fx = ix + 78.f, fw = iw - 78.f;
	float y = p.y + 52.f;
	const bool busy = m_scene == Scene::Connecting;
	auto label = [&](const char* s) { ui::Text(s, Vector2{ ix, y + 8.f }, ui::type::kBodyS, ui::theme::kTextDim); };
	label("서버");
	ui::InputText(Rectangle{ fx, y, fw - 74.f, 32.f }, m_fHost, "127.0.0.1");
	ui::InputText(Rectangle{ fx + fw - 68.f, y, 68.f, 32.f }, m_fPort, "4000");
	y += 40.f;
	label("캐릭터");
	const bool enterName = ui::InputText(Rectangle{ fx, y, fw, 32.f }, m_fName, "영문/숫자 2~12자", !busy && m_fName.text.empty());
	y += 40.f;
	label("비밀번호");
	const bool enterPass = ui::InputText(Rectangle{ fx, y, fw, 32.f }, m_fPass, "(선택)");
	y += 48.f;
	ui::SectionHeader(Vector2{ ix, y }, iw, "새 캐릭터의 직업");
	y += 22.f;
	DrawClassSelect(Rectangle{ ix, y, iw, classH });
	y += classH;
	const bool go = ui::Button(Rectangle{ ix, y, iw, 44.f }, busy ? "접속 중…" : "접속하기", ButtonKind::Primary, !busy, 19.f);
	if (!busy && (go || enterName || enterPass))
		BeginConnect();
	y += 54.f;
	if (busy)
	{
		const float a = t * 4.f;
		const float cx = p.x + PW * 0.5f - 76.f;
		for (int i = 0; i < 8; ++i)
		{
			const float ang = a + i * util::kPi / 4.f;
			DrawCircleV(Vector2{ cx + std::cos(ang) * 7.f, y + 8.f + std::sin(ang) * 7.f }, 2.f, util::WithA(ui::theme::kDawnHi, (60 + i * 24) / 255.f));
		}
		ui::Text("서버에 연결하는 중…", Vector2{ p.x + PW * 0.5f + 10.f, y }, ui::type::kBodyS, ui::theme::kText, Align::Center);
	}
	else if (!m_loginError.empty())
		ui::TextWrapped(m_loginError, Rectangle{ ix, y, iw, 34.f }, ui::type::kBodyS, ui::theme::kBad, 4.f);
	else
		ui::Text("처음 쓰는 이름이면 새 캐릭터로 시작합니다.", Vector2{ p.x + PW * 0.5f, y + 1.f }, ui::type::kCaption + 0.5f, ui::theme::kTextDim, Align::Center);

	// ── 아래: 조작 안내(키캡 + 설명) ──
	{
		struct KeyHint { const char* key; const char* what; };
		static constexpr KeyHint kHints[] = {
			{ "좌클릭", "이동" }, { "우클릭 · C", "공격" }, { "Q~F", "기술" }, { "V", "각성기" }, { "Space", "이동기" },
			{ "G", "대화 · 줍기" }, { "1~4 · F1", "소모품" }, { "Esc", "메뉴 · 조작 설정" },
		};
		const float ks = ui::type::kMicro, ls = ui::type::kCaption;
		auto keyW = [&](const char* s) { return std::round(ui::Fonts().Measure(s, ks, 0.f, ui::Face::Strong).x + 10.f); };
		float total = 0.f;
		for (const auto& h : kHints)
			total += keyW(h.key) + 5.f + ui::Fonts().Measure(h.what, ls, 0.f, ui::Face::Body).x + 16.f;
		total -= 16.f;
		float x = std::round(W * 0.5f - total * 0.5f);
		const float hy = std::round(H - 32.f);
		for (const auto& h : kHints)
		{
			const float kw = keyW(h.key);
			ui::Keycap(Rectangle{ x, hy, kw, 16.f }, h.key);
			x += kw + 5.f;
			ui::TextStyle st; st.size = ls; st.color = Color{ 190, 184, 172, 215 }; st.face = ui::Face::Body;
			x += ui::Fonts().Draw(h.what, Vector2{ x, hy + 1.f }, st) + 16.f;
		}
	}
	ui::Text("v0.9  ·  MMORPG-with-IOCP", Vector2{ W - 16.f, H - 20.f }, 11.f, ui::theme::kTextMute, Align::Right);

	// 로그인 화면은 한가하다 — 대화창 초상화를 한 프레임에 한 장씩 미리 읽어 둔다
	if (t > 1.5f)
		(void)art::PreloadStep();

	// 처음 켰을 때: 어둠에서 천천히 밝아진다
	const float intro = util::Clamp01(t / 1.2f);
	if (intro < 1.f)
		DrawRectangle(0, 0, m_screenW, m_screenH, util::WithA(BLACK, 1.f - util::EaseOutCubic(intro)));
}

void Game::DrawDisconnected()
{
	DrawRectangleRec(Rectangle{ 0, 0, m_uiW, m_uiH }, Color{ 0, 0, 0, 170 });
	const Rectangle r{ m_uiW * 0.5f - 200.f, m_uiH * 0.5f - 90.f, 400.f, 180.f };
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
	const float W = m_uiW, H = m_uiH;
	// 경험치(화면 맨 아래 가는 띠 — 새벽빛)
	{
		const Rectangle r{ 0, H - 5, W, 5 };
		DrawRectangleRec(r, ui::theme::kInk);
		const float fw = std::round(W * m_expShown);
		DrawRectangleRec(Rectangle{ 0, H - 5, fw, 5 }, ui::theme::kExp);
		DrawRectangleRec(Rectangle{ 0, H - 5, fw, 1 }, util::WithA(util::Shade(ui::theme::kExp, 1.4f), 0.8f));
		for (int i = 1; i < 10; ++i)
			DrawRectangle(static_cast<int>(W * i / 10.f), static_cast<int>(H - 5), 1, 5, Color{ 0, 0, 0, 120 });
		const Rectangle hit{ 0, H - 12, W, 12 };
		if (ui::Hovered(hit))
			ui::Tooltip({ { "경험치", ui::theme::kTextHi, 15.f, ui::Face::Strong },
				{ Fmt("%s / %s  (%.1f%%)", texts::Number(m_exp).c_str(), texts::Number(m_expToNext).c_str(), m_expShown * 100.f), ui::theme::kExp, 15.f, ui::Face::Number } });
	}

	// 아래 가운데 판: HP · MP · 능력치(기술 · 소모품 칸은 DrawSkillBar · DrawQuickBar — GameHud.cpp)
	DrawCombatPanel();

	// 왼쪽 위: 나(직업 문장 · 이름 · 그로시)
	{
		const Rectangle r{ 12.f, 12.f, 268.f, 80.f };
		wm::HudScope hs("hud.me", "나", r);
		ui::Panel(r, 0.94f, false);
		const Vector2 ec{ r.x + 40.f, r.y + 40.f };
		ClassEmblem(ec, 26.f, m_classId, m_time);
		DrawRing(ec, 28.f, 30.f, -90.f, -90.f + 360.f * m_expShown, 64, ui::theme::kExp);
		{
			// 레벨 배지
			const std::string lv = std::to_string(m_level);
			ui::TextStyle ls; ls.size = 13.f; ls.color = ui::theme::kTextHi; ls.face = ui::Face::Number;
			const float lw = std::max(20.f, ui::Fonts().Measure(lv, ls.size, 0.f, ui::Face::Number).x + 10.f);
			const Rectangle lb{ std::round(ec.x + 14.f), std::round(ec.y + 12.f), std::round(lw), 17.f };
			ui::Plate(lb, ui::theme::kSurface2, ui::theme::kLineStrong, 2.f, 1.f);
			ui::Fonts().Draw(lv, Vector2{ std::round(lb.x + (lb.width - ui::Fonts().Measure(lv, ls.size, 0.f, ui::Face::Number).x) * 0.5f), lb.y + 1.f }, ls);
		}
		Entity* me = Self();
		const Color nc = me ? NameColor(*me) : ui::theme::kTextHi;
		{
			ui::TextStyle ns; ns.size = 16.f; ns.color = nc; ns.face = ui::Face::Strong;
			ui::Fonts().Draw(m_playerName, Vector2{ r.x + 80, r.y + 12 }, ns);
		}
		ui::Text(ClassTable::Get(m_classId).name, Vector2{ r.x + 80, r.y + 35 }, ui::type::kCaption + 0.5f, util::Shade(ClassColor(m_classId), 1.1f));
		const Color ac = m_alignment < 0 ? ui::theme::kBad : (m_alignment >= AlignmentRule::kLawfulLine ? ui::theme::kInfo : ui::theme::kTextDim);
		ui::Text(Fmt("%s %+d", AlignmentLabel(m_alignment), m_alignment), Vector2{ r.x + r.width - 12, r.y + 14 }, ui::type::kCaption, ac, Align::Right);
		CoinIcon(Vector2{ r.x + 86, r.y + 63 }, 6.f);
		{
			ui::TextStyle ms; ms.size = 15.f; ms.color = ui::theme::kTextHi; ms.face = ui::Face::Number;
			ui::Fonts().Draw(texts::Number(m_inv.adena), Vector2{ r.x + 97, r.y + 55 }, ms);
		}
		if (m_pkCount > 0)
			ui::Text(Fmt("PK %d", m_pkCount), Vector2{ r.x + r.width - 12, r.y + 56 }, ui::type::kCaption, ui::theme::kBad, Align::Right);
		// 남은 능력치 포인트 — 눌러서 캐릭터 창
		if (m_freePoints > 0)
		{
			const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 4.f);
			const Rectangle b{ r.x + r.width - 50.f, r.y + 32.f, 38.f, 19.f };
			ui::Plate(b, util::Mix(Color{ 44, 34, 20, 240 }, Color{ 74, 56, 30, 240 }, pulse), ui::theme::kDawn, 2.f, 1.f);
			ui::TextStyle ps; ps.size = 13.f; ps.color = ui::theme::kDawnHi; ps.face = ui::Face::Number;
			ui::Fonts().DrawCentered(Fmt("+%d", m_freePoints), Vector2{ b.x + b.width * 0.5f, b.y + b.height * 0.5f }, ps);
			ui::Block(b);
			if (ui::Hovered(b)) ui::Tooltip({ { "능력치 포인트", ui::theme::kDawnHi, 15.f, ui::Face::Strong }, { "[" + kb::Label(kb::Act::Character) + "] 키 또는 여기를 눌러 캐릭터 창에서 올리세요.", ui::theme::kText, 13.f } });
			if (ui::Clicked(b)) { m_charOpen = true; sfx::Play(sfx::Id::Open, 0.6f); }
		}
	}
	{
		wm::HudScope hs("hud.buffs", "버프 · 약화", Rectangle{ 12.f, 100.f, 300.f, 30.f });
		DrawStatusIcons(12.f, 100.f);
	}

	// 가운데 알림(위쪽 알림 띠가 길어지면 그 아래로)
	float y = std::max(H * 0.27f, m_bannersBottom + 18.f);
	for (const auto& c : m_center)
	{
		const float age = static_cast<float>(m_now - c.start);
		const float a = age > 2.1f ? 1.f - (age - 2.1f) / 0.5f : 1.f;
		const float pop = age < 0.12f ? util::EaseOutBack(age / 0.12f) : 1.f;
		ui::TextStyle st;
		st.size = 18.f * pop;
		st.color = util::MulA(c.color, a);
		st.outline = 1.5f;
		st.outlineColor = util::MulA(Color{ 0, 0, 0, 200 }, a);
		st.shadow = 2.f;
		st.shadowColor = util::MulA(Color{ 0, 0, 0, 120 }, a);
		st.face = ui::Face::Strong;
		ui::Fonts().DrawCentered(c.text, Vector2{ W * 0.5f, y }, st);
		y += 27.f;
	}
}

void Game::DrawTargetFrame()
{
	Entity* t = Find(m_target);
	if (!t || t->kind == EntityKind::Item || t->removing)
		return;
	const float W = m_uiW;
	const bool boss = t->IsBoss();
	const Rectangle r{ std::round(W * 0.5f - 180.f), 12.f, 360.f, boss ? 78.f : 56.f };
	wm::HudScope hs("hud.target", "대상", Rectangle{ r.x, r.y, r.width, 78.f });
	ui::Panel(r, 0.95f, false);
	if (boss)
		DrawRectangleRec(Rectangle{ r.x + 5, r.y + 1, r.width - 10, 2 }, ui::theme::kEmber);

	// 이름줄: [레벨] 이름 · 별칭/표시
	float x = r.x + 14.f;
	const float ty = r.y + 9.f;
	std::string level;
	if (t->kind == EntityKind::Monster && t->row) level = Fmt("Lv.%d", t->row->level);
	else if (t->kind == EntityKind::Player) level = Fmt("Lv.%d", t->level);
	if (!level.empty())
	{
		ui::TextStyle ls; ls.size = 14.f; ls.color = ui::theme::kTextDim; ls.face = ui::Face::Number;
		x += ui::Fonts().Draw(level, Vector2{ x, ty + 1.f }, ls) + 8.f;
	}
	ui::TextStyle ns; ns.size = boss ? 17.f : 15.f; ns.color = NameColor(*t); ns.face = boss ? ui::Face::Display : ui::Face::Strong;
	x += ui::Fonts().Draw(t->name, Vector2{ x, boss ? ty - 1.f : ty }, ns) + 10.f;
	std::string tag;
	Color tagColor = ui::theme::kTextDim;
	if (t->kind == EntityKind::Monster)
	{
		if (boss) { tag = t->row ? MonsterTable::BossEpithet(t->row->id) : ""; tagColor = Rgb(0xC8A48A); }
		else if (t->aggressive) { tag = "선공"; tagColor = Rgb(0xC87A6A); }
	}
	else if (t->kind == EntityKind::Player)
		tag = AlignmentLabel(t->alignment);
	if (!tag.empty())
	{
		ui::TextStyle gs; gs.size = ui::type::kCaption + 0.5f; gs.color = tagColor; gs.face = boss ? ui::Face::Serif : ui::Face::Body;
		ui::Fonts().Draw(tag, Vector2{ x, ty + 2.f }, gs);
	}
	const Rectangle bar{ r.x + 14, r.y + (boss ? 38.f : 33.f), r.width - 28, boss ? 14.f : 12.f };
	const Color fill = t->kind == EntityKind::Player ? ui::theme::kGood : (boss ? ui::theme::kEmber : ui::theme::kHp);
	ui::Bar(bar, t->hpShown, t->hpLag, fill, Color{ 30, 13, 13, 255 }, t->dead ? "쓰러짐" : Fmt("%d / %d", std::max(0, t->hp), t->maxhp), boss ? 13.f : 12.f);

	// 보스 무력화 게이지(Phase 2-B, MONSTER_GAUGE_NFY): 깎인 몫이 잠깐 남았다 따라온다. 비면 3초 무력화
	if (boss && !t->dead)
	{
		const Rectangle sb{ bar.x + 64.f, bar.y + bar.height + 5.f, bar.width - 64.f, 6.f };
		const bool broken = t->brokenUntil > m_now;
		if (m_now - t->staggerAt > 0.35 && t->staggerLag > t->stagger)
			t->staggerLag = static_cast<uint16_t>(std::max<int>(t->stagger, t->staggerLag - static_cast<int>(900.f * GetFrameTime())));
		ui::TextStyle ls; ls.size = 11.f; ls.color = broken ? ui::theme::kDawn : ui::theme::kTextDim; ls.face = ui::Face::Strong;
		ui::Fonts().Draw(broken ? "무력화!" : "무력화", Vector2{ bar.x, sb.y - 3.f }, ls);
		DrawRectangleRec(sb, Color{ 24, 22, 18, 255 });
		if (broken)
		{
			const float left = static_cast<float>((t->brokenUntil - m_now) / 3.0);
			const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 14.f);
			DrawRectangleRec(Rectangle{ sb.x, sb.y, sb.width * util::Clamp01(left), sb.height }, util::Mix(ui::theme::kDawn, WHITE, 0.35f * pulse));
		}
		else
		{
			DrawRectangleRec(Rectangle{ sb.x, sb.y, sb.width * (t->staggerLag / 1000.f), sb.height }, Color{ 236, 226, 200, 200 });
			DrawRectangleRec(Rectangle{ sb.x, sb.y, sb.width * (t->stagger / 1000.f), sb.height }, util::Mix(ui::theme::kDawn, Rgb(0xC89A50), 0.3f));
		}
		DrawRectangleLinesEx(Rectangle{ sb.x - 1, sb.y - 1, sb.width + 2, sb.height + 2 }, 1.f, ui::theme::kLine);
	}
}

void Game::DrawMinimap()
{
	const float W = m_uiW;
	const float size = 184.f;
	const Rectangle r{ std::round(W - size - 14.f), 12.f, size, size + 50.f };
	wm::HudScope hs("hud.minimap", "미니맵", r);
	ui::Panel(r, 0.95f, false);
	const Rectangle m{ r.x + 8, r.y + 8, size - 16, size - 16 };
	DrawMinimapAt(m, 30, 1.f, false);
	DrawRectangleLinesEx(Rectangle{ m.x - 1, m.y - 1, m.width + 2, m.height + 2 }, 1.f, ui::theme::kLine);

	// 지역/좌표/시계
	const int region = Region();
	const auto* row = WorldData::FindRegion(region);
	const std::string place = InTown() ? std::string(WorldData::kTownName) : (row ? row->name : "");
	{
		ui::TextStyle ps; ps.size = 15.f; ps.color = ui::theme::kTextHi; ps.face = ui::Face::Display; ps.spacing = 0.4f;
		ui::Fonts().Draw(place, Vector2{ r.x + 12, r.y + size - 2 }, ps);
	}
	if (row && !InTown())
		Stars(Vector2{ r.x + r.width - 66.f, r.y + size + 6.f }, row->tier, 8.f);
	else if (InTown())
		ui::Text("안전 지대", Vector2{ r.x + r.width - 12.f, r.y + size - 0.f }, ui::type::kCaption, ui::theme::kGood, Align::Right);
	{
		ui::TextStyle cs; cs.size = 13.f; cs.color = ui::theme::kTextDim; cs.face = ui::Face::Number;
		ui::Fonts().Draw(Fmt("%d, %d", m_predX, m_predY), Vector2{ r.x + 12, r.y + size + 21 }, cs);
	}
	const float hour = m_light.Hour();
	const int hh = static_cast<int>(hour), mm = static_cast<int>((hour - hh) * 60.f);
	const bool night = m_light.Night() > 0.5f;
	const Vector2 ic{ r.x + r.width - 62.f, r.y + size + 28.f };
	if (night)
	{
		DrawCircleV(ic, 6.f, Rgb(0xE8E2D0));
		DrawCircleV(Vector2{ ic.x + 3, ic.y - 2 }, 5.f, ui::theme::kSurface0);
	}
	else
	{
		DrawCircleV(ic, 4.5f, ui::theme::kDawn);
		for (int i = 0; i < 8; ++i)
		{
			const float a = i * util::kPi / 4.f;
			DrawLineEx(Vector2{ ic.x + std::cos(a) * 6.5f, ic.y + std::sin(a) * 6.5f }, Vector2{ ic.x + std::cos(a) * 8.5f, ic.y + std::sin(a) * 8.5f }, 1.2f, ui::theme::kDawn);
		}
	}
	{
		ui::TextStyle hs; hs.size = 14.f; hs.color = ui::theme::kText; hs.face = ui::Face::Number;
		const std::string clock = Fmt("%02d:%02d", hh, mm);
		ui::Fonts().Draw(clock, Vector2{ r.x + r.width - 12.f - ui::Fonts().Measure(clock, hs.size, 0.f, ui::Face::Number).x, r.y + size + 20 }, hs);
	}
}

// 지도 한 장(미니맵 · Tab 확장 미니맵이 같이 쓴다). 지형은 렌더 텍스처에 캐시하고 redraw칸 움직일 때마다 다시 그린다.
// bigMap: 반투명 · 가게 NPC 이름 · 표식 크게.
void Game::DrawMinimapAt(Rectangle m, int range, float alpha, bool bigMap)
{
	const int kRange = range;
	// 타일 1칸 → 마름모 반폭(px). 미니맵은 네모 칸에 크게(모서리는 잘린다), 확장 미니맵은 마름모 전체가 2:1 칸에 꼭 맞게.
	const float k = bigMap ? m.width / ((kRange * 2.f + 1.f) * 2.f) : m.width / (kRange * 2.f + 1.f) * 0.72f;
	const Vector2 c{ m.x + m.width * 0.5f, m.y + m.height * 0.5f };
	auto toMap = [&](float dx, float dy) { return Vector2{ c.x + (dx - dy) * k, c.y + (dx + dy) * k * 0.5f }; };
	const float mk = bigMap ? 1.35f : 1.f;   // 표식 크기
	auto A = [&](Color col) { return util::MulA(col, alpha); };

	// 배경(지형) — 캐시 중심에서 redraw칸 넘게 움직이면 다시 그린다. 여유(margin)만큼 크게 그려 옮겨 그려도 빈틈이 없다.
	RenderTexture2D& rt = bigMap ? m_bigMapRt : m_minimapRt;
	int& cx0 = bigMap ? m_bigMapX : m_minimapX;
	int& cy0 = bigMap ? m_bigMapY : m_minimapY;
	const int redraw = bigMap ? 3 : 1;
	const float margin = std::ceil(redraw * k * 2.f + 2.f);
	// UI 배율(Phase 3-A): 캐시는 물리 픽셀 크기로 굽는다(배율 1.25 · 1.3에서도 흐려지지 않게)
	const float K = std::max(1.f, ui::CurrentXform().k);
	const int texW = static_cast<int>(std::ceil((m.width + margin * 2.f) * K)), texH = static_cast<int>(std::ceil((m.height + margin * 2.f) * K));
	if (rt.id == 0 || rt.texture.width != texW || rt.texture.height != texH)
	{
		if (rt.id != 0) UnloadRenderTexture(rt);
		rt = LoadRenderTexture(texW, texH);
		cx0 = cy0 = -9999;
	}
	if (std::abs(cx0 - m_predX) >= redraw || std::abs(cy0 - m_predY) >= redraw)
	{
		cx0 = m_predX;
		cy0 = m_predY;
		ui::SuspendXform();
		BeginTextureMode(rt);
		ClearBackground(bigMap ? Color{ 10, 12, 14, 0 } : Color{ 10, 12, 14, 255 });
		const Vector2 lc{ texW * 0.5f, texH * 0.5f };
		const float k = std::max(1.f, ui::CurrentXform().k) * (bigMap ? m.width / ((kRange * 2.f + 1.f) * 2.f) : m.width / (kRange * 2.f + 1.f) * 0.72f);
		const int R = kRange + redraw + 1;
		for (int dy = -R; dy <= R; ++dy)
			for (int dx = -R; dx <= R; ++dx)
			{
				const int x = cx0 + dx, y = cy0 + dy;
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
		ui::ResumeXform();
	}
	ui::BeginClip(Rectangle{ m.x, m.y, m.width, m.height });
	{
		// 캐시 중심과 지금 칸의 차이만큼 옮겨 그린다
		const float odx = static_cast<float>(cx0 - m_predX), ody = static_cast<float>(cy0 - m_predY);
		const Vector2 off{ (odx - ody) * k, (odx + ody) * k * 0.5f };
		DrawTexturePro(rt.texture, Rectangle{ 0, 0, static_cast<float>(texW), -static_cast<float>(texH) },
			Rectangle{ std::round((m.x - margin + off.x) * K) / K, std::round((m.y - margin + off.y) * K) / K, texW / K, texH / K },
			Vector2{ 0, 0 }, 0.f, util::WithA(WHITE, alpha));
	}
	// 부드러운 이동 보정(내 실제 위치와 캐시 기준 칸의 차이)
	Entity* me = Self();
	const Vector2 myPos = me ? me->pos : Vector2{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
	for (auto& [id, e] : m_ents)
	{
		if (id == m_selfId || e.dead || e.removing) continue;
		const Vector2 p = toMap(e.pos.x - m_predX, e.pos.y - m_predY);
		Color col = Rgb(0xD8604F);
		float rad = 2.2f;
		if (e.kind == EntityKind::Player) { col = NameColor(e); rad = 2.6f; }
		else if (e.kind == EntityKind::Item) { col = ui::theme::kGold; rad = 1.6f; }
		else if (e.IsBoss()) { col = ui::theme::kEmber; rad = 4.5f + std::sin(static_cast<float>(m_now) * 6.f); }
		DrawCircleV(p, (rad + 0.8f) * mk, A(Color{ 0, 0, 0, 160 }));
		DrawCircleV(p, rad * mk, A(col));
	}
	for (const auto& [nid, v] : m_npcView)
	{
		if (!v.row) continue;
		const Vector2 p = toMap(v.pos.x - m_predX, v.pos.y - m_predY);
		// 퀘스트가 있는 사람은 ! ? 로
		const QuestMark qm = NpcQuestMark(nid);
		if (qm == QuestMark::Available || qm == QuestMark::Ready)
		{
			ui::TextStyle st;
			st.size = 13.f * mk;
			st.color = A(ui::theme::kDawnHi);
			st.outline = 1.5f;
			st.outlineColor = A(Color{ 20, 14, 6, 230 });
			st.face = ui::Face::Bold;
			ui::Fonts().DrawCentered(qm == QuestMark::Ready ? "?" : "!", Vector2{ p.x, p.y - 2.f }, st);
			continue;
		}
		if (WorldData::IsAnimal(v.row->kind) && qm != QuestMark::Talk) continue;
		const bool service = v.row->kind == WorldData::NpcKind::eMerchant || v.row->kind == WorldData::NpcKind::eWeaponSmith
			|| v.row->kind == WorldData::NpcKind::eTeleporter || v.row->kind == WorldData::NpcKind::ePriest || v.row->kind == WorldData::NpcKind::eInnkeeper;
		DrawCircleV(p, (service ? 3.2f : 2.4f) * mk, A(Color{ 0, 0, 0, 160 }));
		DrawCircleV(p, (service ? 2.4f : 1.7f) * mk, A(qm == QuestMark::Talk ? ui::theme::kDawnHi : (service ? Rgb(0xE8D8B8) : Rgb(0x8DBF6E))));
		if (bigMap && service)
		{
			ui::TextStyle ns; ns.size = ui::type::kCaption; ns.color = A(Rgb(0xE8DDC4)); ns.face = ui::Face::Strong;
			ns.outline = 1.3f; ns.outlineColor = A(Color{ 0, 0, 0, 200 });
			ui::Fonts().DrawCentered(v.row->name, Vector2{ p.x, p.y - 12.f }, ns);
		}
	}
	// 퀘스트 목적지(밖이면 가장자리에 붙인다)
	if (m_questDestValid)
	{
		Vector2 p = toMap(m_questDest.tile.x - m_predX, m_questDest.tile.y - m_predY);
		const Rectangle in{ m.x + 7.f, m.y + 7.f, m.width - 14.f, m.height - 14.f };
		const bool inside = CheckCollisionPointRec(p, in);
		if (!inside)
		{
			const Vector2 d = util::Norm(util::Sub(p, c));
			float kk = 1e9f;
			if (d.x > 1e-4f) kk = std::min(kk, (in.x + in.width - c.x) / d.x);
			if (d.x < -1e-4f) kk = std::min(kk, (in.x - c.x) / d.x);
			if (d.y > 1e-4f) kk = std::min(kk, (in.y + in.height - c.y) / d.y);
			if (d.y < -1e-4f) kk = std::min(kk, (in.y - c.y) / d.y);
			p = util::Add(c, util::Mul(d, kk));
			const Vector2 n{ -d.y, d.x };
			DrawTriangle(util::Add(p, util::Mul(d, 6.f * mk)), util::Add(util::Add(p, util::Mul(d, -3.f * mk)), util::Mul(n, -5.f * mk)), util::Add(util::Add(p, util::Mul(d, -3.f * mk)), util::Mul(n, 5.f * mk)), A(ui::theme::kDawn));
		}
		else
		{
			const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 5.f);
			const float s5 = 5.f * mk;
			DrawCircleV(p, (6.f + pulse * 3.f) * mk, util::WithA(ui::theme::kDawn, 0.35f * (1.f - pulse) * alpha));
			DrawTriangle(Vector2{ p.x, p.y - s5 }, Vector2{ p.x - s5, p.y }, Vector2{ p.x + s5, p.y }, A(ui::theme::kDawn));
			DrawTriangle(Vector2{ p.x - s5, p.y }, Vector2{ p.x, p.y + s5 }, Vector2{ p.x + s5, p.y }, A(ui::theme::kDawn));
		}
	}
	// 나(바라보는 방향 화살표)
	{
		const Vector2 p = toMap(myPos.x - m_predX, myPos.y - m_predY);
		float ang = 0.f;
		if (me) ang = (me->faceBack ? -1.f : 1.f) * (me->faceRight ? 0.6f : 2.5f);
		const Vector2 d{ std::cos(ang) * mk, std::sin(ang) * mk };
		const Vector2 n{ -d.y, d.x };
		const Color wc = util::WithA(WHITE, std::min(1.f, alpha + 0.2f));
		DrawTriangle(Vector2{ p.x + d.x * 7, p.y + d.y * 7 }, Vector2{ p.x - d.x * 4 - n.x * 4, p.y - d.y * 4 - n.y * 4 }, Vector2{ p.x - d.x * 4 + n.x * 4, p.y - d.y * 4 + n.y * 4 }, wc);
		DrawTriangle(Vector2{ p.x + d.x * 7, p.y + d.y * 7 }, Vector2{ p.x - d.x * 4 + n.x * 4, p.y - d.y * 4 + n.y * 4 }, Vector2{ p.x - d.x * 4 - n.x * 4, p.y - d.y * 4 - n.y * 4 }, wc);
	}
	ui::EndClip();
}

// ─────────────────────────────────────────────────────────────────────────────
// 채팅
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawChat()
{
	const float H = m_uiH;
	const float w = std::max(220.f, std::min(440.f, HudLeft() - 36.f));   // 전투 HUD 판 왼쪽까지
	const Rectangle log{ 12.f, H - 292.f, w, 206.f };
	const bool hover = ui::Hovered(log);
	const bool open = m_chatOpen && ui::Focused() == &m_chatField;
	if (open || hover)
		ui::Plate(log, Color{ 12, 15, 18, 170 }, util::WithA(ui::theme::kLine, 0.9f), 4.f, 1.f);
	// 최신 줄이 아래
	const float size = 14.f;
	float y = log.y + log.height - 6.f;
	ui::BeginClip(Rectangle{ log.x, log.y, log.width, log.height });
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
	ui::EndClip();

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
void Game::DrawNpcPortrait(Rectangle pr, const WorldData::NpcRow& npc)
{
	ui::Plate(pr, ui::theme::kSurface1, BLANK, 4.f, 0.f);
	// 그린 초상화가 있으면 그것을(말하는 동안 아주 조금 숨 쉰다), 없으면(동물 등) NPC를 크게 다시 그린다
	if (const Texture2D* tex = art::Portrait(npc.id))
	{
		const float breath = 1.02f + 0.012f * std::sin(m_time * 1.5f + npc.id);
		ui::BeginClip(Rectangle{ pr.x + 1.f, pr.y + 1.f, pr.width - 2.f, pr.height - 2.f });
		art::DrawCover(*tex, Rectangle{ pr.x + 1.f, pr.y + 1.f, pr.width - 2.f, pr.height - 2.f }, WHITE, Vector2{ 0.5f, 0.42f }, breath);
		ui::EndClip();
		// 아래를 판 색으로 녹이고, 위에 옅은 빛(판에 끼운 그림처럼)
		const int gh = static_cast<int>(pr.height * 0.3f);
		DrawRectangleGradientV(static_cast<int>(pr.x + 1.f), static_cast<int>(pr.y + pr.height) - gh - 1, static_cast<int>(pr.width - 2.f), gh,
			util::WithA(ui::theme::kSurface0, 0.f), util::WithA(ui::theme::kSurface0, 0.85f));
		ui::Rule(pr.x + 5.f, pr.x + pr.width - 5.f, pr.y + 1.f, Color{ 255, 255, 255, 22 });
	}
	else
	{
		DrawRectangleGradientV(static_cast<int>(pr.x), static_cast<int>(pr.y), static_cast<int>(pr.width), static_cast<int>(pr.height), Color{ 60, 66, 70, 90 }, Color{ 10, 12, 14, 0 });
		ui::BeginClip(Rectangle{ pr.x, pr.y, pr.width, pr.height });
		{
			actors::Pose pose;
			const bool small = WorldData::IsAnimal(npc.kind) || npc.kind == WorldData::NpcKind::eChild;
			pose.base = Vector2{ pr.x + pr.width * 0.5f, pr.y + pr.height + (small ? -8.f : 18.f) };
			pose.scale = small ? 2.2f : 1.7f;
			pose.time = m_time;
			pose.faceRight = true;
			pose.seed = npc.id;
			gfx::BeginWorld(0.f, pose.scale);
			gfx::ResetMod();
			gfx::SetDepthRaw(-0.5f);
			actors::DrawNpc(pose, npc);
			gfx::Flush();
		}
		ui::EndClip();
	}
	ui::Plate(pr, BLANK, ui::theme::kLineStrong, 4.f, 1.f);
}

void Game::DrawNpcDialog()
{
	const auto* npc = WorldData::FindNpc(m_dialogNpc);
	if (!npc) { m_dialogNpc = 0; return; }
	if (m_dialogQuest != 0)
	{
		DrawQuestDialog(*npc);
		return;
	}

	// 이 NPC와 얽힌 퀘스트(보고 · 새 이야기 · 진행 중 · 곧 받을 수 있는 것)
	struct QuestEntry { const QuestData::QuestRow* q; QuestMark mark; };
	std::vector<QuestEntry> quests;
	for (const auto& v : m_quests)
		if (const auto* q = QuestData::Find(v.id); q && q->turnIn == npc->id)
			quests.push_back(QuestEntry{ q, QuestReady(v) ? QuestMark::Ready : QuestMark::Active });
	for (const auto& q : QuestData::kQuests)
	{
		if (q.giver != npc->id || ActiveQuest(q.id)) continue;
		const QuestAvail a = CanTakeQuest(q);
		if (a == QuestAvail::Yes) quests.push_back(QuestEntry{ &q, QuestMark::Available });
		else if (a == QuestAvail::LowLevel && (q.kind == QuestData::QuestKind::Main || q.minLevel <= m_level + 5)) quests.push_back(QuestEntry{ &q, QuestMark::Low });
		else if (a == QuestAvail::Cooldown) quests.push_back(QuestEntry{ &q, QuestMark::None });
	}
	std::stable_sort(quests.begin(), quests.end(), [](const QuestEntry& a, const QuestEntry& b) { return static_cast<int>(a.mark) > static_cast<int>(b.mark); });
	if (quests.size() > 4) quests.resize(4);
	const float extra = quests.empty() ? 0.f : quests.size() * 30.f + 6.f;

	const float W = m_uiW;
	const Rectangle r = wm::Window("npc", Rectangle{ W * 0.5f - 300.f, std::max(12.f, HudTop() - 14.f - (226.f + extra)), 600.f, 226.f + extra }, wm::kMove);   // 전투 HUD 위
	bool open = true;
	ui::Window(r, npc->name, &open);
	if (!open) { CloseNpcWindows(); return; }
	DrawNpcPortrait(Rectangle{ r.x + 16, r.y + 46, 120, 164 }, *npc);

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
	const Rectangle tr{ r.x + 154, r.y + 52, r.width - 174, 100 };
	{
		ui::FaceScope serif(ui::Face::Serif);
		ui::TextWrapped(visible, tr, 16.f, ui::theme::kTextHi, 8.f);
	}
	if (shown < static_cast<int>(cps.size()) && ui::Clicked(tr))
		m_dialogTextAt = m_now - 60.0;
	if (shown >= static_cast<int>(cps.size()))
	{
		// 다 읽었으면 작은 화살표가 깜빡인다
		const float blink = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 6.f);
		DrawTriangle(Vector2{ r.x + r.width - 30, r.y + 142 }, Vector2{ r.x + r.width - 24, r.y + 149 }, Vector2{ r.x + r.width - 18, r.y + 142 }, util::WithA(ui::theme::kDawn, blink));
	}

	// 퀘스트 목록 — 누르면 그 이야기를 펼친다
	for (size_t i = 0; i < quests.size(); ++i)
	{
		const auto& e = quests[i];
		const Rectangle er{ r.x + 152.f, r.y + 156.f + i * 30.f, r.width - 170.f, 27.f };
		const bool hover = ui::Hovered(er);
		const bool bright = e.mark == QuestMark::Ready || e.mark == QuestMark::Available;
		ui::Plate(er, hover ? ui::theme::kSurface3 : ui::theme::kSurface1, bright ? util::WithA(ui::theme::kDawn, hover ? 0.9f : 0.5f) : ui::theme::kLine, 3.f, 1.f);
		const char* glyph = e.mark == QuestMark::Ready || e.mark == QuestMark::Active ? "?" : (e.mark == QuestMark::None ? "…" : "!");
		{
			ui::TextStyle gs; gs.size = 16.f; gs.color = bright ? ui::theme::kDawnHi : ui::theme::kTextDim; gs.face = ui::Face::Bold;
			ui::Fonts().DrawCentered(glyph, Vector2{ er.x + 15.f, er.y + er.height * 0.5f }, gs);
		}
		{
			ui::TextStyle qs; qs.size = 14.f; qs.color = bright ? ui::theme::kTextHi : ui::theme::kText; qs.face = ui::Face::Strong;
			ui::Fonts().Draw(e.q->name, Vector2{ er.x + 30.f, er.y + 6.f }, qs);
		}
		std::string tag;
		switch (e.mark)
		{
		case QuestMark::Ready: tag = "보고"; break;
		case QuestMark::Available: tag = e.q->kind == QuestData::QuestKind::Main ? "메인 이야기" : (e.q->kind == QuestData::QuestKind::Daily ? "반복 의뢰" : (e.q->kind == QuestData::QuestKind::Class ? "직업의 길" : "부가 이야기")); break;
		case QuestMark::Active: tag = "진행 중"; break;
		case QuestMark::Low: tag = Fmt("Lv.%d부터", e.q->minLevel); break;
		default: tag = "내일 다시"; break;
		}
		ui::Text(tag, Vector2{ er.x + er.width - 10.f, er.y + 7.f }, ui::type::kCaption, bright ? ui::theme::kDawn : ui::theme::kTextDim, Align::Right);
		if (ui::Clicked(er))
		{
			OpenQuestInDialog(e.q->id);
			sfx::Play(sfx::Id::Open, 0.6f);
		}
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
		if (button(120, "옛길 이동", ButtonKind::Primary))
		{
			m_shopMode = ShopMode::Teleport;
			m_teleportSel = -1;
			// 퀘스트 목적지로 가는 길이면 미리 골라 둔다
			if (m_questDestValid && m_questDest.viaTeleporter)
				for (const auto& reg : WorldData::kRegions)
					if (reg.teleportFee > 0 && QuestWantsRegion(reg.zoneId)) { m_teleportSel = reg.zoneId; break; }
			sfx::Play(sfx::Id::Open, 0.6f);
		}
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
		const std::string label = remain > 0.0 ? Fmt("수프 (%.0f초)", remain) : std::string("수프 (30 그로시)");
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
	const float W = m_uiW, H = m_uiH;
	const Rectangle r = wm::Window("shop", Rectangle{ std::max(12.f, W * 0.5f - 470.f), std::max(12.f, H * 0.5f - 262.f), 440.f, std::min(H - 124.f, 504.f) }, wm::kMove);
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
		ui::Plate(it, sel ? ui::theme::kSurface3 : (hover ? ui::theme::kSurface2 : ui::theme::kSurface1), sel ? ui::theme::kDawn : ui::theme::kLine, 3.f, 1.f);
		ui::SlotFrame(Rectangle{ it.x + 5, it.y + 4, 36, 36 }, false, false);
		icons::Draw(id, Rectangle{ it.x + 7, it.y + 6, 32, 32 });
		{
			ui::TextStyle ns; ns.size = twoCols ? 13.5f : 14.5f; ns.color = icons::ItemNameColor(id); ns.face = ui::Face::Strong;
			ui::Fonts().Draw(row->displayName, Vector2{ it.x + 50, it.y + 5 }, ns);
		}
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
		else if (row->effect == ItemEffect::eTeleportRandom) desc = "지역 안 옛길 이동";
		else if (row->effect == ItemEffect::eEnchantWeapon) desc = "무기 각인 강화";
		ui::Text(desc, Vector2{ it.x + 50, it.y + 25 }, twoCols ? 11.5f : ui::type::kCaption, ui::theme::kTextDim);
		const bool affordable = m_inv.adena >= row->buyPrice;
		{
			ui::TextStyle ps; ps.size = twoCols ? 14.f : 16.f; ps.color = affordable ? ui::theme::kTextHi : ui::theme::kBad; ps.face = ui::Face::Number;
			const std::string price = texts::Number(row->buyPrice);
			const float pw = ui::Fonts().Measure(price, ps.size, 0.f, ui::Face::Number).x;
			const float py = it.y + (twoCols ? 5.f : 13.f);
			ui::Fonts().Draw(price, Vector2{ it.x + it.width - 10.f - pw, py }, ps);
			if (!twoCols) CoinIcon(Vector2{ it.x + it.width - 20.f - pw, py + ps.size * 0.5f + 1.f }, 5.5f);
		}
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
	ui::Text("수량", Vector2{ r.x + 16, fy + 11 }, ui::type::kBodyS, ui::theme::kTextDim);
	if (ui::Button(Rectangle{ r.x + 60, fy + 4, 34, 32 }, "-", ButtonKind::Normal, sel != nullptr)) m_buyCount = std::max(1, m_buyCount - 1);
	ui::Plate(Rectangle{ r.x + 98, fy + 4, 60, 32 }, Color{ 12, 15, 18, 235 }, ui::theme::kLine, 3.f, 1.f);
	{
		ui::TextStyle qs; qs.size = 17.f; qs.color = ui::theme::kTextHi; qs.face = ui::Face::Number;
		ui::Fonts().DrawCentered(std::to_string(m_buyCount), Vector2{ r.x + 128, fy + 20 }, qs);
	}
	if (ui::Button(Rectangle{ r.x + 162, fy + 4, 34, 32 }, "+", ButtonKind::Normal, sel != nullptr)) m_buyCount = std::min(maxCount, m_buyCount + 1);
	if (ui::Button(Rectangle{ r.x + 200, fy + 4, 48, 32 }, "×10", ButtonKind::Normal, sel != nullptr)) m_buyCount = std::min(maxCount, m_buyCount + 10);
	if (ui::Button(Rectangle{ r.x + 252, fy + 4, 52, 32 }, "최대", ButtonKind::Normal, sel != nullptr)) m_buyCount = maxCount;
	const uint64_t total = sel ? static_cast<uint64_t>(sel->buyPrice) * m_buyCount : 0;
	ui::Text("합계", Vector2{ r.x + 16, fy + 53 }, ui::type::kBodyS, ui::theme::kTextDim);
	CoinIcon(Vector2{ r.x + 58, fy + 62 }, 6.f);
	{
		ui::TextStyle ts; ts.size = 19.f; ts.color = total <= m_inv.adena ? ui::theme::kTextHi : ui::theme::kBad; ts.face = ui::Face::Number;
		ui::Fonts().Draw(texts::Number(static_cast<long long>(total)), Vector2{ r.x + 70, fy + 50 }, ts);
	}
	ui::Text("보유 " + texts::Number(m_inv.adena), Vector2{ r.x + 16, fy + 76 }, ui::type::kCaption, ui::theme::kTextDim);
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
	const float W = m_uiW, H = m_uiH;
	m_invOpen = true;
	const Rectangle r = wm::Window("sell", Rectangle{ std::max(12.f, W * 0.5f - 420.f), std::max(12.f, H * 0.5f - 180.f), 400.f, 300.f }, wm::kMove);
	bool open = true;
	ui::Window(r, std::string(npc->name) + " — 판매", &open);
	if (!open) { CloseNpcWindows(); return; }

	if (m_sellSlot < 0 || m_inv.slots[m_sellSlot].Empty())
	{
		m_sellSlot = -1;
		ui::TextWrapped("오른쪽 소지품 창에서 팔 물건을 클릭하세요.\n착용 중인 장비와 그로시는 팔 수 없습니다.", Rectangle{ r.x + 24, r.y + 70, r.width - 48, 120 }, 16.f, ui::theme::kText, 8.f);
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
		{
			ui::TextStyle ns; ns.size = 16.f; ns.color = icons::ItemNameColor(s.itemId, s.extra); ns.face = ui::Face::Strong;
			ui::Fonts().Draw(texts::ItemNameFull(s.itemId, s.enchant, s.extra), Vector2{ r.x + 98, r.y + 58 }, ns);
		}
		ui::Text("개당 " + texts::Number(unit) + " 그로시" + (row && row->type == ItemType::eEquipment && rar > Rarity::Common
			? Fmt("  (%s ×%.1f)", ItemRules::RarityName(rar), ItemRules::kSellPercent[ItemRules::Idx(rar)] / 100.f) : std::string()),
			Vector2{ r.x + 98, r.y + 86 }, ui::type::kBodyS, ui::theme::kText);
		m_sellCount = std::clamp(m_sellCount, 1, static_cast<int>(s.count));
		const float fy = r.y + 136;
		ui::Text("수량", Vector2{ r.x + 20, fy + 8 }, ui::type::kBodyS, ui::theme::kTextDim);
		if (ui::Button(Rectangle{ r.x + 64, fy, 34, 32 }, "-")) m_sellCount = std::max(1, m_sellCount - 1);
		ui::Plate(Rectangle{ r.x + 102, fy, 60, 32 }, Color{ 12, 15, 18, 235 }, ui::theme::kLine, 3.f, 1.f);
		{
			ui::TextStyle qs; qs.size = 17.f; qs.color = ui::theme::kTextHi; qs.face = ui::Face::Number;
			ui::Fonts().DrawCentered(std::to_string(m_sellCount), Vector2{ r.x + 132, fy + 16 }, qs);
		}
		if (ui::Button(Rectangle{ r.x + 166, fy, 34, 32 }, "+")) m_sellCount = std::min(static_cast<int>(s.count), m_sellCount + 1);
		if (ui::Button(Rectangle{ r.x + 204, fy, 52, 32 }, "전부")) m_sellCount = s.count;
		const uint64_t total = static_cast<uint64_t>(unit) * m_sellCount;
		ui::Text("받을 금액", Vector2{ r.x + 20, fy + 56 }, ui::type::kBodyS, ui::theme::kTextDim);
		CoinIcon(Vector2{ r.x + 92, fy + 65 }, 6.f);
		{
			ui::TextStyle ts; ts.size = 19.f; ts.color = ui::theme::kTextHi; ts.face = ui::Face::Number;
			ui::Fonts().Draw(texts::Number(static_cast<long long>(total)), Vector2{ r.x + 104, fy + 53 }, ts);
		}
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
	const float W = m_uiW, H = m_uiH;
	const Rectangle r = wm::Window("tele", Rectangle{ W * 0.5f - 280.f, std::max(12.f, H * 0.5f - 300.f), 560.f, std::min(H - 130.f, 600.f) }, wm::kMove);
	bool open = true;
	ui::Window(r, "옛길 이동 — 목적지", &open);
	if (!open) { CloseNpcWindows(); return; }

	float y = r.y + 44;
	const float rowH = std::min(32.f, (r.height - 150.f) / 15.f);
	for (const auto& reg : WorldData::kRegions)
	{
		if (reg.teleportFee == 0) continue;   // 마을 지역
		const Rectangle it{ r.x + 12, y, r.width - 24, rowH - 3 };
		const bool hover = ui::Hovered(it), sel = m_teleportSel == reg.zoneId;
		// 옛길(청록)이 이 창의 강조색 — 선택한 줄만 청록 테두리
		ui::Plate(it, sel ? ui::theme::kSurface3 : (hover ? ui::theme::kSurface2 : ui::theme::kSurface1), sel ? ui::theme::kOldway : ui::theme::kLine, 3.f, 1.f);
		const float ty = it.y + (it.height - 14.5f) * 0.5f;
		{
			ui::TextStyle ns; ns.size = 14.5f; ns.color = sel ? ui::theme::kTextHi : ui::theme::kText; ns.face = ui::Face::Strong;
			ui::Fonts().Draw(reg.name, Vector2{ it.x + 12, ty }, ns);
		}
		Stars(Vector2{ it.x + 150, it.y + it.height * 0.5f }, reg.tier, 8.f);
		if (reg.bossSpecies)
			ui::Text("보스 " + texts::MonsterName(reg.bossSpecies), Vector2{ it.x + 222, ty + 1 }, ui::type::kCaption, Rgb(0xC88A74));
		if (QuestWantsRegion(reg.zoneId))
		{
			const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 4.f);
			ui::Text("◆ 퀘스트", Vector2{ it.x + it.width - 96, ty + 1 }, ui::type::kCaption, util::Mix(ui::theme::kDawn, ui::theme::kDawnHi, pulse), Align::Right);
		}
		const bool affordable = m_inv.adena >= reg.teleportFee;
		{
			ui::TextStyle fs; fs.size = 15.f; fs.color = affordable ? ui::theme::kTextHi : ui::theme::kBad; fs.face = ui::Face::Number;
			const std::string fee = texts::Number(reg.teleportFee);
			ui::Fonts().Draw(fee, Vector2{ it.x + it.width - 12.f - ui::Fonts().Measure(fee, fs.size, 0.f, ui::Face::Number).x, ty }, fs);
		}
		ui::Block(it);
		if (ui::Clicked(it)) { m_teleportSel = reg.zoneId; sfx::Play(sfx::Id::Click, 0.6f); }
		y += rowH;
	}
	const float fy = r.y + r.height - 60;
	ui::Divider(r.x + 12, r.x + r.width - 12, fy - 8);
	ui::Text("보유", Vector2{ r.x + 16, fy + 14 }, ui::type::kBodyS, ui::theme::kTextDim);
	CoinIcon(Vector2{ r.x + 56, fy + 23 }, 6.f);
	{
		ui::TextStyle ms; ms.size = 17.f; ms.color = ui::theme::kTextHi; ms.face = ui::Face::Number;
		ui::Fonts().Draw(texts::Number(m_inv.adena), Vector2{ r.x + 68, fy + 12 }, ms);
	}
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
	const float W = m_uiW, H = m_uiH;
	DrawRectangleRec(Rectangle{ 0, 0, m_uiW, m_uiH }, Color{ 0, 0, 0, 120 });
	const Rectangle r{ W * 0.5f - 230.f, H * 0.5f - 190.f, 460.f, 360.f };
	ui::Panel(r);
	{
		ui::TextStyle ts; ts.size = ui::type::kDisplayS; ts.color = ui::theme::kTextHi; ts.face = ui::Face::Display; ts.spacing = 1.f;
		ui::Fonts().DrawCentered("각인 강화", Vector2{ r.x + r.width * 0.5f, r.y + 26 }, ts);
		ui::Rule(r.x + r.width * 0.5f - 60.f, r.x + r.width * 0.5f + 60.f, r.y + 44.f, ui::theme::kLine);
		DrawRectangleRec(Rectangle{ std::round(r.x + r.width * 0.5f - 11.f), std::round(r.y + 43.f), 22.f, 2.f }, ui::theme::kDawn);
	}
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
		{
			ui::TextStyle ns; ns.size = 16.f; ns.color = icons::ItemNameColor(m_enchant.itemId); ns.face = ui::Face::Strong;
			ui::Fonts().DrawCentered(texts::ItemNameEnchanted(m_enchant.itemId, before) + "  →  +" + std::to_string(before + 1), Vector2{ c.x, c.y + 68 }, ns);
		}
		const Color cc = chance >= 100 ? ui::theme::kGood : (chance >= 30 ? ui::theme::kWarn : ui::theme::kBad);
		{
			ui::TextStyle ps; ps.size = 13.f; ps.color = ui::theme::kTextDim; ps.face = ui::Face::Body;
			ui::Fonts().DrawCentered("성공 확률", Vector2{ c.x - 26.f, c.y + 97 }, ps);
			ui::TextStyle vs; vs.size = 20.f; vs.color = cc; vs.face = ui::Face::Number;
			ui::Fonts().DrawCentered(Fmt("%u%%", chance), Vector2{ c.x + 30.f, c.y + 96 }, vs);
		}
		std::string note = chance >= 100 ? "안전한 단계입니다(+6까지 실패 없음)." : "실패하면 무기가 부서집니다.";
		if (blessed) note += "  축성: 크게 오를 수 있음";
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
		ui::TextShadow("각인이 날에 스며듭니다…", Vector2{ c.x, c.y + 80 }, 17.f, Rgb(0xFFE8B0), Align::Center);
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
		st.size = 28.f * pop;
		st.color = ui::theme::kDawnHi;
		st.shadow = 2.f;
		st.shadowColor = Color{ 0, 0, 0, 160 };
		st.face = ui::Face::Display;
		st.spacing = 2.f;
		ui::Fonts().DrawCentered("각인 성공", Vector2{ c.x, c.y + 78 }, st);
		ui::TextStyle ns; ns.size = 16.f; ns.color = icons::ItemNameColor(m_enchant.itemId); ns.face = ui::Face::Strong;
		ui::Fonts().DrawCentered(texts::ItemNameEnchanted(m_enchant.itemId, m_enchant.after), Vector2{ c.x, c.y + 110 }, ns);
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
		st.color = ui::theme::kBad;
		st.shadow = 2.f;
		st.shadowColor = Color{ 0, 0, 0, 160 };
		st.face = ui::Face::Display;
		st.spacing = 2.f;
		ui::Fonts().DrawCentered("각인 실패", Vector2{ c.x, c.y + 60 }, st);
		ui::TextShadow(texts::Josa(texts::ItemNameEnchanted(m_enchant.itemId, m_enchant.before), "이", "가") + " 부서졌습니다.", Vector2{ c.x, c.y + 94 }, 15.f, ui::theme::kTextDim, Align::Center);
	}
	if (ui::Button(Rectangle{ r.x + r.width * 0.5f - 80, r.y + r.height - 60, 160, 44 }, "확인", ButtonKind::Primary) || t > 4.f)
		m_enchant = EnchantFlow{};
}

void Game::DrawDiscard()
{
	const InvSlot& s = m_inv.slots[m_discard.slot];
	if (s.Empty()) { m_discard.open = false; return; }
	const float W = m_uiW, H = m_uiH;
	const Rectangle r{ W * 0.5f - 180.f, H * 0.5f - 100.f, 360.f, 200.f };
	ui::Window(r, "버리기", nullptr);
	ui::SlotFrame(Rectangle{ r.x + 18, r.y + 50, 52, 52 }, false, false, SlotRarity(s) != Rarity::Common ? icons::RarityColor(SlotRarity(s)) : ui::theme::kBorder);
	icons::Draw(s.itemId, Rectangle{ r.x + 20, r.y + 52, 48, 48 });
	{
		ui::TextStyle ns; ns.size = 15.f; ns.color = icons::ItemNameColor(s.itemId, s.extra); ns.face = ui::Face::Strong;
		ui::Fonts().Draw(texts::ItemNameFull(s.itemId, s.enchant, s.extra), Vector2{ r.x + 80, r.y + 52 }, ns);
	}
	if (s.equipped || SlotRarity(s) >= Rarity::Rare || s.enchant > 0)
		ui::Text(s.equipped ? "착용 중인 장비입니다. 정말 버릴까요?" : std::string(ItemRules::RarityName(SlotRarity(s))) + " 등급 장비입니다. 정말 버릴까요?",
			Vector2{ r.x + 80, r.y + 74 }, ui::type::kCaption, ui::theme::kBad);
	m_discard.count = std::clamp(m_discard.count, 1, static_cast<int>(s.count));
	if (ui::Button(Rectangle{ r.x + 80, r.y + 96, 34, 30 }, "-")) m_discard.count = std::max(1, m_discard.count - 1);
	{
		ui::TextStyle cs; cs.size = 16.f; cs.color = ui::theme::kTextHi; cs.face = ui::Face::Number;
		ui::Fonts().DrawCentered(std::to_string(m_discard.count) + " / " + std::to_string(s.count), Vector2{ r.x + 160, r.y + 111 }, cs);
	}
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
	const float W = m_uiW, H = m_uiH;
	const float t = static_cast<float>(m_now - m_deadAt);
	const float a = util::Clamp01(t / 1.2f);
	// 화면이 먹빛으로 가라앉고, 가운데 띠에만 붉은 기운이 남는다
	DrawRectangleRec(Rectangle{ 0, 0, m_uiW, m_uiH }, Color{ 8, 6, 8, static_cast<unsigned char>(170 * a) });
	const unsigned char band = static_cast<unsigned char>(90 * a);
	DrawRectangleGradientV(0, static_cast<int>(H * 0.22f), static_cast<int>(std::ceil(m_uiW)), static_cast<int>(H * 0.14f), Color{ 70, 16, 12, 0 }, Color{ 70, 16, 12, band });
	DrawRectangleGradientV(0, static_cast<int>(H * 0.36f), static_cast<int>(std::ceil(m_uiW)), static_cast<int>(H * 0.14f), Color{ 70, 16, 12, band }, Color{ 70, 16, 12, 0 });
	if (t < 0.6f) return;
	const float k = util::EaseOutCubic(util::Clamp01((t - 0.6f) / 0.6f));
	ui::TextStyle st;
	st.size = 44.f;
	st.color = util::MulA(Rgb(0xD0584A), k);
	st.shadow = 3.f;
	st.shadowColor = util::MulA(Color{ 0, 0, 0, 200 }, k);
	st.face = ui::Face::Display;
	st.spacing = 4.f;
	ui::Fonts().DrawCentered("쓰러졌습니다", Vector2{ W * 0.5f, H * 0.36f - (1.f - k) * 16.f }, st);
	ui::Rule(W * 0.5f - 110.f, W * 0.5f + 110.f, H * 0.36f + 30.f, util::MulA(Color{ 208, 88, 74, 120 }, k));
	const int remain = std::max(0, 30 - static_cast<int>(t));
	ui::Text(Fmt("%d초 뒤 %s에서 다시 눈을 뜹니다.", remain, WorldData::kTownName), Vector2{ W * 0.5f, H * 0.36f + 44.f }, 15.f, util::MulA(ui::theme::kText, k), Align::Center, true);
	if (t > 1.2f)
	{
		const Rectangle b{ W * 0.5f - 130.f, H * 0.36f + 84.f, 260.f, 50.f };
		if (ui::Button(b, m_restartSent ? "돌아가는 중…" : "데니차에서 다시 시작", ButtonKind::Primary, !m_restartSent, 19.f))
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
	const float W = m_uiW;
	float y = 84.f;
	y += DrawQuestStackCards(y);
	for (const auto& b : m_banners)
	{
		const float age = static_cast<float>(m_now - b.start);
		const float in = util::EaseOutBack(util::Clamp01(age / 0.35f));
		const float out = age > b.life - 0.5f ? 1.f - (age - (b.life - 0.5f)) / 0.5f : 1.f;
		const float a = util::Clamp01(out);
		const float yy = y - (1.f - in) * 30.f;
		// 알림 띠: 가운데만 짙은 먹빛 + 아래 가는 선 한 줄(색은 알림 종류)
		const float w = std::min(W - 40.f, ui::Fonts().Measure(b.text, 19.f, 1.f, ui::Face::Display).x + 200.f);
		DrawRectangleGradientH(static_cast<int>(W * 0.5f - w * 0.5f), static_cast<int>(yy), static_cast<int>(w * 0.5f), 38, Color{ 10, 12, 15, 0 }, util::MulA(Color{ 10, 12, 15, 215 }, a));
		DrawRectangleGradientH(static_cast<int>(W * 0.5f), static_cast<int>(yy), static_cast<int>(w * 0.5f), 38, util::MulA(Color{ 10, 12, 15, 215 }, a), Color{ 10, 12, 15, 0 });
		DrawRectangleGradientH(static_cast<int>(W * 0.5f - w * 0.35f), static_cast<int>(yy + 37), static_cast<int>(w * 0.35f), 1, util::MulA(b.color, 0.f), util::MulA(b.color, a * 0.8f));
		DrawRectangleGradientH(static_cast<int>(W * 0.5f), static_cast<int>(yy + 37), static_cast<int>(w * 0.35f), 1, util::MulA(b.color, a * 0.8f), util::MulA(b.color, 0.f));
		ui::TextStyle st;
		st.size = 19.f;
		st.color = util::MulA(util::Mix(b.color, ui::theme::kTextHi, 0.35f), a);
		st.shadow = 1.f;
		st.shadowColor = util::MulA(Color{ 0, 0, 0, 200 }, a);
		st.face = ui::Face::Display;
		st.spacing = 1.f;
		ui::Fonts().DrawCentered(b.text, Vector2{ W * 0.5f, yy + 19.f }, st);
		y += 44.f;
	}
	m_bannersBottom = y;
}

void Game::DrawToasts()
{
	const float W = m_uiW;
	float y = std::max(262.f, m_trackerBottom + 6.f);   // 퀘스트 추적 표시 아래
	const float x1 = m_invOpen ? m_invRect.x - 12.f : W - 14.f;
	for (const auto& t : m_toasts)
	{
		const float age = static_cast<float>(m_now - t.start);
		const float in = util::EaseOutCubic(util::Clamp01(age / 0.25f));
		const float a = age > 3.f ? 1.f - (age - 3.f) / 0.5f : 1.f;
		const float w = ui::Fonts().Measure(t.text, 14.f, 0.f, ui::Face::Strong).x + 62.f;
		const Rectangle r{ std::round(x1 - w + (1.f - in) * 60.f), y, w, 38.f };
		ui::Plate(r, util::MulA(Color{ 15, 18, 22, 228 }, a), util::MulA(ui::theme::kLine, a), 3.f, 1.f);
		DrawRectangleRec(Rectangle{ r.x + 1, r.y + 4, 2, r.height - 8 }, util::MulA(t.color, a));
		icons::Draw(t.itemId, Rectangle{ r.x + 8, r.y + 3, 32, 32 }, util::MulA(WHITE, a));
		ui::TextStyle ts; ts.size = 14.f; ts.color = util::MulA(t.color, a); ts.face = ui::Face::Strong;
		ui::Fonts().Draw(t.text, Vector2{ r.x + 46, r.y + 11 }, ts);
		y += 42.f;
	}
}

void Game::DrawHelp()
{
	const float W = m_uiW, H = m_uiH;
	const Rectangle r = wm::Window("help", Rectangle{ W * 0.5f - 330.f, std::max(8.f, H * 0.5f - 270.f), 660.f, 540.f }, wm::kMove);
	ui::Window(r, "도움말", &m_helpOpen);
	using kb::Act;
	auto L = [](Act a) { return kb::Label(a); };
	const std::pair<std::string, std::string> rows[] = {
		{ "좌클릭", "이동 — 누르고 있으면 커서 쪽으로 계속. 적 = 다가가 계속 공격 · NPC = 대화 · 물건 = 줍기" },
		{ "우클릭 · " + L(Act::BasicAttack), "기본 공격(커서 쪽 적). [" + L(Act::BasicAttack) + "] 키를 누르고 있으면 가까운 적을 이어서" },
		{ L(Act::Skill1) + L(Act::Skill2) + L(Act::Skill3) + L(Act::Skill4) + " · " + L(Act::Skill5) + L(Act::Skill6) + L(Act::Skill7) + L(Act::Skill8),
		  "기술 8칸 — 커서 방향 · 커서 지점 · 커서에 가까운 적으로(사거리 밖이면 다가가서)" },
		{ L(Act::Awaken) + " · " + L(Act::Mobility) + " · " + L(Act::Identity), "각성기 · 이동기 · 고유 기술" },
		{ L(Act::Battle1) + "~" + L(Act::Battle4) + " · " + L(Act::Potion) + " · " + L(Act::Aux1) + "~" + L(Act::Aux4), "전투 칸 · 체력 물약 · 보조 칸 (소지품에서 끌어다 등록)" },
		{ L(Act::Interact), "가장 가까운 NPC와 대화 · 물건 줍기 (머리 위에 표시)" },
		{ L(Act::ForceAttack) + " + 공격 · 기술", "다른 모험가 공격(PK) — 마을 밖에서만, 성향 벌칙 주의" },
		{ L(Act::Inventory) + " " + L(Act::Character) + " " + L(Act::SkillWindow) + " " + L(Act::QuestLog) + " " + L(Act::WorldMap) + " " + L(Act::BigMap),
		  "소지품 · 캐릭터 · 기술 · 퀘스트 일지 · 월드 지도 · 확장 미니맵" },
		{ L(Act::Chat) + " · " + L(Act::Command), "채팅 · 명령어 (&로 시작하면 전체 채팅)" },
		{ L(Act::ShowNames) + " · 휠", "이름 모두 보기 · 확대/축소" },
		{ "Esc", "창 닫기 → 메뉴(조작 설정에서 모든 키를 바꿀 수 있다)" },
	};
	float y = r.y + 52;
	for (const auto& [k, v] : rows)
	{
		ui::TextStyle ks; ks.size = 14.f; ks.color = ui::theme::kTextHi; ks.face = ui::Face::Strong;
		ui::Fonts().Draw(k, Vector2{ r.x + 24, y }, ks);
		const float h = ui::TextWrapped(v, Rectangle{ r.x + 200, y + 1.f, r.width - 224, 40 }, ui::type::kBodyS, ui::theme::kText, 3.f);
		y += std::max(28.f, h + 10.f);
	}
	ui::Divider(r.x + 20, r.x + r.width - 20, y + 4);
	ui::TextWrapped("데니차 성벽 안은 안전지대입니다. 길잡이 마테오에게 가면 옛길로 다른 지역에 갈 수 있고, "
		"잡화상 라다와 대장장이 도브란에게서 물약·두루마리·장비를 사고팔 수 있습니다.",
		Rectangle{ r.x + 24, y + 14, r.width - 48, 80 }, 14.f, ui::theme::kTextDim, 5.f);
}

void Game::DrawWorldMap()
{
	const float W = m_uiW, H = m_uiH;
	const float cell = std::min(132.f, (std::min(W, H) - 140.f) / 4.f);
	const Rectangle r = wm::Window("map", Rectangle{ W * 0.5f - (cell * 4 + 40) * 0.5f, H * 0.5f - (cell * 4 + 90) * 0.5f, cell * 4 + 40, cell * 4 + 90 }, wm::kMove);
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
		// 지역 칸: 위쪽을 조금 어둡게 눌러 이름이 읽히게
		DrawRectangleGradientV(static_cast<int>(c.x), static_cast<int>(c.y), static_cast<int>(c.width), 44, Color{ 8, 10, 12, 150 }, Color{ 8, 10, 12, 0 });
		DrawRectangleLinesEx(c, hover ? 2.f : 1.f, hover ? ui::theme::kDawn : Color{ 0, 0, 0, 130 });
		{
			ui::TextStyle ns; ns.size = 14.f; ns.color = ui::theme::kTextHi; ns.face = ui::Face::Display; ns.shadow = 1.f; ns.shadowColor = Color{ 0, 0, 0, 200 };
			ui::Fonts().Draw(reg.name, Vector2{ c.x + 8, c.y + 7 }, ns);
		}
		Stars(Vector2{ c.x + 13, c.y + 32 }, reg.tier, 7.f);
		const std::string feeLine = "옛길 이동 " + texts::Number(reg.teleportFee) + " 그로시";
		if (reg.bossSpecies)
		{
			const Vector2 bc{ c.x + c.width - 14, c.y + c.height - 14 };
			DrawTriangle(Vector2{ bc.x, bc.y - 7 }, Vector2{ bc.x - 5, bc.y }, Vector2{ bc.x + 5, bc.y }, ui::theme::kEmber);
			DrawTriangle(Vector2{ bc.x - 5, bc.y }, Vector2{ bc.x, bc.y + 7 }, Vector2{ bc.x + 5, bc.y }, ui::theme::kEmber);
			if (hover)
				ui::Tooltip({ { reg.name, ui::theme::kTextHi, 16.f, ui::Face::Display },
					{ texts::MonsterName(reg.bossSpecies) + " — " + MonsterTable::BossEpithet(reg.bossSpecies), Rgb(0xD08A74), 13.f },
					{ feeLine, ui::theme::kText, 13.f } });
		}
		else if (hover && reg.teleportFee > 0)
			ui::Tooltip({ { reg.name, ui::theme::kTextHi, 16.f, ui::Face::Display }, { feeLine, ui::theme::kText, 13.f } });
		if (reg.zoneId == WorldData::kTownZoneId)
		{
			const Vector2 tp{ c.x + (WorldData::kTownX0 + 22 - 500.f) / 500.f * cell, c.y + (WorldData::kTownY0 + 22 - 500.f) / 500.f * cell };
			DrawRectangleV(Vector2{ tp.x - 7, tp.y - 5 }, Vector2{ 14, 10 }, Rgb(0xC8B8A0));
			DrawTriangle(Vector2{ tp.x - 8, tp.y - 5 }, Vector2{ tp.x + 8, tp.y - 5 }, Vector2{ tp.x, tp.y - 12 }, Rgb(0xB8483A));
			ui::Text(WorldData::kTownName, Vector2{ tp.x, tp.y + 8 }, 12.f, ui::theme::kTextHi, Align::Center, true);
		}
	}
	// 퀘스트: 사냥할 지역은 금빛 테두리, 이야기 속 장소는 ◆
	if (const QuestView* fq = FocusQuest())
		if (const auto* q = QuestData::Find(fq->id))
			for (int i = 0; i < QuestData::kMaxObjs; ++i)
			{
				const auto& o = q->objs[i];
				if (o.zone == QuestData::kAnyZone || fq->progress[static_cast<size_t>(i)] >= o.count) continue;
				if (o.kind != QuestData::ObjKind::Kill && o.kind != QuestData::ObjKind::KillIn && o.kind != QuestData::ObjKind::Collect && o.kind != QuestData::ObjKind::Boss) continue;
				const Rectangle c{ x0 + (o.zone % 4) * cell, y0 + (o.zone / 4) * cell, cell - 3, cell - 3 };
				const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 3.f);
				DrawRectangleLinesEx(c, 2.f + pulse, util::WithA(ui::theme::kDawn, 0.5f + 0.4f * pulse));
			}
	for (const auto& poi : QuestData::kPois)
	{
		const Vector2 pp{ x0 + poi.x / 500.f * cell, y0 + poi.y / 500.f * cell };
		const bool dest = m_questDestValid && m_questDest.poi && static_cast<int>(m_questDest.tile.x) == poi.x && static_cast<int>(m_questDest.tile.y) == poi.y;
		const float s = dest ? 6.f + std::sin(static_cast<float>(m_now) * 5.f) * 1.5f : 4.f;
		const Color col = dest ? ui::theme::kDawnHi : Rgb(0xE8DCC0, 200);
		DrawTriangle(Vector2{ pp.x, pp.y - s }, Vector2{ pp.x - s, pp.y }, Vector2{ pp.x + s, pp.y }, col);
		DrawTriangle(Vector2{ pp.x - s, pp.y }, Vector2{ pp.x, pp.y + s }, Vector2{ pp.x + s, pp.y }, col);
		const Rectangle hit{ pp.x - 8.f, pp.y - 8.f, 16.f, 16.f };
		if (dest || ui::Hovered(hit))
			ui::Text(poi.name, Vector2{ pp.x, pp.y + 7.f }, 11.f, col, Align::Center, true);
		if (ui::Hovered(hit))
			ui::Tooltip({ { poi.name, ui::theme::kTextHi, 16.f, ui::Face::Display }, { poi.flavor, ui::theme::kText, 13.f, ui::Face::Serif } });
	}
	if (m_questDestValid && !m_questDest.poi)
	{
		const Vector2 dp{ x0 + m_questDest.tile.x / 500.f * cell, y0 + m_questDest.tile.y / 500.f * cell };
		const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 5.f);
		DrawCircleLinesV(dp, 6.f + pulse * 4.f, ui::theme::kDawn);
		DrawCircleV(dp, 3.f, ui::theme::kDawn);
	}
	if (m_questDestValid)
		ui::Text("목적지 · " + m_questDest.label, Vector2{ r.x + r.width * 0.5f, r.y + r.height - 54 }, ui::type::kBodyS, ui::theme::kDawn, Align::Center);

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
	const SkillDef* def = MyDef(row.id);   // Phase 2-D: 켜진 갈래를 입힌 값(재사용 대기 · 준비)
	const SkillGrowth::State grow = MyGrow(row.id);
	const bool basicRow = def ? def->kind == SkillKind::Basic : row.slot == 0;
	lines.push_back({ SkillGrowth::Growable(row.id) ? Fmt("%s  Lv.%d", row.name, grow.level) : std::string(row.name), util::Mix(cc, ui::theme::kTextHi, 0.35f), 17.f, ui::Face::Strong });
	if (def && def->kindText[0])
		lines.push_back({ def->kindText, util::Mix(cc, ui::theme::kText, 0.5f), 12.f });
	{
		std::string keyLine;
		if (basicRow)
			keyLine = "기본 공격 · " + kb::Label(kb::Act::BasicAttack) + " · 우클릭 (누르고 있으면 가까운 적을 이어서)";
		else if (const int hot = HotOfSkill(row.id); hot > 0)
			keyLine = "단축키 " + kb::Label(kb::HotAct(hot)) + " · 커서 쪽으로";
		else
			keyLine = "기술 칸에 없음 — 기술 창(" + kb::Label(kb::Act::SkillWindow) + ")에서 끌어다 놓기";
		lines.push_back({ keyLine, ui::theme::kTextDim, 12.f });
	}
	const bool magic = row.kind == DamageKind::Magic;
	const int power = magic ? m_stats.magicAttack : m_stats.attack;
	std::string cost;
	if (row.mp > 0) cost += Fmt("MP %d · ", row.mp);
	if (basicRow) cost += Fmt("간격 %.2f초", SkillTable::BasicIntervalMs(m_classId, m_stats.attackSpeed) / 1000.f);
	else
	{
		cost += Fmt("재사용 %.1f초", (def ? SkillDefTable::CooldownMs(*def, row, m_stats.cooldownReduction) : SkillTable::CooldownMs(row, m_stats.cooldownReduction)) / 1000.f);
	}
	const int range = def && def->range > 0 ? def->range : row.range;
	if (range > 0) cost += Fmt(" · 사거리 %d칸", range);
	if (def && def->castMs > 0 && def->kind == SkillKind::Cast) cost += Fmt(" · 준비 %.1f초", def->castMs / 1000.f);
	else if (row.castMs > 0) cost += Fmt(" · 예고 %.1f초", row.castMs / 1000.f);
	if (def && def->kind == SkillKind::Identity) cost += Fmt(" · %s 가득 필요", m_classId == ClassId::Knight ? "서약" : (m_classId == ClassId::Engraver ? "새김" : "그늘"));
	lines.push_back({ "---", WHITE });
	lines.push_back({ cost, ui::theme::kInfo, 13.f });
	const GearExtras gx = MyGearExtras();
	const int boost = row.id < sizeof(gx.skillBoost) ? gx.skillBoost[row.id] : 0;
	if (row.coeff > 0)
	{
		const int each = static_cast<int>(power * row.coeff / 100 * (100 + boost) / 100 * SkillGrowth::DamagePercent(grow.level) / 100u);
		lines.push_back({ row.hits > 1 ? Fmt("피해 약 %d × %d회 (%s)", each, row.hits, magic ? "마법" : "물리") : Fmt("피해 약 %d (%s)", each, magic ? "마법" : "물리"), ui::theme::kTextHi, 13.f });
	}
	if (boost > 0)
		lines.push_back({ Fmt("◈ 장비 기술 강화 +%d%%", boost), ui::theme::kDawn, 13.f });
	if (grow.level > 1)
		lines.push_back({ Fmt("기술 레벨 %d · 피해 +%u%%", grow.level, SkillGrowth::DamagePercent(grow.level) - 100u), ui::theme::kDawnHi, 13.f });
	for (int tier = 0; tier < 2; ++tier)
		if (const SkillGrowth::Variant* v = SkillGrowth::ActiveVariant(row.id, grow, tier))
			lines.push_back({ Fmt("%s · %s — %s", SkillGrowth::TierName(tier), v->name, v->desc), v->color ? util::Mix(Rgb(v->color), ui::theme::kTextHi, 0.4f) : ui::theme::kOldway, 13.f });
	lines.push_back({ row.desc, ui::theme::kText, 13.f });
	if (row.combo && row.combo[0])
		lines.push_back({ std::string("연계 · ") + row.combo, ui::theme::kDawn, 13.f });
	if (m_level < row.unlockLevel)
		lines.push_back({ Fmt("Lv.%d에 배울 수 있습니다", row.unlockLevel), ui::theme::kBad, 13.f });
	ui::Tooltip(std::move(lines));
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
		ui::Plate(b, util::Mix(ui::theme::kSurface1, col, 0.18f), BLANK, 3.f, 0.f);
		glyph(Vector2{ b.x + S * 0.5f, b.y + S * 0.5f }, S * 0.32f, col);
		if (total > 0.f && remain > 0.f)
		{
			const float frac = util::Clamp01(1.f - remain / total);
			ui::BeginClip(Rectangle{ b.x, b.y, b.width, b.height });
			DrawCircleSector(Vector2{ b.x + S * 0.5f, b.y + S * 0.5f }, S * 0.75f, -90.f, -90.f + 360.f * frac, 24, Color{ 0, 0, 0, 120 });
			ui::EndClip();
		}
		// 곧 끝나면 깜빡인다
		const bool ending = remain > 0.f && remain < 2.f;
		const float blink = ending ? 0.5f + 0.5f * std::sin(nowf * 14.f) : 1.f;
		ui::Plate(b, BLANK, util::WithA(col, 0.35f + 0.55f * blink), 3.f, 1.f);
		if (stacks > 1)
		{
			ui::TextStyle ss; ss.size = 12.f; ss.color = ui::theme::kTextHi; ss.outline = 1.2f; ss.face = ui::Face::Number;
			const std::string sstr = std::to_string(stacks);
			ui::Fonts().Draw(sstr, Vector2{ b.x + S - 2.f - ui::Fonts().Measure(sstr, ss.size, 0.f, ui::Face::Number).x, b.y + S - 13.f }, ss);
		}
		if (remain > 0.f)
		{
			ui::TextStyle rs; rs.size = 11.5f; rs.color = ui::theme::kTextDim; rs.outline = 1.f; rs.face = ui::Face::Number;
			ui::Fonts().DrawCentered(Fmt("%.0f", std::ceil(remain)), Vector2{ b.x + S * 0.5f, b.y + S + 8.f }, rs);
		}
		if (ui::Hovered(b))
			ui::Tooltip({ { title, util::Mix(col, ui::theme::kTextHi, 0.3f), 15.f, ui::Face::Strong }, { help, ui::theme::kText, 13.f } });
		x += S + 6.f;
	};
	for (const auto& st : me->statuses)
	{
		if (st.stacks == 0 || nowf >= st.until) continue;
		const auto id = static_cast<StatusId>(st.id);
		const StatusRow& row = StatusTable::Get(id);
		const Color col = Rgb(row.color);
		icon(col, st.until - nowf, 0.f, st.stacks, row.debuff ? std::string(row.name) + " (해로움)" : std::string(row.name), StatusHelp(id),
			[&](Vector2 c, float rr, Color cl)
			{
				// 그린 상태 아이콘(있으면)
				if (!icons::DrawPainted("status_" + std::to_string(st.id), Rectangle{ c.x - rr * 1.45f, c.y - rr * 1.45f, rr * 2.9f, rr * 2.9f }))
					StatusGlyph(id, c, rr, cl, nowf);
			});
	}
	if (m_hasteUntil > m_now)
		icon(Rgb(0x8DBF6E), static_cast<float>(m_hasteUntil - m_now), 0.f, 0, "바람걸음", "이동 속도 증가",
			[&](Vector2 c, float rr, Color cl)
			{
				DrawTriangle(Vector2{ c.x - rr, c.y - rr }, Vector2{ c.x - rr, c.y + rr }, Vector2{ c.x, c.y }, cl);
				DrawTriangle(Vector2{ c.x, c.y - rr }, Vector2{ c.x, c.y + rr }, Vector2{ c.x + rr, c.y }, cl);
			});
	if (m_flags & SUBJECT_EX_PURPLE)
		icon(ui::theme::kEmber, 0.f, 0.f, 0, "적대", "먼저 공격한 상태입니다. 상대가 반격해도 벌칙이 없습니다.",
			[&](Vector2 c, float rr, Color cl) { ui::Text("!", Vector2{ c.x, c.y - rr * 1.1f }, rr * 2.2f, cl, Align::Center); });
}

// ─────────────────────────────────────────────────────────────────────────────
// 연속 타격 수 + 연계 이름(오른쪽)
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawComboCounter()
{
	const float W = m_uiW, H = m_uiH;
	const double age = m_now - m_comboLastAt;
	const float cx = W - 360.f, cy = H * 0.42f;   // 오른쪽은 퀘스트 추적 표시 자리
	if (m_comboHits >= 2 && age < 3.0)
	{
		const float a = age > 2.2 ? static_cast<float>(1.0 - (age - 2.2) / 0.8) : 1.f;
		const float punch = age < 0.12 ? 1.f + 0.4f * static_cast<float>(1.0 - age / 0.12) : 1.f;
		const float heat = util::Clamp01(m_comboHits / 40.f);
		const Color col = util::Mix(ui::theme::kTextHi, ui::theme::kDawn, 0.3f + 0.7f * heat);
		ui::TextStyle st;
		st.size = (40.f + 12.f * heat) * punch;
		st.color = util::MulA(col, a);
		st.outline = 2.f;
		st.outlineColor = util::MulA(Color{ 12, 8, 4, 220 }, a);
		st.shadow = 3.f;
		st.face = ui::Face::NumberBold;
		ui::Fonts().DrawCentered(std::to_string(m_comboHits), Vector2{ cx, cy }, st);
		st.size = 14.f;
		st.color = util::MulA(ui::theme::kTextDim, a);
		st.outline = 1.5f;
		st.shadow = 0.f;
		st.face = ui::Face::Strong;
		st.spacing = 2.f;
		ui::Fonts().DrawCentered("연속", Vector2{ cx, cy + 30.f }, st);
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
		st.color = util::MulA(ui::theme::kDawnHi, a);
		st.outline = 1.8f;
		st.outlineColor = util::MulA(Color{ 20, 12, 4, 230 }, a);
		st.face = ui::Face::Display;
		st.spacing = 1.f;
		ui::Fonts().DrawCentered(m_comboName, Vector2{ cx, cy + 70.f }, st);
		ui::TextStyle ls; ls.size = 11.5f; ls.color = util::MulA(ui::theme::kTextDim, a); ls.outline = 1.2f; ls.face = ui::Face::Strong; ls.spacing = 2.f;
		ui::Fonts().DrawCentered("연계", Vector2{ cx, cy + 52.f }, ls);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 캐릭터 창(C) — 직업 · 능력치 분배 · 전투 능력치
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawCharacter()
{
	const float W = m_uiW, H = m_uiH;
	const Rectangle r = wm::Window("char", Rectangle{ std::max(20.f, W * 0.5f - 470.f), std::max(12.f, H * 0.5f - 290.f), 600.f, std::min(H - 120.f, 560.f) }, wm::kMove);
	ui::Window(r, "캐릭터", &m_charOpen);
	const ClassRow& cls = ClassTable::Get(m_classId);
	const Color cc = ClassColor(m_classId);

	// 왼쪽: 직업 소개
	ClassEmblem(Vector2{ r.x + 50.f, r.y + 88.f }, 30.f, m_classId, m_time);
	{
		ui::TextStyle ns; ns.size = ui::type::kDisplayS + 2.f; ns.color = ui::theme::kTextHi; ns.face = ui::Face::Display; ns.spacing = 1.f;
		ui::Fonts().Draw(cls.name, Vector2{ r.x + 92.f, r.y + 62.f }, ns);
	}
	ui::Text(cls.role, Vector2{ r.x + 92.f, r.y + 92.f }, ui::type::kCaption, util::Shade(cc, 1.05f));
	ui::TextWrapped(cls.desc, Rectangle{ r.x + 20.f, r.y + 132.f, 262.f, 70.f }, ui::type::kBodyS, ui::theme::kText, 6.f);
	ui::Divider(r.x + 18.f, r.x + 282.f, r.y + 204.f);

	// 능력치 6개 + 분배
	const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
	ui::Text(m_freePoints > 0 ? Fmt("남은 포인트  %d", m_freePoints) : "레벨이 오르면 포인트를 얻습니다", Vector2{ r.x + 20.f, r.y + 214.f }, ui::type::kBodyS,
		m_freePoints > 0 ? ui::theme::kDawnHi : ui::theme::kTextDim);
	for (int i = 0; i < 6; ++i)
	{
		const float y = r.y + 244.f + i * 34.f;
		const int base = m_baseStats.v[i], alloc = m_allocStats.v[i], gear = m_gearStats.v[i];
		const Rectangle row{ r.x + 16.f, y - 4.f, 268.f, 30.f };
		const bool hover = ui::Hovered(row);
		if (hover) ui::Plate(row, Color{ 255, 255, 255, 10 }, BLANK, 2.f, 0.f);
		const bool main = IsMainStat(m_classId, i);
		ui::Text(StatName(i), Vector2{ r.x + 24.f, y + 1.f }, ui::type::kBody, main ? ui::theme::kTextHi : ui::theme::kText);
		if (main)
		{
			// 주 능력치 표시: 직업 색 작은 마름모
			const Vector2 d{ r.x + 13.f, y + 9.f };
			DrawTriangle(Vector2{ d.x, d.y - 4 }, Vector2{ d.x - 3, d.y }, Vector2{ d.x + 3, d.y }, cc);
			DrawTriangle(Vector2{ d.x - 3, d.y }, Vector2{ d.x, d.y + 4 }, Vector2{ d.x + 3, d.y }, cc);
		}
		{
			ui::TextStyle vs; vs.size = 17.f; vs.color = ui::theme::kTextHi; vs.face = ui::Face::Number;
			const std::string v = std::to_string(base + alloc + gear);
			ui::Fonts().Draw(v, Vector2{ r.x + 176.f - ui::Fonts().Measure(v, vs.size, 0.f, ui::Face::Number).x, y - 1.f }, vs);
		}
		if (alloc > 0 || gear != 0)
			ui::Text(Fmt("(%+d%s)", alloc + gear, gear != 0 ? " 장비" : ""), Vector2{ r.x + 184.f, y + 2.f }, ui::type::kCaption, ui::theme::kGood);
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
			ui::Tooltip({ { StatName(i), ui::theme::kTextHi, 15.f, ui::Face::Strong },
				{ StatHelp(i), ui::theme::kText, 14.f },
				{ Fmt("직업 %d + 분배 %d + 장비 %d", base, alloc, gear), ui::theme::kTextDim, 13.f },
				{ "Shift + 클릭: 5씩", ui::theme::kTextDim, 12.f } });
	}

	// 오른쪽: 전투 능력치
	const float x2 = r.x + 306.f;
	DrawRectangleRec(Rectangle{ std::round(x2 - 12.f), r.y + 54.f, 1.f, r.height - 72.f }, ui::theme::kLine);
	ui::SectionHeader(Vector2{ x2, r.y + 56.f }, r.x + r.width - 20.f - x2, "전투 능력치");
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
		if (hover) ui::Plate(row, Color{ 255, 255, 255, 10 }, BLANK, 2.f, 0.f);
		ui::Text(ln.name, Vector2{ x2, y }, ui::type::kBodyS, ui::theme::kTextDim);
		{
			ui::TextStyle vs; vs.size = 15.f; vs.color = ui::theme::kTextHi; vs.face = ui::Face::Number;
			ui::Fonts().Draw(ln.value, Vector2{ r.x + r.width - 20.f - ui::Fonts().Measure(ln.value, vs.size, 0.f, ui::Face::Number).x, y - 1.f }, vs);
		}
		if (hover) ui::Tooltip({ { ln.name, ui::theme::kTextHi, 15.f, ui::Face::Strong }, { ln.help, ui::theme::kText, 13.f } });
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
					list.push_back({ Fmt("◈ %s +%d%%", row->name, ex.skillBoost[sk]), ui::theme::kDawn });
		if (ex.itemFind > 0) list.push_back({ Fmt("희귀품 발견 +%d%%", ex.itemFind), ui::theme::kInfo });
		if (ex.adenaFind > 0) list.push_back({ Fmt("그로시 획득 +%d%%", ex.adenaFind), ui::theme::kGold });
		for (const auto& set : ItemRules::kSets)
			if (ex.setCount[set.id] >= 2)
				list.push_back({ Fmt("%s %d/4%s", set.name, ex.setCount[set.id], ex.setCount[set.id] >= 4 ? " 완성" : ""), Rgb(set.color) });

		float yy = r.y + 454.f;
		if (yy + 40.f < r.y + r.height)
		{
			ui::SectionHeader(Vector2{ r.x + 20.f, yy + 6.f }, 262.f, "장비 효과");
			yy += 30.f;
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
	// 직업 세 칸(움직이는 미리보기 + 이름) 아래에 고른 직업의 역할 · 설명.
	// 고른 칸은 직업 색 테두리 + 아래 띠, 미리보기는 기술을 차례로 보여 준다.
	const float gapX = 9.f;
	const float chipH = area.height - 72.f;
	const float chipW = std::floor((area.width - gapX * 2.f) / 3.f);
	for (int i = 0; i < 3; ++i)
	{
		const ClassId id = static_cast<ClassId>(i);
		const ClassRow& cls = ClassTable::Get(id);
		const Color cc = Rgb(cls.color);
		const Rectangle card{ std::round(area.x + i * (chipW + gapX)), std::round(area.y), chipW, chipH };
		const bool sel = m_newClassId == i;
		const bool hover = ui::Hovered(card);
		ui::Block(card);
		ui::Plate(card, sel ? util::Mix(ui::theme::kSurface1, cc, 0.14f) : (hover ? ui::theme::kSurface2 : ui::theme::kSurface1),
			sel ? cc : (hover ? ui::theme::kTextDim : ui::theme::kLine), 4.f, sel ? 1.5f : 1.f);
		const Rectangle pv{ card.x + 3.f, card.y + 3.f, card.width - 6.f, chipH - 30.f };
		if (sel)   // 아래에서 번지는 직업 색(아주 옅게) — 이름 칸까지 이어서 경계가 생기지 않게
			DrawRectangleGradientV(static_cast<int>(card.x + 2.f), static_cast<int>(card.y + chipH * 0.3f), static_cast<int>(card.width - 4.f), static_cast<int>(chipH * 0.7f - 2.f),
				util::WithA(cc, 0.f), util::WithA(cc, 0.17f));

		ui::BeginClip(Rectangle{ pv.x, pv.y, pv.width, pv.height });
		{
			actors::Pose pose;
			// 가장 키 큰 모습(각인술사의 두건 · 지팡이)까지 칸 안에 들어오는 배율
			pose.scale = std::clamp(pv.height / 66.f, 0.75f, 1.2f) * 0.98f;
			pose.base = Vector2{ pv.x + pv.width * 0.5f, pv.y + pv.height - 5.f };
			pose.time = m_time + i * 1.3f;
			pose.faceRight = true;
			pose.seed = 7u + static_cast<uint32_t>(i);
			if (sel || hover)
			{
				const float cycle = 1.35f;
				const int s = static_cast<int>(m_time / cycle) % 5;
				pose.skill = cls.skills[1 + s];
				pose.skillT = std::fmod(m_time, cycle) / cycle * 1.25f;
				if (pose.skillT > 1.f) pose.skillT = -1.f;
				const SkillRow* sr = SkillTable::Find(pose.skill);
				pose.skillHold = sr ? (sr->castMs / 1000.f) / (cycle / 1.25f) : 0.f;
			}
			gfx::BeginWorld(0.f, pose.scale);
			gfx::ResetMod();
			gfx::SetDepthRaw(-0.5f);
			gfx::EllipseGrad(pose.base, 20.f, 7.f, Rgb(0x000000, 120), Rgb(0x000000, 0));
			actors::DrawPlayer(pose, actors::LookForPlayer(0x5EEDu * (i + 3), cls.starterWeapon, 0, static_cast<uint8_t>(i)));
			gfx::Flush();
		}
		ui::EndClip();

		ui::TextStyle ns;
		ns.size = 15.5f;
		ns.color = sel ? ui::theme::kTextHi : (hover ? ui::theme::kText : ui::theme::kTextDim);
		ns.face = ui::Face::Display;
		ns.spacing = 0.6f;
		ui::Fonts().DrawCentered(cls.name, Vector2{ card.x + card.width * 0.5f + 0.3f, card.y + chipH - 15.f }, ns);
		if (sel)
			DrawRectangleRec(Rectangle{ card.x + 8.f, card.y + chipH - 3.f, card.width - 16.f, 2.f }, cc);
		if (ui::Clicked(card) && !sel)
		{
			m_newClassId = static_cast<uint8_t>(i);
			sfx::Play(sfx::Id::Equip, 0.6f);
		}
	}

	// 고른 직업: 역할(직업 색) + 설명
	const ClassRow& cls = ClassTable::Get(static_cast<ClassId>(m_newClassId));
	const float dy = std::round(area.y + chipH + 10.f);
	ui::TextStyle rs;
	rs.size = 12.5f;
	rs.color = util::Shade(Rgb(cls.color), 1.1f);
	rs.face = ui::Face::Strong;
	ui::Fonts().Draw(cls.role, Vector2{ area.x, dy }, rs);
	ui::TextWrapped(cls.desc, Rectangle{ area.x, dy + 20.f, area.width, 42.f }, 12.5f, ui::theme::kText, 4.5f);
}
