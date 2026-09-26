#pragma once

#include "raylib.h"

// ─────────────────────────────────────────────────────────────────────────────
// 지형 높이(Phase 4-A) — 마을 칸의 층(TownMap)을 화면 높이로.
//
// 좌표 약속: 게임의 모든 위치(캐릭터 · 입자 · 데칼 · 소품)는 지금처럼 **바닥 평면(z = 0)의 아이소 px**다.
// 지형 높이는 정점을 낼 때(gfx) 더한다 — 세운 것은 발밑 한 점의 높이(gfx::SetLift), 바닥 데칼은 정점마다
// 그 자리 높이(gfx::SetDrape). 깊이(X+Y)는 바닥 위치에서 나오므로 높이와 무관하게 맞다.
// ─────────────────────────────────────────────────────────────────────────────
namespace hgt
{
	inline constexpr float kLevelZ = 0.75f;   // 1층 = 0.75z = 27px(계단 한 칸이 한 층)

	// 연속 칸 좌표(칸 중심 = 정수)의 지면 높이(z 단위). 마을 밖 = 0. 계단 칸은 오르는 방향으로 고르게 오른다.
	// 바닥 데칼 · 입자 · 빛처럼 "그 자리"를 따르는 것(막힌 칸 위면 그 칸 윗면 — 난간 위 등).
	[[nodiscard]] float GroundZ(float x, float y);
	[[nodiscard]] inline float GroundZ(Vector2 t) { return GroundZ(t.x, t.y); }
	// 몸(캐릭터 · NPC · 머리 위 글)이 설 높이: 화면 위치가 막힌 칸 위에 걸치면(모서리를 스치는 화면 경로 · 칸 경계 반올림)
	// 가장 가까운 걷는 칸의 윗면 — 걷는 동안 끊기지 않는다(TownRules::SurfaceLevel, 서버 검사와 같은 식).
	[[nodiscard]] float BodyZ(Vector2 t, int logicX, int logicY);
	[[nodiscard]] float BodyZ(Vector2 t);
	// 칸의 층(0~4) · 윗면 높이(z)
	[[nodiscard]] int Level(int x, int y);
	[[nodiscard]] inline float LevelZ(int x, int y) { return static_cast<float>(Level(x, y)) * kLevelZ; }
	// 바닥 평면 아이소 px → 그 자리 지면 높이(px, 위로 +)
	[[nodiscard]] float LiftPxAtIso(Vector2 groundIso);
	// 보이는 화면 점(높이가 들어간 아이소 px) → 그 픽셀에 보이는 지형 점의 연속 칸 좌표(앞의 면이 이긴다)
	[[nodiscard]] Vector2 Pick(Vector2 iso);
	// 마을에 높이가 있는가(없으면 모든 함수가 0 — 들판만 있는 빌드)
	[[nodiscard]] bool Any();

	// gfx에 드리우기 함수를 건다(시작 때 한 번)
	void Install();
}
