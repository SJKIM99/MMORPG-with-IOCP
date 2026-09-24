#include "pch.h"
#include "Zone.h"
#include "ServerGlobal.h"
#include "Monster/MonsterHelper.h"
#include "Npc/NpcHelper.h"

Zone::Zone(ZoneId id)
	: _id(id)
	, _sector(
		static_cast<short>(ZoneLayout::GetZoneCoordById(id).x * ZoneLayout::SectorsPerZoneX),
		static_cast<short>(ZoneLayout::GetZoneCoordById(id).y * ZoneLayout::SectorsPerZoneY))
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
	// ZoneManager::IsCurrentThreadOwner()가 이 스레드를 자신의 ZoneId로 식별할 수 있도록 설정.
	LCurrentZoneId = _id;

	// GSector 설정 이후 이 존 담당 몬스터를 초기화한다.
	// GetRandomPosition이 GSector 범위 안에서만 좌표를 뽑으므로
	// 메인 스레드(GSector=nullptr)에서 호출하면 크래시가 발생했던 문제를 해결한다.
	MonsterHelper::InitForZone(_id);
	// 마을 Zone이면 NPC 배회 틱을 이 스레드에서 건다(이후 틱도 이 Zone으로 온다).
	NpcHelper::InitForZone(static_cast<uint16_t>(_id));

	_logicThread.Run();
}
