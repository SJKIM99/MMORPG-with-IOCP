#pragma once
#include "Monster.h"
#include "Zone/ZoneTypes.h"

namespace MonsterHelper
{
	void RandomMove(ObjID& monsterId);
	void AStarMove(ObjID& monsterId, short nextX, short nextY);

	void WakeUpMonster(ObjID& monsterId, ObjID& wakerId, bool forceWake = false);

	void HandleRandomMove(const ObjID& monsterId);
	void HandleRespawn(const ObjID& monsterId);
	void HandleAggroMove(const ObjID& monsterId, const ObjID& targetId);
	void HandleAttackToPlayer(const ObjID& monsterId, const ObjID& playerId);

	// Zone 전용 스레드에서 GSector 설정 이후 호출.
	// stride 방식으로 전 존에 AGGRO/PASSIVE를 균등 분배한다.
	void InitForZone(ZoneId zoneId);
};
