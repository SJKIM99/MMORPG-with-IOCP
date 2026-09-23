#pragma once

#include <string>
#include <vector>
#include "Transform/Vec3.h"

class dtNavMesh;
class dtNavMeshQuery;
class dtQueryFilter;

// 리전 하나의 내비메시. CLAUDE.md 7장.
//
//   빌드는 오프라인, 쿼리는 런타임. 여기서는 .navmesh 를 **읽기만** 한다.
//
// ── 스레드 규약 (여기를 어기면 재현 불가능한 크래시가 난다) ──────────────────
//
//   dtNavMesh       읽기 전용이다. **모든 Zone 스레드가 하나를 공유한다.**
//   dtNavMeshQuery  **스레드 안전하지 않다.** 내부에 노드 풀과 열린 목록을
//                   상태로 들고 있어서 두 스레드가 같은 것을 쓰면 서로의
//                   탐색 상태를 짓밟는다. **Zone 스레드마다 하나씩 소유한다.**
//
// 그래서 이 클래스는 dtNavMesh 만 들고, 쿼리 객체는 NavQuery 가 스레드마다
// 따로 만든다. 7장이 "코드에 주석으로 명시할 것" 이라고 못 박은 부분이다.
//
// dtCrowd 는 쓰지 않는다 — 단일 스레드 전역 상태를 가정해 Zone 스레드 모델과
// 정면으로 부딪힌다. 근거는 docs/decisions/008-navmesh-thread-model.md.
class NavMesh
{
public:
	NavMesh() = default;
	~NavMesh();

	NavMesh(const NavMesh&) = delete;
	NavMesh& operator=(const NavMesh&) = delete;

	// vector<Entry> 에 담기려면 이동이 가능해야 한다. 복사는 여전히 막는다 —
	// dtNavMesh 를 두 번 해제하면 안 되고, 애초에 공유해서 쓰는 것이 목적이다.
	NavMesh(NavMesh&& other) noexcept : _mesh(other._mesh) { other._mesh = nullptr; }
	NavMesh& operator=(NavMesh&& other) noexcept
	{
		if (this != &other) { std::swap(_mesh, other._mesh); }
		return *this;
	}

	bool Load(const std::string& path, std::string& outError);

	[[nodiscard]] bool IsLoaded() const noexcept { return _mesh != nullptr; }

	// 공유용. **이 포인터로 쿼리를 만들지 말고 NavQuery 를 쓸 것.**
	[[nodiscard]] const dtNavMesh* Raw() const noexcept { return _mesh; }
	[[nodiscard]] dtNavMesh* Raw() noexcept { return _mesh; }

	[[nodiscard]] int TileCount() const noexcept;
	[[nodiscard]] int PolyCount() const noexcept;

private:
	dtNavMesh* _mesh = nullptr;
};

// Zone 스레드 하나가 소유하는 쿼리 객체.
//
// 값싸지 않다(노드 풀을 미리 잡는다). Zone 스레드가 시작할 때 한 번 만들고
// 그 스레드에서만 쓴다. 다른 스레드로 넘기지 말 것.
class NavQuery
{
public:
	NavQuery() = default;
	~NavQuery();

	NavQuery(const NavQuery&) = delete;
	NavQuery& operator=(const NavQuery&) = delete;

	// maxNodes 는 A* 가 펼칠 수 있는 노드 수. 크면 메모리를 먹고 작으면
	// 먼 경로를 못 찾는다. Zone 하나가 128m 라 2048 이면 넉넉하다.
	bool Init(const NavMesh& mesh, int maxNodes = 2048);

	[[nodiscard]] bool IsReady() const noexcept { return _query != nullptr; }

	// 이 지점이 내비메시 위인가. 서버 권위 이동 검증의 핵심이다 (9장 —
	// "내비메시 폴리곤 밖이면 거부").
	//
	// outSnapped 에는 내비메시 위로 끌어당긴 좌표가 들어간다. **Y 가 여기서
	// 나온다** — 별도 높이맵 샘플러를 두지 않는 이유다.
	[[nodiscard]] bool SampleWalkable(const Vec3& position, float searchRadius,
	                                  Vec3& outSnapped) const;

	// 경로를 찾아 꺾은선으로 돌려준다. 실패하면 빈 벡터.
	[[nodiscard]] std::vector<Vec3> FindPath(const Vec3& start, const Vec3& goal,
	                                         float searchRadius = 4.0f,
	                                         int maxPoints = 64) const;

private:
	dtNavMeshQuery* _query = nullptr;
	dtQueryFilter*  _filter = nullptr;
};
