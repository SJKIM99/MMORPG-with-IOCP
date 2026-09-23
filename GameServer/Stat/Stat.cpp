#include "pch.h"
#include "Stat.h"

uint16_t Stat::TakeDamage(uint16_t amount) noexcept
{
	const uint16_t current = m_hp.load();
	const uint16_t next    = (current > amount) ? static_cast<uint16_t>(current - amount) : 0;
	m_hp.store(next);
	if (next == 0)
		m_die.store(true);
	return next;
}

void Stat::HealHp(uint16_t amount, uint16_t maxCap) noexcept
{
	const uint32_t current = m_hp.load();
	const uint16_t next    = (current + amount >= maxCap)
		                    ? maxCap
		                    : static_cast<uint16_t>(current + amount);
	m_hp.store(next);
}

uint8_t Stat::AddExp(uint32_t amount) noexcept
{
	if (m_level >= MAX_LEVEL)
		return 0;

	uint8_t gained = 0;
	m_exp += amount;
	while (m_level < MAX_LEVEL)
	{
		const uint32_t needed = LevelTable::ExpToNext(m_level);
		if (m_exp < needed)
			break;
		m_exp -= needed;
		++m_level;
		++gained;
	}

	// 만렙에 도달하면 남은 경험치는 의미가 없으므로 0으로 둔다(표시가 꼬이지 않게).
	if (m_level >= MAX_LEVEL)
		m_exp = 0;

	return gained;
}

uint32_t Stat::LoseExp(uint32_t amount) noexcept
{
	const uint32_t lost = (amount < m_exp) ? amount : m_exp;
	m_exp -= lost;
	return lost;
}
