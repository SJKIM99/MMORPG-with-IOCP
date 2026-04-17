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
	short m_pathGoalX = -1;
	short m_pathGoalY = -1;

	// 마지막 이동 시점에 이 몬스터를 보고 있던 유저 목록.
	// 다음 이동 시 oldList로 재사용해 CollectUsers 호출을 절반으로 줄인다.
	// GameLogicThread 단일 소비자이므로 동기화 불필요.
	vector<ObjID> m_viewList;

public:
	using SharedPtr = shared_ptr<Monster>;
	using WeakPtr = weak_ptr<Monster>;

public:
	Monster() = default;
	Monster(const ObjID& objID) : Subject(objID) {};

	virtual void InitInstance() override;

	MONSTER_TYPE GetType() const { return m_Type; }
	vector<NODE>& GetPath() { return m_AstarPath; }
	bool IsPathTarget(short x, short y) const noexcept { return m_pathGoalX == x && m_pathGoalY == y; }
	bool HasCachedPathTo(short x, short y) const noexcept { return IsPathTarget(x, y) && !m_AstarPath.empty(); }

	bool IsActive() const { return m_Active.load(); }
	bool IsAttacking() const { return m_Attack.load(); }

	// TryActivate: false → already active, true → first to activate
	bool TryActivate() { return !m_Active.exchange(true); }

	void SetType(MONSTER_TYPE t) { m_Type = t; }
	void CachePathTarget(short x, short y) noexcept { m_pathGoalX = x; m_pathGoalY = y; }
	void ClearPath()             { m_AstarPath.clear(); m_pathGoalX = -1; m_pathGoalY = -1; }

	void SetActive(bool active) { m_Active.store(active); }
	void SetAttack(bool attack) { m_Attack.store(attack); }

	const vector<ObjID>& GetViewList() const noexcept { return m_viewList; }
	void SetViewList(vector<ObjID> list) noexcept { m_viewList = std::move(list); }
	void ClearViewList() noexcept { m_viewList.clear(); }

	void  SetSpawn(short x, short y) { m_spawnX = x; m_spawnY = y; }
	short GetSpawnX() const { return m_spawnX; }
	short GetSpawnY() const { return m_spawnY; }

};
