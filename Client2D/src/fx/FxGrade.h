#pragma once

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 연출 등급표(Phase 2-A · DESIGN_SYSTEM 10장) — 모든 기술은 등급 하나를 갖고, 등급이 예산을 정한다.
//
// | 등급 | 히트스톱 | 흔들림 | 화면 효과 | 순간 조명 | 입자 | 바닥 자국 | 소리 겹 |
// | T1 기본 공격 | 30~45ms | 0.08 | — | 1칸 · 0.1초 | 10~25 | — | 2 |
// | T2 일반 기술 | 50~70ms | 0.15~0.25 | 약한 블룸 | 2칸 · 0.2초 | 40~120 | 2초 | 3 |
// | T3 강기술 · 차지 완성 · 연계 | 80~110ms | 0.35~0.5 | 충격파 · 줌 펀치 3% | 4칸 · 0.35초 | 150~400 | 4~6초 | 4 |
// | T4 각성기 | 150~200ms | 0.7~0.9 | 암전 · 컷인 · 방사형 흐림 · 색수차 | 8칸 · 0.6초 | 600~1,500 | 8~10초 | 5 + 대사 |
//
// 칸 → 월드 px: 한 칸의 바닥 가로 반지름은 32px(아이소). "2칸 조명" = 반경 64px.
// ─────────────────────────────────────────────────────────────────────────────
namespace fxgrade
{
	enum class Tier : uint8_t { T1 = 1, T2 = 2, T3 = 3, T4 = 4 };

	struct Budget
	{
		float hitstop;        // 초(기본값 — 치명 · 연계 · 처치는 범위 위쪽까지)
		float hitstopMax;
		float trauma;
		float traumaMax;
		float zoomPunch;      // 0.03 = 3%
		float lightRadius;    // 월드 px
		float lightSec;
		int   particles;      // 대략(입자 수 목표 — 위쪽 한도)
		float decalSec;       // 바닥 자국 수명
		int   soundLayers;
		bool  bloomBoost;     // 약한 블룸 이상
		bool  shockwave;      // 충격파 왜곡
		bool  cinematic;      // 암전 · 컷인 · 방사형 흐림 · 색수차
	};

	inline constexpr Budget kBudget[5] = {
		{ 0.040f, 0.045f, 0.06f, 0.08f, 0.000f,  32.f, 0.10f,   25,  0.f, 2, false, false, false },   // (0 = T1과 같다)
		{ 0.035f, 0.045f, 0.06f, 0.08f, 0.000f,  32.f, 0.10f,   25,  0.f, 2, false, false, false },   // T1
		{ 0.055f, 0.070f, 0.15f, 0.25f, 0.010f,  64.f, 0.20f,  120,  2.f, 3, true,  false, false },   // T2
		{ 0.085f, 0.110f, 0.35f, 0.50f, 0.030f, 128.f, 0.35f,  400,  5.f, 4, true,  true,  false },   // T3
		{ 0.160f, 0.200f, 0.70f, 0.90f, 0.050f, 256.f, 0.60f, 1500,  9.f, 5, true,  true,  true  },   // T4
	};

	[[nodiscard]] constexpr const Budget& Get(Tier t) noexcept
	{
		const int i = static_cast<int>(t);
		return kBudget[i >= 1 && i <= 4 ? i : 1];
	}

	// 많이 맞힐수록 보상: 한 번에 5마리 이상이면 흔들림 +20%(상한 등급 최대치), 소리 한 겹 더
	[[nodiscard]] constexpr float MultiTargetShake(int targets) noexcept { return targets >= 5 ? 1.2f : 1.f; }
	[[nodiscard]] constexpr int MultiTargetLayers(int targets) noexcept { return targets >= 5 ? 1 : 0; }
}
