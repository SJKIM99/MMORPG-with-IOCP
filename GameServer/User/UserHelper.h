#pragma once

class Subject;
class GameSession;

namespace UserHelper
{
	void SendMovePacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendAddPlayerPacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendRemovePlayerPacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendLoginSuccessPacket(Subject::SharedPtr sender);
	void SendLoginFailPacket(Subject::SharedPtr sender);
	void SendPlayerAttackToMonsterPacket(Subject::SharedPtr sender, const ObjID& targetId, int32_t damage);
	void SendMonsterDiePacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendRespawnMonsterPacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendMonsterAttackToPlayerPacket(Subject::SharedPtr sender, const ObjID& monsterId);
	void SendHealPacket(Subject::SharedPtr sender);
	void SendPlayerDiePacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendRespawnPlayerPacket(Subject::SharedPtr sender, const ObjID& targetId);
	void SendStatChangePacket(Subject::SharedPtr sender);
	[[nodiscard]] bool FlushPlayerSave(const ObjID& targetId);

	void AttackMonster(ObjID& monsterId, ObjID& playerId, int damage = PLAYER_OFFENSIVE);
	void SkillAttack(ObjID& playerId);

	void HandleHeal(const ObjID& playerId);
	void HandleRespawn(const ObjID& playerId);

	void HandleLoginFail(const shared_ptr<GameSession>& session);
	void HandleGetUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo);
	void HandleAddUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo);
}
