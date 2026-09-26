#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 바람 하나(Phase 4-B) — 방향 · 세기 하나로 깃발 · 빨래 · 나무 · 풀 · 굴뚝 연기 · 풍차 · 바람 소리가 함께 움직인다.
//
// 시간만으로 정해진다(난수 상태 없음) → 녹화 · 회귀가 늘 같은 바람을 본다. 돌풍은 바람 방향으로 흘러가는
// 결(화면 위치의 함수)이라 나무 한 줄이 차례로 휘고, 풀밭 위로 밝은 물결이 지나간다(지형 셰이더 uWind).
// 연출 전용 — 서버 판정과 무관하다.
// ─────────────────────────────────────────────────────────────────────────────
#include "raylib.h"

namespace wind
{
	// 매 프레임 한 번(게임 시각 초)
	void Update(float time);

	[[nodiscard]] float Time();
	// 전체 세기 0~1(느린 변화 + 마을 전체 돌풍)
	[[nodiscard]] float Strength();
	// 바람이 불어 가는 쪽(화면 좌표 단위 벡터 — 아이소 px 공간)
	[[nodiscard]] Vector2 Dir();
	// 이 자리(아이소 px)의 돌풍 0~1 — 바람 방향으로 흘러가는 결
	[[nodiscard]] float Gust(Vector2 iso);
	// 이 자리의 세기(전체 세기 × 돌풍) 0~1
	[[nodiscard]] float At(Vector2 iso);

	// 흔들림: 바람 쪽으로 기울기(lean) + 흔들림(osc). 반환값은 -1~1 근처의 가로 비율 — 그리는 쪽이 px로 곱한다.
	// freq는 흔들림 빠르기(나무 1.2 · 풀 2.5 · 깃발 5), seed는 개체마다 위상을 흩는 값
	[[nodiscard]] float Sway(Vector2 iso, float freq, float seed);

	// 깃발 · 천: 펄럭이는 물결(u = 깃대에서 끝까지 0~1)의 세로 변위 비율과 천이 뻗는 정도(0 늘어짐 ~ 1 팽팽)
	[[nodiscard]] float FlagWave(Vector2 iso, float u, float seed);
	[[nodiscard]] float FlagSpread(Vector2 iso);

	// 지형 셰이더에 넘길 값: xy = 방향(타일 좌표계 — 셰이더의 uv 기준), z = 세기, w = 결의 위상(시간 누적)
	[[nodiscard]] Vector4 ShaderParams();
}
