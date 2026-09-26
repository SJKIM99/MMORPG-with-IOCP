#include "render/Art.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

// stb_image(공개 도메인, v2.30) — 이 번역 단위 안에서 JPEG만 쓴다(STB_IMAGE_STATIC).
// raylib 안의 stb_image와 이름이 부딪히지 않는다.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "third_party/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace art
{
	namespace
	{
		std::string g_dir = "assets/art";
		struct Slot { Texture2D tex{}; bool tried = false; };
		std::unordered_map<std::string, Slot> g_cache;

		const Texture2D* Load(const std::string& name)
		{
			Slot& s = g_cache[name];
			if (s.tried)
				return s.tex.id != 0 ? &s.tex : nullptr;
			s.tried = true;
			const std::string path = g_dir + "/" + name;
			if (!FileExists(path.c_str()))
				return nullptr;
			int size = 0;
			unsigned char* file = LoadFileData(path.c_str(), &size);
			if (!file)
				return nullptr;
			int w = 0, h = 0, n = 0;
			unsigned char* px = stbi_load_from_memory(file, size, &w, &h, &n, 4);
			UnloadFileData(file);
			if (!px)
				return nullptr;
			Image img{};
			img.data = px;
			img.width = w;
			img.height = h;
			img.mipmaps = 1;
			img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
			s.tex = LoadTextureFromImage(img);
			stbi_image_free(px);
			if (s.tex.id == 0)
				return nullptr;
			// 확대 · 축소가 잦다(초상화는 작게, 대표 그림은 화면 크기로) — 밉맵 + 3선형 필터
			GenTextureMipmaps(&s.tex);
			SetTextureFilter(s.tex, TEXTURE_FILTER_TRILINEAR);
			return &s.tex;
		}
	}

	void Init(const std::string& dir)
	{
		g_dir = dir;
	}

	void Shutdown()
	{
		for (auto& [name, s] : g_cache)
			if (s.tex.id != 0)
				UnloadTexture(s.tex);
		g_cache.clear();
	}

	const Texture2D* KeyArt() { return Load("key_art.jpg"); }
	const Texture2D* Cutin(int classId) { return classId >= 0 && classId < 3 ? Load("cutin_" + std::to_string(classId) + ".jpg") : nullptr; }

	const Texture2D* Chapter(int chapter)
	{
		if (chapter < 1 || chapter > 9) return nullptr;
		return Load("chapter_" + std::to_string(chapter) + ".jpg");
	}

	const Texture2D* Portrait(uint16_t npcId)
	{
		// 이름 있는 경비병은 초상화가 따로 있고(경비대장 야로), 나머지 경비병은 한 장을 같이 쓴다
		// Phase 4-B 교관 두샨 · 파수꾼 보그단도 경비병 그림을 빌려 입으므로 경비병 초상화를 같이 쓴다
		switch (npcId)
		{
		case 5: case 14: case 15: case 16: case 27: case 31: return Load("portrait_guard.jpg");
		default: break;
		}
		// 1~13: 처음 주민, 21~30 · 33: Phase 4-B 새 주민(한 장에 열 칸으로 그린 것 — tools/assetgen/portraits.py). 동물(17~20 · 32)은 없음
		if ((npcId >= 1 && npcId <= 13) || (npcId >= 21 && npcId <= 30) || npcId == 33)
			return Load("portrait_" + std::to_string(npcId) + ".jpg");
		return nullptr;
	}

	bool PreloadStep()
	{
		static const uint16_t kIds[] = { 6, 7, 1, 2, 3, 8, 9, 4, 5, 10, 11, 12, 13,   // 5 = 경비병 공용 초상화
			21, 22, 23, 24, 25, 26, 28, 29, 30, 33 };
		static size_t next = 0;
		if (next >= sizeof(kIds) / sizeof(kIds[0]))
			return false;
		(void)Portrait(kIds[next++]);
		return true;
	}

	namespace
	{
		// cover 잘라 내기: 원본에서 쓸 사각형과 배율
		struct Crop { Rectangle src; float scale; };
		Crop CoverCrop(const Texture2D& tex, Rectangle dst, Vector2 focus, float zoom)
		{
			const float tw = static_cast<float>(tex.width), th = static_cast<float>(tex.height);
			const float scale = std::max(dst.width / tw, dst.height / th) * std::max(1.f, zoom);
			const float sw = dst.width / scale, sh = dst.height / scale;   // 원본에서 잘라 낼 크기
			const float sx = std::clamp(focus.x * tw - sw * 0.5f, 0.f, tw - sw);
			const float sy = std::clamp(focus.y * th - sh * 0.5f, 0.f, th - sh);
			return Crop{ Rectangle{ sx, sy, sw, sh }, scale };
		}
	}

	void DrawCover(const Texture2D& tex, Rectangle dst, Color tint, Vector2 focus, float zoom)
	{
		if (tex.id == 0 || dst.width <= 0.f || dst.height <= 0.f) return;
		const Crop c = CoverCrop(tex, dst, focus, zoom);
		DrawTexturePro(tex, c.src, dst, Vector2{ 0, 0 }, 0.f, tint);
	}

	Vector2 CoverPoint(const Texture2D& tex, Rectangle dst, Vector2 focus, float zoom, Vector2 uv)
	{
		if (tex.id == 0 || dst.width <= 0.f || dst.height <= 0.f) return Vector2{ dst.x, dst.y };
		const Crop c = CoverCrop(tex, dst, focus, zoom);
		return Vector2{ dst.x + (uv.x * tex.width - c.src.x) * c.scale, dst.y + (uv.y * tex.height - c.src.y) * c.scale };
	}

	void Glint(Vector2 p, float size, Color c, float intensity)
	{
		if (intensity <= 0.01f || size <= 0.f) return;
		const float i = std::min(intensity, 1.5f);
		p.x = std::round(p.x);
		p.y = std::round(p.y);
		BeginBlendMode(BLEND_ADDITIVE);
		// 무리(넓고 옅게)
		DrawCircleGradient(static_cast<int>(p.x), static_cast<int>(p.y), size * 2.6f, util::WithA(c, 0.20f * i), util::WithA(c, 0.f));
		DrawCircleGradient(static_cast<int>(p.x), static_cast<int>(p.y), size * 1.0f, util::WithA(c, 0.30f * i), util::WithA(c, 0.f));
		// 십자 빛살: 가로가 길고 세로는 짧다(별 그림의 관례). 가운데가 가장 밝다.
		const int L = static_cast<int>(size * (2.0f + 1.4f * i));
		const int V = static_cast<int>(L * 0.62f);
		const Color ray = util::WithA(c, std::min(1.f, 0.85f * i));
		const Color none = util::WithA(c, 0.f);
		const int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
		DrawRectangleGradientH(x - L, y, L, 1, none, ray);
		DrawRectangleGradientH(x + 1, y, L, 1, ray, none);
		DrawRectangleGradientV(x, y - V, 1, V, none, ray);
		DrawRectangleGradientV(x, y + 1, 1, V, ray, none);
		// 흰 심
		DrawCircleV(Vector2{ p.x + 0.5f, p.y + 0.5f }, std::max(1.f, size * 0.28f), util::WithA(Color{ 255, 250, 238, 255 }, std::min(1.f, 0.9f * i)));
		EndBlendMode();
	}

	float StarPulse(float t, float phase)
	{
		t += phase;
		// 느린 숨(0.7~1.0) + 잔떨림 + 11초마다 한 번 번쩍(0.5초)
		float v = 0.78f + 0.16f * std::sin(t * 1.7f) + 0.06f * std::sin(t * 5.3f + 1.3f);
		const float f = std::fmod(t, 11.f) - 6.f;
		v += 0.55f * std::max(0.f, 1.f - std::fabs(f) / 0.45f);
		return v;
	}
}
