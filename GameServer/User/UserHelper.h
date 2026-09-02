#pragma once

class Subject;
class GameSession;
class User;

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
	void SendSUBJECT_ATTACK_NFY(Subject::SharedPtr viewer, const ObjID& victimId, const ObjID& attackerId, int32_t victimHp);
	void SendUSER_HEAL_INF(Subject::SharedPtr sender);
	void SendUSER_STAT_CHANGE_INF(Subject::SharedPtr sender);
	void SendITEM_LIST_ACK(Subject::SharedPtr sender);
	void SendITEM_EQUIP_ACK(Subject::SharedPtr sender, uint16_t slotIndex, bool success);
	void SendITEM_UNEQUIP_ACK(Subject::SharedPtr sender, uint16_t slotIndex, bool success);
	void SendITEM_SWAP_ACK(Subject::SharedPtr sender, uint16_t slotIndexA, uint16_t slotIndexB, bool success);
	void SendITEM_ACQUIRE_INF(Subject::SharedPtr sender, uint16_t slotIndex);
	void SendSYSTEM_MESSAGE_INF(Subject::SharedPtr sender, SystemMessageCode code, int32_t param1, int32_t param2);
	void SendITEM_DISCARD_ACK(Subject::SharedPtr sender, uint16_t slotIndex, bool success);
	void SendITEM_PICKUP_ACK(Subject::SharedPtr sender, bool success);
	void SendSUBJECT_EQUIP_CHANGE_NFY(Subject::SharedPtr sender, const ObjID& targetId, uint16_t itemId);
	[[nodiscard]] bool SaveUserInfo(const ObjID& targetId);

	// player의 현재 장착 상태(Inventory::GetEquippedItemId())를 다시 계산해서
	// 주변에 이미 보이는 뷰어들에게 SUBJECT_EQUIP_CHANGE_NFY로 알린다. 장착/탈착/
	// (장착 중이던 아이템을) 버리기 중 어느 것이든 성공한 뒤 호출하면 된다 —
	// "무엇이 바뀌었는지"를 따지지 않고 항상 최종 상태를 그대로 알리므로 항상 정확하다.
	void BroadcastEquipChange(const shared_ptr<User>& player);

	void AttackMonster(ObjID& monsterId, ObjID& playerId, int damage = PLAYER_OFFENSIVE);
	void SkillAttack(ObjID& playerId);
	void HandleAttack(Subject::SharedPtr attacker, uint8_t facing);
	void BroadcastChat(Subject::SharedPtr sender, const char mess[]);

	void HandleHeal(const ObjID& playerId);
	void HandleRespawn(const ObjID& playerId);

	void HandleLoginFail(const shared_ptr<GameSession>& session);
	void HandleGetUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo, const vector<DB_ITEM_INFO>& items);
	void HandleAddUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo);
}
