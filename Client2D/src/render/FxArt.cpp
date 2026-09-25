#include "render/FxArt.h"

#include <array>

namespace fxart
{
	namespace
	{
		// process.py FX_NAMES와 같은 이름 · 순서
		constexpr const char* kNames[Count] = {
			"slash_arc", "slash_heavy", "thrust", "claw",
			"hit_star", "hit_crit", "hit_ring", "hit_magic",
			"flame", "fireball", "embers", "fire_ring",
			"ice_burst", "frost_crystal", "snowflake", "ice_spikes",
			"bolt", "bolt_fork", "arc", "spark_ball",
			"rune_circle", "rune_glyph", "orb", "pillar_teal",
			"heal_motes", "pillar_gold", "halo", "dome",
			"poison_cloud", "venom", "shadow_smoke", "dagger_flash",
			"dust", "smoke", "dirt", "mist",
			"loot_beam", "glint", "coin_spray", "glow",
		};
		std::array<Texture2D, Count> g_tex{};
	}

	bool Init(const std::string& dir)
	{
		Shutdown();
		int n = 0;
		for (int i = 0; i < Count; ++i)
		{
			const std::string path = dir + "/" + kNames[i] + ".png";
			if (!FileExists(path.c_str()))
				continue;
			Texture2D t = LoadTexture(path.c_str());
			if (t.id == 0)
				continue;
			GenTextureMipmaps(&t);
			SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
			SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
			g_tex[i] = t;
			++n;
		}
		TraceLog(LOG_INFO, "FXART: %d painted effects", n);
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
