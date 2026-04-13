#pragma once

#include "Core\Types.h"
#include "ObjID.h"

#include <vector>

namespace WorldHelper
{
	[[nodiscard]] uint32 GetNowTime();
	void MovePositionByDirection(short& x, short& y, char direction);
	[[nodiscard]] bool CanSee(const ObjID& a, const ObjID& b);
	[[nodiscard]] bool CanAttack(const ObjID& a, const ObjID& b);
	void UpdateObjectPosition(ObjID& subjectId, short nextX, short nextY);
	void PlaceObjectAtRandomWalkablePosition(ObjID& subjectId);
	[[nodiscard]] std::vector<ObjID> CollectVisiblePlayersAround(ObjID& monsterId);
	void BroadcastNpcVisibilityDelta(
		ObjID npcId,
		const std::vector<ObjID>& oldList,
		const std::vector<ObjID>& newList);
	void NotifyPlayerEnteredWorld(ObjID& playerId, bool isRespawn);
	void UpdatePlayerViewList(ObjID& clientId);
	void WakeUpNpc(ObjID& npcId, ObjID& wakerId);
	void AttackNpc(ObjID& npcId, ObjID& playerId);
}
