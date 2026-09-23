#include "render/Lighting.h"
#include "render/Assets.h"
#include "core/Util.h"

#include "rlgl.h"
#include <cmath>
#include <ctime>

void Lighting::Shutdown()
{
	if (m_rt.id != 0)
		UnloadRenderTexture(m_rt);
	m_rt = RenderTexture2D{};
}

void Lighting::UpdateClock(float hourOverride)
{
	if (hourOverride >= 0.f)
		m_hour = std::fmod(hourOverride, 24.f);
	else
	{
		const double now = static_cast<double>(std::time(nullptr));
		m_hour = static_cast<float>(std::fmod(now, 1440.0) / 60.0);  // 24분 = 하루
	}

	// 밤 정도: 19~21시 어두워지고, 4~6시 밝아진다
	const float h = m_hour;
	float night;
	if (h >= 21.f || h < 4.f) night = 1.f;
	else if (h >= 19.f) night = (h - 19.f) / 2.f;
	else if (h < 6.f) night = 1.f - (h - 4.f) / 2.f;
	else night = 0.f;
	m_night = night * night * (3.f - 2.f * night);

	// 주변광: 낮 흰색 → 노을(주황) → 밤(푸른빛, 너무 어둡지 않게)
	const Color day{ 255, 255, 255, 255 };
	const Color dusk{ 255, 196, 160, 255 };
	const Color night2{ 104, 118, 176, 255 };
	if (m_night < 0.5f) m_ambient = util::Mix(day, dusk, m_night * 2.f);
	else m_ambient = util::Mix(dusk, night2, (m_night - 0.5f) * 2.f);
}

void Lighting::Apply(const Camera2D& cam, int screenW, int screenH, const std::vector<props::Light>& lights)
{
	if (m_night < 0.02f)
		return;

	if (m_rt.id == 0 || m_w != screenW || m_h != screenH)
	{
		Shutdown();
		m_rt = LoadRenderTexture(screenW, screenH);
		m_w = screenW;
		m_h = screenH;
	}

	const Texture2D& glow = assets::Get().glow;
	BeginTextureMode(m_rt);
	ClearBackground(m_ambient);
	BeginBlendMode(BLEND_ADDITIVE);
	for (const auto& l : lights)
	{
		const Vector2 s = GetWorldToScreen2D(l.iso, cam);
		const float r = l.radius * cam.zoom;
		if (s.x < -r || s.y < -r || s.x > screenW + r || s.y > screenH + r)
			continue;
		const float k = util::Clamp01(l.intensity) * m_night;
		const Color c{ static_cast<unsigned char>(l.color.r * k), static_cast<unsigned char>(l.color.g * k), static_cast<unsigned char>(l.color.b * k), 255 };
		DrawTexturePro(glow, Rectangle{ 0, 0, (float)glow.width, (float)glow.height }, Rectangle{ s.x, s.y, r * 2.f, r * 1.3f }, Vector2{ r, r * 0.65f }, 0.f, c);
	}
	EndBlendMode();
	EndTextureMode();

	BeginBlendMode(BLEND_MULTIPLIED);
	DrawTextureRec(m_rt.texture, Rectangle{ 0, 0, (float)m_rt.texture.width, -(float)m_rt.texture.height }, Vector2{ 0, 0 }, WHITE);
	EndBlendMode();
}
