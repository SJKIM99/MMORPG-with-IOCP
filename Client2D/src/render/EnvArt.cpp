#include "render/EnvArt.h"

#include <array>

namespace envart
{
	namespace
	{
		constexpr const char* kTexFile[static_cast<int>(Tex::Count)] = {
			"wall_plaster.jpg", "wall_stone.jpg", "roof_red.jpg", "roof_slate.jpg", "wood_planks.jpg", "wall_town.jpg" };

		std::array<Texture2D, static_cast<int>(Tex::Count)> g_tex{};
		Texture2D g_ground{};

		Texture2D LoadMip(const std::string& path, bool aniso)
		{
			if (!FileExists(path.c_str()))
				return Texture2D{};
			Texture2D t = LoadTexture(path.c_str());
			if (t.id == 0)
				return t;
			GenTextureMipmaps(&t);
			SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
			if (aniso) SetTextureFilter(t, TEXTURE_FILTER_ANISOTROPIC_8X);   // raylib: 이방성은 최대값만 바꾼다(삼선형 위에 얹는다)
			// 이음매 없는 텍스처 — 반복(면 가장자리에서 반대쪽 색을 읽어도 이어진다)
			SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
			return t;
		}
	}

	bool Init(const std::string& dir)
	{
		Shutdown();
		for (int i = 0; i < static_cast<int>(Tex::Count); ++i)
			g_tex[i] = LoadMip(dir + "/" + kTexFile[i], true);
		g_ground = LoadMip(dir + "/ground_atlas.jpg", false);
		if (g_ground.id != 0)
		{
			// 아틀라스는 칸 사이를 넘어가면 안 된다 — 셰이더가 칸 안에서만 읽지만, 가장자리는 붙잡아 둔다
			SetTextureWrap(g_ground, TEXTURE_WRAP_CLAMP);
			// 비스듬히 누운 바닥(아이소 2:1)이라 이방성 필터가 선명함을 지킨다
			SetTextureFilter(g_ground, TEXTURE_FILTER_ANISOTROPIC_8X);
		}
		int n = 0;
		for (const auto& t : g_tex) n += t.id != 0;
		TraceLog(LOG_INFO, "ENVART: ground %s, %d surface textures", g_ground.id ? "ok" : "missing", n);
		return g_ground.id != 0 || n > 0;
	}

	void Shutdown()
	{
		for (auto& t : g_tex)
		{
			if (t.id != 0) UnloadTexture(t);
			t = Texture2D{};
		}
		if (g_ground.id != 0) UnloadTexture(g_ground);
		g_ground = Texture2D{};
	}

	const Texture2D* Get(Tex t)
	{
		const Texture2D& tex = g_tex[static_cast<int>(t)];
		return tex.id != 0 ? &tex : nullptr;
	}

	const Texture2D* GroundAtlas() { return g_ground.id != 0 ? &g_ground : nullptr; }
}
