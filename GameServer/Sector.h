#pragma once

#include "GameObjectManager.h"

struct SectorCoord
{
	short x = -1;
	short y = -1;

	[[nodiscard]] constexpr bool IsAssigned() const noexcept
	{
		return x >= 0 && y >= 0;
	}

	constexpr bool operator==(const SectorCoord&) const noexcept = default;
};

class Sector
{
public:
	using SectorObjects = std::unordered_set<ObjID>;
	using NeighborSnapshot = std::vector<shared_ptr<Subject>>;
	static constexpr short kSectorWidth = static_cast<short>(W_WIDTH / SECTOR_RANGE);
	static constexpr short kSectorHeight = static_cast<short>(W_HEIGHT / SECTOR_RANGE);

public:
	Sector() = default;
	~Sector() = default;

	[[nodiscard]] bool IsValidSector(short sectorX, short sectorY) const noexcept;
	[[nodiscard]] bool IsValidSector(const SectorCoord& sector) const noexcept;
	[[nodiscard]] SectorCoord GetSectorCoord(short worldX, short worldY) const noexcept;
	[[nodiscard]] const SectorObjects& GetObjects(short sectorX, short sectorY) const;
	[[nodiscard]] NeighborSnapshot CollectNeighborObjects(short sectorX, short sectorY) const;
	[[nodiscard]] bool UpdateObjectSector(ObjID& subjectId, short worldX, short worldY, short& inOutSectorX, short& inOutSectorY);
	void RemoveObject(ObjID& objectId, short& inOutSectorX, short& inOutSectorY);

	template<typename Callback>
	requires std::invocable<Callback&, const shared_ptr<Subject>&>
	void ForEachNeighborObject(short sectorX, short sectorY, Callback&& callback) const
	{
		// Iterate over a shared_ptr snapshot so callbacks may safely remove objects
		// from sectors/object manager without invalidating the traversal.
		NeighborSnapshot snapshot = CollectNeighborObjects(sectorX, sectorY);
		for (const auto& object : snapshot)
			callback(object);
	}

private:
	using SectorGrid = std::array<std::array<SectorObjects, kSectorWidth>, kSectorHeight>;

	[[nodiscard]] SectorObjects& GetObjectsMutable(short sectorX, short sectorY);

private:
	SectorGrid _sectors;
};
