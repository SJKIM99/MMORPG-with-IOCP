#pragma once

#include "Subject.h"
#include "Item/Inventory.h"

class User : public Subject
{
public:
	using SharedPtr = shared_ptr<User>;
	using WeakPtr = weak_ptr<User>;

public:
	uint32_t m_lastMoveTime   = 0;
	uint32_t m_lastAttackTime = 0;
	uint32_t m_lastSkillTime  = 0;

private:
	shared_ptr<GameSession> m_session;
	// Monster는 인벤토리가 없으므로 Subject가 아닌 User에 둔다 (m_session과 같은 이유).
	Inventory::SharedPtr m_inventory;
	// Players.playerId(DB 발급 정수 PK). 로그인/계정생성 시 DB 스레드가 한 번 조회해
	// UserHelper::HandleGetUserInfo/HandleAddUserInfo에서 곧바로 세팅해준다 —
	// 이후 Inventory 저장 경로는 매번 name으로 재조회하지 않고 이 값을 그대로 쓴다.
	int m_playerId = 0;

public:
	User() = default;
	~User() = default;

	virtual void InitInstance() override;

	[[nodiscard]] shared_ptr<GameSession> GetGameSession() const;
	void SetGameSession(const shared_ptr<GameSession>& session);

	// GetStat()과 동일하게 shared_ptr을 복사해서 반환한다.
	[[nodiscard]] Inventory::SharedPtr GetInventory() noexcept { return m_inventory; }

	[[nodiscard]] int GetPlayerId() const noexcept { return m_playerId; }
	void SetPlayerId(int playerId) noexcept { m_playerId = playerId; }
};
