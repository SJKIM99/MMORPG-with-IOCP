#include "pch.h"
#include "Data/MoveRule.h"
#include "User.h"
#include "TimerThread.h"

void User::InitInstance()
{
	Subject::InitInstance();

	GetStat()->SetMaxHp(PLAYER_MAX_HP);
	GetStat()->SetHp(PLAYER_MAX_HP);
	GetStat()->SetOffensive(PLAYER_OFFENSIVE);
	GetStat()->SetDead(false);

	m_inventory = std::make_shared<Inventory>();
	m_inventory->Attach(self<User>());
}

shared_ptr<GameSession> User::GetGameSession() const
{
	return m_session;
}

void User::SetGameSession(const shared_ptr<GameSession>& session)
{
	m_session = session;
}

bool User::IsExtended() const noexcept
{
	return m_session != nullptr && m_session->IsExtended();
}

void User::ApplyLevelStats(bool fullHeal)
{
	const uint8_t level = GetStat()->GetLevel();
	const uint16_t maxHp = LevelTable::MaxHp(level);
	GetStat()->SetMaxHp(maxHp);
	if (fullHeal || GetStat()->GetHp() > maxHp)
		GetStat()->SetHp(maxHp);

	// 공격력 = 레벨 기본 + 장착 무기 보너스 + 강화 보너스. 계산은 장비 상태를 아는
	// Inventory가 한 곳에서 한다(장착/탈착/강화 때도 같은 함수가 불린다).
	if (m_inventory != nullptr)
		m_inventory->RecomputeOffensive();
}

uint32_t User::GetMoveSpeedPercent(uint32_t now) const noexcept
{
	uint32_t speed = 100;
	if (IsHasted(now))
		speed += MoveRule::kHasteBonusPercent;
	return MoveRule::ClampSpeed(speed);
}
