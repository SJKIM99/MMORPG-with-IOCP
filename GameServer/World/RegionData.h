#pragma once

#include <string>
#include <vector>
#include "Transform/Vec3.h"

// world/build/<region>.bin 리더.
//
// 포맷 정의는 tools/region_format.py 다. 그 파일이 원본이고 여기는 옮겨 적은
// 것이다 — 셋 중 하나가 어긋나면 terrain_hash 대조에서 걸린다.
//
// **의존성이 없다.** JSON 을 읽으려면 새 라이브러리를 들여야 하는데(2장 승인
// 대상) 바이너리는 표준 라이브러리만으로 끝난다. 그것이 바이너리로 간 이유이지
// 파일 크기가 아니다 (town 1.10MB -> 0.25MB 는 시작할 때 한 번 읽는 값이다).
//
// 클라이언트(Client/World/RegionBinary.cs)가 **같은 파일**을 읽는다.
// 5장의 "서버와 클라가 같은 JSON 을 읽는다"를 한 단계 더 좁힌 것이다 —
// 파서가 하나면 콜리전 불일치가 생길 자리가 없다.

enum class RegionCollision : uint8_t
{
	None = 0,
	StaticWalkable = 1,
	StaticBlocker = 2,
};

enum class RegionNav : uint8_t
{
	Include = 0,
	Exclude = 1,
	AreaWater = 2,
};

// 배치 플래그 비트
inline constexpr uint8_t kPlacementSmoke = 1 << 0;

struct RegionPlacement
{
	uint32_t        asset = 0;      // 문자열 표 인덱스
	Vec3            pos{};
	float           yaw = 0.0f;
	float           scale = 1.0f;
	RegionCollision collision = RegionCollision::None;
	RegionNav       nav = RegionNav::Include;
	uint8_t         flags = 0;
};

struct RegionNpc
{
	uint32_t              id = 0;
	uint32_t              asset = 0;
	Vec3                  pos{};
	float                 yaw = 0.0f;
	uint32_t              role = 0;
	uint32_t              animation = 0;
	std::vector<uint32_t> idleVariants;
};

struct RegionSpawn
{
	uint32_t id = 0;
	uint32_t monster = 0;
	uint32_t asset = 0;
	Vec3     center{};
	float    radius = 0.0f;
	uint32_t count = 0;
	uint32_t respawnMs = 0;
};

struct RegionRoute
{
	uint32_t          id = 0;
	bool              loop = false;
	std::vector<Vec3> waypoints;
};

class RegionData
{
public:
	// 실패하면 nullptr 를 돌려주고 outError 에 이유를 적는다.
	// 서버 시작에 필요한 파일이므로 예외를 던지지 않고 호출부가 판단하게 한다.
	static std::unique_ptr<RegionData> Load(const std::string& path, std::string& outError);

	// 저장소 어디에서 실행하든 world/build 를 찾는다. 서버는 Binary\Debug 에서
	// 돌고 툴은 저장소 루트에서 돌기 때문에 상대 경로를 박을 수 없다.
	static std::string FindRegionFile(const std::string& regionId);

	[[nodiscard]] const std::string& Id() const noexcept { return _id; }
	[[nodiscard]] const std::string& DisplayName() const noexcept { return _displayName; }

	[[nodiscard]] float SizeX() const noexcept { return _sizeX; }
	[[nodiscard]] float SizeZ() const noexcept { return _sizeZ; }
	[[nodiscard]] float SectorSize() const noexcept { return _sectorSize; }
	[[nodiscard]] const Vec3& SpawnPoint() const noexcept { return _spawn; }
	[[nodiscard]] float MinY() const noexcept { return _minY; }
	[[nodiscard]] float MaxY() const noexcept { return _maxY; }
	[[nodiscard]] uint32_t Seed() const noexcept { return _seed; }
	[[nodiscard]] bool PvpAllowed() const noexcept { return _pvp; }
	[[nodiscard]] bool SpawnAllowed() const noexcept { return _spawnAllowed; }

	// 지형 — XZ 격자. 인덱스는 iz * resolution + ix 로 파이썬·C# 과 같다.
	[[nodiscard]] int TerrainResolution() const noexcept { return _resolution; }
	[[nodiscard]] float TerrainCell() const noexcept { return _cell; }
	[[nodiscard]] const std::vector<float>& Heights() const noexcept { return _heights; }
	// 파이썬 기록기·C# 리더와 대조하는 값. 세 구현이 같아야 한다.
	[[nodiscard]] uint32_t TerrainHash() const noexcept { return _terrainHash; }

	// 수면 높이. NoWater() 보다 작으면 그 격자점에는 물이 없다.
	[[nodiscard]] const std::vector<float>& Water() const noexcept { return _water; }
	[[nodiscard]] float NoWater() const noexcept { return _noWater; }

	[[nodiscard]] const std::vector<RegionPlacement>& Placements() const noexcept { return _placements; }
	[[nodiscard]] const std::vector<RegionNpc>& Npcs() const noexcept { return _npcs; }
	[[nodiscard]] const std::vector<RegionSpawn>& Spawns() const noexcept { return _spawns; }
	[[nodiscard]] const std::vector<RegionRoute>& Routes() const noexcept { return _routes; }

	// 문자열 표 조회. 범위를 벗어나면 빈 문자열을 돌려준다.
	[[nodiscard]] const std::string& Str(uint32_t index) const noexcept;

	// 시작 로그 한 줄. 무엇을 읽었는지 눈으로 확인하는 용도다.
	void PrintSummary() const;

private:
	std::string _id;
	std::string _displayName;

	float _sizeX = 0.0f;
	float _sizeZ = 0.0f;
	float _sectorSize = 0.0f;
	Vec3  _spawn{};
	float _minY = 0.0f;
	float _maxY = 0.0f;
	uint32_t _seed = 0;
	bool _pvp = false;
	bool _spawnAllowed = false;

	uint32_t _terrainHash = 0;
	int   _resolution = 0;
	float _cell = 0.0f;
	std::vector<float> _heights;

	std::vector<float> _water;
	float _noWater = -1000.0f;

	std::vector<std::string>    _strings;
	std::vector<RegionPlacement> _placements;
	std::vector<RegionNpc>       _npcs;
	std::vector<RegionSpawn>     _spawns;
	std::vector<RegionRoute>     _routes;
};
