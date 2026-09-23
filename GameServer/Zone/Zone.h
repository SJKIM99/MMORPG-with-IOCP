#pragma once

#include "Thread/GameLogicThread.h"
#include "ZoneTypes.h"
#include "Area/Sector.h"
#include "World/NavMesh.h"

class Zone
{
public:
	using Task = GameLogicThread::Task;

public:
	explicit Zone(ZoneId id);
	~Zone() = default;

	[[nodiscard]] ZoneId GetId() const noexcept { return _id; }
	[[nodiscard]] Sector& GetSector() noexcept { return _sector; }

	// **이 Zone 스레드 전용 내비메시 쿼리.** dtNavMeshQuery 는 스레드 안전하지
	// 않아 Zone 마다 하나씩 가진다 (7장, NavMesh.h 주석 참고).
	[[nodiscard]] NavQuery& GetNavQuery() noexcept { return _navQuery; }

	void Enqueue(Task task);
	void Run();

private:
	ZoneId _id = InvalidZoneId;
	Sector _sector;
	NavQuery _navQuery;
	GameLogicThread _logicThread;
};
