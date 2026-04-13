#pragma once

#include "Subject.h"

class User : public Subject
{
	shared_ptr<GameSession> _session;

public:
	using SharedPtr = shared_ptr<User>;
	using WeakPtr = weak_ptr<User>;

public:
	uint32 _lastMoveTime   = 0;
	uint32 _lastAttackTime = 0;

	User() = default;
	~User() = default;

	virtual void InitInstance() override;
	virtual bool OnUpdate() override;

	[[nodiscard]] shared_ptr<GameSession> GetGameSession() const;
	void SetGameSession(const shared_ptr<GameSession>& session);
};
