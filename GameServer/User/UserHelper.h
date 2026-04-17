#pragma once

class Subject;
class GameSession;

namespace UserHelper
{
	void SendSUBJECT_MOVE_NFY(Subject::SharedPtr sender, const ObjID& targetId);
	void SendSUBJECT_MOVE_NFY(Subject::SharedPtr sender, Subject::SharedPtr target);
	void SendSUBJECT_ADD_NFY(Subject::SharedPtr sender, const ObjID& targetId);
	void SendSUBJECT_ADD_NFY(Subject::SharedPtr sender, Subject::SharedPtr target);
	void SendSUBJECT_REMOVE_NFY(Subject::SharedPtr sender, const ObjID& targetId);
	void SendUSER_LOGIN_ACK(Subject::SharedPtr sender);
	void SendUSER_LOGIN_FAIL_ACK(Subject::SharedPtr sender);
	void SendUSER_ATTACK_ACK(Subject::SharedPtr sender, const ObjID& targetId, int32_t damage);
	void SendUSER_ATTACK_ACK(Subject::SharedPtr sender, Subject::SharedPtr target, int32_t damage);
	void SendSUBJECT_DIE_NFY(Subject::SharedPtr sender, const ObjID& targetId);
	void SendSUBJECT_DIE_NFY(Subject::SharedPtr sender, Subject::SharedPtr target);
	void SendSUBJECT_RESPAWN_NFY(Subject::SharedPtr sender, const ObjID& targetId);
	void SendSUBJECT_RESPAWN_NFY(Subject::SharedPtr sender, Subject::SharedPtr target);
	void SendSUBJECT_ATTACK_NFY(Subject::SharedPtr sender, const ObjID& attackerId);
	void SendUSER_HEAL_INF(Subject::SharedPtr sender);
	void SendUSER_STAT_CHANGE_INF(Subject::SharedPtr sender);
	[[nodiscard]] bool SaveUserInfo(const ObjID& targetId);

	void AttackMonster(ObjID& monsterId, ObjID& playerId, int damage = PLAYER_OFFENSIVE);
	void SkillAttack(ObjID& playerId);

	void HandleHeal(const ObjID& playerId);
	void HandleRespawn(const ObjID& playerId);

	void HandleLoginFail(const shared_ptr<GameSession>& session);
	void HandleGetUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo);
	void HandleAddUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo);
}
