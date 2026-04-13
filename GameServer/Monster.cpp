#include "pch.h"
#include "Monster.h"
#include "GameObjectManager.h"
#include "WorldHelper.h"

void Monster::InitInstance()
{
	Subject::InitInstance();

	GetStat()->SetMaxHp(NPC_MAX_HP);
	GetStat()->SetHp(NPC_MAX_HP);
	GetStat()->SetOffensive(NPC_OFFENSIVE);
	GetStat()->SetDead(false);

	SetType(GetDatabaseID() <= AGGRO_NPC_BOUNDARY ? MONSTER_TYPE::AGGRO : MONSTER_TYPE::PASSIVE);
	SetName(std::format("NPC{}", GetDatabaseID()));
	SetActive(false);
	SetAttack(false);
}