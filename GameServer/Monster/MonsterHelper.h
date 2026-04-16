#pragma once
#include "Monster.h"

namespace MonsterHelper
{
	void RandomMove(ObjID& monsterId);
	void AStarMove(ObjID& monsterId, short nextX, short nextY);

	void WakeUpMonster(ObjID& monsterId, ObjID& wakerId, bool forceWake = false);

	void HandleRandomMove(const ObjID& monsterId);
	void HandleRespawn(const ObjID& monsterId);
	void HandleAggroMove(const ObjID& monsterId, const ObjID& targetId);
	void HandleAttackToPlayer(const ObjID& monsterId, const ObjID& playerId);

	bool Init();
};
