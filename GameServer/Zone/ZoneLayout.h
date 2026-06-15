#pragma once

#include "Protocol.h"
#include "ZoneTypes.h"

class ZoneLayout
{
public:
	static constexpr short SectorCountX = static_cast<short>(W_WIDTH / SECTOR_RANGE);
	static constexpr short SectorCountY = static_cast<short>(W_HEIGHT / SECTOR_RANGE);
	static constexpr short SectorsPerZoneX = 50;
	static constexpr short SectorsPerZoneY = 50;
	static constexpr short ZoneCountX = static_cast<short>((SectorCountX + SectorsPerZoneX - 1) / SectorsPerZoneX);
	static constexpr short ZoneCountY = static_cast<short>((SectorCountY + SectorsPerZoneY - 1) / SectorsPerZoneY);
	static constexpr short ZoneCount = static_cast<short>(ZoneCountX * ZoneCountY);

	static_assert(SectorCountX > 0 && SectorCountY > 0);
	static_assert(ZoneCount > 0);

public:
	[[nodiscard]] static constexpr bool IsValidWorldPosition(int worldX, int worldY) noexcept
	{
		return worldX >= 0 && worldX < W_WIDTH && worldY >= 0 && worldY < W_HEIGHT;
	}

	[[nodiscard]] static constexpr bool IsValidSector(short sectorX, short sectorY) noexcept
	{
		return sectorX >= 0 && sectorX < SectorCountX
			&& sectorY >= 0 && sectorY < SectorCountY;
	}

	[[nodiscard]] static constexpr bool IsValidZone(const ZoneCoord& zone) noexcept
	{
		return zone.x >= 0 && zone.x < ZoneCountX
			&& zone.y >= 0 && zone.y < ZoneCountY;
	}

	[[nodiscard]] static constexpr bool IsValidZoneId(ZoneId zoneId) noexcept
	{
		return zoneId < static_cast<ZoneId>(ZoneCount);
	}

	[[nodiscard]] static constexpr ZoneCoord GetZoneCoordBySector(short sectorX, short sectorY) noexcept
	{
		if (!IsValidSector(sectorX, sectorY))
			return {};

		return ZoneCoord{
			static_cast<short>(sectorX / SectorsPerZoneX),
			static_cast<short>(sectorY / SectorsPerZoneY)
		};
	}

	[[nodiscard]] static constexpr ZoneCoord GetZoneCoordByWorld(int worldX, int worldY) noexcept
	{
		if (!IsValidWorldPosition(worldX, worldY))
			return {};

		return GetZoneCoordBySector(
			static_cast<short>(worldX / SECTOR_RANGE),
			static_cast<short>(worldY / SECTOR_RANGE));
	}

	[[nodiscard]] static constexpr ZoneId GetZoneId(const ZoneCoord& zone) noexcept
	{
		if (!IsValidZone(zone))
			return InvalidZoneId;

		return static_cast<ZoneId>(zone.y * ZoneCountX + zone.x);
	}

	[[nodiscard]] static constexpr ZoneId GetZoneIdBySector(short sectorX, short sectorY) noexcept
	{
		return GetZoneId(GetZoneCoordBySector(sectorX, sectorY));
	}

	[[nodiscard]] static constexpr ZoneId GetZoneIdByWorld(int worldX, int worldY) noexcept
	{
		return GetZoneId(GetZoneCoordByWorld(worldX, worldY));
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
