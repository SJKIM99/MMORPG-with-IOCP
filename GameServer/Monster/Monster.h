#pragma once

#include "AStar.h"
#include "Subject.h"

class Monster : public Subject
{
	MONSTER_TYPE m_Type;
	vector<NODE> m_AstarPath;
	std::atomic<bool> m_Active;
	std::atomic<bool> m_Attack;

	short m_spawnX = 0;
	short m_spawnY = 0;

public:
	Monster() = default;
	Monster(const ObjID& objID) : Subject(objID) {};

	virtual void InitInstance() override;

	MONSTER_TYPE GetType() const { return m_Type; }
	vector<NODE>& GetPath() { return m_AstarPath; }

	bool IsActive() const { return m_Active.load(); }
	bool IsAttacking() const { return m_Attack.load(); }

	// TryActivate: false → already active, true → first to activate
	bool TryActivate() { return !m_Active.exchange(true); }

	void SetType(MONSTER_TYPE t) { m_Type = t; }
	void ClearPath()             { m_AstarPath.clear(); }

	void SetActive(bool active) { m_Active.store(active); }
	void SetAttack(bool attack) { m_Attack.store(attack); }

	void  SetSpawn(short x, short y) { m_spawnX = x; m_spawnY = y; }
	short GetSpawnX() const { return m_spawnX; }
	short GetSpawnY() const { return m_spawnY; }
};
