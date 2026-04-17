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

Sector::NeighborSnapshot Sector::CollectNeighborObjects(short sectorX, short sectorY) const
{
	NeighborSnapshot snapshot;

	if (IsValidSector(sectorX, sectorY) == false)
		return snapshot;

	const short minY = std::max<short>(0, static_cast<short>(sectorY - 1));
	const short maxY = std::min<short>(static_cast<short>(kSectorHeight - 1), static_cast<short>(sectorY + 1));
	const short minX = std::max<short>(0, static_cast<short>(sectorX - 1));
	const short maxX = std::min<short>(static_cast<short>(kSectorWidth - 1), static_cast<short>(sectorX + 1));

	for (short currentY = minY; currentY <= maxY; ++currentY)
	{
		for (short currentX = minX; currentX <= maxX; ++currentX)
		{
			for (const ObjID subjectId : _sectors[currentY][currentX])
			{
				if (const auto subject = ::GetGameObject<Subject>(subjectId); subject != nullptr)
					snapshot.push_back(subject);
			}
		}
	}

	return snapshot;
}

Sector::SectorObjects& Sector::GetObjects(short sectorX, short sectorY)
{
	ASSERT_CRASH(IsValidSector(sectorX, sectorY));
	return _sectors[sectorY][sectorX];
}

bool Sector::UpdateObjectSector(ObjID& subjectId, short worldX, short worldY, short& inOutSectorX, short& inOutSectorY)
{
	const SectorCoord nextSector = GetSectorCoord(worldX, worldY);
	if (IsValidSector(nextSector) == false)
		return false;

	if (inOutSectorX == nextSector.x && inOutSectorY == nextSector.y)
		return false;

	if (IsValidSector(inOutSectorX, inOutSectorY))
		GetObjects(inOutSectorX, inOutSectorY).erase(subjectId);

	GetObjects(nextSector.x, nextSector.y).insert(subjectId);
	inOutSectorX = nextSector.x;
	inOutSectorY = nextSector.y;
	return true;
}

void Sector::RemoveObject(ObjID& objectId, short& inOutSectorX, short& inOutSectorY)
{
	if (IsValidSector(inOutSectorX, inOutSectorY) == false)
		return;

	GetObjects(inOutSectorX, inOutSectorY).erase(objectId);
	inOutSectorX = -1;
	inOutSectorY = -1;
}
