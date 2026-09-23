#include "pch.h"
#include "Monster.h"
#include "GameObjectManager.h"

void Monster::InitInstance()
{
	Subject::InitInstance();

	// 종족 id는 ObjID의 KIND에 실려 온다(MonsterHelper::InitForZone이 생성 시 지정).
	// KIND가 0이거나 테이블에 없으면 예전 동작(고정 스탯, ID 구간으로 선공 여부)으로 둔다.
	m_species = MonsterTable::Find(GetKIND());
	if (m_species != nullptr)
	{
		GetStat()->SetMaxHp(m_species->maxHp);
		GetStat()->SetHp(m_species->maxHp);
		GetStat()->SetOffensive(m_species->attack);
		GetStat()->SetLevel(m_species->level);
		SetType(m_species->aggressive ? MONSTER_TYPE::AGGRO : MONSTER_TYPE::PASSIVE);
		SetName(m_species->name);
	}
	else
	{
		GetStat()->SetMaxHp(MONSTER_MAX_HP);
		GetStat()->SetHp(MONSTER_MAX_HP);
		GetStat()->SetOffensive(MONSTER_OFFENSIVE);
		SetType(GetDatabaseID() <= AGGRO_MONSTER_BOUNDARY ? MONSTER_TYPE::AGGRO : MONSTER_TYPE::PASSIVE);
		SetName(std::format("Monster{}", GetDatabaseID()));
	}
	GetStat()->SetDead(false);

	ClearPath();
	ClearProvoke();
	SetActive(false);
	SetAttack(false);
}
