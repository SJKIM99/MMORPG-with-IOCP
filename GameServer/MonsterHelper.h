#pragma once
#include "Monster.h"

namespace MonsterHelper
{
	// 이동 유틸
	void RandomMove(ObjID& monsterId);
	void AStarMove(ObjID& monsterId, short nextX, short nextY);

	// 타이머 이벤트 핸들러 (GameLogicThread에서 호출)
	void HandleRandomMove(const ObjID& npcId);
	void HandleRespawn(const ObjID& npcId);
	void HandleAggroMove(const ObjID& npcId, const ObjID& targetId);
	void HandleAttackToPlayer(const ObjID& npcId, const ObjID& playerId);

	bool Init();
};

