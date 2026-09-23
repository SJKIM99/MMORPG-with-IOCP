#pragma once

#include "Protocol.h"
#include "ZoneTypes.h"

// 격자 축 이름이 X/Z 인 이유: 새 좌표계에서 Y 는 **높이**이고, Sector/Zone 은
// XZ 평면만 나눈다(CLAUDE.md 3장 — "Y는 분할하지 않는다. 시야 처리에 Y 분할은
// 이득이 없고 복잡도만 늘린다"). 월드 크기 상수(W_WIDTH/W_HEIGHT)는 아직
// 2D 시절 이름 그대로인데, Week 1 의 3번 단계에서 리전별 런타임 값으로
// 바뀌면서 함께 정리된다.

class ZoneLayout
{
public:
	static constexpr short SectorCountX = static_cast<short>(W_WIDTH / SECTOR_RANGE);
	static constexpr short SectorCountZ = static_cast<short>(W_HEIGHT / SECTOR_RANGE);
	static constexpr short SectorsPerZoneX = 50;
	static constexpr short SectorsPerZoneZ = 50;
	static constexpr short ZoneCountX = static_cast<short>((SectorCountX + SectorsPerZoneX - 1) / SectorsPerZoneX);
	static constexpr short ZoneCountZ = static_cast<short>((SectorCountZ + SectorsPerZoneZ - 1) / SectorsPerZoneZ);
	static constexpr short ZoneCount = static_cast<short>(ZoneCountX * ZoneCountZ);

	static_assert(SectorCountX > 0 && SectorCountZ > 0);
	static_assert(ZoneCount > 0);

public:
	[[nodiscard]] static constexpr bool IsValidWorldPosition(float worldX, float worldZ) noexcept
	{
		return worldX >= 0.0f && worldX < static_cast<float>(W_WIDTH)
			&& worldZ >= 0.0f && worldZ < static_cast<float>(W_HEIGHT);
	}

	[[nodiscard]] static constexpr bool IsValidSector(short sectorX, short sectorZ) noexcept
	{
		return sectorX >= 0 && sectorX < SectorCountX
			&& sectorZ >= 0 && sectorZ < SectorCountZ;
	}

	[[nodiscard]] static constexpr bool IsValidZone(const ZoneCoord& zone) noexcept
	{
		return zone.x >= 0 && zone.x < ZoneCountX
			&& zone.z >= 0 && zone.z < ZoneCountZ;
	}

	[[nodiscard]] static constexpr bool IsValidZoneId(ZoneId zoneId) noexcept
	{
		return zoneId < static_cast<ZoneId>(ZoneCount);
	}

	[[nodiscard]] static constexpr ZoneCoord GetZoneCoordBySector(short sectorX, short sectorZ) noexcept
	{
		if (!IsValidSector(sectorX, sectorZ))
			return {};

		return ZoneCoord{
			static_cast<short>(sectorX / SectorsPerZoneX),
			static_cast<short>(sectorZ / SectorsPerZoneZ)
		};
	}

	[[nodiscard]] static constexpr ZoneCoord GetZoneCoordByWorld(float worldX, float worldZ) noexcept
	{
		if (!IsValidWorldPosition(worldX, worldZ))
			return {};

		return GetZoneCoordBySector(
			static_cast<short>(worldX / static_cast<float>(SECTOR_RANGE)),
			static_cast<short>(worldZ / static_cast<float>(SECTOR_RANGE)));
	}

	[[nodiscard]] static constexpr ZoneId GetZoneId(const ZoneCoord& zone) noexcept
	{
		if (!IsValidZone(zone))
			return InvalidZoneId;

		return static_cast<ZoneId>(zone.z * ZoneCountX + zone.x);
	}

	[[nodiscard]] static constexpr ZoneId GetZoneIdBySector(short sectorX, short sectorZ) noexcept
	{
		return GetZoneId(GetZoneCoordBySector(sectorX, sectorZ));
	}

	[[nodiscard]] static constexpr ZoneId GetZoneIdByWorld(float worldX, float worldZ) noexcept
	{
		return GetZoneId(GetZoneCoordByWorld(worldX, worldZ));
	}

	[[nodiscard]] static constexpr ZoneCoord GetZoneCoordById(ZoneId zoneId) noexcept
	{
		if (!IsValidZoneId(zoneId))
			return {};

		return ZoneCoord{
			static_cast<short>(zoneId % ZoneCountX),
			static_cast<short>(zoneId / ZoneCountX)
		};
	}
};
