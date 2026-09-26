#include "game/Game.h"
#include "ui/Ui.h"
#include "ui/Font.h"
#include "Data/WorldData.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// Phase 4 시험 시나리오(--script)
//
//   tour      카메라 투어 — 마을 전경에서 구획을 차례로 돌며 이름을 띄운다(HUD 숨김 · 시야 안개 끔).
//             --hour 12 / --hour 21로 낮 · 밤을 따로 녹화한다(--record).
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	struct TourStop
	{
		float x, y;            // 마을 기준 칸(kTownX0 · kTownY0에서)
		float zoom;
		float hold;            // 머무는 시간(초)
		const char* name;      // 구획 이름(비우면 자막 없음)
		const char* sub;       // 한 줄 설명
	};

	// 지금 마을(44×44) — Phase 4 전 기준선. 새 마을은 WorldData의 구획 표에서 만든다.
	const TourStop kOldTown[] = {
		{ 22.f, 22.f, 0.62f, 2.6f, "데니차", "변경의 마지막 마을" },
		{ 22.f, 39.f, 1.15f, 1.6f, "남문", "성문 · 경비" },
		{ 21.5f, 24.f, 1.35f, 1.8f, "분수 광장", "분수 · 노점 · 음유시인" },
		{ 8.f, 9.f, 1.2f, 1.6f, "잡화점 · 대장간", "북서쪽 거리" },
		{ 35.f, 35.f, 1.2f, 1.6f, "예배당 · 여관", "남동쪽 거리" },
		{ 37.f, 17.f, 1.25f, 1.6f, "훈련장", "허수아비 셋" },
		{ 22.f, 22.f, 0.62f, 2.2f, "", "" },
	};

	float EaseInOut(float u) { return u < 0.5f ? 4.f * u * u * u : 1.f - std::pow(-2.f * u + 2.f, 3.f) / 2.f; }
}

bool Game::RunScriptWorld(float dt)
{
	(void)dt;
	const std::string& s = m_opts.script;
	if (s == "tour")
	{
		const TourStop* stops = kOldTown;
		const int n = static_cast<int>(std::size(kOldTown));
		const float travel = 2.4f;
		// 단계: 0 = 준비(1초) → 1 = 투어
		if (m_scriptStep == 0)
		{
			if (m_scriptT > 1.0f)
			{
				m_tourCam.on = true;
				m_tourCam.tile = Vector2{ WorldData::kTownX0 + stops[0].x, WorldData::kTownY0 + stops[0].y };
				m_tourCam.zoom = stops[0].zoom;
				m_cam.SnapTo(TileToIso(m_tourCam.tile, 0.f));
				m_cam.SetZoomFree(stops[0].zoom);
				m_scriptStep = 1;
				m_scriptMark = m_scriptT;
				std::printf("[tour] start stops=%d\n", n);
			}
			return true;
		}
		// 지금 어느 구간인가: 정지(hold) → 이동(travel) → 다음 정지 …
		float t = m_scriptT - m_scriptMark;
		for (int i = 0; i < n; ++i)
		{
			const TourStop& a = stops[i];
			if (t < a.hold)
			{
				m_tourCam.tile = Vector2{ WorldData::kTownX0 + a.x, WorldData::kTownY0 + a.y };
				m_tourCam.zoom = a.zoom;
				if (a.name[0] && m_tourCam.caption != a.name)
				{
					m_tourCam.caption = a.name;
					m_tourCam.sub = a.sub;
					m_tourCam.captionAt = m_now;
					std::printf("[tour] t=%.2f stop=%d %s\n", m_scriptT, i, a.name);
				}
				return true;
			}
			t -= a.hold;
			if (i + 1 >= n) break;
			if (t < travel)
			{
				const TourStop& b = stops[i + 1];
				const float u = EaseInOut(t / travel);
				m_tourCam.tile = Vector2{ WorldData::kTownX0 + a.x + (b.x - a.x) * u, WorldData::kTownY0 + a.y + (b.y - a.y) * u };
				m_tourCam.zoom = a.zoom + (b.zoom - a.zoom) * u;
				return true;
			}
			t -= travel;
		}
		if (m_scriptStep == 1)
		{
			std::printf("[tour] done t=%.2f\n", m_scriptT);
			m_scriptStep = 2;
		}
		return true;
	}
	return false;
}

// 투어 자막: 왼쪽 아래에 구획 이름(Display) + 한 줄 설명 — 0.4초에 떠오르고 머문 뒤 사라진다
void Game::DrawTourCaption()
{
	if (m_tourCam.caption.empty()) return;
	const float age = static_cast<float>(m_now - m_tourCam.captionAt);
	const float in = util::Clamp01(age / 0.4f);
	const float out = 1.f - util::Clamp01((age - 2.6f) / 0.5f);
	const float a = util::EaseOutCubic(in) * out;
	if (a <= 0.01f) return;
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const float k = H / 720.f;
	const Vector2 at{ std::round(48.f * k), std::round(H - 118.f * k + (1.f - util::EaseOutCubic(in)) * 12.f * k) };
	// 먹빛 띠(왼쪽에서 오른쪽으로 옅어진다)
	DrawRectangleGradientH(0, static_cast<int>(at.y - 14.f * k), static_cast<int>(W * 0.55f), static_cast<int>(92.f * k),
		util::MulA(Color{ 8, 10, 12, 200 }, a), Color{ 8, 10, 12, 0 });
	ui::TextStyle ts;
	ts.size = std::round(34.f * k);
	ts.face = ui::Face::Display;
	ts.color = util::MulA(ui::theme::kTextHi, a);
	ts.outline = 1.5f;
	ts.outlineColor = util::MulA(Color{ 0, 0, 0, 160 }, a);
	ui::Fonts().Draw(m_tourCam.caption, at, ts);
	DrawRectangle(static_cast<int>(at.x), static_cast<int>(at.y + 44.f * k), static_cast<int>(64.f * k), 2, util::MulA(ui::theme::kDawn, a));
	ui::TextStyle ss;
	ss.size = std::round(15.f * k);
	ss.face = ui::Face::Body;
	ss.color = util::MulA(ui::theme::kText, a);
	ss.outline = 1.f;
	ss.outlineColor = util::MulA(Color{ 0, 0, 0, 150 }, a);
	ui::Fonts().Draw(m_tourCam.sub, Vector2{ at.x, at.y + 52.f * k }, ss);
}
