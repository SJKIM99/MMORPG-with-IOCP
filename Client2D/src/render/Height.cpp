#include "render/Height.h"
#include "render/Draw.h"
#include "net/Shared.h"
#include "Data/TownRules.h"

#include <cmath>

namespace hgt
{
	namespace
	{
		bool ComputeAny()
		{
			for (int y = 0; y < WorldData::kTownH; ++y)
				for (int x = 0; x < WorldData::kTownW; ++x)
					if (TownMap::kLevel[y][x] != '0') return true;
			return false;
		}
		const bool g_any = ComputeAny();
		constexpr float kMaxZ = 4.f * kLevelZ;
	}

	bool Any() { return g_any; }

	int Level(int x, int y)
	{
		return WorldData::TownLevel(x, y);
	}

	float GroundZ(float x, float y)
	{
		if (!g_any) return 0.f;
		return TownRules::GroundLevel(x, y) * kLevelZ;   // 서버 검사(TownRules::Check)와 같은 식
	}

	float BodyZ(Vector2 t, int logicX, int logicY)
	{
		if (!g_any) return 0.f;
		return TownRules::SurfaceLevel(t.x, t.y, logicX, logicY) * kLevelZ;
	}

	float BodyZ(Vector2 t)
	{
		if (!g_any) return 0.f;
		return TownRules::SurfaceLevel(t.x, t.y, static_cast<int>(std::floor(t.x + 0.5f)), static_cast<int>(std::floor(t.y + 0.5f))) * kLevelZ;
	}

	float LiftPxAtIso(Vector2 groundIso)
	{
		if (!g_any) return 0.f;
		const Vector2 t = gfx::IsoToTile(groundIso);
		return GroundZ(t.x, t.y) * gfx::kUnitZ;
	}

	Vector2 Pick(Vector2 iso)
	{
		const Vector2 flat = gfx::IsoToTile(iso);
		if (!g_any) return flat;
		// 마을에서 먼 곳은 계산하지 않는다(시선이 닿을 수 있는 가장 높은 곳 = 4층 → 바닥 위치로 9칸 앞까지)
		const float reach = kMaxZ * gfx::kUnitZ / gfx::kHalfH + 1.f;
		if (flat.x + reach < WorldData::kTownX0 - 1 || flat.y + reach < WorldData::kTownY0 - 1
			|| flat.x > WorldData::kTownX0 + WorldData::kTownW + 1 || flat.y > WorldData::kTownY0 + WorldData::kTownH + 1)
			return flat;
		// 시선 위의 점: 높이 z에서 바닥 위치 = IsoToTile(iso + (0, z·kUnitZ)). 위에서 아래로 걸어 내려가며
		// 처음 지형(윗면이나 옆면)에 닿는 곳이 그 픽셀에 보이는 점이다
		constexpr float kStep = 0.05f;
		for (float z = kMaxZ; z > 0.f; z -= kStep)
		{
			const Vector2 t = gfx::IsoToTile(Vector2{ iso.x, iso.y + z * gfx::kUnitZ });
			if (GroundZ(t.x, t.y) >= z - 1e-4f)
				return t;
		}
		return flat;
	}

	void Install()
	{
		gfx::SetGroundLiftFn(&LiftPxAtIso);
	}
}
