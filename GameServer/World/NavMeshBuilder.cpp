#include "pch.h"
#include "NavMeshBuilder.h"

#include <fstream>
#include <sstream>

#include <recastnavigation/Recast.h>
#include <recastnavigation/DetourNavMesh.h>
#include <recastnavigation/DetourNavMeshBuilder.h>

namespace
{
	// OBJ 의 usemtl 그룹 -> Recast area id.
	//
	// build_collision.py 가 walkable / blocker / water 로 나눠 준다.
	// blocker 를 **불러오되 걸을 수 없는 면으로 표시**하는 것이 핵심이다.
	// 아예 빼면 건물 안으로 길이 나고, 그냥 넣으면 지붕이 걸을 수 있는 섬이 된다.
	constexpr unsigned char kAreaGround = RC_WALKABLE_AREA;   // 63
	constexpr unsigned char kAreaBlocked = RC_NULL_AREA;      // 0

	struct ObjMesh
	{
		std::vector<float> verts;          // xyz 연속
		std::vector<int> tris;             // 정점 인덱스 3개씩
		std::vector<unsigned char> areas;  // 삼각형마다 하나
		float bmin[3]{};
		float bmax[3]{};
	};

	bool LoadObj(const std::string& path, ObjMesh& out, std::string& outError)
	{
		std::ifstream file(path);
		if (!file)
		{
			outError = "OBJ 를 열 수 없다: " + path;
			return false;
		}

		unsigned char currentArea = kAreaGround;
		std::string line;
		while (std::getline(file, line))
		{
			if (line.size() < 2 || line[0] == '#')
				continue;

			std::istringstream in(line);
			std::string tag;
			in >> tag;

			if (tag == "v")
			{
				float x, y, z;
				in >> x >> y >> z;
				out.verts.insert(out.verts.end(), { x, y, z });
			}
			else if (tag == "usemtl")
			{
				std::string name;
				in >> name;
				// water 는 애초에 지형에서 빼 두었으므로 여기 올 일이 없지만,
				// 나중에 얕은 물을 area 로 다루게 되면 여기서 갈라진다.
				currentArea = (name == "blocker") ? kAreaBlocked : kAreaGround;
			}
			else if (tag == "f")
			{
				// build_collision.py 는 "f a b c" 만 쓴다 (텍스처/법선 없음).
				int a = 0, b = 0, c = 0;
				in >> a >> b >> c;
				if (a <= 0 || b <= 0 || c <= 0)
					continue;
				out.tris.insert(out.tris.end(), { a - 1, b - 1, c - 1 });
				out.areas.push_back(currentArea);
			}
		}

		if (out.tris.empty())
		{
			outError = "OBJ 에 삼각형이 없다: " + path;
			return false;
		}

		rcCalcBounds(out.verts.data(), static_cast<int>(out.verts.size() / 3), out.bmin, out.bmax);
		return true;
	}
}

bool NavMeshBuilder::BuildFromObj(const std::string& objPath, const std::string& outPath,
                                  const Config& cfg, std::string& outError)
{
	ObjMesh mesh;
	if (!LoadObj(objPath, mesh, outError))
		return false;

	const int vertCount = static_cast<int>(mesh.verts.size() / 3);
	const int triCount = static_cast<int>(mesh.tris.size() / 3);

	rcConfig rc{};
	rc.cs = cfg.cellSize;
	rc.ch = cfg.cellHeight;
	rc.walkableSlopeAngle = cfg.agentMaxSlope;
	rc.walkableHeight = static_cast<int>(std::ceil(cfg.agentHeight / rc.ch));
	rc.walkableClimb  = static_cast<int>(std::floor(cfg.agentMaxClimb / rc.ch));
	rc.walkableRadius = static_cast<int>(std::ceil(cfg.agentRadius / rc.cs));
	rc.maxEdgeLen = static_cast<int>(cfg.edgeMaxLen / rc.cs);
	rc.maxSimplificationError = cfg.edgeMaxError;
	rc.minRegionArea = static_cast<int>(rcSqr(cfg.regionMinSize));
	rc.mergeRegionArea = static_cast<int>(rcSqr(cfg.regionMergeSize));
	rc.maxVertsPerPoly = static_cast<int>(cfg.vertsPerPoly);
	rc.detailSampleDist = cfg.detailSampleDist < 0.9f ? 0.0f : rc.cs * cfg.detailSampleDist;
	rc.detailSampleMaxError = rc.ch * cfg.detailSampleMaxError;
	rcVcopy(rc.bmin, mesh.bmin);
	rcVcopy(rc.bmax, mesh.bmax);
	rcCalcGridSize(rc.bmin, rc.bmax, rc.cs, &rc.width, &rc.height);

	rcContext ctx(false);

	// 1) 복셀화 — 삼각형을 높이필드에 굽는다.
	rcHeightfield* solid = rcAllocHeightfield();
	if (solid == nullptr
		|| !rcCreateHeightfield(&ctx, *solid, rc.width, rc.height, rc.bmin, rc.bmax, rc.cs, rc.ch))
	{
		outError = "rcCreateHeightfield 실패";
		rcFreeHeightField(solid);
		return false;
	}

	// **경사로 걸러낸 뒤 blocker 를 덮어쓴다.**
	// rcMarkWalkableTriangles 는 경사만 보므로 건물 지붕도 통과시킨다.
	// build_collision.py 가 준 그룹으로 그걸 눌러 준다.
	std::vector<unsigned char> areas(triCount, kAreaBlocked);
	rcMarkWalkableTriangles(&ctx, rc.walkableSlopeAngle,
		mesh.verts.data(), vertCount, mesh.tris.data(), triCount, areas.data());
	for (int i = 0; i < triCount; ++i)
	{
		if (mesh.areas[i] == kAreaBlocked)
			areas[i] = kAreaBlocked;
	}

	if (!rcRasterizeTriangles(&ctx, mesh.verts.data(), vertCount,
		mesh.tris.data(), areas.data(), triCount, *solid, rc.walkableClimb))
	{
		outError = "rcRasterizeTriangles 실패";
		rcFreeHeightField(solid);
		return false;
	}

	// 2) 걸을 수 없는 면 정리.
	rcFilterLowHangingWalkableObstacles(&ctx, rc.walkableClimb, *solid);
	rcFilterLedgeSpans(&ctx, rc.walkableHeight, rc.walkableClimb, *solid);
	rcFilterWalkableLowHeightSpans(&ctx, rc.walkableHeight, *solid);

	// 3) 압축 높이필드 -> 영역 분할.
	rcCompactHeightfield* chf = rcAllocCompactHeightfield();
	if (chf == nullptr
		|| !rcBuildCompactHeightfield(&ctx, rc.walkableHeight, rc.walkableClimb, *solid, *chf))
	{
		outError = "rcBuildCompactHeightfield 실패";
		rcFreeHeightField(solid);
		rcFreeCompactHeightfield(chf);
		return false;
	}
	rcFreeHeightField(solid);
	solid = nullptr;

	// 에이전트 반지름만큼 벽에서 물러난다. 이걸 빼먹으면 경로가 벽을 스쳐
	// 지나가고, 클라이언트 캡슐이 거기서 낀다.
	if (!rcErodeWalkableArea(&ctx, rc.walkableRadius, *chf)
		|| !rcBuildDistanceField(&ctx, *chf)
		|| !rcBuildRegions(&ctx, *chf, 0, rc.minRegionArea, rc.mergeRegionArea))
	{
		outError = "영역 분할 실패";
		rcFreeCompactHeightfield(chf);
		return false;
	}

	// 4) 윤곽 -> 폴리곤 -> 상세 메시.
	rcContourSet* cset = rcAllocContourSet();
	if (cset == nullptr
		|| !rcBuildContours(&ctx, *chf, rc.maxSimplificationError, rc.maxEdgeLen, *cset))
	{
		outError = "rcBuildContours 실패";
		rcFreeCompactHeightfield(chf);
		rcFreeContourSet(cset);
		return false;
	}

	rcPolyMesh* pmesh = rcAllocPolyMesh();
	if (pmesh == nullptr || !rcBuildPolyMesh(&ctx, *cset, rc.maxVertsPerPoly, *pmesh))
	{
		outError = "rcBuildPolyMesh 실패";
		rcFreeCompactHeightfield(chf);
		rcFreeContourSet(cset);
		rcFreePolyMesh(pmesh);
		return false;
	}

	rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
	if (dmesh == nullptr
		|| !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, rc.detailSampleDist,
			rc.detailSampleMaxError, *dmesh))
	{
		outError = "rcBuildPolyMeshDetail 실패";
		rcFreeCompactHeightfield(chf);
		rcFreeContourSet(cset);
		rcFreePolyMesh(pmesh);
		rcFreePolyMeshDetail(dmesh);
		return false;
	}
	rcFreeCompactHeightfield(chf);
	rcFreeContourSet(cset);

	// 5) Detour 가 읽을 수 있는 형태로.
	for (int i = 0; i < pmesh->npolys; ++i)
	{
		if (pmesh->areas[i] == RC_WALKABLE_AREA)
			pmesh->flags[i] = 1;     // 1 = 걸을 수 있음. 쿼리 필터가 이 값을 본다.
	}

	dtNavMeshCreateParams params{};
	params.verts = pmesh->verts;
	params.vertCount = pmesh->nverts;
	params.polys = pmesh->polys;
	params.polyAreas = pmesh->areas;
	params.polyFlags = pmesh->flags;
	params.polyCount = pmesh->npolys;
	params.nvp = pmesh->nvp;
	params.detailMeshes = dmesh->meshes;
	params.detailVerts = dmesh->verts;
	params.detailVertsCount = dmesh->nverts;
	params.detailTris = dmesh->tris;
	params.detailTriCount = dmesh->ntris;
	params.walkableHeight = cfg.agentHeight;
	params.walkableRadius = cfg.agentRadius;
	params.walkableClimb = cfg.agentMaxClimb;
	rcVcopy(params.bmin, pmesh->bmin);
	rcVcopy(params.bmax, pmesh->bmax);
	params.cs = rc.cs;
	params.ch = rc.ch;
	params.buildBvTree = true;

	unsigned char* navData = nullptr;
	int navDataSize = 0;
	if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
	{
		outError = "dtCreateNavMeshData 실패 — 폴리곤이 너무 많거나 비었다";
		rcFreePolyMesh(pmesh);
		rcFreePolyMeshDetail(dmesh);
		return false;
	}

	std::ofstream out(outPath, std::ios::binary);
	if (!out)
	{
		outError = "출력 파일을 열 수 없다: " + outPath;
		dtFree(navData);
		rcFreePolyMesh(pmesh);
		rcFreePolyMeshDetail(dmesh);
		return false;
	}
	out.write(reinterpret_cast<const char*>(navData), navDataSize);
	out.close();

	cout << "[navmesh] " << outPath
		<< "  poly " << pmesh->npolys
		<< "  vert " << pmesh->nverts
		<< "  detailTri " << dmesh->ntris
		<< "  " << navDataSize / 1024 << " KB"
		<< "  grid " << rc.width << "x" << rc.height
		<< endl;

	dtFree(navData);
	rcFreePolyMesh(pmesh);
	rcFreePolyMeshDetail(dmesh);
	return true;
}
