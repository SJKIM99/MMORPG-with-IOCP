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

public:
	User() = default;
	~User() = default;

	virtual void InitInstance() override;

	[[nodiscard]] shared_ptr<GameSession> GetGameSession() const;
	void SetGameSession(const shared_ptr<GameSession>& session);

	// GetStat()과 동일하게 shared_ptr을 복사해서 반환한다.
	[[nodiscard]] Inventory::SharedPtr GetInventory() noexcept { return m_inventory; }
};
