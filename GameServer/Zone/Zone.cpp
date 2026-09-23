#include "pch.h"
#include "Zone.h"
#include "World/WorldRegistry.h"
#include "ServerGlobal.h"
#include "Monster/MonsterHelper.h"

namespace
{
	// 전역 ZoneId -> 그 Zone 이 맡는 섹터 시작 좌표.
	// ZoneId 상위 바이트가 리전, 하위 바이트가 리전 안의 Zone 번호다.
	short ZoneOffsetX(ZoneId id)
	{
		const ZoneGrid& grid = GWorld->Grid(RegionOfZone(id));
		const ZoneId local = LocalZoneOf(id);
		return static_cast<short>((local % grid.zoneCountX) * ZoneGrid::kSectorsPerZone);
	}

	short ZoneOffsetZ(ZoneId id)
	{
		const ZoneGrid& grid = GWorld->Grid(RegionOfZone(id));
		const ZoneId local = LocalZoneOf(id);
		return static_cast<short>((local / grid.zoneCountX) * ZoneGrid::kSectorsPerZone);
	}
}

Zone::Zone(ZoneId id)
	: _id(id)
	, _sector(RegionOfZone(id), ZoneOffsetX(id), ZoneOffsetZ(id))
{
}

void Zone::Enqueue(Task task)
{
	_logicThread.Enqueue(std::move(task));
}

void Zone::Run()
{
	// 이 Zone 스레드의 TLS GSector를 자신의 Sector로 설정한다.
	// 이후 이 스레드에서 실행되는 모든 게임 로직은 GSector를 통해 Zone 전용 섹터에 접근한다.
	GSector = &_sector;

	// 이 스레드 전용 쿼리를 만든다. dtNavMesh 는 공유하지만 dtNavMeshQuery 는
	// 내부에 탐색 상태(노드 풀, 열린 목록)를 들고 있어 **절대 공유하면 안 된다.**
	// 두 스레드가 같은 것을 쓰면 서로의 탐색을 짓밟아 재현 불가능한 크래시가 난다.
	const NavMesh& nav = GWorld->Nav(RegionOfZone(_id));
	if (nav.IsLoaded() && !_navQuery.Init(nav))
		cout << "[Zone " << _id << "] NavQuery 초기화 실패" << endl;

	GNavQuery = &_navQuery;
	// ZoneManager::IsCurrentThreadOwner()가 이 스레드를 자신의 ZoneId로 식별할 수 있도록 설정.
	LCurrentZoneId = _id;

	// GSector 설정 이후 이 존 담당 몬스터를 초기화한다.
	// GetRandomPosition이 GSector 범위 안에서만 좌표를 뽑으므로
	// 메인 스레드(GSector=nullptr)에서 호출하면 크래시가 발생했던 문제를 해결한다.
	MonsterHelper::InitForZone(_id);

	_logicThread.Run();
}
