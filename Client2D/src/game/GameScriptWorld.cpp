#include "game/Game.h"
#include "audio/Ambience.h"
#include "render/Wind.h"
#include "ui/Ui.h"
#include "ui/Font.h"
#include "Data/WorldData.h"
#include "render/Height.h"
#include "render/Props.h"
#include "render/Draw.h"
#include "render/Terrain.h"
#include "render/Critters.h"
#include "Data/TownRules.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Phase 4 시험 시나리오(--script)
//
//   tour          카메라 투어 — 마을 전경에서 구획을 차례로 돌며 이름을 띄운다(HUD 숨김 · 시야 안개 끔).
//                 --hour 12 / --hour 21로 낮 · 밤을 따로 녹화한다(--record).
//   terraincheck  경계 칸 전수 검사(서버와 같은 TownRules::Check — 절벽 통과 · 허공 걷기) + 보이지 않는 벽 +
//                 탐침: 가장자리를 사이에 둔 낮은 칸 → 높은 칸을 걸어가게 해 계단으로 돌아가는지, 화면 지면이 끊기지 않는지.
//                 끝에 [terraincheck] done PASS|FAIL. 서버 쪽 덫(이동마다 같은 검사)은 서버 로그의 "[Town] level-rule violation".
//   goto:x,y      마을 기준 칸으로 걸어간다(길찾기 · 계단 확인용). 도착하면 [goto] 한 줄
//   birds         가장 가까운 비둘기 떼로 걸어가 날려 보내고 물러나 다시 내려앉는지 본다(render/Critters). [birds] done PASS|FAIL
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

	// 투어 자리: 마을 원본의 구획 표(districts.txt → TownMap::kDistricts, 월드 칸). 첫 줄은 마을 전경,
	// 마지막에 전경으로 돌아온다. 전경은 오래, 구획은 짧게 머문다.
	struct Stops
	{
		TourStop s[40];
		int n = 0;
	};
	Stops BuildStops()
	{
		Stops st;
		const int nd = static_cast<int>(std::size(TownMap::kDistricts));
		for (int i = 0; i < nd && st.n < 38; ++i)
		{
			const auto& d = TownMap::kDistricts[i];
			st.s[st.n++] = TourStop{ d.x - WorldData::kTownX0, d.y - WorldData::kTownY0, d.zoom, i == 0 ? 2.6f : 1.7f, d.name, d.sub };
		}
		if (nd > 0)
		{
			const auto& d = TownMap::kDistricts[0];
			st.s[st.n++] = TourStop{ d.x - WorldData::kTownX0, d.y - WorldData::kTownY0, d.zoom, 2.2f, "", "" };
		}
		return st;
	}
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
	// birds — 가장 가까운 비둘기 떼로 걸어 들어가 날려 보내고, 몇 칸 물러나 다시 내려앉는 것을 본다(Phase 4 자기 검토 —
	// 마을의 작은 생명). [birds] 줄: start · startle(날아오른 수) · landed · done(물고기 뛴 수)
	if (s == "birds")
	{
		static Vector2 from{};
		static int flockIdx = -1, startles0 = 0, landings0 = 0;
		const critters::Stats st = critters::GetStats();
		const critters::FlockInfo fi = critters::Flock(flockIdx);
		auto elapsed = [this] { return m_scriptT - m_scriptMark; };
		auto next = [this](int step) { m_scriptStep = step; m_scriptMark = m_scriptT; };
		if (m_scriptStep == 0 && m_scriptT > 3.0f && Self())
		{
			from = Self()->pos;
			flockIdx = critters::NearestFlock(from);
			const critters::FlockInfo f0 = critters::Flock(flockIdx);
			startles0 = f0.startles;
			landings0 = f0.landings;
			std::printf("[birds] start t=%.2f flocks=%d birds=%d from=%.0f,%.0f flock=%d at %.1f,%.1f\n", m_scriptT, st.flocks, st.birds,
				from.x - WorldData::kTownX0, from.y - WorldData::kTownY0, flockIdx, f0.center.x - WorldData::kTownX0, f0.center.y - WorldData::kTownY0);
			if (flockIdx < 0) { std::printf("[birds] done FAIL no flock\n"); m_scriptStep = 9; return true; }
			SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(std::lround(f0.center.x)), static_cast<int>(std::lround(f0.center.y)) });
			next(1);
		}
		else if (m_scriptStep == 1 && fi.startles > startles0)
		{
			std::printf("[birds] startle t=%.2f flying=%d\n", m_scriptT, fi.flying);
			next(2);
		}
		else if (m_scriptStep == 1 && elapsed() > 70.f)
		{
			std::printf("[birds] done FAIL no startle\n");
			m_scriptStep = 9;
		}
		else if (m_scriptStep == 2 && elapsed() > 1.6f)
		{
			std::printf("[birds] flying t=%.2f flying=%d\n", m_scriptT, fi.flying);
			// 온 쪽으로 5칸 물러난다 — 떼가 제자리로 돌아와 내려앉는다(아직 있으면 다른 자리로 간다)
			const Vector2 d{ from.x - fi.center.x, from.y - fi.center.y };
			const float l = std::max(0.001f, std::hypot(d.x, d.y));
			SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(std::lround(fi.center.x + d.x / l * 5.f)), static_cast<int>(std::lround(fi.center.y + d.y / l * 5.f)) });
			next(3);
		}
		else if (m_scriptStep == 3 && (fi.landings > landings0 || elapsed() > 30.f))
		{
			std::printf("[birds] landed t=%.2f ok=%d at %.1f,%.1f\n", m_scriptT, fi.landings > landings0 ? 1 : 0,
				fi.center.x - WorldData::kTownX0, fi.center.y - WorldData::kTownY0);
			next(4);
		}
		else if (m_scriptStep == 4 && elapsed() > 2.5f)
		{
			std::printf("[birds] done %s fishJumps=%d birds=%d\n", fi.landings > landings0 ? "PASS" : "FAIL", st.fishJumps, st.birds);
			std::fflush(stdout);
			m_scriptStep = 9;
		}
		return true;
	}
	// arrive — 남문 밖 들판으로 걸어 나갔다가 다시 남문으로 들어온다(도착 연출 · 첫인상 녹화). [arrive] 줄
	if (s == "arrive")
	{
		static double leg2At = 1e18;
		if (m_scriptStep == 0 && m_scriptT > 4.0f && Self())
		{
			std::printf("[arrive] start t=%.2f at %d,%d\n", m_scriptT, m_predX - WorldData::kTownX0, m_predY - WorldData::kTownY0);
			SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(TownMap::kSouthGateOut.x), static_cast<int>(TownMap::kSouthGateOut.y) + 2 });
			m_scriptStep = 1;
			m_scriptMark = m_scriptT;
		}
		else if (m_scriptStep == 1 && ((m_goal.kind == GoalKind::None && !WorldData::IsInTown(m_predX, m_predY) && m_scriptT - m_scriptMark > 1.f) || m_scriptT - m_scriptMark > 80.f))
		{
			std::printf("[arrive] outside t=%.2f at %d,%d\n", m_scriptT, m_predX - WorldData::kTownX0, m_predY - WorldData::kTownY0);
			SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(TownMap::kSouthGate.x), static_cast<int>(TownMap::kSouthGate.y) - 8 });
			m_scriptStep = 2;
			m_scriptMark = m_scriptT;
			leg2At = m_now;
		}
		else if (m_scriptStep == 2 && m_arrival.at >= leg2At)
		{
			std::printf("[arrive] camera t=%.2f at %d,%d\n", m_scriptT, m_predX - WorldData::kTownX0, m_predY - WorldData::kTownY0);
			std::fflush(stdout);
			m_scriptStep = 3;
			// --screenshot을 주었으면 연출이 가장 멀리 간 때(0.9초 뒤)를 찍는다
			if (!m_opts.screenshot.empty()) m_opts.screenshotAfter = std::min(m_opts.screenshotAfter, m_runTime + 0.9f);
		}
		else if (m_scriptStep >= 2 && m_scriptStep < 4 && (m_goal.kind == GoalKind::None || m_scriptT - m_scriptMark > 60.f))
		{
			std::printf("[arrive] done t=%.2f at %d,%d%s\n", m_scriptT, m_predX - WorldData::kTownX0, m_predY - WorldData::kTownY0, m_scriptStep == 3 ? "" : " (연출 없음)");
			std::fflush(stdout);
			m_scriptStep = 4;
		}
		return true;
	}
	// look:x,y[,zoom] — 마을 기준 칸에 카메라를 세워 둔다(HUD 없이 풍경만 — 첫인상 구도 · 문서 그림). 캐릭터는 제자리
	if (s.rfind("look:", 0) == 0)
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			float lx = 0.f, ly = 0.f, lz = 1.45f;
			std::sscanf(s.c_str() + 5, "%f,%f,%f", &lx, &ly, &lz);
			m_tourCam.on = true;
			m_tourCam.tile = Vector2{ WorldData::kTownX0 + lx, WorldData::kTownY0 + ly };
			m_tourCam.zoom = lz;
			m_cam.SnapTo(TileToIso(m_tourCam.tile, 0.f));
			m_cam.SetZoomFree(lz);
			m_scriptStep = 1;
		}
		return true;
	}
	// bldsolo:N — 건물 N 하나만 자홍 바탕에(HUD · 캐릭터 · 지형 없이) 찍는다: bldsolo_N.png. Higgsfield 참고 그림 · 비교용(Phase 4-C)
	if (s.rfind("bldsolo:", 0) == 0)
	{
		const int n = std::atoi(s.c_str() + 8);
		if (n < 0 || n >= static_cast<int>(std::size(WorldData::kBuildings))) return true;
		const auto& b = WorldData::kBuildings[n];
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			Terrain::SetSolo(n);
			m_tourCam.on = true;
			const float span = static_cast<float>(std::max(b.x1 - b.x0, b.y1 - b.y0) + 1);
			m_tourCam.tile = Vector2{ (b.x0 + b.x1) * 0.5f + 1.1f, (b.y0 + b.y1) * 0.5f + 1.1f };
			m_tourCam.zoom = std::clamp(12.f / (span + 4.f), 0.5f, 1.2f);   // 탑 · 첨탑처럼 높은 건물도 꼭대기가 잘리지 않게
			m_cam.SnapTo(TileToIso(m_tourCam.tile, 0.f));
			m_cam.SetZoomFree(m_tourCam.zoom);
			m_scriptStep = 1;
			m_scriptMark = m_scriptT;
		}
		if (m_scriptStep == 1 && m_scriptT - m_scriptMark > 2.5f)
		{
			char name[64];
			std::snprintf(name, sizeof(name), "bldsolo_%d.png", n);
			TakeScreenshot(name);
			std::printf("[bldsolo] %d kind=%d zoom=%.2f cam=%.2f,%.2f\n", n, static_cast<int>(b.kind), m_tourCam.zoom, m_tourCam.tile.x, m_tourCam.tile.y);
			std::fflush(stdout);
			// 곁 파일: 화면 px ↔ 아이소 px 변환 값(tools/assetgen/buildings.py가 읽는다)
			//   iso.x = (sx − offX) / zoom + targetX,  iso.y = (sy − offY) / zoom + targetY + lift
			std::snprintf(name, sizeof(name), "bldsolo_%d.txt", n);
			if (FILE* fp = std::fopen(name, "w"))
			{
				std::fprintf(fp, "target %.3f %.3f\noffset %.3f %.3f\nzoom %.5f\nlift %.3f\nbuilding %d %d %d %d %d\nrotation %.3f\n",
					m_cam2d.target.x, m_cam2d.target.y, m_cam2d.offset.x, m_cam2d.offset.y, m_cam2d.zoom,
					static_cast<float>(b.level) * 0.75f * gfx::kUnitZ, b.x0, b.y0, b.x1, b.y1, b.level, m_cam2d.rotation);
				std::fclose(fp);
			}
			m_scriptStep = 2;
			m_quit = true;   // 한 장이면 끝(도구가 여러 건물을 차례로 부른다)
		}
		return true;
	}
	// bldgallery — 건물 종류마다(주택은 변형마다) 한 채씩 카메라를 옮겨 찍는다(Phase 4-C 점검 · README 그림)
	//   bld_NN.png(실행 폴더) + [bld] 줄. --hour로 낮 · 밤을 고른다.
	if (s == "bldgallery" || s.rfind("bldgallery:", 0) == 0)
	{
		static std::vector<int> picks;
		static int lastShot = -1;
		if (picks.empty() && s.size() > 11)
			picks.push_back(std::atoi(s.c_str() + 11));   // bldgallery:N — 건물 N 하나만(마을 속에서)
		if (picks.empty())
		{
			std::vector<int> seen;
			for (int i = 0; i < static_cast<int>(std::size(WorldData::kBuildings)); ++i)
			{
				const auto& b = WorldData::kBuildings[i];
				const int key = static_cast<int>(b.kind) * 16 + (b.kind == WorldData::BuildingKind::eHouse ? b.variant % 10 : 0);
				if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
				seen.push_back(key);
				picks.push_back(i);
			}
		}
		constexpr float kHold = 2.8f;
		if (m_scriptStep == 0)
		{
			if (m_scriptT > 1.0f) { m_tourCam.on = true; m_scriptStep = 1; m_scriptMark = m_scriptT; std::printf("[bld] start %zu\n", picks.size()); }
			return true;
		}
		const int i = static_cast<int>((m_scriptT - m_scriptMark) / kHold);
		if (i >= static_cast<int>(picks.size()))
		{
			if (m_scriptStep == 1) { std::printf("[bld] done\n"); std::fflush(stdout); m_scriptStep = 2; }
			return true;
		}
		const auto& b = WorldData::kBuildings[picks[static_cast<size_t>(i)]];
		const float h = static_cast<float>(std::max(b.x1 - b.x0, b.y1 - b.y0));
		const Vector2 tile{ (b.x0 + b.x1) * 0.5f + 0.6f + h * 0.08f, (b.y0 + b.y1) * 0.5f + 0.6f + h * 0.08f };
		const float t = m_scriptT - m_scriptMark - static_cast<float>(i) * kHold;
		if (t < 0.2f || m_tourCam.tile.x != tile.x || m_tourCam.tile.y != tile.y)
		{
			m_tourCam.tile = tile;
			m_tourCam.zoom = std::clamp(12.f / (h + 6.f), 0.62f, 1.3f);
			m_cam.SnapTo(TileToIso(tile, 0.f));
			m_cam.SetZoomFree(m_tourCam.zoom);
		}
		if (t > 1.9f && lastShot != i)
		{
			lastShot = i;
			char name[64];
			std::snprintf(name, sizeof(name), "bld_%02d.png", i);
			TakeScreenshot(name);
			std::printf("[bld] %d kind=%d variant=%d at %d,%d sign=%s\n", i, static_cast<int>(b.kind), b.variant,
				b.x0 - WorldData::kTownX0, b.y0 - WorldData::kTownY0, b.sign);
			std::fflush(stdout);
		}
		return true;
	}
	if (s == "tour")
	{
		static const Stops kStops = BuildStops();
		const TourStop* stops = kStops.s;
		const int n = kStops.n;
		// 구간마다 거리에 맞춘 이동 시간(초당 14칸, 1.6~3.2초)
		auto travelOf = [&](int i)
		{
			if (i + 1 >= n) return 0.f;
			const float d = std::hypot(stops[i + 1].x - stops[i].x, stops[i + 1].y - stops[i].y);
			return std::clamp(d / 14.f, 1.6f, 3.2f);
		};
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
					// 소리 구역 점검: 이 자리에서 들리는 바탕 소리(0~1) — CLIENT_AMB_DUMP=폴더면 6초 WAV도 쓴다
					const amb::Mix mix = amb::MixAt(m_tourCam.tile, m_light.Night(), wind::Strength());
					std::printf("[amb] stop=%d fountain=%.2f river=%.2f wind=%.2f market=%.2f forge=%.2f bell=%.2f (windStrength=%.2f)\n", i,
						mix.gain[0], mix.gain[1], mix.gain[2], mix.gain[3], mix.gain[4], mix.bell, wind::Strength());
					if (const char* dump = std::getenv("CLIENT_AMB_DUMP"))
					{
						char path[512];
						std::snprintf(path, sizeof(path), "%s/amb_%02d.wav", dump, i);
						amb::DumpWav(path, m_tourCam.tile, m_light.Night(), wind::Strength(), 6.f);
					}
				}
				return true;
			}
			t -= a.hold;
			if (i + 1 >= n) break;
			const float travel = travelOf(i);
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
					// 계단 칸은 짝이 될 수 없다(계단 자체가 층을 잇는다 — 곧게 올라가도 맞는 길이다)
					auto stair = [](char c) { return c == '^' || c == 'v' || c == '<' || c == '>'; };
					if (stair(WorldData::TownCell(ax, ay)) || stair(WorldData::TownCell(bx, by))) continue;
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
