#pragma once

#include "GameObjectManager.h"
#include "Zone/ZoneLayout.h"

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

	// 이 Zone이 담당하는 섹터 범위: [offsetX .. offsetX+kLocalWidth) x [offsetY .. offsetY+kLocalHeight)
	// 좌표계는 전역 섹터 좌표 (0 ~ kTotalSectorCount-1) 를 그대로 사용한다.
	static constexpr short kLocalWidth  = ZoneLayout::SectorsPerZoneX;
	static constexpr short kLocalHeight = ZoneLayout::SectorsPerZoneY;

public:
	explicit Sector(short offsetX, short offsetY);

	[[nodiscard]] short GetOffsetX() const noexcept { return _offsetX; }
	[[nodiscard]] short GetOffsetY() const noexcept { return _offsetY; }

	[[nodiscard]] bool IsValidSector(short sectorX, short sectorY) const noexcept;
	[[nodiscard]] bool IsValidSector(const SectorCoord& sector) const noexcept;
	[[nodiscard]] SectorCoord GetSectorCoord(short worldX, short worldY) const noexcept;
	[[nodiscard]] const SectorObjects& GetObjects(short sectorX, short sectorY) const;
	[[nodiscard]] NeighborSnapshot CollectNeighborObjects(short sectorX, short sectorY) const;
	[[nodiscard]] bool UpdateObjectSector(ObjID& subjectId, short worldX, short worldY, short& inOutSectorX, short& inOutSectorY);
	bool UpdateObjectSectorAndPosition(ObjID& subjectId, short worldX, short worldY,
	                                   short& inOutSectorX, short& inOutSectorY,
	                                   short& inOutX, short& inOutY);
	void RemoveObject(ObjID& objectId, short& inOutSectorX, short& inOutSectorY);

	template<typename Callback>
	requires std::invocable<Callback&, const shared_ptr<Subject>&>
	void ForEachNeighborObject(short sectorX, short sectorY, Callback&& callback) const
	{
		NeighborSnapshot snapshot = CollectNeighborObjects(sectorX, sectorY);
		for (const auto& object : snapshot)
			callback(object);
	}

	template<typename Callback>
	requires std::invocable<Callback&, const ObjID&>
	void ForEachNeighborObjID(short sectorX, short sectorY, Callback&& callback) const
	{
		if (!IsValidSector(sectorX, sectorY))
			return;

		// 존 경계(offsetX ~ offsetX+kLocalWidth-1)에서 자연스럽게 탐색이 멈춘다.
		// 다른 존 섹터는 이 배열에 존재하지 않으므로 존 경계 = 시야 차단이 자동 적용된다.
		const short minY = std::max<short>(_offsetY, static_cast<short>(sectorY - 1));
		const short maxY = std::min<short>(static_cast<short>(_offsetY + kLocalHeight - 1), static_cast<short>(sectorY + 1));
		const short minX = std::max<short>(_offsetX, static_cast<short>(sectorX - 1));
		const short maxX = std::min<short>(static_cast<short>(_offsetX + kLocalWidth - 1), static_cast<short>(sectorX + 1));

		std::vector<ObjID> snapshot;
		for (short y = minY; y <= maxY; ++y)
			for (short x = minX; x <= maxX; ++x)
				for (const ObjID& id : _sectors[y - _offsetY][x - _offsetX])
					snapshot.push_back(id);

		for (const ObjID& id : snapshot)
			callback(id);
	}

private:
	using SectorGrid = std::array<std::array<SectorObjects, kLocalWidth>, kLocalHeight>;

	[[nodiscard]] SectorObjects& GetObjects(short sectorX, short sectorY);

private:
	short _offsetX;
	short _offsetY;
	SectorGrid _sectors;
};
