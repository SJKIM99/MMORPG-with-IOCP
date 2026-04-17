#pragma once

#include "ObjID.h"

#include <memory>
#include <vector>

class User;

namespace SectorHelper
{
	void UpdatePosition(ObjID& subjectId, short nextX, short nextY);
	void GetRandomPosition(ObjID& subjectId);
	[[nodiscard]] std::vector<ObjID> CollectUsers(ObjID& monsterId);
	[[nodiscard]] std::vector<ObjID> CollectSubjects(const std::shared_ptr<User>& player);
	void Replace(ObjID monsterId, const std::vector<ObjID>& oldList, const std::vector<ObjID>& newList);
	void NotifyPlayerEnteredWorld(ObjID& playerId, bool isRespawn);
	void HandlePlayerMove(const std::shared_ptr<User>& player, short nextX, short nextY);
}
