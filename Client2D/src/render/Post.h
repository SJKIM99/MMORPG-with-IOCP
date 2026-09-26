#pragma once

#include "raylib.h"

#include <cstdint>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// 화면 후처리(Phase 2-A) — 기술 빛이 주변을 물들이는 블룸 + 화면 왜곡 · 방사형 흐림 · 색수차 · 색 빠짐 · 암전.
//
//  · 격자 복사(grab pass): 월드를 기본 프레임버퍼(MSAA)에 그대로 그린 뒤, 효과가 있을 때만 화면을 텍스처로
//    복사(MSAA 풀기)해 한 번의 합성 셰이더로 다시 그린다 — 깊이 버퍼 기반 월드 패스 · 조명 곱하기를 건드리지 않는다.
//  · 블룸은 "효과 빛"에서만: 더하기 혼합으로 그린 효과를 반 해상도 빛 버퍼에 한 번 더 그리고(gfx 빛 전용 모드)
//    두 번 흐려 더한다 — 흰 돌 광장 · 눈밭이 번지지 않는다.
//  · 모든 효과는 수명 곡선을 가진 "사건"으로 넣는다(월드 좌표). 한 프레임에 충격파 8 · 아지랑이 4까지.
//  · 품질: 낮음 = 모두 끔(격자 복사 없음) · 보통 = 블룸 ¼ 해상도, 왜곡 켬 · 높음 = 블룸 ½ 해상도 + 두 단 흐림.
//  · 광과민 보호(SetPhotoSafe): 색수차 · 방사형 흐림 · 암전 · 색 빠짐의 세기를 절반으로(번쩍임은 fx::Effects가 따로).
// ─────────────────────────────────────────────────────────────────────────────
namespace post
{
	enum class Quality : uint8_t { Low, Medium, High };
	void SetQuality(Quality q);
	[[nodiscard]] Quality GetQuality();
	void SetPhotoSafe(bool on);
	[[nodiscard]] bool PhotoSafe();

	// ── 사건(월드 아이소 px) ──
	// 퍼지는 충격파 고리: 반경 r0 → r1(px, 월드), 굵기 width, 세기(+ 밖으로 밀기 / − 안으로)
	void Shockwave(Vector2 iso, float r0, float r1, float width, float strength, float life);
	// 열 아지랑이(원 안이 일렁인다)
	void HeatHaze(Vector2 iso, float radius, float strength, float life);
	// 순간이동 일그러짐(가운데로 빨려 들었다가 풀린다)
	void Warp(Vector2 iso, float radius, float strength, float life);
	// 방사형 흐림(돌진 · 각성기) — 세기 0~1
	void RadialBlur(Vector2 iso, float strength, float life);
	// 색수차(화면 가장자리로 갈수록 색이 갈라진다)
	void Chroma(float strength, float life);
	// 색 빠짐(완벽 회피 · 각성기)
	void Desaturate(float amount, float life);
	// 암전: 0 → amount(attack초) → 유지(hold초) → 0(release초). focus 주변은 덜 어둡다(각성기 대상)
	void Darken(Vector2 focusIso, float amount, float attack, float hold, float release);

	// realDt: 암전 · 색 빠짐(연출 시간표), worldDt: 충격파 · 아지랑이 · 흐림(히트스톱이면 같이 멈춘다)
	void Update(float realDt, float worldDt);
	void Clear();
	// 월드 · 조명 뒤, 화면 위 겹(비네트 · UI) 전에 부른다. drawEmissive: 빛 버퍼에 효과를 다시 그리는 함수
	// (호출 전에 BeginMode2D(반 해상도 카메라) · gfx 빛 전용 모드가 켜져 있다).
	void Apply(const Camera2D& cam, int screenW, int screenH, const std::function<void()>& drawEmissive);
	void Shutdown();

	// F3 계측
	struct Stats { int shocks = 0, heats = 0; bool grabbed = false; bool bloom = false; float bloomMs = 0.f; };
	[[nodiscard]] const Stats& LastStats();
}
