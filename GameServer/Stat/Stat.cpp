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

bool Stat::AddExp(uint32_t amount) noexcept
{
	if (m_level >= MAX_LEVEL)
		return false;

	m_exp += amount;
	const uint32_t needed = EXP_TABLE[m_level - 1];
	if (m_exp >= needed)
	{
		m_exp -= needed;
		m_level++;
		return true;
	}
	return false;
}
