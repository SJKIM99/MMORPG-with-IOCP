#include "pch.h"
#include "WorldRegistry.h"

#include <filesystem>

WorldRegistry* GWorld = nullptr;

bool ZoneGrid::Build(const RegionData& region, ZoneGrid& out, std::string& outError)
{
	out.sizeX      = region.SizeX();
	out.sizeZ      = region.SizeZ();
	out.sectorSize = region.SectorSize();

	if (out.sizeX <= 0.0f || out.sizeZ <= 0.0f)
	{
		outError = region.Id() + ": 리전 크기가 0 이하다";
		return false;
	}
	if (out.sectorSize <= 0.0f)
	{
		outError = region.Id() + ": sector_size 가 0 이하다";
		return false;
	}

	// 나누어떨어지지 않으면 올림해서 마지막 칸이 리전 밖을 조금 덮게 둔다.
	// Contains() 가 좌표를 먼저 걸러 주므로 밖의 칸에는 아무도 들어오지 않는다.
	out.sectorCountX = static_cast<short>(std::ceil(out.sizeX / out.sectorSize));
	out.sectorCountZ = static_cast<short>(std::ceil(out.sizeZ / out.sectorSize));
	out.zoneCountX = static_cast<short>(
		(out.sectorCountX + kSectorsPerZone - 1) / kSectorsPerZone);
	out.zoneCountZ = static_cast<short>(
		(out.sectorCountZ + kSectorsPerZone - 1) / kSectorsPerZone);

	// 옛 ZoneLayout 이 static_assert 로 지키던 것을 여기로 옮겼다.
	// 크기가 파일에서 오므로 컴파일 타임에 확인할 수 없다.
	if (out.sectorCountX <= 0 || out.sectorCountZ <= 0)
	{
		outError = region.Id() + ": Sector 수가 0 이다";
		return false;
	}
	if (out.ZoneCount() <= 0 || out.ZoneCount() > kZoneIdLocalMask)
	{
		outError = region.Id() + ": Zone 수 " + std::to_string(out.ZoneCount())
			+ " 가 ZoneId 하위 바이트에 들어가지 않는다";
		return false;
	}
	return true;
}

bool WorldRegistry::Load(const std::vector<std::string>& regionIds, std::string& outError)
{
	if (regionIds.size() > kZoneIdLocalMask)
	{
		outError = "리전이 너무 많다 — ZoneId 상위 바이트에 들어가지 않는다";
		return false;
	}

	_regions.clear();
	_regions.reserve(regionIds.size());

	for (const std::string& id : regionIds)
	{
		const std::string path = RegionData::FindRegionFile(id);
		if (path.empty())
		{
			outError = id + ".bin 을 찾지 못했다 — tools/build_region.py 를 먼저 돌려라";
			return false;
		}

		Entry entry;
		entry.data = RegionData::Load(path, outError);
		if (entry.data == nullptr)
			return false;

		if (!ZoneGrid::Build(*entry.data, entry.grid, outError))
			return false;

		// 내비메시는 **있으면 쓰고 없으면 넘어간다.** 아직 안 구운 리전이 있을
		// 수 있고, 그때 서버가 아예 안 뜨는 것보다는 "내비 없음" 으로 도는 편이
		// 낫다 — 이동 검증이 붙는 6번 단계에서 필수로 바꾼다.
		std::filesystem::path navPath(path);
		navPath.replace_extension(".navmesh");
		if (std::filesystem::exists(navPath))
		{
			std::string navError;
			if (!entry.nav.Load(navPath.string(), navError))
			{
				outError = navError;
				return false;
			}
		}

		_regions.push_back(std::move(entry));
	}
	return true;
}

RegionIndex WorldRegistry::IndexOf(const std::string& regionId) const noexcept
{
	for (size_t i = 0; i < _regions.size(); ++i)
	{
		if (_regions[i].data->Id() == regionId)
			return static_cast<RegionIndex>(i);
	}
	return InvalidRegionIndex;
}

ZoneId WorldRegistry::ZoneAt(RegionIndex region, float x, float z) const noexcept
{
	if (!IsValidRegion(region))
		return InvalidZoneId;

	const ZoneId local = _regions[region].grid.LocalZoneAt(x, z);
	if (local == InvalidZoneId)
		return InvalidZoneId;

	return MakeZoneId(region, local);
}

bool WorldRegistry::IsValidZoneId(ZoneId zoneId) const noexcept
{
	if (zoneId == InvalidZoneId)
		return false;

	const RegionIndex region = RegionOfZone(zoneId);
	if (!IsValidRegion(region))
		return false;

	return LocalZoneOf(zoneId) < static_cast<ZoneId>(_regions[region].grid.ZoneCount());
}

std::vector<ZoneId> WorldRegistry::AllZoneIds() const
{
	std::vector<ZoneId> out;
	for (size_t r = 0; r < _regions.size(); ++r)
	{
		const short count = _regions[r].grid.ZoneCount();
		for (short local = 0; local < count; ++local)
			out.push_back(MakeZoneId(static_cast<RegionIndex>(r), static_cast<ZoneId>(local)));
	}
	return out;
}

std::vector<ZoneId> WorldRegistry::SpawnableZoneIds() const
{
	std::vector<ZoneId> out;
	for (size_t r = 0; r < _regions.size(); ++r)
	{
		if (!_regions[r].data->SpawnAllowed())
			continue;

		const short count = _regions[r].grid.ZoneCount();
		for (short local = 0; local < count; ++local)
			out.push_back(MakeZoneId(static_cast<RegionIndex>(r), static_cast<ZoneId>(local)));
	}
	return out;
}

int WorldRegistry::SpawnSlotOf(ZoneId zoneId) const
{
	const std::vector<ZoneId> ids = SpawnableZoneIds();
	for (size_t i = 0; i < ids.size(); ++i)
	{
		if (ids[i] == zoneId)
			return static_cast<int>(i);
	}
	return -1;
}

void WorldRegistry::PrintSummary() const
{
	for (size_t i = 0; i < _regions.size(); ++i)
	{
		const Entry& e = _regions[i];
		cout << "[world] region " << i << " " << e.data->Id()
			<< "  " << e.grid.sizeX << "x" << e.grid.sizeZ << "m"
			<< "  sector " << e.grid.sectorSize << "m -> "
			<< e.grid.sectorCountX << "x" << e.grid.sectorCountZ
			<< "  zone " << e.grid.zoneCountX << "x" << e.grid.zoneCountZ
			<< " (" << e.grid.ZoneCount() << ")"
			<< "  nav " << (e.nav.IsLoaded()
				? ("poly " + std::to_string(e.nav.PolyCount())) : std::string("없음"))
			<< "  zoneId " << MakeZoneId(static_cast<RegionIndex>(i), 0)
			<< ".." << MakeZoneId(static_cast<RegionIndex>(i),
				static_cast<ZoneId>(e.grid.ZoneCount() - 1))
			<< endl;
	}

	size_t total = 0;
	for (const Entry& e : _regions) total += e.grid.ZoneCount();
	cout << "[world] 리전 " << _regions.size() << "개, Zone 합계 " << total << "개" << endl;
}
