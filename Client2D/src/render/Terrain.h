#pragma once

#include "raylib.h"

// ─────────────────────────────────────────────────────────────────────────────
// 지형: 바닥 셰이더 + 화면 안의 소품/마을 구조물 그리기
// ─────────────────────────────────────────────────────────────────────────────
class Terrain
{
public:
	bool Init();
	void Shutdown();

	// 화면 공간에서 호출(BeginMode2D 밖). 카메라는 BeginMode2D에 쓰는 것과 같은 값.
	void DrawGround(const Camera2D& cam, int screenW, int screenH, Vector2 playerTile, float time, bool fog, float grid);
	// 월드 공간(BeginMode2D 안, 깊이 테스트 켠 상태)에서 호출
	void DrawProps(const Camera2D& cam, int screenW, int screenH, Vector2 playerTile, float time, bool fog);

	// 시야 밖 어둡게(셰이더와 같은 식) — 캐릭터/소품 색에 곱할 값
	static Color FogTint(float tileX, float tileY, Vector2 playerTile, bool fog);

	[[nodiscard]] bool Ready() const { return m_shader.id != 0; }

private:
	Shader    m_shader{};
	Texture2D m_town{};
	int m_locScreen = -1, m_locCamTarget = -1, m_locCamOffset = -1, m_locZoom = -1, m_locRot = -1;
	int m_locTime = -1, m_locPlayer = -1, m_locFog = -1, m_locGrid = -1;
	int m_locPalA = -1, m_locPalB = -1, m_locPalC = -1;
	int m_locTown = -1, m_locNoise = -1, m_locTownOrigin = -1, m_locTownSize = -1;
};
