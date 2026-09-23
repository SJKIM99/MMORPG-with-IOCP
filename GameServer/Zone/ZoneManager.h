#pragma once

#include "Thread/GameLogicThread.h"
#include "World/WorldRegistry.h"

class GameSession;
class Zone;

class ZoneManager
{
public:
	using Task = GameLogicThread::Task;

public:
	explicit ZoneManager(shared_ptr<GameLogicThread> defaultThread);
	~ZoneManager();

	// Zone 생성은 **생성자가 아니라 여기서** 한다.
	//
	// ZoneManager 는 GServerGlobal 의 정적 초기화(= main 진입 전)에서 만들어지는데,
	// Zone 은 리전 데이터(크기 / Sector 수)가 있어야 만들 수 있고 그 데이터는
	// main 안에서 파일을 읽어야 나온다. 생성자에서 만들려다 GWorld 가 nullptr 인
	// 채로 접근해 main 에 들어오기도 전에 죽었다.
	void BuildZones();

	void Enqueue(Task task);
	void EnqueueByZone(ZoneId zoneId, Task task);
	// 리전을 함께 받는다. 좌표만으로는 Zone 이 정해지지 않는다 —
	// 마을 (100, 100) 과 필드 (100, 100) 은 서로 다른 Zone 이다.
	void EnqueueByWorld(RegionIndex region, float worldX, float worldZ, Task task);
	void EnqueueByObject(const ObjID& objectId, Task task);
	void EnqueueBySession(const shared_ptr<GameSession>& session, Task task);

	void RunZone(ZoneId zoneId);

	void UpdateObjectZone(const ObjID& objectId, ZoneId zoneId);
	void RemoveObject(const ObjID& objectId);

	[[nodiscard]] ZoneId GetObjectZone(const ObjID& objectId) const;
	[[nodiscard]] size_t GetZoneCount() const noexcept { return _zones.size(); }

	// 지금 이 함수를 호출한 스레드가 objectId를 소유한 Zone 스레드인지 확인한다.
	// ASSERT_OWNED_BY_CURRENT_ZONE 매크로가 내부적으로 사용하는 판정 함수다.
	[[nodiscard]] bool IsCurrentThreadOwner(const ObjID& objectId) const noexcept;

private:
	// ZoneId 를 인덱스로 쓸 수 없다 — 리전이 상위 바이트에 박혀 있어
	// 값이 성기다(마을 0~3, 필드 256~271). 조밀한 배열 + 조회표로 나눈다.
	[[nodiscard]] Zone* FindZone(ZoneId zoneId) const noexcept;

	shared_ptr<GameLogicThread> _defaultThread;
	vector<unique_ptr<Zone>> _zones;
	vector<vector<uint16_t>> _zoneSlotByRegion;   // [리전][리전 내 번호] -> _zones 인덱스
	mutable mutex _objectZoneLock;
	unordered_map<ObjID, ZoneId> _objectZones;
	atomic<int> _readyZoneCount{ 0 };
};

extern shared_ptr<ZoneManager> GZoneManager;

// objectId를 소유한 Zone 스레드 위에서 실행 중이 아니면 즉시 크래시한다.
// 인벤토리처럼 "항상 자기 자신의 Zone 스레드에서만 mutate되어야 하는" 데이터를
// 락 없이 안전하게 두기 위한 전제 조건을 코드 레벨에서 강제하는 용도.
// Release 빌드에서는 완전히 제거되어 성능 영향이 없다.
#ifdef _DEBUG
#define ASSERT_OWNED_BY_CURRENT_ZONE(objId) ASSERT_CRASH(GZoneManager->IsCurrentThreadOwner(objId))
#else
#define ASSERT_OWNED_BY_CURRENT_ZONE(objId) ((void)0)
#endif
