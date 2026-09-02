#pragma once

class DBConnectionPool;
class GameSession;

class DBThread
{
public:
	using Clock    = std::chrono::steady_clock;
	using Duration = Clock::duration;

	DBThread() = default;
	~DBThread() = default;

	void DoDataBase();
	void RequestLogin(const shared_ptr<GameSession>& session, const std::string& name, const std::string& password);
	void RequestSaveUser(const ObjID& subjectId, const DB_USER_INFO& info);

	// 인벤토리 슬롯 하나를 즉시(디바운스 없이) 저장/삭제한다. 호출한 존 스레드를
	// 막지 않는 비동기 enqueue — 실제 ODBC 호출은 DB 워커 스레드(8개)가 처리한다.
	// playerId는 로그인 시 User에 캐싱해둔 정수 PK를 그대로 넘긴다(name 조회 없음).
	void RequestSaveItem(int playerId, uint16_t slotIndex, uint16_t itemId, uint16_t count, bool equipped);
	void RequestDeleteItem(int playerId, uint16_t slotIndex);

	// 두 슬롯을 하나의 트랜잭션으로 함께 반영한다(장착 시 이전 장비 자동 탈착,
	// 슬롯 교체처럼 "논리적으로 한 동작"인 경우 반드시 이걸 쓴다 — Inventory.cpp 참고).
	void RequestSaveTwoItems(int playerId, const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b);

	void Schedule(shared_ptr<DB_EVENT_BASE> event);

	[[nodiscard]] static Clock::time_point Now() noexcept { return Clock::now(); }

private:
	struct DBEventCompare
	{
		bool operator()(const shared_ptr<DB_EVENT_BASE>& lhs, const shared_ptr<DB_EVENT_BASE>& rhs) const noexcept;
	};

	void ProcessEvent(const shared_ptr<DB_EVENT_BASE>& event);

private:
	std::priority_queue<shared_ptr<DB_EVENT_BASE>, std::vector<shared_ptr<DB_EVENT_BASE>>, DBEventCompare> _events;
	std::mutex             _lock;
	std::condition_variable _cv;
	uint64_t               _nextSequence = 0;
};
