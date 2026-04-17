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

	// shared_ptr 스냅샷 없이 ObjID만 직접 순회한다.
	// 전제: 콜백이 섹터 내용을 수정하지 않아야 한다 (GameLogicThread 단일 소비자 보장).
	// 유저만 필요한 경우처럼, 카테고리 필터 후 필요한 객체만 조회하면
	// 몬스터 전체에 대한 GetGameObject + atomic refcount 증가를 생략할 수 있다.
	template<typename Callback>
	requires std::invocable<Callback&, const ObjID&>
	void ForEachNeighborObjID(short sectorX, short sectorY, Callback&& callback) const
	{
		if (!IsValidSector(sectorX, sectorY))
			return;

		const short minY = std::max<short>(0, static_cast<short>(sectorY - 1));
		const short maxY = std::min<short>(static_cast<short>(kSectorHeight - 1), static_cast<short>(sectorY + 1));
		const short minX = std::max<short>(0, static_cast<short>(sectorX - 1));
		const short maxX = std::min<short>(static_cast<short>(kSectorWidth - 1), static_cast<short>(sectorX + 1));

		for (short y = minY; y <= maxY; ++y)
			for (short x = minX; x <= maxX; ++x)
				for (const ObjID& id : _sectors[y][x])
					callback(id);
	}

private:
	using SectorGrid = std::array<std::array<SectorObjects, kSectorWidth>, kSectorHeight>;

	[[nodiscard]] SectorObjects& GetObjects(short sectorX, short sectorY);

private:
	SectorGrid _sectors;
};
