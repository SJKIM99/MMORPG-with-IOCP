#pragma once

#include "raylib.h"

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 그린 효과(Higgsfield — 검은 바탕 효과판을 밝기 = 알파로 푼 것, tools/assetgen/process.py).
// assets/fx/<이름>.png 한 장 = 효과 하나. 파티클(Shape::Tex)로 띄워 크기 · 회전 · 색 변화를 준다.
// 파일이 없으면 nullptr — 부르는 쪽은 예전 절차적 효과만 쓴다.
// ─────────────────────────────────────────────────────────────────────────────
namespace fxart
{
	enum Id : int
	{
		SlashArc, SlashHeavy, Thrust, Claw,
		HitStar, HitCrit, HitRing, HitMagic,
		Flame, Fireball, Embers, FireRing,
		IceBurst, FrostCrystal, Snowflake, IceSpikes,
		Bolt, BoltFork, Arc, SparkBall,
		RuneCircle, RuneGlyph, Orb, PillarTeal,
		HealMotes, PillarGold, Halo, Dome,
		PoisonCloud, Venom, ShadowSmoke, DaggerFlash,
		Dust, Smoke, Dirt, Mist,
		LootBeam, Glint, CoinSpray, Glow,
		Count
	};

	bool Init(const std::string& dir);
	void Shutdown();
	[[nodiscard]] const Texture2D* Get(Id id);
}
