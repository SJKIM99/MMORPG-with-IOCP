#include "game/Game.h"
#include "ui/Ui.h"
#include "ui/Font.h"
#include "Data/WorldData.h"
#include "render/Height.h"
#include "render/Props.h"
#include "Data/TownRules.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// ─────────────────────────────────────────────────────────────────────────────
// Phase 4 시험 시나리오(--script)
//
//   tour          카메라 투어 — 마을 전경에서 구획을 차례로 돌며 이름을 띄운다(HUD 숨김 · 시야 안개 끔).
//                 --hour 12 / --hour 21로 낮 · 밤을 따로 녹화한다(--record).
//   terraincheck  경계 칸 전수 검사(서버와 같은 TownRules::Check — 절벽 통과 · 허공 걷기) + 보이지 않는 벽 +
//                 탐침: 가장자리를 사이에 둔 낮은 칸 → 높은 칸을 걸어가게 해 계단으로 돌아가는지, 화면 지면이 끊기지 않는지.
//                 끝에 [terraincheck] done PASS|FAIL. 서버 쪽 덫(이동마다 같은 검사)은 서버 로그의 "[Town] level-rule violation".
//   goto:x,y      마을 기준 칸으로 걸어간다(길찾기 · 계단 확인용). 도착하면 [goto] 한 줄
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
	// goto:x,y — 마을 기준 칸으로 걸어간다(길찾기 · 계단 · 가장자리 확인용). 도착하면 [goto] 한 줄
	if (s.rfind("goto:", 0) == 0)
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			float gx = 0.f, gy = 0.f;
			std::sscanf(s.c_str() + 5, "%f,%f", &gx, &gy);
			SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kTownX0 + static_cast<int>(gx), WorldData::kTownY0 + static_cast<int>(gy) });
			m_scriptStep = 1;
			m_scriptMark = m_scriptT;
		}
		if (m_scriptStep == 1 && (m_goal.kind == GoalKind::None || m_scriptT - m_scriptMark > 90.f))
		{
			std::printf("[goto] t=%.2f at %d,%d (town %d,%d) level=%d z=%.2f%s\n", m_scriptT, m_predX, m_predY,
				m_predX - WorldData::kTownX0, m_predY - WorldData::kTownY0, WorldData::TownLevel(m_predX, m_predY),
				Self() ? hgt::BodyZ(Self()->pos) : -1.f, m_goal.kind == GoalKind::None ? "" : " (timeout)");
			m_scriptStep = 2;
		}
		return true;
	}
	if (s == "terraincheck")
		return RunTerrainCheck(dt);
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

// ─────────────────────────────────────────────────────────────────────────────
// terraincheck — Phase 4 완료 기준 "서버 충돌과 화면 지형 일치 — 절벽 통과 · 허공 걷기 0건(경계 칸 전수 검사)"
// ─────────────────────────────────────────────────────────────────────────────
bool Game::RunTerrainCheck(float dt)
{
	Entity* me = Self();
	TerrainProbe& pr = m_probe;
	auto finish = [&](bool probeOk, const char* why)
	{
		std::printf("[terraincheck] done %s%s\n", pr.staticOk && probeOk ? "PASS" : "FAIL", why);
		std::fflush(stdout);
		m_scriptStep = 9;
		// --screenshot을 주었으면 바로 찍고 끝낸다(회귀 묶음이 기다리지 않게)
		if (!m_opts.screenshot.empty()) m_opts.screenshotAfter = std::min(m_opts.screenshotAfter, m_runTime + 0.5f);
	};
	switch (m_scriptStep)
	{
	case 0:
	{
		if (m_scriptT < 1.0f || !me) break;
		// 1) 전수 검사 — 서버가 켤 때 도는 것과 같은 함수(Data/TownRules.h)
		const TownRules::Report r = TownRules::Check();
		std::printf("[terraincheck] 마을 %dx%d · 걷는 칸 %d(층 0~4: %d %d %d %d %d) · 일반 걸음 %d · 강제 걸음 %d · 절벽 통과 %d · 허공 걷기 %d\n",
			WorldData::kTownW, WorldData::kTownH, r.walkable, r.levelCells[0], r.levelCells[1], r.levelCells[2], r.levelCells[3], r.levelCells[4],
			r.steps, r.forced, r.cliff, r.air);
		if (!r.Ok())
			std::printf("[terraincheck]   처음 어긋난 곳: 마을 (%d, %d) 방향 %d %s\n", r.firstX - WorldData::kTownX0, r.firstY - WorldData::kTownY0, r.firstDir, r.firstForced ? "강제" : "걸음");
		// 2) 보이지 않는 벽 — 막힌 칸마다 무언가 그려지는가(Props.h 원칙)
		int invisible = 0;
		std::string ex;
		for (int y = WorldData::kTownY0; y < WorldData::kTownY0 + WorldData::kTownH; ++y)
			for (int x = WorldData::kTownX0; x < WorldData::kTownX0 + WorldData::kTownW; ++x)
			{
				const char c = WorldData::TownCell(x, y);
				if (!WorldData::IsTownCellBlocked(c) || props::TownCellVisible(c)) continue;
				if (++invisible <= 6) { char b[32]; std::snprintf(b, sizeof(b), " (%d,%d '%c')", x - WorldData::kTownX0, y - WorldData::kTownY0, c); ex += b; }
			}
		std::printf("[terraincheck] 보이지 않는 벽 %d%s\n", invisible, ex.c_str());
		pr = TerrainProbe{};
		pr.staticOk = r.Ok() && invisible == 0;

		// 3) 탐침 짝: 내가 걸어서 닿는 칸(길찾기와 같은 모서리 규칙) 가운데, 가장자리 R 하나를 사이에 두고
		//    마주 보는 낮은 칸 A · 높은 칸 B — 나와 가까운 것. 곧은 거리는 2칸인데 계단으로 돌아가야 한다.
		const int W = WorldData::kTownW, H = WorldData::kTownH, X0 = WorldData::kTownX0, Y0 = WorldData::kTownY0;
		std::vector<int> dist(static_cast<size_t>(W * H), -1);
		auto idx = [&](int x, int y) { return (y - Y0) * W + (x - X0); };
		std::vector<std::pair<int, int>> q;
		if (WorldData::IsInTown(m_predX, m_predY)) { q.push_back({ m_predX, m_predY }); dist[static_cast<size_t>(idx(m_predX, m_predY))] = 0; }
		for (size_t h = 0; h < q.size(); ++h)
		{
			const auto [cx, cy] = q[h];
			for (int d = 0; d < 8; ++d)
			{
				int dx = 0, dy = 0;
				MoveRule::Delta(d, dx, dy);
				const int nx = cx + dx, ny = cy + dy;
				if (!WorldData::IsInTown(nx, ny) || dist[static_cast<size_t>(idx(nx, ny))] >= 0 || !MoveRule::CleanStep(cx, cy, dx, dy)) continue;
				dist[static_cast<size_t>(idx(nx, ny))] = dist[static_cast<size_t>(idx(cx, cy))] + 1;
				q.push_back({ nx, ny });
			}
		}
		int best = 1 << 30;
		int rimX = 0, rimY = 0;
		for (int y = Y0 + 1; y < Y0 + H - 1; ++y)
			for (int x = X0 + 1; x < X0 + W - 1; ++x)
			{
				if (WorldData::TownCell(x, y) != 'R') continue;
				constexpr int kD[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
				for (const auto& d : kD)
				{
					const int ax = x + d[0], ay = y + d[1], bx = x - d[0], by = y - d[1];
					if (!WorldData::IsInTown(ax, ay) || !WorldData::IsInTown(bx, by)) continue;
					if (WorldData::IsBlocked(ax, ay) || WorldData::IsBlocked(bx, by)) continue;
					if (WorldData::TownLevel(ax, ay) >= WorldData::TownLevel(bx, by)) continue;
					const int da = dist[static_cast<size_t>(idx(ax, ay))], db = dist[static_cast<size_t>(idx(bx, by))];
					if (da < 0 || db < 0 || da >= best) continue;
					best = da;
					pr.ax = ax; pr.ay = ay; pr.bx = bx; pr.by = by; rimX = x; rimY = y;
				}
			}
		if (best == (1 << 30))
		{
			std::printf("[terraincheck] 탐침: 걸어서 닿는 곳에 층이 다른 가장자리가 없다 — 건너뜀\n");
			finish(true, "");
			break;
		}
		std::printf("[terraincheck] 탐침 A 마을 (%d, %d) %d층 → B (%d, %d) %d층 — 가장자리 (%d, %d)를 사이에 둔 두 칸\n",
			pr.ax - X0, pr.ay - Y0, WorldData::TownLevel(pr.ax, pr.ay), pr.bx - X0, pr.by - Y0, WorldData::TownLevel(pr.bx, pr.by), rimX - X0, rimY - Y0);
		SetGoal(Goal{ GoalKind::Move, ObjID{}, pr.ax, pr.ay });
		m_scriptStep = 1;
		m_scriptMark = m_scriptT;
		break;
	}
	case 1:   // A로 가는 중
		if (m_goal.kind != GoalKind::None && m_scriptT - m_scriptMark < 90.f) break;
		if (m_predX != pr.ax || m_predY != pr.ay)
		{
			finish(false, " — 탐침 A에 닿지 못했다");
			break;
		}
		SetGoal(Goal{ GoalKind::Move, ObjID{}, pr.bx, pr.by });
		pr.haveLast = false;
		pr.startAt = m_now;
		m_scriptStep = 2;
		m_scriptMark = m_scriptT;
		break;
	case 2:   // A → B: 화면 위치(보간된 몸 위치)의 지면이 끊기지 않는가 — 프레임 사이 곧은 선을 1/32칸 간격으로
	{
		if (me)
		{
			if (pr.haveLast)
			{
				const TownRules::Jump j = TownRules::PathJump(pr.last.x, pr.last.y, me->pos.x, me->pos.y, me->tx, me->ty);
				pr.maxUp = std::max(pr.maxUp, j.up);
				pr.maxDown = std::max(pr.maxDown, j.down);
			}
			pr.last = me->pos;
			pr.haveLast = true;
		}
		if (pr.pathLen == 0 && !m_path.empty())
		{
			pr.pathLen = static_cast<int>(m_path.size());
			int dx = 0, dy = 0;
			for (const auto& st : m_path)
				if (WorldData::StairsDir(WorldData::TownCell(st.x, st.y), dx, dy)) ++pr.stairs;
		}
		if (m_goal.kind != GoalKind::None && m_scriptT - m_scriptMark < 120.f)
		{
			if (std::getenv("HARVEN_PROBE_TRACE") && static_cast<int>((m_scriptT - m_scriptMark) * 2.f) != static_cast<int>((m_scriptT - m_scriptMark - dt) * 2.f))
				std::printf("[terraincheck]   t=%.1f 논리 (%d, %d) 화면 (%.2f, %.2f) 목표 %d 경로 %d\n", m_scriptT - m_scriptMark, m_predX - WorldData::kTownX0, m_predY - WorldData::kTownY0,
					me ? me->pos.x - WorldData::kTownX0 : -1.f, me ? me->pos.y - WorldData::kTownY0 : -1.f, static_cast<int>(m_goal.kind), static_cast<int>(m_path.size()));
			break;
		}
		const bool arrived = m_predX == pr.bx && m_predY == pr.by;
		const bool smooth = pr.maxUp < TownRules::kJumpLimit && pr.maxDown < TownRules::kJumpLimit;
		std::printf("[terraincheck] 탐침 %s: 도착 %s · 경로 %d칸(곧은 거리 2칸) · 계단 칸 %d · 화면 지면 끊김 오름 %.2f · 내림 %.2f층 · %.1f초\n",
			arrived && smooth && pr.stairs > 0 ? "통과" : "실패", arrived ? "예" : "아니오", pr.pathLen, pr.stairs, pr.maxUp, pr.maxDown, static_cast<float>(m_now - pr.startAt));
		finish(arrived && smooth && pr.stairs > 0, "");
		break;
	}
	default:
		break;
	}
	return true;
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
