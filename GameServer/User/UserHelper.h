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
	void SendPLAYER_ATTACK_NFY(Subject::SharedPtr viewer, const ObjID& attackerId, uint8_t facing);
	void SendSC_CHAT(Subject::SharedPtr viewer, const ObjID& senderId, const char mess[]);
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
	void SendITEM_USE_ACK(Subject::SharedPtr sender, uint16_t slotIndex, bool success);
	void SendSUBJECT_EQUIP_CHANGE_NFY(Subject::SharedPtr sender, const ObjID& targetId, uint16_t itemId);
	[[nodiscard]] bool SaveUserInfo(const ObjID& targetId);

	// player의 현재 장착 상태(Inventory::GetEquippedItemId())를 다시 계산해서
	// 주변에 이미 보이는 뷰어들에게 SUBJECT_EQUIP_CHANGE_NFY로 알린다. 장착/탈착/
	// (장착 중이던 아이템을) 버리기 중 어느 것이든 성공한 뒤 호출하면 된다 —
	// "무엇이 바뀌었는지"를 따지지 않고 항상 최종 상태를 그대로 알리므로 항상 정확하다.
	void BroadcastEquipChange(const shared_ptr<User>& player);

	// damage는 항상 호출자가 명시한다(기본값 없음) — 평타는 공격력(장비·강화 보너스가
	// 반영된 값)에 편차/치명타를 굴린 값을, 스킬은 CombatRule::SkillDamage를 넘긴다.
	// 과거처럼 기본 인자에 기대면 장비 보너스를 깜빡 빠뜨리기 쉬워서 일부러 없앴다.
	// damageFlags(SubjectDamageFlag)는 확장 세션에 보내는 SUBJECT_DAMAGE_NFY 표시용.
	void AttackMonster(ObjID& monsterId, ObjID& playerId, int damage, uint8_t damageFlags = 0);
	void SkillAttack(ObjID& playerId);
	void HandleAttack(Subject::SharedPtr attacker, uint8_t facing);
	void BroadcastChat(Subject::SharedPtr sender, const char mess[]);

	void HandleHeal(const ObjID& playerId);
	// deathSeq: 부활 타이머를 예약한 시점의 User::m_deathSeq. 그 사이에 이미
	// 리스타트했거나 다시 죽었다면(값이 다르면) 이 타이머는 무시된다.
	void HandleRespawn(const ObjID& playerId, uint32_t deathSeq);

	// 경험치 획득 처리(레벨업 시 스탯 재계산, 알림, 레벨업 이펙트까지)
	void GainExp(const shared_ptr<User>& player, uint32_t amount);

	// ── 확장 프로토콜 송신(확장 세션에만 보낸다 — 내부에서 확인) ─────────────────
	void SendUSER_INFO_EX_INF(const shared_ptr<User>& user);
	void SendSUBJECT_INFO_EX_NFY(Subject::SharedPtr viewer, Subject::SharedPtr target);
	// player의 확장 정보가 바뀌었을 때(레벨/성향치/보라돌이/가속/무기 강화) 본인과
	// 주변의 확장 세션에 다시 알린다.
	void BroadcastInfoEx(const shared_ptr<User>& player);
	void SendITEM_LIST_EX_ACK(const shared_ptr<User>& user);
	void SendITEM_SLOT_EX_INF(const shared_ptr<User>& user, uint16_t slotIndex);
	void SendADENA_INF(const shared_ptr<User>& user, int32_t delta, AdenaReason reason);
	void SendENCHANT_RESULT_ACK(const shared_ptr<User>& user, EnchantResult result, uint16_t targetSlot, uint16_t itemId, uint8_t before, uint8_t after);
	void SendSHOP_RESULT_ACK(const shared_ptr<User>& user, ShopKind kind, ShopResult result, uint32_t seq, uint16_t itemId, uint16_t count, int32_t adenaDelta);
	// 확장 코드용 시스템 메시지(레거시 세션이면 보내지 않는다)
	void SendSystemEx(const shared_ptr<User>& user, SystemMessageCode code, int32_t param1 = 0, int32_t param2 = 0);
	// victim을 볼 수 있는 확장 세션 모두에게 피해를 알린다.
	void BroadcastDamage(const Subject::SharedPtr& victim, const ObjID& attackerId, int32_t damage, uint8_t flags);
	// (x, y)를 볼 수 있는 확장 세션 모두에게 이펙트를 알린다. center는 섹터 탐색 기준.
	void BroadcastEffect(const Subject::SharedPtr& center, const ObjID& subjectId, EffectId effect, short x, short y, int32_t param);
	// 모든 Zone의 확장 세션에 공지(보스 출현 등). 드물게 일어나는 일이라 세션 목록을 훑는다.
	void BroadcastWorldNotice(WorldNoticeCode code, int32_t param1, int32_t param2, const std::string& name);
	void BroadcastWorldChat(const shared_ptr<User>& sender, const char mess[]);

	void HandleLoginFail(const shared_ptr<GameSession>& session);
	void HandleGetUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo, const vector<DB_ITEM_INFO>& items);
	void HandleAddUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo);
}
