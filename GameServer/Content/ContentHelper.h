#pragma once

class User;
class Monster;

// ─────────────────────────────────────────────────────────────────────────────
// 리니지풍 컨텐츠 처리 — 대상 지정 공격/PK·성향치, 사망·리스타트, 아이템 효과
// (귀환/순간이동/가속), 무기 강화, 상점·텔레포터.
//
// 전제(기존 서버와 동일): 모든 함수는 대상 플레이어를 소유한 Zone 스레드 위에서
// 실행된다(Route::Dispatch 경유 또는 같은 Zone의 타이머). 그래서 인벤토리/스탯/성향치를
// 락 없이 다룬다. 다른 Zone으로 넘어가야 하는 일(리스타트, 텔레포트)은 기존 Zone
// Transfer와 같은 방식 — 소유권을 넘기고 새 Zone 큐에 메시지로 위임 — 으로 처리한다.
// ─────────────────────────────────────────────────────────────────────────────
namespace ContentHelper
{
	// ── 전투 ──
	// USER_TARGET_ATTACK_REQ. 대상이 몬스터면 사냥, 플레이어면 PK 규칙(보라돌이/카오틱).
	void HandleTargetAttack(const shared_ptr<User>& attacker, const ObjID& targetId);
	// V2 스킬의 PvP 피해(보라돌이/카오틱 규칙·방어·회피·철벽 태세 포함)
	void ApplyPvpDamage(const shared_ptr<User>& attacker, const shared_ptr<User>& victim, uint32_t damage, uint8_t flags,
		uint16_t skillId, uint8_t comboId, uint8_t flags2, uint8_t hitIndex);

	// 플레이어 사망 공통 처리(몬스터에게든 PK든): 사망 알림, 섹터 제거, 경험치 페널티,
	// 카오틱 아이템 드롭, 부활 타이머 예약.
	void HandlePlayerDeath(const shared_ptr<User>& victim, const ObjID& killerId, bool killedByPlayer);

	// 몬스터 처치 보상 중 성향치/보스 공지(경험치·드롭은 UserHelper::AttackMonster가 한다)
	void OnMonsterKilled(const shared_ptr<User>& killer, const shared_ptr<Monster>& monster);

	// ── 사망/부활 ──
	void HandleRestartRequest(const shared_ptr<User>& player);   // USER_RESTART_REQ
	void RestartInTown(const shared_ptr<User>& player);          // 마을 광장에서 부활(필요하면 Zone 이동)

	// ── 이동 ──
	// 살아 있는 플레이어를 (x, y)로 순간이동(같은 Zone이든 다른 Zone이든). 이펙트 포함.
	void TeleportPlayer(const shared_ptr<User>& player, short x, short y);

	// ── 아이템 ──
	// 회복 물약이 아닌 소비 아이템(귀환/순간이동 주문서, 초록 물약) 사용
	void UseEffectItem(const shared_ptr<User>& player, uint16_t slotIndex);
	void HandleEnchant(const shared_ptr<User>& player, uint16_t scrollSlot, uint16_t targetSlot);
	void GiveStarterKit(const shared_ptr<User>& player);

	// ── 상점/텔레포터 ──
	void HandleShopBuy(const shared_ptr<User>& player, uint16_t npcId, uint16_t itemId, uint16_t count, uint32_t seq);
	void HandleShopSell(const shared_ptr<User>& player, uint16_t npcId, uint16_t slotIndex, uint16_t count, uint32_t seq);
	void HandleNpcTeleport(const shared_ptr<User>& player, uint16_t npcId, uint16_t destZone, uint32_t seq);

	// ── 상태 만료 타이머 ──
	void HandlePurpleExpire(const ObjID& playerId);
	void HandleHasteExpire(const ObjID& playerId);

	// 성향치 DB 저장(force가 아니면 변화가 충분히 클 때만)
	void SaveAlignment(const shared_ptr<User>& player, bool force);
}
