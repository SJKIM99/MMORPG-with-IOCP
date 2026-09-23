#pragma once

#include "raylib.h"
#include "net/Shared.h"

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
	// 0 = 낮, 1 = 한밤(창문 불빛 등)
	void SetNight(float night);

	// 필드(마을 밖)의 막힌 칸 하나
	void DrawFieldObstacle(int x, int y, WorldData::Biome biome, float time);

	// 마을 칸(여러 칸짜리 구조물은 좌상단 칸에서 한 번만 그린다)
	void DrawTownCell(int x, int y, char cell, float time);
	void DrawBuilding(const WorldData::BuildingRow& b, float time);

	// 지역 → 바이옴 팔레트(셰이더 uniform용). rgb 0~1, style = Biome 값
	struct Palette { float a[3], b[3], c[3]; };
	Palette PaletteFor(int regionId);
}
