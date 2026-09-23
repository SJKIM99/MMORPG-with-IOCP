#include "pch.h"
#include "ZoneManager.h"
#include "GameSession.h"
#include "Zone.h"

ZoneManager::ZoneManager(shared_ptr<GameLogicThread> defaultThread)
	: _defaultThread(std::move(defaultThread))
{
	ASSERT_CRASH(_defaultThread != nullptr);
}

ZoneManager::~ZoneManager() = default;

void ZoneManager::BuildZones()
{
	ASSERT_CRASH(GWorld != nullptr);
	ASSERT_CRASH(_zones.empty());

	// 모든 리전의 Zone 을 만든다. ZoneId 는 (리전 << 8 | 리전 내 번호) 라
	// 값이 성기므로, 조밀한 _zones 와 그 자리를 가리키는 조회표를 따로 둔다.
	const vector<ZoneId> ids = GWorld->AllZoneIds();
	_zones.reserve(ids.size());
	_zoneSlotByRegion.resize(GWorld->RegionCount());
	for (size_t r = 0; r < GWorld->RegionCount(); ++r)
		_zoneSlotByRegion[r].assign(GWorld->Grid(static_cast<RegionIndex>(r)).ZoneCount(), 0);

	for (const ZoneId id : ids)
	{
		_zoneSlotByRegion[RegionOfZone(id)][LocalZoneOf(id)] =
			static_cast<uint16_t>(_zones.size());
		_zones.push_back(make_unique<Zone>(id));
	}

	cout << "[world] Zone " << _zones.size() << "개 생성" << endl;
}

void ZoneManager::Enqueue(Task task)
{
	ASSERT_CRASH(_defaultThread != nullptr);
	_defaultThread->Enqueue(std::move(task));
}

Zone* ZoneManager::FindZone(ZoneId zoneId) const noexcept
{
	if (GWorld == nullptr || !GWorld->IsValidZoneId(zoneId))
		return nullptr;

	const RegionIndex region = RegionOfZone(zoneId);
	const ZoneId local = LocalZoneOf(zoneId);
	if (region >= _zoneSlotByRegion.size() || local >= _zoneSlotByRegion[region].size())
		return nullptr;

	return _zones[_zoneSlotByRegion[region][local]].get();
}

void ZoneManager::EnqueueByZone(ZoneId zoneId, Task task)
{
	if (Zone* zone = FindZone(zoneId))
	{
		zone->Enqueue(std::move(task));
		return;
	}

	Enqueue(std::move(task));
}

void ZoneManager::EnqueueByWorld(RegionIndex region, float worldX, float worldZ, Task task)
{
	EnqueueByZone(GWorld->ZoneAt(region, worldX, worldZ), std::move(task));
}

void ZoneManager::EnqueueByObject(const ObjID& objectId, Task task)
{
	EnqueueByZone(GetObjectZone(objectId), std::move(task));
}

void ZoneManager::EnqueueBySession(const shared_ptr<GameSession>& session, Task task)
{
	const ZoneId zoneId = session != nullptr ? session->GetRoutingZoneId() : InvalidZoneId;
	EnqueueByZone(zoneId, std::move(task));
}

void ZoneManager::RunZone(ZoneId zoneId)
{
	// **ZoneId 로 _zones 를 직접 인덱싱하면 안 된다.** 리전이 상위 바이트에
	// 박혀 있어 필드 Zone 의 id 는 256 부터인데 _zones 는 20개뿐이다.
	// 그대로 두었더니 std::vector 범위 밖 접근이 되어 MSVC 디버그 STL 의
	// __debugbreak(0x80000003)로 서버가 올라오지 못했다. 마을(0~3)만 살아남아
	// "필드 Zone 만 죽는다"처럼 보였다.
	Zone* zone = FindZone(zoneId);
	ASSERT_CRASH(zone != nullptr);

	const int ready = ++_readyZoneCount;
	if (ready == static_cast<int>(_zones.size()))
		std::cout << "[Server] All " << ready << " zone threads are ready.\n";

	zone->Run();
}

void ZoneManager::UpdateObjectZone(const ObjID& objectId, ZoneId zoneId)
{
	if (GWorld == nullptr || !GWorld->IsValidZoneId(zoneId))
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

bool ZoneManager::IsCurrentThreadOwner(const ObjID& objectId) const noexcept
{
	return LCurrentZoneId != InvalidZoneId && GetObjectZone(objectId) == LCurrentZoneId;
}

