#pragma once

#include "AStar.h"
#include "Subject.h"
#include "GameObjectManager.h"

// -------------------------------------------------------
// User  : 플레이어(PC) 전용 클래스
//         Player 클래스 제거 후 기능을 흡수
// -------------------------------------------------------
class User : public Subject
{
public:
	User();

	static void InitializePlayers();

	void InitInstance() override;
	void ResetGameplayState() override;

	void Heal();
	[[nodiscard]] bool TryBuildSaveInfo(DB_PLAYER_INFO& outPlayerInfo) const;

	int  GetTarget() const     { return _traceNpcId.load(); }
	void SetTarget(int id)     { _traceNpcId.store(id); }

private:
	Atomic<int> _traceNpcId = -1;
};

// -------------------------------------------------------
// Monster : NPC 전용 클래스 (Subject 직접 상속)
// -------------------------------------------------------
class Monster : public Subject
{
public:
	Monster();

	void InitInstance() override;
	void ResetGameplayState() override;

	MONSTER_TYPE    GetType()  const         { return _type; }
	void            SetType(MONSTER_TYPE t)  { _type = t; }
	vector<NODE>&   GetPath()                { return _astarPath; }
	void            ClearPath()              { _astarPath.clear(); }

	// Lifecycle (moved from NPC singleton)
	static void InitAll();
	static void RandomMove(uint32 npcId);
	static void AStarMove(uint32 npcId, short nextX, short nextY);

private:
	MONSTER_TYPE _type;
	vector<NODE> _astarPath;
};

// -------------------------------------------------------
// ID helpers  (PC: [0, MAX_USER), NPC: [MAX_USER, MAX_USER+MAX_NPC))
// -------------------------------------------------------
inline bool     IsPc(uint32 id)      { return id < MAX_USER; }
inline bool     IsNPC(uint32 id)     { return !IsPc(id); }
inline User*    AsUser(uint32 id)    { return static_cast<User*>((*GObjectManager)[id].get()); }
inline Monster* AsMonster(uint32 id) { return static_cast<Monster*>((*GObjectManager)[id].get()); }
