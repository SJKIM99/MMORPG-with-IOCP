#pragma once

#include "ObjID.h"

#include <vector>

namespace SectorHelper
{
	void UpdateObjectPosition(ObjID& subjectId, short nextX, short nextY);
	void PlaceObjectAtRandomWalkablePosition(ObjID& subjectId);
	[[nodiscard]] std::vector<ObjID> CollectVisiblePlayersAround(ObjID& monsterId);
	void BroadcastMonsterVisibilityDelta(
		ObjID monsterId,
		const std::vector<ObjID>& oldList,
		const std::vector<ObjID>& newList);
	void NotifyPlayerEnteredWorld(ObjID& playerId, bool isRespawn);
	void UpdatePlayerViewList(ObjID& clientId);
}
