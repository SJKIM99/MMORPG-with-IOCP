#include "pch.h"
#include "NavMesh.h"

#include <fstream>

#include <recastnavigation/DetourNavMesh.h>
#include <recastnavigation/DetourNavMeshQuery.h>
#include <recastnavigation/DetourCommon.h>

namespace
{
	// NavMeshBuilder 가 걸을 수 있는 폴리곤에 넣는 플래그.
	constexpr unsigned short kFlagWalkable = 1;

	inline void ToDetour(const Vec3& v, float* out) noexcept
	{
		out[0] = v.x; out[1] = v.y; out[2] = v.z;
	}

	inline Vec3 FromDetour(const float* v) noexcept
	{
		return Vec3{ v[0], v[1], v[2] };
	}
}

// ---------------------------------------------------------------------------
// NavMesh — 읽기 전용, 모든 Zone 스레드가 공유
// ---------------------------------------------------------------------------

NavMesh::~NavMesh()
{
	if (_mesh != nullptr)
	{
		dtFreeNavMesh(_mesh);
		_mesh = nullptr;
	}
}

bool NavMesh::Load(const std::string& path, std::string& outError)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
	{
		outError = ".navmesh 를 열 수 없다: " + path
			+ " — GameServer.exe --build-navmesh 를 먼저 돌려라";
		return false;
	}

	const std::streamsize size = file.tellg();
	if (size <= 0)
	{
		outError = ".navmesh 가 비어 있다: " + path;
		return false;
	}

	// dtNavMesh 가 이 메모리를 **소유**한다(DT_TILE_FREE_DATA). dtAlloc 으로
	// 잡아야 dtFreeNavMesh 가 같은 할당기로 돌려줄 수 있다.
	auto* data = static_cast<unsigned char*>(dtAlloc(static_cast<size_t>(size), DT_ALLOC_PERM));
	if (data == nullptr)
	{
		outError = "내비메시 메모리 할당 실패";
		return false;
	}

	file.seekg(0);
	if (!file.read(reinterpret_cast<char*>(data), size))
	{
		outError = ".navmesh 를 다 읽지 못했다: " + path;
		dtFree(data);
		return false;
	}

	dtNavMesh* mesh = dtAllocNavMesh();
	if (mesh == nullptr)
	{
		outError = "dtAllocNavMesh 실패";
		dtFree(data);
		return false;
	}

	const dtStatus status = mesh->init(data, static_cast<int>(size), DT_TILE_FREE_DATA);
	if (dtStatusFailed(status))
	{
		outError = "dtNavMesh::init 실패 — 포맷이 다르거나 손상됐다: " + path;
		dtFreeNavMesh(mesh);   // data 는 init 실패 시 소유되지 않는다
		dtFree(data);
		return false;
	}

	if (_mesh != nullptr)
		dtFreeNavMesh(_mesh);
	_mesh = mesh;
	return true;
}

int NavMesh::TileCount() const noexcept
{
	return _mesh != nullptr ? _mesh->getMaxTiles() : 0;
}

int NavMesh::PolyCount() const noexcept
{
	if (_mesh == nullptr)
		return 0;

	// getTile(int) 은 private 이다. 공개 API 인 getTileAt 을 쓴다.
	// dtCreateNavMeshData 로 만든 내비메시는 **타일 하나**이므로 (0,0,0) 이다.
	// 타일 단위 빌드(큰 월드)로 가면 여기를 순회로 바꾼다.
	const dtMeshTile* tile = _mesh->getTileAt(0, 0, 0);
	return (tile != nullptr && tile->header != nullptr) ? tile->header->polyCount : 0;
}

// ---------------------------------------------------------------------------
// NavQuery — Zone 스레드마다 하나
// ---------------------------------------------------------------------------

NavQuery::~NavQuery()
{
	if (_query != nullptr)
	{
		dtFreeNavMeshQuery(_query);
		_query = nullptr;
	}
	delete _filter;
	_filter = nullptr;
}

bool NavQuery::Init(const NavMesh& mesh, int maxNodes)
{
	if (!mesh.IsLoaded())
		return false;

	_query = dtAllocNavMeshQuery();
	if (_query == nullptr)
		return false;

	// const 를 벗기는 이유: dtNavMeshQuery::init 이 dtNavMesh* 를 요구하지만
	// 쿼리는 내비메시를 **읽기만** 한다. 공유가 안전한 근거가 이것이다.
	if (dtStatusFailed(_query->init(const_cast<dtNavMesh*>(mesh.Raw()), maxNodes)))
	{
		dtFreeNavMeshQuery(_query);
		_query = nullptr;
		return false;
	}

	_filter = new dtQueryFilter();
	_filter->setIncludeFlags(kFlagWalkable);
	_filter->setExcludeFlags(0);
	return true;
}

bool NavQuery::SampleWalkable(const Vec3& position, float searchRadius, Vec3& outSnapped) const
{
	if (_query == nullptr)
		return false;

	float center[3];
	ToDetour(position, center);

	// Y 로도 찾아야 한다. 다층 구조가 없더라도 지형 높이와 입력 좌표가
	// 조금 어긋나 있을 수 있어 세로 여유를 크게 준다.
	const float extents[3] = { searchRadius, searchRadius * 4.0f, searchRadius };

	dtPolyRef poly = 0;
	float nearest[3]{};
	if (dtStatusFailed(_query->findNearestPoly(center, extents, _filter, &poly, nearest))
		|| poly == 0)
	{
		return false;
	}

	outSnapped = FromDetour(nearest);
	return true;
}

std::vector<Vec3> NavQuery::FindPath(const Vec3& start, const Vec3& goal,
                                     float searchRadius, int maxPoints) const
{
	std::vector<Vec3> result;
	if (_query == nullptr)
		return result;

	float startPos[3], goalPos[3];
	ToDetour(start, startPos);
	ToDetour(goal, goalPos);
	const float extents[3] = { searchRadius, searchRadius * 4.0f, searchRadius };

	dtPolyRef startRef = 0, goalRef = 0;
	float startNearest[3]{}, goalNearest[3]{};
	if (dtStatusFailed(_query->findNearestPoly(startPos, extents, _filter, &startRef, startNearest))
		|| dtStatusFailed(_query->findNearestPoly(goalPos, extents, _filter, &goalRef, goalNearest))
		|| startRef == 0 || goalRef == 0)
	{
		return result;
	}

	// 폴리곤 통로를 먼저 찾고(findPath), 그 안에서 직선 경로를 뽑는다
	// (findStraightPath). dtCrowd 를 쓰지 않는 이유는 7장에 적힌 대로다 —
	// 단일 스레드 전역 상태를 가정해 Zone 스레드 모델과 부딪힌다.
	constexpr int kMaxPolys = 256;
	dtPolyRef polys[kMaxPolys];
	int polyCount = 0;
	if (dtStatusFailed(_query->findPath(startRef, goalRef, startNearest, goalNearest,
		_filter, polys, &polyCount, kMaxPolys)) || polyCount == 0)
	{
		return result;
	}

	// 목표 폴리곤에 닿지 못했으면 갈 수 있는 데까지만 준다.
	float end[3];
	dtVcopy(end, goalNearest);
	if (polys[polyCount - 1] != goalRef)
		_query->closestPointOnPoly(polys[polyCount - 1], goalNearest, end, nullptr);

	std::vector<float> straight(static_cast<size_t>(maxPoints) * 3);
	int straightCount = 0;
	if (dtStatusFailed(_query->findStraightPath(startNearest, end, polys, polyCount,
		straight.data(), nullptr, nullptr, &straightCount, maxPoints)))
	{
		return result;
	}

	result.reserve(straightCount);
	for (int i = 0; i < straightCount; ++i)
		result.push_back(FromDetour(&straight[static_cast<size_t>(i) * 3]));
	return result;
}
