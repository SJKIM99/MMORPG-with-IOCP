#include "pch.h"
#include "ZoneManager.h"
#include "GameSession.h"
#include "Zone.h"

ZoneManager::ZoneManager(shared_ptr<GameLogicThread> defaultThread)
	: _defaultThread(std::move(defaultThread))
{
	ASSERT_CRASH(_defaultThread != nullptr);

	_zones.reserve(ZoneLayout::ZoneCount);
	for (ZoneId id = 0; id < static_cast<ZoneId>(ZoneLayout::ZoneCount); ++id)
		_zones.push_back(make_unique<Zone>(id));
}

ZoneManager::~ZoneManager() = default;

void ZoneManager::Enqueue(Task task)
{
	ASSERT_CRASH(_defaultThread != nullptr);
	_defaultThread->Enqueue(std::move(task));
}

void ZoneManager::EnqueueByZone(ZoneId zoneId, Task task)
{
	if (ZoneLayout::IsValidZoneId(zoneId))
	{
		_zones[zoneId]->Enqueue(std::move(task));
		return;
	}

	Enqueue(std::move(task));
}

void ZoneManager::EnqueueByWorld(int worldX, int worldY, Task task)
{
	EnqueueByZone(ZoneLayout::GetZoneIdByWorld(worldX, worldY), std::move(task));
}

void ZoneManager::EnqueueByObject(const ObjID& objectId, Task task)
{
	EnqueueByZone(GetObjectZone(objectId), std::move(task));
}

void ZoneManager::EnqueueBySession(const shared_ptr<GameSession>& session, Task task)
{
	const ZoneId zoneId = session != nullptr ? session->GetZoneId() : InvalidZoneId;
	EnqueueByZone(zoneId, std::move(task));
}

void ZoneManager::RunZone(ZoneId zoneId)
{
	ASSERT_CRASH(ZoneLayout::IsValidZoneId(zoneId));

	const int ready = ++_readyZoneCount;
	if (ready == static_cast<int>(_zones.size()))
		std::cout << "[Server] All " << ready << " zone threads are ready.\n";

	_zones[zoneId]->Run();
}

void ZoneManager::UpdateObjectZone(const ObjID& objectId, ZoneId zoneId)
{
	if (!ZoneLayout::IsValidZoneId(zoneId))
		return;

	scoped_lock lock(_objectZoneLock);
	_objectZones[objectId] = zoneId;
}

void ZoneManager::RemoveObject(const ObjID& objectId)
{
	scoped_lock lock(_objectZoneLock);
	_objectZones.erase(objectId);
}

ZoneId ZoneManager::GetObjectZone(const ObjID& objectId) const
{
	scoped_lock lock(_objectZoneLock);
	const auto it = _objectZones.find(objectId);
	if (it == _objectZones.end())
		return InvalidZoneId;

	return it->second;
}

