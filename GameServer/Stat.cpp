#include "pch.h"
#include "Stat.h"

bool Stat::OnUpdate()
{
	return true;
}

uint16 Stat::TakeDamage(uint16 amount) noexcept
{
	const uint16 current = _hp.load();
	const uint16 next    = (current > amount) ? static_cast<uint16>(current - amount) : 0;
	_hp.store(next);
	if (next == 0)
		_die.store(true);
	return next;
}

void Stat::HealHp(uint16 amount, uint16 maxCap) noexcept
{
	const uint32 current = _hp.load();
	const uint16 next    = (current + amount >= maxCap)
		                    ? maxCap
		                    : static_cast<uint16>(current + amount);
	_hp.store(next);
}

bool Stat::AddExp(uint32 amount) noexcept
{
	if (_level >= MAX_LEVEL)
		return false;

	_exp += amount;
	const uint32 needed = EXP_TABLE[_level - 1];
	if (_exp >= needed)
	{
		_exp -= needed;
		_level++;
		return true;
	}
	return false;
}

