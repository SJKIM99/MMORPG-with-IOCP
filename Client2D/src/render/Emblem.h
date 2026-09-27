#pragma once

#include "raylib.h"

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 결사 문장 — 모양 12 × 바탕색 6(GuildRule::Emblem, 서버와 같은 번호).
// 그림 파일 없이 도형으로 그린다: 머리 위(14px) · 채팅 · 결사 창(72px) · 세우기 창 고르기 칸이 같은 함수를 쓴다.
// 바탕색은 흙 · 쇠 · 숲의 빛깔만(보라 · 네온 금지 — DESIGN_SYSTEM). 모양은 기사단 깃발에서 온 것들.
// ─────────────────────────────────────────────────────────────────────────────
namespace emblem
{
	[[nodiscard]] Color Field(int color);              // 바탕색
	[[nodiscard]] const char* ShapeName(int shape);    // 방패 · 종 · 별 · 늑대 · 매 · 참나무 · 탑 · 열쇠 · 칼 · 달 · 불꽃 · 물결
	[[nodiscard]] const char* ColorName(int color);    // 쪽빛 · 적갈 · 이끼 · 황토 · 먹 · 청록
	// center = 방패 가운데, size = 방패 높이(px). glow = 0~1 바깥 빛(세운 순간 · 고른 칸)
	void Draw(Vector2 center, float size, uint8_t emblem, float alpha = 1.f, float glow = 0.f);
	// 모양만 따로(고르기 칸에서 바탕 없이) — ink = 글자 색
	void DrawShape(Vector2 center, float size, int shape, Color ink, Color hole);
}
