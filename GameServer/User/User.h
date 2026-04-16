#pragma once

#include "Subject.h"

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

public:
	User() = default;
	~User() = default;

	virtual void InitInstance() override;
	virtual bool OnUpdate() override;

	[[nodiscard]] shared_ptr<GameSession> GetGameSession() const;
	void SetGameSession(const shared_ptr<GameSession>& session);
};
