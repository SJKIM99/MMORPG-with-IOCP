#pragma once

#include "raylib.h"

#include <cstdint>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 일러스트(assets/art) — 대표 그림 · 장 그림 · NPC 초상화.
//
//  · 그림은 JPEG로 싣고(용량), 처음 쓸 때 읽는다(시작 시간 · 메모리 절약).
//    raylib 기본 빌드는 JPEG를 읽지 않으므로 stb_image로 직접 푼다(Art.cpp).
//  · 파일이 없으면 nullptr — 부르는 쪽은 예전 그림(절차적 그림)으로 대신한다.
//    그림이 빠져도 게임은 그대로 돌아간다.
//  · 스타일 규칙은 docs/DESIGN_SYSTEM.md 6장.
// ─────────────────────────────────────────────────────────────────────────────
namespace art
{
	void Init(const std::string& dir);   // assets/art
	void Shutdown();

	[[nodiscard]] const Texture2D* KeyArt();
	[[nodiscard]] const Texture2D* Chapter(int chapter);        // 1~5
	[[nodiscard]] const Texture2D* Portrait(uint16_t npcId);    // 없으면 nullptr(동물 등)

	// 한가할 때(로그인 화면) 한 프레임에 한 장씩 초상화를 미리 읽는다 — 대화창을 처음 열 때 멈칫하지 않게.
	// 더 읽을 것이 없으면 false.
	bool PreloadStep();

	// dst를 빈틈없이 채우도록(cover) 잘라 그린다. focus = 남길 중심(0~1), zoom ≥ 1이면 더 확대.
	void DrawCover(const Texture2D& tex, Rectangle dst, Color tint, Vector2 focus = Vector2{ 0.5f, 0.5f }, float zoom = 1.f);
	// 같은 인자로 DrawCover했을 때 그림 속 한 점(uv, 0~1)이 화면 어디에 오는가 — 그림 위에 빛 · 별을 얹을 때
	[[nodiscard]] Vector2 CoverPoint(const Texture2D& tex, Rectangle dst, Vector2 focus, float zoom, Vector2 uv);

	// 반짝임: 십자 빛살 + 무리 + 흰 심(더하기 혼합). 그림 속 별 · 불씨를 살아 있게 한다.
	// intensity 0 = 안 보임, 1 = 보통, 1.5까지 = 번쩍
	void Glint(Vector2 pos, float size, Color color, float intensity);
	// 샛별이 숨 쉬듯 반짝이는 세기(가끔 한 번 번쩍) — 로그인 · 장 그림이 같은 박자를 쓴다
	[[nodiscard]] float StarPulse(float time, float phase = 0.f);
}
