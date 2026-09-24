#pragma once

#include <memory>

class User;
struct SKILL_CAST_REQ_PACKET;

// ─────────────────────────────────────────────────────────────────────────────
// 스킬 시전(서버 권위) — 검증(직업·레벨·쿨다운·MP·사거리·상태) → 소모 → 알림 →
// 판정(즉시, 또는 castMs 예고 뒤). 표는 Data/CombatData.h의 SkillTable.
// ─────────────────────────────────────────────────────────────────────────────
namespace SkillHelper
{
	void HandleCast(const std::shared_ptr<User>& caster, const SKILL_CAST_REQ_PACKET& req);
	// 예고 시간이 지난 스킬 판정(EV_SKILL_IMPACT)
	void HandleImpact(const ObjID& casterId, uint32_t impactSeq);
}
