#pragma once

#include "Core\Types.h"

class Subject;
class GameSession;

namespace UserHelper
{
	// 패킷 송신
	void SendMovePacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendAddPlayerPacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendRemovePlayerPacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendLoginSuccessPacket(Subject::SharedPtr sender);
	void SendLoginFailPacket(Subject::SharedPtr sender);
	void SendPlayerAttackToNpcPacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendNpcDiePacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendRespawnNpcPacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendNpcAttackToPlayerPacket(Subject::SharedPtr sender);
	void SendHealPacket(Subject::SharedPtr sender);
	void SendPlayerDiePacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendRespawnPlayerPacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendStatChangePacket(Subject::SharedPtr sender);
	[[nodiscard]] bool FlushPlayerSave(const ObjID& targetId);

	// 타이머 이벤트 핸들러 (GameLogicThread에서 호출)
	void HandleHeal(const ObjID& playerId);
	void HandleRespawn(const ObjID& playerId);

	// DB 콜백 핸들러 (GameLogicThread에서 호출)
	void HandleLoginFail(const shared_ptr<GameSession>& session);
	void HandleGetUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo);
	void HandleAddUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo);
}
