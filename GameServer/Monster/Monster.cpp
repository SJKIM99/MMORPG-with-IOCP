#include "pch.h"
#include "Monster.h"
#include "GameObjectManager.h"

void Monster::InitInstance()
{
	Subject::InitInstance();

	GetStat()->SetMaxHp(MONSTER_MAX_HP);
	GetStat()->SetHp(MONSTER_MAX_HP);
	GetStat()->SetOffensive(MONSTER_OFFENSIVE);
	GetStat()->SetDead(false);

	SetType(GetDatabaseID() <= AGGRO_MONSTER_BOUNDARY ? MONSTER_TYPE::AGGRO : MONSTER_TYPE::PASSIVE);
	SetName(std::format("Monster{}", GetDatabaseID()));
	SetActive(false);
	SetAttack(false);
}
