#pragma once

#include "Data/CombatData.h"

class Subject;
class User;
class Monster;

// ─────────────────────────────────────────────────────────────────────────────
// 전투 V2 공용 처리 — 피해 계산·적용, 상태이상, 넉백, 자원(MP), 능력치 알림.
// 모든 함수는 대상의 소유 Zone 스레드에서 호출된다(스킬 시전자와 같은 Zone).
// ─────────────────────────────────────────────────────────────────────────────
namespace CombatHelper
{
	struct HitCtx
	{
		uint16_t skillId  = 0;
		uint8_t  comboId  = 0;
		uint8_t  flags    = 0;   // SubjectDamageFlag
		uint8_t  flags2   = 0;   // SubjectDamageFlag2
		uint8_t  hitIndex = 0;
	};

	// 몬스터가 V2 규칙(대각선 포함 인접)으로 대상을 칠 수 있는가
	[[nodiscard]] bool MonsterCanReach(const std::shared_ptr<Monster>& monster, const std::shared_ptr<User>& target);

	// 시전자 주변(같은 Zone 섹터 이웃)에서 id로 찾기
	[[nodiscard]] std::shared_ptr<Subject> FindNear(const std::shared_ptr<Subject>& me, const ObjID& id);
	// (cx, cy) 체비셰프 radius 안의 살아 있는 몬스터(마을 밖)
	[[nodiscard]] std::vector<std::shared_ptr<Monster>> MonstersAround(const std::shared_ptr<Subject>& me, int cx, int cy, int radius);

	// 유저 공격 피해 굴림(±10%, 치명타). forceCrit이면 확정 치명타.
	[[nodiscard]] uint32_t RollUserDamage(const std::shared_ptr<User>& u, DamageKind kind, uint32_t coeffPercent, bool forceCrit, bool& outCrit);

	// 플레이어 → 몬스터(방어 적용 후 UserHelper::AttackMonster로 처치·보상까지)
	void HitMonster(const std::shared_ptr<User>& attacker, const std::shared_ptr<Monster>& monster, uint32_t damage, const HitCtx& ctx);
	// 몬스터 → 확장 세션 플레이어(회피·방어·철벽 태세·반격). 죽었으면 true.
	bool HitPlayerFromMonster(const std::shared_ptr<Monster>& monster, const std::shared_ptr<User>& victim, uint32_t raw, uint8_t flags);

	// 상태이상
	void ApplyStatus(const std::shared_ptr<Subject>& target, StatusId id, uint32_t durationMs, uint8_t stacks, const ObjID& source, uint16_t tickDamage = 0);
	void RemoveStatus(const std::shared_ptr<Subject>& target, StatusId id);
	void HandleStatusTick(const ObjID& subjectId);

	// 넉백 — 몬스터를 (fromX, fromY) 반대쪽으로 tiles칸 민다. 벽에 막히면 outWall.
	bool Knockback(const std::shared_ptr<Monster>& target, int fromX, int fromY, int tiles, bool& outWall);
	// 서버가 옮기는 플레이어 이동(돌진·도약·그림자 돌진). 걸음 시간 stepMs로 보간.
	bool MovePlayerTo(const std::shared_ptr<User>& player, short x, short y, uint32_t stepMs);

	// 자원·능력치 알림(확장 세션)
	void SendResource(const std::shared_ptr<User>& u);
	void SendStats(const std::shared_ptr<User>& u);
	void GainMp(const std::shared_ptr<User>& u, int amount);
	// 레벨업·장비 변경·포인트 분배 뒤: 다시 계산하고 바뀌었으면 알린다.
	void RefreshStats(const std::shared_ptr<User>& u, bool fullHeal);
	void HandleStatAlloc(const std::shared_ptr<User>& u, uint8_t stat, uint8_t amount);
}
