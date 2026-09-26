#pragma once

#include "raylib.h"
#include "render/Props.h"

#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 순간 조명(Phase 2-A) — 타격 · 폭발마다 켜졌다 꺼지는 점광원(색 · 세기 · 반경 곡선).
//
//  · 낮: 월드를 다 그린 뒤 광원 자리를 "곱-더하기"(목적색 × (1 + 빛))로 밝힌다 — 흰 안개가 아니라
//    그 자리의 풀 · 돌 · 캐릭터 색이 그대로 밝아진다. 아주 어두운 바닥도 보이게 약한 더하기를 조금 섞는다.
//  · 밤: 기존 라이트맵(Lighting)에 광원으로 더해 어둠을 밀어낸다(+ 낮과 같은 곱-더하기를 약하게).
//  · 반경은 월드 px(바닥 타원 가로 1 : 세로 0.65). 켜짐(attack) → 유지 없음 → 꺼짐(나머지 수명, 지수 곡선).
// ─────────────────────────────────────────────────────────────────────────────
namespace flashlight
{
	void Add(Vector2 iso, float radius, Color color, float intensity, float life, float attack = 0.02f);
	void Update(float dt);
	void Clear();
	// 밤 라이트맵에 넣을 광원(Lighting::Apply 전에 모은 목록 뒤에 붙인다)
	void AppendTo(std::vector<props::Light>& lights);
	// 월드 · 조명 뒤, 화면 효과 전에(낮 · 밤 모두)
	void Apply(const Camera2D& cam, int screenW, int screenH, float night);
	[[nodiscard]] int Count();
}
