#include "ui/UiArt.h"

#include <array>

namespace uiart
{
	namespace
	{
		constexpr const char* kFiles[Count] = { "panel_tex.png", "corner.png", "divider.png", "plaque.png",
			"cursor_arrow.png", "cursor_attack.png", "cursor_pick.png", "cursor_talk.png" };
		std::array<Texture2D, Count> g_tex{};
	}

	bool Init(const std::string& dir)
	{
		Shutdown();
		int n = 0;
		for (int i = 0; i < Count; ++i)
		{
			const std::string path = dir + "/" + kFiles[i];
			if (!FileExists(path.c_str()))
				continue;
			Texture2D t = LoadTexture(path.c_str());
			if (t.id == 0)
				continue;
			GenTextureMipmaps(&t);
			SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
			// 바탕 돌결은 반복해서 깐다
			SetTextureWrap(t, i == PanelTex ? TEXTURE_WRAP_REPEAT : TEXTURE_WRAP_CLAMP);
			g_tex[i] = t;
			++n;
		}
		TraceLog(LOG_INFO, "UIART: %d pieces", n);
		return n > 0;
	}

	void Shutdown()
	{
		for (auto& t : g_tex)
		{
			if (t.id != 0) UnloadTexture(t);
			t = Texture2D{};
		}
	}

	const Texture2D* Get(Id id)
	{
		if (id < 0 || id >= Count) return nullptr;
		return g_tex[id].id != 0 ? &g_tex[id] : nullptr;
	}
}
