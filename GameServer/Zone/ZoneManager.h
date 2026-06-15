#pragma once

#include "Thread/GameLogicThread.h"
#include "ZoneLayout.h"

class GameSession;
class Zone;

class ZoneManager
{
public:
	using Task = GameLogicThread::Task;

public:
	explicit ZoneManager(shared_ptr<GameLogicThread> defaultThread);
	~ZoneManager();

	void Enqueue(Task task);
	void EnqueueByZone(ZoneId zoneId, Task task);
	void EnqueueByWorld(int worldX, int worldY, Task task);
	void EnqueueByObject(const ObjID& objectId, Task task);
	void EnqueueBySession(const shared_ptr<GameSession>& session, Task task);

	void RunZone(ZoneId zoneId);

	void UpdateObjectZone(const ObjID& objectId, ZoneId zoneId);
	void RemoveObject(const ObjID& objectId);

	[[nodiscard]] ZoneId GetObjectZone(const ObjID& objectId) const;
	[[nodiscard]] size_t GetZoneCount() const noexcept { return _zones.size(); }

private:
	shared_ptr<GameLogicThread> _defaultThread;
	vector<unique_ptr<Zone>> _zones;
	mutable mutex _objectZoneLock;
	unordered_map<ObjID, ZoneId> _objectZones;
	atomic<int> _readyZoneCount{ 0 };
};

extern shared_ptr<ZoneManager> GZoneManager;
