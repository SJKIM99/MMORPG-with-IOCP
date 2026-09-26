#pragma once

#include "Data/CombatData.h"
#include "Data/SkillData.h"

class Subject;
class User;
class Monster;
enum class SkillResult : uint8_t;

// ─────────────────────────────────────────────────────────────────────────────
// 기술 판정 내부 도구 — SkillHelper(예전 요청)와 SkillEx(Phase 2-B 확장 요청)가 함께 쓴다.
// 모두 시전자의 소유 Zone 스레드에서 부른다.
// ─────────────────────────────────────────────────────────────────────────────
namespace SkillDetail
{
	// 한 번의 적중 — 연계(대상 상태) 판정, 피해, 상태이상, 넉백까지.
	struct HitSpec
	{
		uint32_t coeff = 100;
		bool     forceCrit = false;
		uint32_t bonusPercent = 0;   // 추가 피해 %(연계 외)
		uint8_t  comboId = 0;        // 강제 연계 표시
		uint8_t  flags2 = 0;
		uint8_t  hitIndex = 0;       // 예전: 연타 번호 / 확장: 0x80 | 타격표 번호(클라가 시각표로 반응을 찾는다)
		bool     applyStatus = true;
		bool     applyKnockback = true;
		bool     evaluateCombo = true;
		// ── Phase 2-B ──
		HitReact react = HitReact::None;   // 확장 타격의 반응(예전 넉백 칸 대신)
		uint8_t  stagger = 0;              // 보스 무력화 수치
		bool     counter = false;          // 보스 강공격 예고를 끊는다
		bool     extras = false;           // 잔향 · 분신 · 함성 · 후방 보너스 · 게이지(확장 타격)
		uint32_t castSeq = 0;              // 분신은 시전마다 한 번
		short    fromX = 0, fromY = 0;     // 넉백 · 끌어당김 기준(시전자 또는 지점)
	};

	void SendResult(const std::shared_ptr<User>& u, uint16_t seq, uint16_t skillId, SkillResult r, uint32_t ms);
	[[nodiscard]] std::shared_ptr<Subject> ResolveEnemy(const std::shared_ptr<User>& u, const ObjID& targetId);
	[[nodiscard]] bool IsMonster(const std::shared_ptr<Subject>& s);
	[[nodiscard]] bool Walkable(int x, int y);
	[[nodiscard]] std::shared_ptr<Monster> MonsterAt(const std::shared_ptr<User>& u, int x, int y);
	// 돌려주는 값 = 실제로 준 피해(0이면 헛침 · 사망)
	uint32_t DealHit(const std::shared_ptr<User>& u, const SkillRow& row, const std::shared_ptr<Subject>& target, HitSpec spec);
	// 예전 판정(기술 id마다 switch) — 확장 경로의 Legacy 기술도 이것을 부른다
	void Execute(const std::shared_ptr<User>& u, const SkillRow& row, const ObjID& targetId, short tx, short ty);
	// 옛길 도약 착지 칸(막혔으면 가까운 쪽으로). 못 가면 false
	bool BlinkLanding(const std::shared_ptr<User>& u, const SkillRow& row, short& tx, short& ty);

	// 확장 경로에서 예전 판정(Execute)을 부르는 동안, 그 안의 DealHit에 표의 무력화 · 반격 · 반응 · 게이지를 싣는다.
	// 같은 Zone 스레드 안에서 동기로만 쓰므로 thread_local 한 칸이면 된다.
	class LegacyScope
	{
	public:
		explicit LegacyScope(const HitSpec& ctx);
		~LegacyScope();
		LegacyScope(const LegacyScope&) = delete;
		LegacyScope& operator=(const LegacyScope&) = delete;
	private:
		const HitSpec* m_prev;
	};
}
