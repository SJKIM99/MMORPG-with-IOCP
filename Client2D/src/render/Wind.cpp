#include "render/Wind.h"

#include <algorithm>
#include <cmath>

namespace wind
{
	namespace
	{
		constexpr float kTau = 6.28318530718f;
		float g_t = 0.f;
		float g_strength = 0.4f;
		Vector2 g_tileDir{ 0.94f, -0.34f };   // 타일 좌표계(동북동으로 분다)
		Vector2 g_dir{ 0.97f, 0.22f };         // 화면(아이소 px)

		float Wave(float x) { return 0.5f + 0.5f * std::sin(x * kTau); }
	}

	void Update(float time)
	{
		g_t = time;
		// 방향: 서남서에서 불어와 동북동으로 — 97초 주기로 ±10° 남짓 흔들린다
		const float th = -0.35f + 0.18f * std::sin(time * kTau / 97.f);
		g_tileDir = Vector2{ std::cos(th), std::sin(th) };
		const float ix = (g_tileDir.x - g_tileDir.y) * 32.f, iy = (g_tileDir.x + g_tileDir.y) * 16.f;
		const float len = std::sqrt(ix * ix + iy * iy);
		g_dir = Vector2{ ix / len, iy / len };
		// 세기: 느린 부풂 두 겹 + 가끔 마을 전체에 드는 큰 돌풍
		const float base = 0.42f + 0.16f * std::sin(time * kTau / 53.f + 1.3f) + 0.08f * std::sin(time * kTau / 17.5f);
		const float big = std::pow(Wave(time / 11.f + 0.7f), 3.f) * 0.3f;
		g_strength = std::clamp(base + big, 0.f, 1.f);
	}

	float Time() { return g_t; }
	float Strength() { return g_strength; }
	Vector2 Dir() { return g_dir; }

	float Gust(Vector2 iso)
	{
		// 바람 방향으로 흘러가는 결 두 겹: 긴 결(파장 420px · 초당 126px) + 짧은 결(170px · 초당 128px)
		const float along = iso.x * g_dir.x + iso.y * g_dir.y;
		const float a = Wave(along / 420.f - g_t * 0.30f);
		const float b = Wave(along / 170.f - g_t * 0.75f + 0.31f);
		return std::pow(0.6f * a + 0.4f * b, 1.5f);
	}

	float At(Vector2 iso) { return std::clamp(g_strength * (0.55f + 0.75f * Gust(iso)), 0.f, 1.f); }

	float Sway(Vector2 iso, float freq, float seed)
	{
		const float s = At(iso);
		const float along = iso.x * g_dir.x + iso.y * g_dir.y;
		const float lean = s * 0.55f * (g_dir.x >= 0.f ? 1.f : -1.f);
		const float osc = std::sin(g_t * freq * (0.85f + 0.4f * s) + seed - along / 90.f) * (0.2f + 0.6f * s);
		return lean + osc;
	}

	float FlagWave(Vector2 iso, float u, float seed)
	{
		const float s = At(iso);
		const float f = 1.6f + 2.4f * s;   // 셀수록 빠르게 펄럭인다
		return std::sin((u * 1.4f - g_t * f) * kTau + seed) * u * (0.35f + 0.65f * s);
	}

	float FlagSpread(Vector2 iso) { return 0.3f + 0.7f * At(iso); }

	Vector4 ShaderParams() { return Vector4{ g_tileDir.x, g_tileDir.y, g_strength, g_t }; }
}
