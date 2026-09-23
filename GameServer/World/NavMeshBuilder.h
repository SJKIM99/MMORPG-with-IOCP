#pragma once

#include <string>

// world/build/<region>.obj  ->  world/build/<region>.navmesh
//
// **오프라인 전용이다.** CLAUDE.md 7장 — "런타임에 내비메시를 빌드하지 않는다.
// 바이너리를 로드한다." 서버는 시작할 때 이미 만들어진 .navmesh 를 읽기만 한다.
//
// 이 코드가 왜 서버 프로젝트 안에 있는가:
// 별도 vcxproj 를 만들면 Recast 링크 설정과 좌표 규약을 두 군데에서 관리하게
// 된다. 빌더와 로더가 같은 상수(에이전트 크기, 셀 크기)를 봐야 하므로 한
// 프로젝트에 두고 실행 모드로 가른다 — `GameServer.exe --build-navmesh`.
// 산출물은 커밋되고, 평소 서버 기동 경로에는 이 코드가 끼어들지 않는다.
namespace NavMeshBuilder
{
	// 내비메시 빌드 설정. 에이전트 치수는 클라이언트 캡슐과 맞춰야 한다 —
	// 어긋나면 "서버는 지나갈 수 있다는데 클라에서는 벽에 낀다" 가 된다.
	struct Config
	{
		// PlayerSpawner 의 CapsuleHeight 2.2 / CapsuleRadius 0.42 와 같은 값.
		float agentHeight = 2.2f;
		float agentRadius = 0.42f;

		// PlayerController 의 MaxStepHeight 0.75 와 같은 값.
		float agentMaxClimb = 0.75f;

		// FloorMaxAngle 52도. 이보다 가파르면 Godot 이 바닥이 아니라 벽으로
		// 분류하므로 내비메시도 같은 기준이어야 한다. 지형 생성기는 걸을 수
		// 있는 면을 40도 아래로 이미 다듬어 두었다.
		float agentMaxSlope = 52.0f;

		// 복셀 크기. 0.3m 면 마을 256m 가 854^2 가 되어 빌드가 몇 초에 끝난다.
		float cellSize = 0.3f;
		float cellHeight = 0.2f;

		float regionMinSize = 8.0f;
		float regionMergeSize = 20.0f;
		float edgeMaxLen = 12.0f;
		float edgeMaxError = 1.3f;
		float vertsPerPoly = 6.0f;
		float detailSampleDist = 6.0f;
		float detailSampleMaxError = 1.0f;
	};

	// 성공하면 true. 실패 이유는 outError 에 적는다.
	bool BuildFromObj(const std::string& objPath, const std::string& outPath,
	                  const Config& config, std::string& outError);
}
