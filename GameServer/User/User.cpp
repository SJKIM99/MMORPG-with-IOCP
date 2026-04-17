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

shared_ptr<GameSession> User::GetGameSession() const
{
	return m_session;
}

void User::SetGameSession(const shared_ptr<GameSession>& session)
{
	m_session = session;
}
