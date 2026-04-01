#pragma once

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
	using SectorObjects = std::unordered_set<uint32>;
	static constexpr short kSectorWidth = static_cast<short>(W_WIDTH / SECTOR_RANGE);
	static constexpr short kSectorHeight = static_cast<short>(W_HEIGHT / SECTOR_RANGE);

public:
	Sector() = default;
	~Sector() = default;

	[[nodiscard]] bool IsValidSector(short sectorX, short sectorY) const noexcept;
	[[nodiscard]] bool IsValidSector(const SectorCoord& sector) const noexcept;
	[[nodiscard]] SectorCoord GetSectorCoord(short worldX, short worldY) const noexcept;
	[[nodiscard]] const SectorObjects& GetObjects(short sectorX, short sectorY) const;
	[[nodiscard]] bool UpdateObjectSector(uint32 objectId, short worldX, short worldY, short& inOutSectorX, short& inOutSectorY);
	void RemoveObject(uint32 objectId, short& inOutSectorX, short& inOutSectorY);

	template<typename Callback>
	requires std::invocable<Callback&, uint32>
	void ForEachNeighborObject(short sectorX, short sectorY, Callback&& callback) const
	{
		if (IsValidSector(sectorX, sectorY) == false)
			return;

		const short minY = std::max<short>(0, static_cast<short>(sectorY - 1));
		const short maxY = std::min<short>(static_cast<short>(kSectorHeight - 1), static_cast<short>(sectorY + 1));
		const short minX = std::max<short>(0, static_cast<short>(sectorX - 1));
		const short maxX = std::min<short>(static_cast<short>(kSectorWidth - 1), static_cast<short>(sectorX + 1));

		for (short currentY = minY; currentY <= maxY; ++currentY)
		{
			for (short currentX = minX; currentX <= maxX; ++currentX)
			{
				for (const uint32 objectId : _sectors[currentY][currentX])
				{
					callback(objectId);
				}
			}
		}
	}

private:
	using SectorGrid = std::array<std::array<SectorObjects, kSectorWidth>, kSectorHeight>;

	[[nodiscard]] SectorObjects& GetObjectsMutable(short sectorX, short sectorY);

private:
	SectorGrid _sectors;
};
