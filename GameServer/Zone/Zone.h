#pragma once

#include "Thread/GameLogicThread.h"
#include "ZoneTypes.h"
#include "Area/Sector.h"

class Zone
{
public:
	using Task = GameLogicThread::Task;

public:
	explicit Zone(ZoneId id);
	~Zone() = default;

	[[nodiscard]] ZoneId GetId() const noexcept { return _id; }
	[[nodiscard]] Sector& GetSector() noexcept { return _sector; }

	void Enqueue(Task task);
	void Run();

private:
	ZoneId _id = InvalidZoneId;
	Sector _sector;
	GameLogicThread _logicThread;
};
