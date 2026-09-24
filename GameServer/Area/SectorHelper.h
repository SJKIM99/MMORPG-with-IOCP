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
	// notifySelf=false: 확장 세션의 순번 이동(USER_MOVE_EX_REQ)은 본인에게 SUBJECT_MOVE_NFY 대신
	// USER_MOVE_EX_ACK로 결과를 알려 주므로 본인 알림을 생략한다.
	void HandlePlayerMove(const std::shared_ptr<User>& player, short nextX, short nextY, bool notifySelf = true);

	// Zone B 스레드에서 실행 — HandleZoneTransfer가 비동기로 위임하는 진입 처리
	void HandleEnterZone(const ObjID& playerId, short nextX, short nextY);
}
