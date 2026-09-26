#pragma once

#include "raylib.h"
#include "net/Shared.h"

#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 지형 소품(나무/바위/수정/비석...)과 마을 구조물(성벽/망루/건물/분수/가판대).
// 모두 벡터 도형으로 그린다. 서버 충돌 칸(WorldData::IsBlocked)과 1:1로 대응한다 —
// "막힌 칸에는 반드시 무언가 보인다"가 원칙이다.
// ─────────────────────────────────────────────────────────────────────────────
namespace props
{
	// 밤 조명 수집(가로등/용암 등). Lighting이 한 프레임 동안 모아서 쓴다.
	struct Light
	{
		Vector2 iso;       // 아이소 px(바닥 기준)
		float radius;      // px
		Color color;
		float intensity;
	};
	std::vector<Light>& FrameLights();
	// 빛 하나 더하기 — 지금 그리는 것의 높이(gfx::Lift)만큼 올려 담는다(Phase 4-A). 직접 push_back하지 말 것.
	void PushLight(Light l);
	// 0 = 낮, 1 = 한밤(창문 불빛 등)
	void SetNight(float night);
	[[nodiscard]] float NightLevel();

	// 필드(마을 밖)의 막힌 칸 하나
	void DrawFieldObstacle(int x, int y, WorldData::Biome biome, float time);

	// 마을 칸(여러 칸짜리 구조물은 좌상단 칸에서 한 번만 그린다)
	void DrawTownCell(int x, int y, char cell, float time);
	// 건물 키트(render/Buildings.cpp — Phase 4-C): 종류 · 변형 · 구획 규칙으로 짓는다
	void DrawBuilding(const WorldData::BuildingRow& b, float time);
	// 건물을 다 그린 뒤 한 번: 모아 둔 굴뚝 연기(깊이 쓰기 끔) · 등불 빛(더하기)
	void DrawBuildingFx();
	// 그린 건물(assets/buildings — Phase 4-C): 폴더와 켜기(끄면 모두 키트 — 비교 캡처 · --kit-buildings)
	void SetBuildingArt(const std::string& dir, bool on);
	// 소품(props.txt — Phase 4-C): 막히는 소품 칸 하나(없으면 false) · 화면 칸 범위 안의 줄 소품(빨랫줄 · 줄깃발)
	bool DrawTownProp(int x, int y, float time);
	void DrawLineProps(int x0, int y0, int x1, int y1, float time);
	// 막힌 마을 칸 문자가 화면에 무언가로 그려지는가(보이지 않는 벽 검사 — --script terraincheck)
	[[nodiscard]] bool TownCellVisible(char cell);

	// 지역 → 바이옴 팔레트(셰이더 uniform용). rgb 0~1, style = Biome 값
	struct Palette { float a[3], b[3], c[3]; };
	Palette PaletteFor(int regionId);
}
