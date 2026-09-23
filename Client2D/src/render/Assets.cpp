#include "render/Assets.h"
#include "core/Util.h"

#include <cmath>
#include <functional>

namespace assets
{
	namespace
	{
		Textures g_tex;

		// f(u, v) → (밝기 0~1, 알파 0~1). u, v ∈ [-1, 1]
		Texture2D Make(int w, int h, const std::function<Color(float, float)>& f, bool smooth = true)
		{
			Image img{};
			img.width = w;
			img.height = h;
			img.mipmaps = 1;
			img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
			img.data = MemAlloc(static_cast<unsigned int>(w * h * 4));
			auto* px = static_cast<unsigned char*>(img.data);
			for (int y = 0; y < h; ++y)
				for (int x = 0; x < w; ++x)
				{
					const float u = (x + 0.5f) / w * 2.f - 1.f;
					const float v = (y + 0.5f) / h * 2.f - 1.f;
					const Color c = f(u, v);
					unsigned char* p = px + (y * w + x) * 4;
					p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
				}
			Texture2D tex = LoadTextureFromImage(img);
			UnloadImage(img);
			SetTextureFilter(tex, smooth ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
			SetTextureWrap(tex, TEXTURE_WRAP_CLAMP);
			return tex;
		}

		Color White(float a) { return Color{ 255, 255, 255, static_cast<unsigned char>(util::Clamp01(a) * 255.f) }; }

		float ValueNoise(float x, float y, int period)
		{
			const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
			const float fx = x - ix, fy = y - iy;
			auto h = [&](int a, int b) { return util::Hash01(((a % period) + period) % period, ((b % period) + period) % period, 91); };
			const float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
			return util::Lerp(util::Lerp(h(ix, iy), h(ix + 1, iy), ux), util::Lerp(h(ix, iy + 1), h(ix + 1, iy + 1), ux), uy);
		}
	}

	void Load()
	{
		g_tex.glow = Make(128, 128, [](float u, float v)
		{
			const float d = std::sqrt(u * u + v * v);
			const float a = std::pow(util::Clamp01(1.f - d), 2.2f);
			return White(a);
		});

		g_tex.soft = Make(64, 64, [](float u, float v)
		{
			const float d = std::sqrt(u * u + v * v);
			return White(util::Clamp01((1.f - d) * 3.f));
		});

		g_tex.smoke = Make(128, 128, [](float u, float v)
		{
			const float d = std::sqrt(u * u + v * v);
			const float ang = std::atan2(v, u);
			const float wobble = 0.12f * std::sin(ang * 5.f) + 0.08f * std::sin(ang * 9.f + 1.3f);
			const float n = ValueNoise((u + 1.f) * 4.f, (v + 1.f) * 4.f, 8) * 0.35f;
			const float a = util::Clamp01((0.85f + wobble - d) * 2.2f) * (0.65f + n);
			return White(a);
		});

		g_tex.spark = Make(64, 16, [](float u, float v)
		{
			const float a = util::Clamp01(1.f - std::fabs(v) * 1.2f) * util::Clamp01(1.f - std::fabs(u));
			return White(std::pow(a, 1.5f));
		});

		g_tex.star = Make(64, 64, [](float u, float v)
		{
			const float d = std::sqrt(u * u + v * v);
			const float cross = std::max(util::Clamp01(1.f - std::fabs(u) * 9.f) * util::Clamp01(1.f - std::fabs(v)),
				util::Clamp01(1.f - std::fabs(v) * 9.f) * util::Clamp01(1.f - std::fabs(u)));
			const float core = std::pow(util::Clamp01(1.f - d * 2.2f), 2.f);
			return White(std::max(cross, core));
		});

		g_tex.ring = Make(128, 128, [](float u, float v)
		{
			const float d = std::sqrt(u * u + v * v);
			const float a = util::Clamp01(1.f - std::fabs(d - 0.86f) * 9.f);
			return White(a);
		});

		g_tex.beam = Make(32, 128, [](float u, float v)
		{
			const float across = std::pow(util::Clamp01(1.f - std::fabs(u)), 2.5f);
			const float along = util::Clamp01((1.f - v) * 0.5f);  // 아래(=1)는 진하고 위로 갈수록 옅게
			return White(across * std::pow(along, 0.6f));
		});

		g_tex.vignette = Make(256, 256, [](float u, float v)
		{
			const float d = std::sqrt(u * u * 0.9f + v * v * 1.1f);
			const float a = util::Clamp01((d - 0.55f) * 1.4f);
			return Color{ 0, 0, 0, static_cast<unsigned char>(a * a * 255.f) };
		});

		g_tex.noise = Make(256, 256, [](float u, float v)
		{
			const float x = (u + 1.f) * 0.5f * 16.f, y = (v + 1.f) * 0.5f * 16.f;
			float n = 0.f, amp = 0.5f, freq = 1.f;
			for (int o = 0; o < 4; ++o)
			{
				n += ValueNoise(x * freq, y * freq, static_cast<int>(16 * freq)) * amp;
				amp *= 0.5f; freq *= 2.f;
			}
			const unsigned char b = static_cast<unsigned char>(util::Clamp01(n) * 255.f);
			const unsigned char g = static_cast<unsigned char>(util::Hash01(static_cast<int>(x * 16), static_cast<int>(y * 16), 5) * 255.f);
			return Color{ b, g, b, 255 };
		});
		SetTextureWrap(g_tex.noise, TEXTURE_WRAP_REPEAT);
	}

	void Unload()
	{
		for (Texture2D* t : { &g_tex.glow, &g_tex.soft, &g_tex.smoke, &g_tex.spark, &g_tex.star, &g_tex.ring, &g_tex.beam, &g_tex.vignette, &g_tex.noise })
		{
			if (t->id != 0)
				UnloadTexture(*t);
			*t = Texture2D{};
		}
	}

	const Textures& Get() { return g_tex; }
}
