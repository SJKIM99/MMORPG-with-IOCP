#include "pch.h"
#include "Sector.h"

Sector::Sector(short offsetX, short offsetZ)
	: _offsetX(offsetX), _offsetZ(offsetZ)
{
}

bool Sector::IsValidSector(short sectorX, short sectorZ) const noexcept
{
	return sectorX >= _offsetX && sectorX < _offsetX + kLocalWidth
		&& sectorZ >= _offsetZ && sectorZ < _offsetZ + kLocalDepth;
}

bool Sector::IsValidSector(const SectorCoord& sector) const noexcept
{
	return IsValidSector(sector.x, sector.z);
}

SectorCoord Sector::GetSectorCoord(float worldX, float worldZ) const noexcept
{
	if (worldX < 0.0f || worldX >= static_cast<float>(W_WIDTH)
		|| worldZ < 0.0f || worldZ >= static_cast<float>(W_HEIGHT))
		return {};

	return SectorCoord{
		static_cast<short>(worldX / static_cast<float>(SECTOR_RANGE)),
		static_cast<short>(worldZ / static_cast<float>(SECTOR_RANGE))
	};
}

const Sector::SectorObjects& Sector::GetObjects(short sectorX, short sectorZ) const
{
	ASSERT_CRASH(IsValidSector(sectorX, sectorZ));
	return _sectors[sectorZ - _offsetZ][sectorX - _offsetX];
}

Sector::SectorObjects& Sector::GetObjects(short sectorX, short sectorZ)
{
	ASSERT_CRASH(IsValidSector(sectorX, sectorZ));
	return _sectors[sectorZ - _offsetZ][sectorX - _offsetX];
}

Sector::NeighborSnapshot Sector::CollectNeighborObjects(short sectorX, short sectorZ) const
{
	NeighborSnapshot snapshot;

	if (IsValidSector(sectorX, sectorZ) == false)
		return snapshot;

	std::vector<ObjID> idSnapshot;

	const short minZ = std::max<short>(_offsetZ, static_cast<short>(sectorZ - 1));
	const short maxZ = std::min<short>(static_cast<short>(_offsetZ + kLocalDepth - 1), static_cast<short>(sectorZ + 1));
	const short minX = std::max<short>(_offsetX, static_cast<short>(sectorX - 1));
	const short maxX = std::min<short>(static_cast<short>(_offsetX + kLocalWidth - 1), static_cast<short>(sectorX + 1));

	for (short currentZ = minZ; currentZ <= maxZ; ++currentZ)
		for (short currentX = minX; currentX <= maxX; ++currentX)
			for (const ObjID subjectId : _sectors[currentZ - _offsetZ][currentX - _offsetX])
				idSnapshot.push_back(subjectId);

	snapshot.reserve(idSnapshot.size());
	for (const ObjID subjectId : idSnapshot)
		if (const auto subject = ::GetGameObject<Subject>(subjectId); subject != nullptr)
			snapshot.push_back(subject);

	return snapshot;
}

bool Sector::UpdateObjectSector(ObjID& subjectId, float worldX, float worldZ, short& inOutSectorX, short& inOutSectorZ)
{
	const SectorCoord nextSector = GetSectorCoord(worldX, worldZ);
	if (IsValidSector(nextSector) == false)
		return false;

	if (inOutSectorX == nextSector.x && inOutSectorZ == nextSector.z)
		return false;

	if (IsValidSector(inOutSectorX, inOutSectorZ))
		GetObjects(inOutSectorX, inOutSectorZ).erase(subjectId);

	GetObjects(nextSector.x, nextSector.z).insert(subjectId);
	inOutSectorX = nextSector.x;
	inOutSectorZ = nextSector.z;
	return true;
}

bool Sector::UpdateObjectSectorAndPosition(
	ObjID& subjectId, float worldX, float worldZ,
	short& inOutSectorX, short& inOutSectorZ,
	float& inOutX, float& inOutZ)
{
	const SectorCoord nextSector = GetSectorCoord(worldX, worldZ);
	if (IsValidSector(nextSector) == false)
		return false;

	if (inOutSectorX != nextSector.x || inOutSectorZ != nextSector.z)
	{
		if (IsValidSector(inOutSectorX, inOutSectorZ))
			GetObjects(inOutSectorX, inOutSectorZ).erase(subjectId);

		GetObjects(nextSector.x, nextSector.z).insert(subjectId);
		inOutSectorX = nextSector.x;
		inOutSectorZ = nextSector.z;
	}

	inOutX = worldX;
	inOutZ = worldZ;
	return true;
}

void Sector::RemoveObject(ObjID& objectId, short& inOutSectorX, short& inOutSectorZ)
{
	if (IsValidSector(inOutSectorX, inOutSectorZ) == false)
		return;

	GetObjects(inOutSectorX, inOutSectorZ).erase(objectId);
	inOutSectorX = -1;
	inOutSectorZ = -1;
}
