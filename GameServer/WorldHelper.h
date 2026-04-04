#pragma once

#include "Core\Types.h"

#include <vector>

namespace WorldHelper
{
	[[nodiscard]] uint32 GetNowTime();
	void MovePositionByDirection(short& x, short& y, char direction);
	[[nodiscard]] bool CanSee(uint32 from, uint32 to);
	[[nodiscard]] bool CanAttack(uint32 from, uint32 to);
	void UpdateObjectPosition(uint32 objectId, short nextX, short nextY);
	void PlaceObjectAtRandomWalkablePosition(uint32 objectId);
	[[nodiscard]] std::vector<uint32> CollectVisiblePlayersAround(uint32 npcId);
	void BroadcastNpcVisibilityDelta(
		uint32 npcId,
		const std::vector<uint32>& oldList,
		const std::vector<uint32>& newList);
	void NotifyPlayerEnteredWorld(uint32 playerId, bool isRespawn);
	void UpdatePlayerViewList(uint32 clientId);
	void WakeUpNpc(uint32 npcId, uint32 wakerId);
	void AttackNpc(uint32 npcId, uint32 playerId);
}
