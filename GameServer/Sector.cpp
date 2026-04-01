#include "pch.h"
#include "Sector.h"

bool Sector::IsValidSector(short sectorX, short sectorY) const noexcept
{
	return sectorX >= 0 && sectorX < kSectorWidth
		&& sectorY >= 0 && sectorY < kSectorHeight;
}

bool Sector::IsValidSector(const SectorCoord& sector) const noexcept
{
	return IsValidSector(sector.x, sector.y);
}

SectorCoord Sector::GetSectorCoord(short worldX, short worldY) const noexcept
{
	if (worldX < 0 || worldX >= W_WIDTH || worldY < 0 || worldY >= W_HEIGHT)
		return {};

	return SectorCoord{
		static_cast<short>(worldX / SECTOR_RANGE),
		static_cast<short>(worldY / SECTOR_RANGE)
	};
}

const Sector::SectorObjects& Sector::GetObjects(short sectorX, short sectorY) const
{
	ASSERT_CRASH(IsValidSector(sectorX, sectorY));
	return _sectors[sectorY][sectorX];
}

Sector::SectorObjects& Sector::GetObjectsMutable(short sectorX, short sectorY)
{
	ASSERT_CRASH(IsValidSector(sectorX, sectorY));
	return _sectors[sectorY][sectorX];
}

bool Sector::UpdateObjectSector(uint32 objectId, short worldX, short worldY, short& inOutSectorX, short& inOutSectorY)
{
	const SectorCoord nextSector = GetSectorCoord(worldX, worldY);
	if (IsValidSector(nextSector) == false)
		return false;

	if (inOutSectorX == nextSector.x && inOutSectorY == nextSector.y)
		return false;

	if (IsValidSector(inOutSectorX, inOutSectorY))
		GetObjectsMutable(inOutSectorX, inOutSectorY).erase(objectId);

	GetObjectsMutable(nextSector.x, nextSector.y).insert(objectId);
	inOutSectorX = nextSector.x;
	inOutSectorY = nextSector.y;
	return true;
}

void Sector::RemoveObject(uint32 objectId, short& inOutSectorX, short& inOutSectorY)
{
	if (IsValidSector(inOutSectorX, inOutSectorY) == false)
		return;

	GetObjectsMutable(inOutSectorX, inOutSectorY).erase(objectId);
	inOutSectorX = -1;
	inOutSectorY = -1;
}
