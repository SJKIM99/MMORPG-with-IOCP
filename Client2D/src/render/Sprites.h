#pragma once

#include "raylib.h"
#include "render/Draw.h"

#include <string>
#include <string_view>

// ─────────────────────────────────────────────────────────────────────────────
// 그린 스프라이트(Higgsfield로 만든 캐릭터 · 소품 그림) — 아틀라스 + 목록.
//
//  · assets/sprites/actors.txt · props.txt 한 줄 = 이름 쪽 x y w h 발x 발y 기준높이
//    (tools/assetgen/process.py가 만든다). 아틀라스는 actors_N.png · props_N.png.
//  · motion.txt · motion_N.png — 동작 영상에서 뽑은 한 주기 프레임(tools/assetgen/motion.py, 예: pc_knight_run00~11).
//  · 발(anchor)을 바닥 위치에 놓고, "서 있는 모습의 높이(기준높이)"를 월드 높이로 맞춰 그린다.
//    같은 캐릭터의 모든 동작은 같은 배율 — 동작이 바뀌어도 몸 크기가 튀지 않는다.
//  · 파일이 없거나 이름이 없으면 nullptr — 부르는 쪽은 예전 절차적 그림으로 대신한다.
//  · 월드 셰이더(알파 테스트 + 번쩍임)는 DrawWorld가 켠다(BeginWorld/EndWorld).
// ─────────────────────────────────────────────────────────────────────────────
namespace sprites
{
	struct Sprite
	{
		const Texture2D* tex = nullptr;
		Rectangle src{};
		Vector2 anchor{};   // 발(src 안의 px)
		float ref = 1.f;    // 이 캐릭터 · 소품의 서 있는 높이(px)
	};

	bool Init(const std::string& dir);   // assets/sprites
	void Shutdown();
	[[nodiscard]] bool Ready();

	[[nodiscard]] const Sprite* Find(std::string_view name);

	// 월드: 발을 base에, 기준높이를 worldH(px)로 맞춰 그린다(현재 깊이 · 틴트 · 번쩍임)
	void Draw(const Sprite& s, Vector2 base, float worldH, const gfx::SpriteXform& xf);

	// 화면(UI): dst 안에 비율을 지켜 맞춘다(아래 가운데 정렬)
	void DrawFit(const Sprite& s, Rectangle dst, Color tint = WHITE);

	// 월드 셰이더 — 알파 테스트(불투명 패스 0.45, 반투명 패스 0)와 번쩍임 풀기
	void BeginWorld(float alphaTest);
	void SetAlphaTest(float alphaTest);
	void EndWorld();
	[[nodiscard]] bool ShaderActive();
}
