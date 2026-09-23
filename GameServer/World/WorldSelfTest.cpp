#include "pch.h"
#include "WorldSelfTest.h"
#include "WorldRegistry.h"

// Sector / Zone 수학 자가검사.
//
// 원래 3번 단계의 검증 수단은 STRESS_TEST 봇이었다. 봇을 3D 로 고치는 일을
// 뒤로 미뤘으므로 여기서 대신 확인한다 — 그리고 **이 쪽이 더 강하다.**
// 봇은 지나간 자리만 확인하지만 여기서는 격자를 전수로 훑는다.
//
// 축이 바뀌거나(X<->Z) 경계가 한 칸 어긋나도 월드가 정사각형이면 크래시 없이
// 조용히 틀린다. 그런 것을 잡으려고 만든 검사다.

namespace
{
	int g_failures = 0;

	void Fail(const std::string& what)
	{
		++g_failures;
		cout << "[worldtest] 실패 " << what << endl;
	}

	// 리전 하나를 전수로 훑는다.
	void CheckRegion(const WorldRegistry& world, RegionIndex index)
	{
		const RegionData& region = world.Region(index);
		const ZoneGrid& grid = world.Grid(index);

		// 1) 모든 Sector 칸의 중심이 자기 칸으로 되돌아오는가.
		//    좌표 -> Sector 변환이 한 칸 밀리면 여기서 걸린다.
		int badSector = 0;
		for (short sz = 0; sz < grid.sectorCountZ; ++sz)
		{
			for (short sx = 0; sx < grid.sectorCountX; ++sx)
			{
				const float cx = (sx + 0.5f) * grid.sectorSize;
				const float cz = (sz + 0.5f) * grid.sectorSize;
				if (!grid.Contains(cx, cz))
					continue;   // 리전 밖으로 삐져나온 마지막 칸

				if (grid.SectorX(cx) != sx || grid.SectorZ(cz) != sz)
					++badSector;
			}
		}
		if (badSector != 0)
			Fail(region.Id() + ": Sector 왕복 불일치 " + std::to_string(badSector) + "칸");

		// 2) 모든 Sector 가 유효한 Zone 에 속하고, 모든 Zone 이 최소 한 칸을 갖는가.
		//    Zone 하나가 비면 그 스레드는 영원히 놀고, 반대로 범위를 넘으면 크래시한다.
		std::vector<int> sectorsPerZone(grid.ZoneCount(), 0);
		int badZone = 0;
		for (short sz = 0; sz < grid.sectorCountZ; ++sz)
		{
			for (short sx = 0; sx < grid.sectorCountX; ++sx)
			{
				const ZoneId local = grid.LocalZoneBySector(sx, sz);
				if (local == InvalidZoneId || local >= static_cast<ZoneId>(grid.ZoneCount()))
				{
					++badZone;
					continue;
				}
				++sectorsPerZone[local];
			}
		}
		if (badZone != 0)
			Fail(region.Id() + ": Zone 범위를 벗어난 Sector " + std::to_string(badZone) + "칸");

		for (size_t z = 0; z < sectorsPerZone.size(); ++z)
		{
			if (sectorsPerZone[z] == 0)
				Fail(region.Id() + ": Zone " + std::to_string(z) + " 에 Sector 가 없다");
		}

		// 3) 경계. 0 은 안, size 는 밖이어야 한다. 부호를 뒤집거나 <= 로 쓰면 걸린다.
		if (!grid.Contains(0.0f, 0.0f))
			Fail(region.Id() + ": (0,0) 이 리전 밖으로 판정된다");
		if (grid.Contains(grid.sizeX, 0.0f))
			Fail(region.Id() + ": x = size 가 리전 안으로 판정된다");
		if (grid.Contains(0.0f, grid.sizeZ))
			Fail(region.Id() + ": z = size 가 리전 안으로 판정된다");
		if (grid.Contains(-0.01f, 0.0f) || grid.Contains(0.0f, -0.01f))
			Fail(region.Id() + ": 음수 좌표가 리전 안으로 판정된다");

		// 4) 리전 안을 반 칸 간격으로 훑어 ZoneAt 이 항상 유효한 값을 주는가.
		//    Sector 경유 결과와도 같아야 한다 — 두 경로가 갈라지면 이관이 꼬인다.
		const float step = grid.sectorSize * 0.5f;
		int badAt = 0;
		int mismatch = 0;
		int samples = 0;
		for (float z = 0.0f; z < grid.sizeZ; z += step)
		{
			for (float x = 0.0f; x < grid.sizeX; x += step)
			{
				++samples;
				const ZoneId zoneId = world.ZoneAt(index, x, z);
				if (!world.IsValidZoneId(zoneId))
				{
					++badAt;
					continue;
				}
				if (RegionOfZone(zoneId) != index)
				{
					++mismatch;
					continue;
				}
				const ZoneId viaSector =
					grid.LocalZoneBySector(grid.SectorX(x), grid.SectorZ(z));
				if (LocalZoneOf(zoneId) != viaSector)
					++mismatch;
			}
		}
		if (badAt != 0)
			Fail(region.Id() + ": 리전 안 " + std::to_string(badAt) + "곳에서 Zone 을 못 찾는다");
		if (mismatch != 0)
			Fail(region.Id() + ": 좌표 경로와 Sector 경로가 "
				+ std::to_string(mismatch) + "곳에서 다르다");

		// 5) 리전 밖은 반드시 거절해야 한다.
		if (world.ZoneAt(index, -1.0f, 0.0f) != InvalidZoneId
			|| world.ZoneAt(index, grid.sizeX, 0.0f) != InvalidZoneId
			|| world.ZoneAt(index, 0.0f, grid.sizeZ) != InvalidZoneId)
		{
			Fail(region.Id() + ": 리전 밖 좌표가 Zone 을 돌려준다");
		}

		cout << "[worldtest] " << region.Id()
			<< "  Sector " << grid.sectorCountX << "x" << grid.sectorCountZ
			<< "  Zone " << grid.ZoneCount()
			<< "  표본 " << samples << "곳 검사" << endl;
	}
}

namespace
{
	// 내비메시가 실제로 쿼리에 답하는지 본다.
	//
	// 폴리곤 수만 보는 것으로는 부족하다 — 감는 방향이 뒤집혔을 때 마을이
	// 9폴리곤으로 "빌드 성공" 했다. 걸어 다닐 지점을 실제로 찍어 봐야 안다.
	void CheckNav(const WorldRegistry& world, RegionIndex index)
	{
		const RegionData& region = world.Region(index);
		const ZoneGrid& grid = world.Grid(index);
		const NavMesh& nav = world.Nav(index);

		if (!nav.IsLoaded())
		{
			cout << "[worldtest] " << region.Id() << " 내비메시 없음 — 건너뜀" << endl;
			return;
		}

		// 검사용 쿼리. 실제 쿼리는 Zone 스레드가 각자 소유한다(7장) — 여기서는
		// 메인 스레드가 혼자 쓰므로 하나 만들어 쓰고 버린다.
		NavQuery query;
		if (!query.Init(nav))
		{
			Fail(region.Id() + ": NavQuery 초기화 실패");
			return;
		}

		// 1) 리전 곳곳이 내비메시 위에 있는가.
		//    물·건물 안은 당연히 아니므로 비율로 본다. 감는 방향이 뒤집히면
		//    여기서 0% 가 나온다.
		const float step = grid.sectorSize;
		int sampled = 0, onMesh = 0;
		float worstDrop = 0.0f;
		for (float z = step * 0.5f; z < grid.sizeZ; z += step)
		{
			for (float x = step * 0.5f; x < grid.sizeX; x += step)
			{
				++sampled;
				Vec3 snapped;
				if (!query.SampleWalkable(Vec3{ x, 0.0f, z }, 4.0f, snapped))
					continue;

				++onMesh;
				// 끌어당긴 지점이 원래 XZ 에서 너무 멀면 내비메시가 엉뚱한 곳에 있다.
				const float moved = Vec3{ x, snapped.y, z }.Distance2D(snapped);
				worstDrop = std::max<float>(worstDrop, moved);
			}
		}

		const int percent = sampled > 0 ? (onMesh * 100 / sampled) : 0;
		if (percent < 40)
			Fail(region.Id() + ": 내비메시 위 지점이 " + std::to_string(percent)
				+ "% 뿐이다 — 삼각형 감는 방향이나 경사 한계를 의심할 것");

		// 2) 스폰 지점은 반드시 걸을 수 있어야 한다. 아니면 로그인하자마자
		//    내비메시 밖에 서고, 6번 단계의 이동 검증이 전부 거부한다.
		Vec3 spawnSnap;
		if (!query.SampleWalkable(region.SpawnPoint(), 6.0f, spawnSnap))
			Fail(region.Id() + ": spawn_point 가 내비메시 밖이다");

		// 3) 경로가 실제로 나오는가. 리전을 가로지르는 두 점을 잡는다.
		const Vec3 from{ grid.sizeX * 0.25f, 0.0f, grid.sizeZ * 0.25f };
		const Vec3 to{ grid.sizeX * 0.75f, 0.0f, grid.sizeZ * 0.75f };
		const std::vector<Vec3> path = query.FindPath(from, to, 8.0f);
		if (path.size() < 2)
			Fail(region.Id() + ": 리전을 가로지르는 경로를 못 찾는다");

		// 4) 리전 밖은 거절해야 한다.
		Vec3 outside;
		if (query.SampleWalkable(Vec3{ -500.0f, 0.0f, -500.0f }, 4.0f, outside))
			Fail(region.Id() + ": 리전 밖 좌표가 내비메시 위라고 나온다");

		cout << "[worldtest] " << region.Id()
			<< "  nav poly " << nav.PolyCount()
			<< "  표본 " << sampled << "곳 중 " << onMesh << "곳(" << percent << "%) 통행 가능"
			<< "  최대 끌림 " << worstDrop << "m"
			<< "  경로 " << path.size() << "점" << endl;
	}
}

int WorldSelfTest::Run(const WorldRegistry& world)
{
	g_failures = 0;

	for (RegionIndex i = 0; i < static_cast<RegionIndex>(world.RegionCount()); ++i)
	{
		CheckRegion(world, i);
		CheckNav(world, i);
	}

	// 6) ZoneId 포장/해체 왕복. 리전을 상위 바이트에 박았으므로 여기서
	//    어긋나면 **다른 리전의 Zone 큐로 일이 흘러간다.** 조용히 틀리는 유형이다.
	const std::vector<ZoneId> all = world.AllZoneIds();
	std::unordered_set<ZoneId> seen;
	int badPack = 0;
	for (const ZoneId id : all)
	{
		if (!seen.insert(id).second)
		{
			Fail("ZoneId " + std::to_string(id) + " 가 중복된다 — 리전끼리 겹쳤다");
			continue;
		}
		if (!world.IsValidZoneId(id))
			++badPack;
		else if (MakeZoneId(RegionOfZone(id), LocalZoneOf(id)) != id)
			++badPack;
	}
	if (badPack != 0)
		Fail("ZoneId 왕복 실패 " + std::to_string(badPack) + "건");

	// 7) 없는 리전과 InvalidZoneId 는 거절해야 한다.
	if (world.IsValidZoneId(InvalidZoneId))
		Fail("InvalidZoneId 가 유효하다고 나온다");
	if (world.ZoneAt(InvalidRegionIndex, 0.0f, 0.0f) != InvalidZoneId)
		Fail("없는 리전이 Zone 을 돌려준다");

	cout << "[worldtest] " << (g_failures == 0 ? "OK  " : "실패")
		<< " Zone 합계 " << all.size() << "개, 실패 " << g_failures << "건" << endl;
	return g_failures;
}
