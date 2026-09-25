#pragma once

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 레벨/전투/강화/성향치 규칙 — 서버와 클라이언트가 "같은 파일"을 읽는다.
//
// 서버는 판정에, 클라이언트는 표시(다음 레벨까지 경험치, 강화 성공 확률 안내,
// 이름 색 등)에 쓴다. 한쪽만 숫자를 바꿔 둘이 어긋나는 일을 원천적으로 막기
// 위해 전부 constexpr 함수/상수로 두고 Windows/서버 전용 헤더를 끌어오지 않는다
// (<cstdint>만 사용 — 클라이언트 빌드에서도 그대로 컴파일된다).
// ─────────────────────────────────────────────────────────────────────────────
namespace LevelTable
{
	inline constexpr uint8_t kMaxLevel = 50;

	// 레벨 L에서 L+1로 가는 데 필요한 경험치. L=1일 때 30으로, 기존 EXP_TABLE[0]과
	// 같은 값에서 시작한다(초반 체감을 기존과 맞추기 위함).
	[[nodiscard]] constexpr uint32_t ExpToNext(uint8_t level) noexcept
	{
		if (level >= kMaxLevel)
			return 0;  // 만렙 — 더 이상 오르지 않는다
		const uint32_t l = level;
		return 10u * l * l + 20u;
	}

	[[nodiscard]] constexpr uint16_t MaxHp(uint8_t level) noexcept
	{
		// 1레벨 100(기존 PLAYER_MAX_HP와 동일) + 레벨당 14
		return static_cast<uint16_t>(100 + (level > 0 ? level - 1 : 0) * 14);
	}

	[[nodiscard]] constexpr uint16_t BaseAttack(uint8_t level) noexcept
	{
		// 1레벨 10(기존 PLAYER_OFFENSIVE와 동일) + 레벨당 1
		return static_cast<uint16_t>(10 + (level > 0 ? level - 1 : 0));
	}

	// 5초마다 자연 회복되는 HP. 마을(안전지대)에서는 두 배.
	[[nodiscard]] constexpr uint16_t RegenPerTick(uint8_t level, bool inSafeZone) noexcept
	{
		const uint16_t base = static_cast<uint16_t>(10 + level / 2);
		return inSafeZone ? static_cast<uint16_t>(base * 2) : base;
	}

	// 몬스터에게 죽었을 때 잃는 경험치: 현재 레벨 필요 경험치의 5%.
	// 레벨 다운은 하지 않는다(현재 레벨의 누적 경험치 안에서만 깎는다).
	[[nodiscard]] constexpr uint32_t DeathExpPenalty(uint8_t level, uint32_t currentExp) noexcept
	{
		const uint32_t penalty = ExpToNext(level) / 20;
		return penalty < currentExp ? penalty : currentExp;
	}

	static_assert(ExpToNext(1) == 30);
	static_assert(MaxHp(1) == 100);
	static_assert(BaseAttack(1) == 10);
}

// ─────────────────────────────────────────────────────────────────────────────
// 전투 공식
// ─────────────────────────────────────────────────────────────────────────────
namespace CombatRule
{
	// 데미지 편차(±15%)와 치명타. 난수는 호출자(서버)가 넘긴다 — 이 헤더는 순수 함수만 둔다.
	inline constexpr uint32_t kCritChancePercent = 8;
	inline constexpr uint32_t kCritMultiplierPercent = 160;

	// roll01000: 0~1000 균등 난수, critRoll100: 0~99 균등 난수
	[[nodiscard]] constexpr uint32_t RollDamage(uint32_t attack, uint32_t roll01000, uint32_t critRoll100, bool& outCrit) noexcept
	{
		// 85% ~ 115%
		const uint32_t scaled = attack * (850 + (roll01000 % 301)) / 1000;
		outCrit = critRoll100 < kCritChancePercent;
		const uint32_t dmg = outCrit ? scaled * kCritMultiplierPercent / 100 : scaled;
		return dmg > 0 ? dmg : 1;
	}

	// 십자베기(USER_SKILL_REQ) 피해량 — 공격력에 비례. 1레벨 나무검(13) 기준 46으로
	// 기존 고정값 SKILL_DAMAGE(50)과 비슷한 체감에서 시작한다.
	[[nodiscard]] constexpr uint32_t SkillDamage(uint32_t attack) noexcept
	{
		return attack * 2 + 20;
	}

	// 공격/이동 쿨타임(ms). 서버 판정은 "now > last + cooldown"이다.
	inline constexpr uint32_t kMoveCooldownMs       = 500;
	inline constexpr uint32_t kHasteMoveCooldownMs  = 330;
	inline constexpr uint32_t kAttackCooldownMs     = 500;
	inline constexpr uint32_t kSkillCooldownMs      = 5000;
	inline constexpr uint32_t kPotionCooldownMs     = 5000;  // 기존 물약 쿨타임 그대로
	inline constexpr uint32_t kScrollCooldownMs     = 1000;  // 두루마리/바람걸음 물약
}

// ─────────────────────────────────────────────────────────────────────────────
// 무기 강화(인챈트)
// ─────────────────────────────────────────────────────────────────────────────
namespace EnchantRule
{
	inline constexpr uint8_t kSafeLevel = 6;   // +6까지는 항상 성공(안전 인챈트)
	inline constexpr uint8_t kMaxLevel  = 15;

	// 현재 강화 수치 → 다음 단계 성공 확률(%). 안전 구간은 100.
	[[nodiscard]] constexpr uint32_t SuccessPercent(uint8_t current) noexcept
	{
		if (current < kSafeLevel)  return 100;
		switch (current)
		{
		case 6:  return 40;
		case 7:  return 30;
		case 8:  return 22;
		case 9:  return 15;
		case 10: return 10;
		case 11: return 8;
		case 12: return 7;
		case 13: return 6;
		default: return 5;
		}
	}

	// 강화 수치가 공격력에 더하는 보너스: +6까지 1씩, 그 위로는 2씩.
	[[nodiscard]] constexpr uint16_t AttackBonus(uint8_t enchant) noexcept
	{
		return enchant <= kSafeLevel ? enchant : static_cast<uint16_t>(kSafeLevel + (enchant - kSafeLevel) * 2);
	}

	// 이 수치 이상 성공하면 월드 공지
	inline constexpr uint8_t kWorldNoticeLevel = 9;

	static_assert(AttackBonus(10) == 14);
}

// ─────────────────────────────────────────────────────────────────────────────
// 성향치(명예/악명)와 PK
// ─────────────────────────────────────────────────────────────────────────────
namespace AlignmentRule
{
	inline constexpr int16_t kMax           = 32767;
	inline constexpr int16_t kMin           = -32768;
	inline constexpr int16_t kLawfulLine    = 500;    // 이 이상이면 명예(파란 이름)
	inline constexpr int16_t kPkPenalty     = 1000;   // 무고한 플레이어를 죽이면 이만큼 떨어진다
	inline constexpr uint32_t kPurpleDurationMs = 60'000;  // 먼저 공격하면 적대(정당방위 대상)
	inline constexpr uint32_t kChaoticDropPercent = 40;    // 악명 사망 시 아이템 드롭 확률

	enum class NameColor : uint8_t { eNeutral, eLawful, eChaotic, ePurple };

	[[nodiscard]] constexpr NameColor ColorOf(int16_t alignment, bool purple) noexcept
	{
		if (purple)                   return NameColor::ePurple;
		if (alignment < 0)            return NameColor::eChaotic;
		if (alignment >= kLawfulLine) return NameColor::eLawful;
		return NameColor::eNeutral;
	}

	// 몬스터 처치 시 성향치 상승량(몬스터 레벨 기반, 최소 1)
	[[nodiscard]] constexpr int16_t GainForMonster(uint8_t monsterLevel) noexcept
	{
		return static_cast<int16_t>(monsterLevel / 2 > 0 ? monsterLevel / 2 : 1);
	}

	[[nodiscard]] constexpr int16_t Clamp(int32_t v) noexcept
	{
		return static_cast<int16_t>(v > kMax ? kMax : (v < kMin ? kMin : v));
	}
}
