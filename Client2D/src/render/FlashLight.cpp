#include "render/FlashLight.h"
#include "render/Draw.h"
#include "fx/FxScale.h"
#include "render/Assets.h"
#include "core/Util.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace flashlight
{
	namespace
	{
		struct L { Vector2 iso; float r; Color c; float k; float t; float life; float attack; };
		std::vector<L> g_lights;
		// 곱-더하기는 원본 알파를 보지 않는다 — 색 자체가 가장자리로 0이 되는 둥근 빛(미리 곱한 알파)이 필요하다
		Texture2D g_falloff{};
		bool g_texTried = false;
		// 빛 모음(화면 ½ 해상도): 광원을 여기에 더한 뒤 곱-더하기를 한 번만 — 광원마다 곱하면 겹친 자리가 (1+a)(1+b)(1+c)…로
		// 불어나 하얗게 타 버렸다(단죄 절정: T4 조명 + 대상마다 T3 조명). 모은 빛은 8비트라 1에서 멈추고, 합성은 그 0.85배까지만 밝힌다
		RenderTexture2D g_acc{};
		constexpr float kCap = 0.85f;

		const Texture2D& Falloff()
		{
			if (!g_texTried)
			{
				g_texTried = true;
				constexpr int N = 128;
				Image img = GenImageColor(N, N, BLACK);
				auto* px = static_cast<Color*>(img.data);
				for (int y = 0; y < N; ++y)
					for (int x = 0; x < N; ++x)
					{
						const float dx = (x + 0.5f) / N * 2.f - 1.f, dy = (y + 0.5f) / N * 2.f - 1.f;
						const float r = std::sqrt(dx * dx + dy * dy);
						const float k = r >= 1.f ? 0.f : std::pow(1.f - r, 1.8f) * (1.f - r * r * 0.2f);
						const unsigned char v = static_cast<unsigned char>(std::clamp(k, 0.f, 1.f) * 255.f);
						px[y * N + x] = Color{ v, v, v, 255 };
					}
				g_falloff = LoadTextureFromImage(img);
				UnloadImage(img);
				SetTextureFilter(g_falloff, TEXTURE_FILTER_BILINEAR);
			}
			return g_falloff;
		}

		float Env(const L& l)
		{
			if (l.t < l.attack) return l.t / l.attack;
			const float u = (l.t - l.attack) / std::max(0.01f, l.life - l.attack);
			return std::exp(-u * 3.2f) * (1.f - u);   // 빠르게 식고 꼬리가 조금 남는다
		}
	}

	void Add(Vector2 iso, float radius, Color color, float intensity, float life, float attack)
	{
		intensity *= fxscale::Alpha();
		if (intensity <= 0.02f) return;
		if (g_lights.size() >= 48) g_lights.erase(g_lights.begin());
		iso.y -= gfx::LiftAt(iso);   // 지형 높이(Phase 4-A): 효과가 난 자리의 지면만큼 올린다
		g_lights.push_back(L{ iso, radius, color, intensity, 0.f, std::max(0.05f, life), std::clamp(attack, 0.005f, life * 0.5f) });
	}

	void Update(float dt)
	{
		for (auto& l : g_lights) l.t += dt;
		g_lights.erase(std::remove_if(g_lights.begin(), g_lights.end(), [](const L& l) { return l.t >= l.life; }), g_lights.end());
	}

	void Clear() { g_lights.clear(); }
	int Count() { return static_cast<int>(g_lights.size()); }

	void AppendTo(std::vector<props::Light>& lights)
	{
		for (const auto& l : g_lights)
		{
			const float e = Env(l);
			if (e <= 0.01f) continue;
			lights.push_back(props::Light{ l.iso, l.r * 1.25f, l.c, std::min(1.f, l.k * e * 1.4f) });
		}
	}

	void Apply(const Camera2D& cam, int screenW, int screenH, float night)
	{
		if (g_lights.empty()) return;
		const Texture2D& glow = Falloff();
		if (glow.id == 0) return;
		rlDrawRenderBatchActive();
		// ① 모든 광원을 빛 모음에 더한다(½ 해상도)
		const int aw = std::max(1, screenW / 2), ah = std::max(1, screenH / 2);
		if (g_acc.id == 0 || g_acc.texture.width != aw || g_acc.texture.height != ah)
		{
			if (g_acc.id != 0) UnloadRenderTexture(g_acc);
			g_acc = LoadRenderTexture(aw, ah);
			SetTextureFilter(g_acc.texture, TEXTURE_FILTER_BILINEAR);
		}
		const float dayK = 1.f - 0.5f * night;
		bool any = false;
		BeginTextureMode(g_acc);
		ClearBackground(BLACK);
		rlSetBlendMode(RL_BLEND_ADDITIVE);
		for (const auto& l : g_lights)
		{
			const float e = Env(l) * l.k * dayK;
			if (e <= 0.01f) continue;
			const Vector2 s = GetWorldToScreen2D(l.iso, cam);
			const float r = l.r * cam.zoom;
			if (s.x < -r || s.y < -r || s.x > screenW + r || s.y > screenH + r) continue;
			any = true;
			const Color c{ static_cast<unsigned char>(std::min(255.f, l.c.r * e)), static_cast<unsigned char>(std::min(255.f, l.c.g * e)),
				static_cast<unsigned char>(std::min(255.f, l.c.b * e)), 255 };
			const Vector2 h{ s.x * 0.5f, s.y * 0.5f };
			const float hr = r * 0.5f;
			// 바닥 타원 + 위로 조금 올린 둥근 빛(캐릭터 몸까지 밝힌다)
			DrawTexturePro(glow, Rectangle{ 0, 0, (float)glow.width, (float)glow.height }, Rectangle{ h.x, h.y, hr * 2.f, hr * 1.3f }, Vector2{ hr, hr * 0.65f }, 0.f, c);
			DrawTexturePro(glow, Rectangle{ 0, 0, (float)glow.width, (float)glow.height }, Rectangle{ h.x, h.y - hr * 0.35f, hr * 1.2f, hr * 1.2f }, Vector2{ hr * 0.6f, hr * 0.6f }, 0.f,
				Color{ static_cast<unsigned char>(c.r / 2), static_cast<unsigned char>(c.g / 2), static_cast<unsigned char>(c.b / 2), 255 });
		}
		rlDrawRenderBatchActive();
		rlSetBlendMode(RL_BLEND_ALPHA);
		EndTextureMode();
		if (!any) return;
		// ② 곱-더하기 한 번: 결과 = 목적색 + 모은 빛 × 목적색 × kCap(겹쳐도 최대 1.85배)
		rlSetBlendFactors(0x0306 /*GL_DST_COLOR*/, 1 /*GL_ONE*/, 0x8006 /*GL_FUNC_ADD*/);
		rlSetBlendMode(RL_BLEND_CUSTOM);
		// 밝은 바닥(gfx::SetBrightGround — 눈밭 · 모래)은 곱-더하기 여유가 없다(0.85 × 1.85 = 1.57 → 흰색): 그만큼 덜 밝힌다
		const float bright = gfx::BrightGround();
		const unsigned char cap = static_cast<unsigned char>(255.f * kCap * (1.f - 0.6f * bright));
		DrawTexturePro(g_acc.texture, Rectangle{ 0, 0, (float)aw, -(float)ah }, Rectangle{ 0, 0, (float)screenW, (float)screenH }, Vector2{ 0, 0 }, 0.f, Color{ cap, cap, cap, 255 });
		rlDrawRenderBatchActive();
		// 약한 더하기(검은 바닥도 조금 밝게)
		rlSetBlendMode(RL_BLEND_ADDITIVE);
		for (const auto& l : g_lights)
		{
			const float e = Env(l) * l.k * 0.22f * (1.f - 0.7f * bright);
			if (e <= 0.01f) continue;
			const Vector2 s = GetWorldToScreen2D(l.iso, cam);
			const float r = l.r * cam.zoom * 0.7f;
			if (s.x < -r || s.y < -r || s.x > screenW + r || s.y > screenH + r) continue;
			const Color c{ static_cast<unsigned char>(std::min(255.f, l.c.r * e)), static_cast<unsigned char>(std::min(255.f, l.c.g * e)),
				static_cast<unsigned char>(std::min(255.f, l.c.b * e)), 255 };
			DrawTexturePro(glow, Rectangle{ 0, 0, (float)glow.width, (float)glow.height }, Rectangle{ s.x, s.y, r * 2.f, r * 1.3f }, Vector2{ r, r * 0.65f }, 0.f, c);
		}
		rlDrawRenderBatchActive();
		rlSetBlendMode(RL_BLEND_ALPHA);
	}
}
