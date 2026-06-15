#include "pch.h"
#include "Sector.h"

Sector::Sector(short offsetX, short offsetY)
	: _offsetX(offsetX), _offsetY(offsetY)
{
}

bool Sector::IsValidSector(short sectorX, short sectorY) const noexcept
{
	return sectorX >= _offsetX && sectorX < _offsetX + kLocalWidth
		&& sectorY >= _offsetY && sectorY < _offsetY + kLocalHeight;
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
	return _sectors[sectorY - _offsetY][sectorX - _offsetX];
}

Sector::SectorObjects& Sector::GetObjects(short sectorX, short sectorY)
{
	ASSERT_CRASH(IsValidSector(sectorX, sectorY));
	return _sectors[sectorY - _offsetY][sectorX - _offsetX];
}

Sector::NeighborSnapshot Sector::CollectNeighborObjects(short sectorX, short sectorY) const
{
	NeighborSnapshot snapshot;

	if (IsValidSector(sectorX, sectorY) == false)
		return snapshot;

	std::vector<ObjID> idSnapshot;

	const short minY = std::max<short>(_offsetY, static_cast<short>(sectorY - 1));
	const short maxY = std::min<short>(static_cast<short>(_offsetY + kLocalHeight - 1), static_cast<short>(sectorY + 1));
	const short minX = std::max<short>(_offsetX, static_cast<short>(sectorX - 1));
	const short maxX = std::min<short>(static_cast<short>(_offsetX + kLocalWidth - 1), static_cast<short>(sectorX + 1));

	for (short currentY = minY; currentY <= maxY; ++currentY)
		for (short currentX = minX; currentX <= maxX; ++currentX)
			for (const ObjID subjectId : _sectors[currentY - _offsetY][currentX - _offsetX])
				idSnapshot.push_back(subjectId);

	snapshot.reserve(idSnapshot.size());
	for (const ObjID subjectId : idSnapshot)
		if (const auto subject = ::GetGameObject<Subject>(subjectId); subject != nullptr)
			snapshot.push_back(subject);

	return snapshot;
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

bool Sector::UpdateObjectSectorAndPosition(
	ObjID& subjectId, short worldX, short worldY,
	short& inOutSectorX, short& inOutSectorY,
	short& inOutX, short& inOutY)
{
	const SectorCoord nextSector = GetSectorCoord(worldX, worldY);
	if (IsValidSector(nextSector) == false)
		return false;

	if (inOutSectorX != nextSector.x || inOutSectorY != nextSector.y)
	{
		if (IsValidSector(inOutSectorX, inOutSectorY))
			GetObjects(inOutSectorX, inOutSectorY).erase(subjectId);

		GetObjects(nextSector.x, nextSector.y).insert(subjectId);
		inOutSectorX = nextSector.x;
		inOutSectorY = nextSector.y;
	}

	inOutX = worldX;
	inOutY = worldY;
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
