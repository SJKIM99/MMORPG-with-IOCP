#include "pch.h"
#include "User.h"
#include "TimerThread.h"

void User::InitInstance()
{
	Subject::InitInstance();

	GetStat()->SetMaxHp(PLAYER_MAX_HP);
	GetStat()->SetHp(PLAYER_MAX_HP);
	GetStat()->SetOffensive(PLAYER_OFFENSIVE);
	GetStat()->SetDead(false);
}

bool User::OnUpdate()
{
	return true;
}

shared_ptr<GameSession> User::GetGameSession() const
{
	return _session;
}

void User::SetGameSession(const shared_ptr<GameSession>& session)
{
	_session = session;
}