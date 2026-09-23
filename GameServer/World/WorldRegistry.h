#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "RegionData.h"
#include "NavMesh.h"
#include "Zone/ZoneTypes.h"

// 여러 리전을 **동시에** 들고 있는 곳. Week 1 의 3번 단계.
//
// 2D 시절에는 월드가 하나(2000x2000 타일)뿐이라 좌표만 보면 Zone 을 알 수
// 있었다. 이제는 마을 256m, 필드 512m 가 같은 프로세스 안에 나란히 있으므로
// **좌표만으로는 Zone 이 정해지지 않는다.** 리전을 함께 알아야 한다.
//
// 그래서 ZoneId 에 리전을 박는다:
//
//     ZoneId = (리전 인덱스 << 8) | 리전 안에서의 Zone 번호
//
// 이렇게 두면 기존 Zone 이관 경로(Transform::m_transferring, Zone 큐 전달)를
// 고치지 않고 **리전을 넘는 이동까지 같은 길로 흘려보낼 수 있다.** 포탈을 붙일
// 때 이관 코드를 다시 건드리지 않으려고 지금 이 모양으로 해 둔다.

using RegionIndex = uint8_t;
inline constexpr RegionIndex InvalidRegionIndex = 0xFF;

// ZoneId 비트 나누기. 리전 최대 255개, 리전당 Zone 최대 255개면 충분하다.
inline constexpr int kZoneIdRegionShift = 8;
inline constexpr ZoneId kZoneIdLocalMask = 0x00FF;

[[nodiscard]] inline constexpr RegionIndex RegionOfZone(ZoneId zoneId) noexcept
{
	return static_cast<RegionIndex>(zoneId >> kZoneIdRegionShift);
}

[[nodiscard]] inline constexpr ZoneId LocalZoneOf(ZoneId zoneId) noexcept
{
	return static_cast<ZoneId>(zoneId & kZoneIdLocalMask);
}

[[nodiscard]] inline constexpr ZoneId MakeZoneId(RegionIndex region, ZoneId local) noexcept
{
	return static_cast<ZoneId>((static_cast<ZoneId>(region) << kZoneIdRegionShift) | local);
}

// 한 리전의 Sector / Zone 격자.
//
// 옛 ZoneLayout 이 하던 일을 **리전마다** 한다. constexpr 이었던 이유는 월드가
// 하나였기 때문이고, 이제는 리전 크기가 파일에서 오므로 런타임 값이다.
// 컴파일 타임 static_assert 로 지키던 것은 Validate() 로 옮겼다.
struct ZoneGrid
{
	// 한 Zone 이 담당하는 Sector 수 (한 변). Sector 격자를 std::array 로 두기
	// 위해 컴파일 타임 상수로 남긴다.
	//
	// 32m Sector x 4 = 128m 짜리 Zone. 마을(256m)은 2x2 = 4개, 필드(512m)는
	// 4x4 = 16개가 된다. 이 값을 데이터로 빼는 것은 Week 2 의 A/B/C 측정에서
	// Zone 수를 바꿔 가며 재는 손잡이가 필요해질 때 한다.
	static constexpr short kSectorsPerZone = 4;

	float sizeX = 0.0f;        // 리전 한 변 (m)
	float sizeZ = 0.0f;
	float sectorSize = 0.0f;   // Sector 한 변 (m)

	short sectorCountX = 0;
	short sectorCountZ = 0;
	short zoneCountX = 0;
	short zoneCountZ = 0;

	[[nodiscard]] short ZoneCount() const noexcept
	{
		return static_cast<short>(zoneCountX * zoneCountZ);
	}

	[[nodiscard]] bool Contains(float x, float z) const noexcept
	{
		return x >= 0.0f && x < sizeX && z >= 0.0f && z < sizeZ;
	}

	[[nodiscard]] bool IsValidSector(short sx, short sz) const noexcept
	{
		return sx >= 0 && sx < sectorCountX && sz >= 0 && sz < sectorCountZ;
	}

	// 범위 밖이면 {-1, -1}. 호출부가 반드시 확인해야 한다.
	[[nodiscard]] short SectorX(float x) const noexcept
	{
		return Contains(x, 0.0f) ? static_cast<short>(x / sectorSize) : static_cast<short>(-1);
	}

	[[nodiscard]] short SectorZ(float z) const noexcept
	{
		return Contains(0.0f, z) ? static_cast<short>(z / sectorSize) : static_cast<short>(-1);
	}

	[[nodiscard]] ZoneId LocalZoneBySector(short sx, short sz) const noexcept
	{
		if (!IsValidSector(sx, sz))
			return InvalidZoneId;

		const short zx = static_cast<short>(sx / kSectorsPerZone);
		const short zz = static_cast<short>(sz / kSectorsPerZone);
		return static_cast<ZoneId>(zz * zoneCountX + zx);
	}

	[[nodiscard]] ZoneId LocalZoneAt(float x, float z) const noexcept
	{
		if (!Contains(x, z))
			return InvalidZoneId;
		return LocalZoneBySector(SectorX(x), SectorZ(z));
	}

	// 리전 데이터에서 격자를 계산한다. 말이 되지 않으면 false.
	static bool Build(const RegionData& region, ZoneGrid& out, std::string& outError);
};

class WorldRegistry
{
public:
	// world/build/<id>.bin 을 순서대로 읽는다. 인덱스는 이 순서다.
	bool Load(const std::vector<std::string>& regionIds, std::string& outError);

	[[nodiscard]] size_t RegionCount() const noexcept { return _regions.size(); }
	[[nodiscard]] bool IsValidRegion(RegionIndex index) const noexcept
	{
		return index < _regions.size();
	}

	[[nodiscard]] const RegionData& Region(RegionIndex index) const { return *_regions[index].data; }
	[[nodiscard]] const ZoneGrid& Grid(RegionIndex index) const { return _regions[index].grid; }

	// 리전의 내비메시. **읽기 전용이라 모든 Zone 스레드가 공유한다** (7장).
	// 쿼리는 여기서 만들지 말고 Zone 스레드가 가진 NavQuery 를 쓸 것.
	[[nodiscard]] const NavMesh& Nav(RegionIndex index) const { return _regions[index].nav; }

	// 없으면 InvalidRegionIndex.
	[[nodiscard]] RegionIndex IndexOf(const std::string& regionId) const noexcept;

	// 새 플레이어가 들어오는 리전. Load() 에 넘긴 첫 번째다(마을).
	// 리전 간 이동이 붙으면 마지막 접속 위치에서 복원하게 되지만, 지금은
	// 저장된 좌표가 2D 시절 값이라 어차피 마을로 보내야 한다.
	[[nodiscard]] RegionIndex DefaultRegion() const noexcept { return 0; }

	// 리전 + 좌표 -> 전역 ZoneId. 범위 밖이면 InvalidZoneId.
	[[nodiscard]] ZoneId ZoneAt(RegionIndex region, float x, float z) const noexcept;

	[[nodiscard]] bool IsValidZoneId(ZoneId zoneId) const noexcept;

	// 모든 리전의 Zone 을 (전역 ZoneId) 순서대로 훑는다. ZoneManager 가 쓴다.
	[[nodiscard]] std::vector<ZoneId> AllZoneIds() const;

	// 몬스터가 생길 수 있는 Zone 들. 리전의 flags.spawn 을 따른다 —
	// 마을은 평화지역이라 false 다 (CLAUDE.md 9장).
	[[nodiscard]] std::vector<ZoneId> SpawnableZoneIds() const;

	// SpawnableZoneIds() 안에서 이 Zone 이 몇 번째인가. 없으면 -1.
	//
	// **몬스터 ID 를 Zone 끼리 겹치지 않게 나누는 데 쓴다.** ZoneId 를 그대로
	// 쓰면 안 된다 — 리전이 상위 바이트에 박혀 값이 성기므로, 마을 Zone 0 이
	// 만드는 수열(0, 4, 8, ...)과 필드 Zone 256 이 만드는 수열(256, 272, ...)이
	// 256 에서 부딪힌다. 실제로 그 충돌로 서버가 죽었다.
	[[nodiscard]] int SpawnSlotOf(ZoneId zoneId) const;

	void PrintSummary() const;

private:
	struct Entry
	{
		std::unique_ptr<RegionData> data;
		ZoneGrid grid;
		NavMesh nav;
	};

	std::vector<Entry> _regions;
};

extern WorldRegistry* GWorld;
