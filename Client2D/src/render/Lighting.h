#pragma once

#include "raylib.h"
#include "render/Props.h"

#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 낮/밤 조명. 화면 크기 라이트맵에 "주변광 + 광원(가산)"을 그린 뒤 장면 위에
// 곱하기(multiply)로 덮는다. 낮에는 패스 자체를 건너뛴다.
//
// 하루 = 실제 24분(1분 = 게임 1시간). 시계를 유닉스 시간에서 뽑으므로 같은 시각의
// 모든 클라이언트가 같은 하늘을 본다(서버 동기화 없이도).
// ─────────────────────────────────────────────────────────────────────────────
class Lighting
{
public:
	void Shutdown();

	// hourOverride < 0 이면 실제 시계
	void UpdateClock(float hourOverride);
	[[nodiscard]] float Hour() const { return m_hour; }
	[[nodiscard]] float Night() const { return m_night; }          // 0 낮 ~ 1 한밤
	[[nodiscard]] Color Ambient() const { return m_ambient; }

	// 화면 공간 광원을 모아 라이트맵을 만들고 장면에 곱한다.
	void Apply(const Camera2D& cam, int screenW, int screenH, const std::vector<props::Light>& lights);

private:
	RenderTexture2D m_rt{};
	int m_w = 0, m_h = 0;
	float m_hour = 12.f;
	float m_night = 0.f;
	Color m_ambient = WHITE;
};
