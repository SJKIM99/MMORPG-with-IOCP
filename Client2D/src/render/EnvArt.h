#pragma once

#include "raylib.h"

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 배경 그림(Higgsfield로 만든 바닥 · 벽 · 지붕 텍스처) — tools/assetgen/process.py가 만든다.
//
//  · terrain/ground_atlas.jpg: 바닥 텍스처 20장(지역 15 + 마을 5)을 5x4 칸에 모은 한 장.
//    칸마다 가장자리를 32px씩 감싸 두어(이음매 없는 반복) 셰이더가 textureGrad로 반복 · 밉맵을 제대로 읽는다.
//  · terrain/<이름>.jpg: 건물 벽 · 지붕 · 성벽 텍스처(3D 면에 gfx::FaceTex로 입힌다).
//  · 파일이 없으면 nullptr — 예전 절차적 바닥 · 단색 면으로 그린다.
// ─────────────────────────────────────────────────────────────────────────────
namespace envart
{
	enum class Tex { Plaster, Stone, RoofRed, RoofSlate, Wood, TownWall, Count };

	// 바닥 텍스처 칸 번호(ground_atlas의 순서 = catalog.py TERRAIN 순서 = Biome 값 + 마을 5)
	inline constexpr int kGroundCols = 5;
	inline constexpr int kGroundRows = 4;
	inline constexpr float kGroundCell = 576.f;   // 512 + 32 x 2
	inline constexpr float kGroundPad = 32.f;
	inline constexpr float kGroundTex = 512.f;
	enum Ground { TownCobble = 15, TownPlaza = 16, TownFlowerbed = 17, TownDirt = 18, TownStone = 19 };

	bool Init(const std::string& terrainDir);
	void Shutdown();

	[[nodiscard]] const Texture2D* Get(Tex t);
	[[nodiscard]] const Texture2D* GroundAtlas();
}
