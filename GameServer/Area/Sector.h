#pragma once

#include "GameObjectManager.h"
#include "World/WorldRegistry.h"

// Sector 격자 인덱스. XZ 평면만 나눈다 — Y(높이)는 나누지 않는다(CLAUDE.md 3장).
struct SectorCoord
{
	short x = -1;
	short z = -1;

	[[nodiscard]] constexpr bool IsAssigned() const noexcept
	{
		return x >= 0 && z >= 0;
	}

	constexpr bool operator==(const SectorCoord&) const noexcept = default;
};

class Sector
{
public:
	using SectorObjects = std::unordered_set<ObjID>;
	using NeighborSnapshot = std::vector<shared_ptr<Subject>>;

	// 이 Zone이 담당하는 섹터 범위: [offsetX .. offsetX+kLocalWidth) x [offsetZ .. offsetZ+kLocalDepth)
	// 좌표계는 **리전 안의** 섹터 좌표 (0 ~ sectorCount-1) 다.
	// 리전마다 크기가 다르므로 전역 좌표라는 개념이 더 이상 없다.
	static constexpr short kLocalWidth = ZoneGrid::kSectorsPerZone;
	static constexpr short kLocalDepth = ZoneGrid::kSectorsPerZone;

public:
	// region 은 이 Sector 가 속한 리전. 경계 판정에 그 리전의 크기를 쓴다 —
	// 마을(256m)과 필드(512m)가 같은 프로세스에 있으므로 월드 상수를 쓸 수 없다.
	Sector(RegionIndex region, short offsetX, short offsetZ);

	[[nodiscard]] RegionIndex GetRegion() const noexcept { return _region; }

	[[nodiscard]] short GetOffsetX() const noexcept { return _offsetX; }
	[[nodiscard]] short GetOffsetZ() const noexcept { return _offsetZ; }

	[[nodiscard]] bool IsValidSector(short sectorX, short sectorZ) const noexcept;
	[[nodiscard]] bool IsValidSector(const SectorCoord& sector) const noexcept;
	[[nodiscard]] SectorCoord GetSectorCoord(float worldX, float worldZ) const noexcept;
	[[nodiscard]] const SectorObjects& GetObjects(short sectorX, short sectorZ) const;
	[[nodiscard]] NeighborSnapshot CollectNeighborObjects(short sectorX, short sectorZ) const;
	[[nodiscard]] bool UpdateObjectSector(ObjID& subjectId, float worldX, float worldZ, short& inOutSectorX, short& inOutSectorZ);
	bool UpdateObjectSectorAndPosition(ObjID& subjectId, float worldX, float worldZ,
	                                   short& inOutSectorX, short& inOutSectorZ,
	                                   float& inOutX, float& inOutZ);
	void RemoveObject(ObjID& objectId, short& inOutSectorX, short& inOutSectorZ);

	template<typename Callback>
	requires std::invocable<Callback&, const shared_ptr<Subject>&>
	void ForEachNeighborObject(short sectorX, short sectorZ, Callback&& callback) const
	{
		NeighborSnapshot snapshot = CollectNeighborObjects(sectorX, sectorZ);
		for (const auto& object : snapshot)
			callback(object);
	}

	template<typename Callback>
	requires std::invocable<Callback&, const ObjID&>
	void ForEachNeighborObjID(short sectorX, short sectorZ, Callback&& callback) const
	{
		if (!IsValidSector(sectorX, sectorZ))
			return;

		// 존 경계(offsetX ~ offsetX+kLocalWidth-1)에서 자연스럽게 탐색이 멈춘다.
		// 다른 존 섹터는 이 배열에 존재하지 않으므로 존 경계 = 시야 차단이 자동 적용된다.
		const short minZ = std::max<short>(_offsetZ, static_cast<short>(sectorZ - 1));
		const short maxZ = std::min<short>(static_cast<short>(_offsetZ + kLocalDepth - 1), static_cast<short>(sectorZ + 1));
		const short minX = std::max<short>(_offsetX, static_cast<short>(sectorX - 1));
		const short maxX = std::min<short>(static_cast<short>(_offsetX + kLocalWidth - 1), static_cast<short>(sectorX + 1));

		std::vector<ObjID> snapshot;
		for (short y = minZ; y <= maxZ; ++y)
			for (short x = minX; x <= maxX; ++x)
				for (const ObjID& id : _sectors[y - _offsetZ][x - _offsetX])
					snapshot.push_back(id);

		for (const ObjID& id : snapshot)
			callback(id);
	}

private:
	using SectorGrid = std::array<std::array<SectorObjects, kLocalWidth>, kLocalDepth>;

	[[nodiscard]] SectorObjects& GetObjects(short sectorX, short sectorZ);

private:
	RegionIndex _region;
	short _offsetX;
	short _offsetZ;
	SectorGrid _sectors;
};
