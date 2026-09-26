#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "Data/WorldData.h"
#include "Data/MoveRule.h"

// ─────────────────────────────────────────────────────────────────────────────
// 마을 높낮이 규칙(Phase 4-A) — 서버 · 클라 공용.
//
//   CellSurface(칸, x, y)   그 칸의 윗면 높이(층 단위)를 (x, y)에서 — 평지는 층 그대로, 계단은 오르는 방향으로 고르게.
//   GroundLevel(x, y)       점이 든 칸의 윗면(반올림으로 칸을 고른다). 바닥 데칼 · 입자 · 빛처럼 "그 자리"를 따르는 것.
//   SurfaceLevel(x, y, 칸)  몸(캐릭터 · NPC)이 설 높이. 점이 막힌 칸 위에 있으면(화면 경로가 모서리를 스칠 때 ·
//                           칸 경계에서 반올림이 옆 칸을 고를 때) 가장 가까운 **걷는** 칸의 윗면으로 — 같은 거리면 논리 칸.
//                           걷는 칸끼리는 맞닿은 변에서 높이가 같으므로(생성기 층 규칙) 이 값은 걷는 동안 끊기지 않는다.
//   PathJump(…)             곧은 선(클라가 걸음 · 돌진을 그리는 길) 위에서 SurfaceLevel이 한 번에 뛰는 양(1/32칸 간격).
//                           계단은 한 간격에 1/32층 안팎이라 kJumpLimit(0.5층)에 못 미친다.
//   Check()                 모든 걷는 칸 × 8방향 전수 검사. 서버가 받아 주는 걸음(일반 Apply · 강제 CleanStep)이 오르막으로
//                           끊기면 "절벽 통과", 내리막으로 끊기면 "허공 걷기". 서버는 켤 때(Collision.cpp), 클라는
//                           --script terraincheck가 부른다. 둘 다 0이어야 한다.
//   PathOk(…)               서버가 실제 이동마다 부르는 같은 검사(한 칸 걸음 · 돌진 끝점) — 어기면 로그를 남긴다.
// ─────────────────────────────────────────────────────────────────────────────
namespace TownRules
{
	[[nodiscard]] inline float CellSurface(int tx, int ty, float x, float y) noexcept
	{
		if (!WorldData::IsInTown(tx, ty))
			return 0.f;
		const int L = WorldData::TownLevel(tx, ty);
		int dx = 0, dy = 0;
		if (WorldData::StairsDir(WorldData::TownCell(tx, ty), dx, dy))
		{
			// 칸 안의 위치(오르는 방향으로 -0.5 → +0.5) → L층 → L+1층
			const float u = (x - static_cast<float>(tx)) * static_cast<float>(dx) + (y - static_cast<float>(ty)) * static_cast<float>(dy);
			const float t = std::clamp(u + 0.5f, 0.f, 1.f);
			return static_cast<float>(L) + t;
		}
		return static_cast<float>(L);
	}

	[[nodiscard]] inline float GroundLevel(float x, float y) noexcept
	{
		return CellSurface(static_cast<int>(std::floor(x + 0.5f)), static_cast<int>(std::floor(y + 0.5f)), x, y);
	}

	[[nodiscard]] inline float SurfaceLevel(float x, float y, int prefX, int prefY) noexcept
	{
		const int tx = static_cast<int>(std::floor(x + 0.5f));
		const int ty = static_cast<int>(std::floor(y + 0.5f));
		if (!WorldData::IsInTown(tx, ty) || !WorldData::IsBlocked(tx, ty))
			return CellSurface(tx, ty, x, y);
		int bx = tx, by = ty;
		float bestD = 1e9f;
		for (int oy = -1; oy <= 1; ++oy)
			for (int ox = -1; ox <= 1; ++ox)
			{
				const int cx = tx + ox, cy = ty + oy;
				if (WorldData::IsBlocked(cx, cy))
					continue;
				const float ddx = (std::max)(0.f, std::fabs(x - static_cast<float>(cx)) - 0.5f);
				const float ddy = (std::max)(0.f, std::fabs(y - static_cast<float>(cy)) - 0.5f);
				const float d = ddx * ddx + ddy * ddy;
				if (d < bestD - 1e-6f || (d <= bestD + 1e-6f && cx == prefX && cy == prefY))
				{
					bestD = d; bx = cx; by = cy;
				}
			}
		return CellSurface(bx, by, x, y);   // 둘레가 모두 막혔으면 그 칸 그대로(난간 위 등)
	}

	inline constexpr float kJumpLimit = 0.5f;   // 한 간격에 이만큼 뛰면 끊김 — 한 층(1.0)의 절반

	struct Jump
	{
		float up = 0.f;     // 가장 큰 오르막 끊김(층)
		float down = 0.f;   // 가장 큰 내리막 끊김(층)
		[[nodiscard]] bool Ok() const noexcept { return up < kJumpLimit && down < kJumpLimit; }
	};

	// (ax, ay) → (bx, by) 곧은 선 위 몸의 높이 끊김. 논리 칸 = 도착 칸(클라는 걸음을 보내는 순간 논리 칸을 옮기고
	// 화면 위치가 뒤따른다 — 원격 개체도 알림을 받으면 도착 칸이 논리 칸).
	[[nodiscard]] inline Jump PathJump(float ax, float ay, float bx, float by, int prefX, int prefY, int perTile = 32) noexcept
	{
		Jump j;
		const float len = (std::max)(std::fabs(bx - ax), std::fabs(by - ay));
		const int n = (std::max)(1, static_cast<int>(std::ceil(len * static_cast<float>(perTile))));
		float prev = SurfaceLevel(ax, ay, prefX, prefY);
		for (int i = 1; i <= n; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(n);
			const float h = SurfaceLevel(ax + (bx - ax) * t, ay + (by - ay) * t, prefX, prefY);
			j.up = (std::max)(j.up, h - prev);
			j.down = (std::max)(j.down, prev - h);
			prev = h;
		}
		return j;
	}

	[[nodiscard]] inline Jump PathJump(int ax, int ay, int bx, int by, int perTile = 32) noexcept
	{
		return PathJump(static_cast<float>(ax), static_cast<float>(ay), static_cast<float>(bx), static_cast<float>(by), bx, by, perTile);
	}

	// 서버 이동 한 번(마을 밖이면 늘 true — 들판에는 높이가 없다). 걸음마다 부르므로 싸게:
	// 같은 층 평지의 한 칸 걸음(거의 전부)은 곧은 선이 두 칸(과 모서리 한 점)만 지나니 바로 통과,
	// 나머지만 1/8칸 간격으로 본다(계단은 1/8칸에 1/8층 — 끊김 기준 0.5층에 못 미친다).
	[[nodiscard]] inline bool PathOk(int ax, int ay, int bx, int by) noexcept
	{
		if (!WorldData::IsInTown(ax, ay) && !WorldData::IsInTown(bx, by))
			return true;
		int sdx = 0, sdy = 0;
		const int cheb = (std::max)(std::abs(bx - ax), std::abs(by - ay));
		if (cheb <= 1 && WorldData::TownLevel(ax, ay) == WorldData::TownLevel(bx, by)
			&& !WorldData::StairsDir(WorldData::TownCell(ax, ay), sdx, sdy) && !WorldData::StairsDir(WorldData::TownCell(bx, by), sdx, sdy))
			return true;
		return PathJump(ax, ay, bx, by, 8).Ok();
	}

	struct Report
	{
		int walkable = 0;   // 걷는 마을 칸
		int steps = 0;      // 일반 걸음(움직인 것만) — 칸 × 방향
		int forced = 0;     // 강제 이동 한 걸음(CleanStep 통과)
		int cliff = 0;      // 절벽 통과(오르막 끊김)
		int air = 0;        // 허공 걷기(내리막 끊김)
		int levelCells[5] = {};   // 층별 걷는 칸
		int firstX = -1, firstY = -1, firstDir = -1;
		bool firstForced = false;
		[[nodiscard]] bool Ok() const noexcept { return cliff == 0 && air == 0; }
	};

	[[nodiscard]] inline Report Check() noexcept
	{
		Report r;
		auto note = [&r](int x, int y, int dir, bool forced, const Jump& j)
		{
			if (j.up >= kJumpLimit) ++r.cliff;
			if (j.down >= kJumpLimit) ++r.air;
			if (!j.Ok() && r.firstX < 0)
			{
				r.firstX = x; r.firstY = y; r.firstDir = dir; r.firstForced = forced;
			}
		};
		auto merge = [](Jump a, const Jump& b) { a.up = (std::max)(a.up, b.up); a.down = (std::max)(a.down, b.down); return a; };
		for (int y = WorldData::kTownY0; y < WorldData::kTownY0 + WorldData::kTownH; ++y)
			for (int x = WorldData::kTownX0; x < WorldData::kTownX0 + WorldData::kTownW; ++x)
			{
				if (WorldData::IsBlocked(x, y))
					continue;
				++r.walkable;
				const int L = WorldData::TownLevel(x, y);
				if (L >= 0 && L <= 4) ++r.levelCells[L];
				for (int dir = 0; dir < 8; ++dir)
				{
					int dx = 0, dy = 0;
					MoveRule::Delta(dir, dx, dy);
					// 일반 걸음: 서버는 축마다(Y 먼저) 옮긴다 — 화면의 곧은 선과, 서버가 거친 칸을 지나는 두 직선을 모두 본다
					int nx = x, ny = y;
					MoveRule::Apply(nx, ny, dir);
					if (nx != x || ny != y)
					{
						++r.steps;
						Jump j = PathJump(x, y, nx, ny);
						if (nx != x && ny != y)
							j = merge(merge(j, PathJump(x, y, x, ny)), PathJump(x, ny, nx, ny));
						note(x, y, dir, false, j);
					}
					// 강제 이동 한 걸음: 곧은 선으로 미끄러진다
					if (MoveRule::CleanStep(x, y, dx, dy))
					{
						++r.forced;
						note(x, y, dir, true, PathJump(x, y, x + dx, y + dy));
					}
				}
			}
		return r;
	}
}
