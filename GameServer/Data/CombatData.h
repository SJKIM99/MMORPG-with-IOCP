#pragma once

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 전투 데이터 — 직업 · 능력치 규칙 · 스킬 · 상태이상. 서버·클라 공용.
//
// 서버는 이 표로 판정(사거리·쿨다운·MP·피해·상태이상·연계)하고, 클라이언트는 같은
// 표로 스킬바/툴팁/사거리 표시/예측 연출을 한다. 모양(이펙트·소리)은 클라 몫이다.
//
// 설계 원칙
//  · 직업마다 "플레이 방식"이 다르다: 기사=버티며 반격, 룬술사=거리 두고 제어·폭딜,
//    그림자 사냥꾼=파고들어 뒤를 잡고 연타.
//  · 연계는 짧고 직관적이다: "상태를 걸고 → 그 상태에 강한 기술로 터뜨린다".
//    연계가 성공하면 서버가 DAMAGE_COMBO + comboId를 실어 보내고 클라가 크게 연출한다.
// ─────────────────────────────────────────────────────────────────────────────

enum class ClassId : uint8_t
{
	Knight = 0,        // 황혼의 기사
	RuneCaster = 1,    // 룬술사
	ShadowHunter = 2,  // 그림자 사냥꾼
	Count
};

enum class StatusId : uint8_t
{
	None = 0,
	Stun,        // 기절: 이동·공격 불가
	Freeze,      // 빙결: 이동 불가, 공격 느려짐. 화염에 맞으면 증발 폭발
	Burn,        // 화상: 초당 피해
	Poison,      // 중독: 초당 피해(중첩), 월영 난무가 터뜨린다
	Slow,        // 둔화: 이동 느려짐
	Guard,       // 철벽 태세: 받는 피해 감소 + 반격
	Ambush,      // 기습: 다음 공격 확정 치명타
	Evade,       // 잔상: 잠깐 모든 공격을 피함
	Marked,      // 룬 표식(번개 폭발 연쇄 대상)
	Count
};

struct StatusRow
{
	StatusId    id;
	const char* name;
	bool        debuff;
	bool        blocksMove;
	bool        blocksAttack;
	uint8_t     maxStacks;
	uint16_t    tickMs;        // 0 = 지속 피해 없음
	uint16_t    tickPercent;   // 틱 피해 = 거는 사람 공격력 × % (중첩마다)
	uint16_t    slowPercent;   // 이동속도 감소 %
	uint32_t    color;         // 클라 표시
};

namespace StatusTable
{
	inline constexpr StatusRow kRows[] =
	{
		{ StatusId::None,   "",        false, false, false, 0,    0,   0,  0, 0xFFFFFF },
		{ StatusId::Stun,   "기절",    true,  true,  true,  1,    0,   0,  0, 0xFFE070 },
		{ StatusId::Freeze, "빙결",    true,  true,  false, 1,    0,   0,  0, 0x8AD8FF },
		{ StatusId::Burn,   "화상",    true,  false, false, 1, 1000,  18,  0, 0xFF7A2A },
		{ StatusId::Poison, "중독",    true,  false, false, 5, 1000,   8,  0, 0x7ADF4A },
		{ StatusId::Slow,   "둔화",    true,  false, false, 1,    0,   0, 40, 0x9A9AFF },
		{ StatusId::Guard,  "철벽 태세", false, false, false, 1,  0,   0, 30, 0xFFC860 },
		{ StatusId::Ambush, "기습",    false, false, false, 1,    0,   0,  0, 0xC08AFF },
		{ StatusId::Evade,  "잔상",    false, false, false, 1,    0,   0,  0, 0xB0A0FF },
		{ StatusId::Marked, "룬 표식", true,  false, false, 1,    0,   0,  0, 0x6AE8FF },
	};
	static_assert(sizeof(kRows) / sizeof(kRows[0]) == static_cast<size_t>(StatusId::Count));

	[[nodiscard]] constexpr const StatusRow& Get(StatusId id) noexcept
	{
		return kRows[static_cast<uint8_t>(id) < static_cast<uint8_t>(StatusId::Count) ? static_cast<uint8_t>(id) : 0];
	}
}

// ── 능력치 ────────────────────────────────────────────────────────────────────
enum class StatKind : uint8_t { Str, Dex, Int, Vit, Wis, Luk, Count };

struct BaseStats
{
	int16_t v[6] = {};   // STR DEX INT VIT WIS LUK
	[[nodiscard]] constexpr int16_t Get(StatKind k) const noexcept { return v[static_cast<int>(k)]; }
};

// 전투 능력치(서버가 계산해서 USER_STATS_INF로 보낸다)
struct DerivedStats
{
	uint16_t maxHp = 100, maxMp = 50;
	uint16_t attack = 10, magicAttack = 10;
	uint16_t defense = 0, magicDefense = 0;
	uint16_t attackSpeed = 100;     // % (높을수록 빠름)
	uint16_t moveSpeed = 100;       // %
	uint16_t critChance = 50;       // ‰ (50 = 5%)
	uint16_t critDamage = 150;      // %
	uint16_t accuracy = 100;
	uint16_t evasion = 0;
	uint16_t penetration = 0;
	uint16_t cooldownReduction = 0; // %
	uint16_t statusResist = 0;      // %
	uint16_t hpRegen = 5, mpRegen = 3;  // 5초마다
};

struct ClassRow
{
	ClassId     id;
	const char* name;
	const char* role;
	const char* desc;
	int16_t     base[6];        // 1레벨 STR DEX INT VIT WIS LUK
	uint8_t     growth10[6];    // 레벨당 성장 ×10 (15 = 1.5)
	uint16_t    baseHp, hpPerLevel;
	uint16_t    baseMp, mpPerLevel;
	uint8_t     atkStr10, atkDex10;   // 공격력 = 무기 + STR×atkStr/10 + DEX×atkDex/10
	uint16_t    basicIntervalMs;      // 기본 공격 간격(공격속도 100%)
	uint8_t     basicRange;           // 기본 공격 사거리(체비셰프)
	uint16_t    skills[6];            // [0] = 기본 공격
	uint16_t    starterWeapon;        // 초보자 무기 ItemTableId
	uint32_t    color;
};

namespace ClassTable
{
	inline constexpr ClassRow kRows[] =
	{
		{ ClassId::Knight, "황혼의 기사", "근접 · 수호 · 반격",
		  "해가 지는 순간을 지키는 기사단의 마지막 생존자. 방패로 적을 무너뜨리고, 버티다 되받아친다.",
		  { 12, 6, 3, 12, 4, 5 }, { 15, 5, 2, 15, 3, 3 }, 90, 16, 60, 4, 12, 3, 620, 1,
		  { 1, 2, 3, 4, 5, 6 }, 2, 0xE8B45A },
		{ ClassId::RuneCaster, "룬술사", "원거리 · 제어 · 폭딜",
		  "멈춘 숲의 속삭임을 룬으로 옮겨 적는 자. 얼리고, 태우고, 벼락으로 끝낸다.",
		  { 4, 6, 14, 6, 10, 5 }, { 2, 3, 18, 6, 12, 3 }, 64, 9, 110, 11, 5, 2, 700, 7,
		  { 7, 8, 9, 10, 11, 12 }, 19, 0x6AD8FF },
		{ ClassId::ShadowHunter, "그림자 사냥꾼", "근접 · 암살 · 기동",
		  "달이 없는 밤에만 사냥하는 추적자. 뒤를 잡고, 독을 쌓고, 한 번에 터뜨린다.",
		  { 8, 14, 3, 8, 4, 8 }, { 8, 16, 2, 8, 3, 6 }, 74, 11, 80, 6, 5, 11, 460, 1,
		  { 13, 14, 15, 16, 17, 18 }, 20, 0xB88AFF },
	};
	static_assert(sizeof(kRows) / sizeof(kRows[0]) == static_cast<size_t>(ClassId::Count));

	[[nodiscard]] constexpr const ClassRow& Get(ClassId id) noexcept
	{
		return kRows[static_cast<uint8_t>(id) < static_cast<uint8_t>(ClassId::Count) ? static_cast<uint8_t>(id) : 0];
	}
	[[nodiscard]] constexpr bool IsValid(uint8_t id) noexcept { return id < static_cast<uint8_t>(ClassId::Count); }

	// 레벨에 따른 기본 능력치(자유 포인트 제외)
	[[nodiscard]] constexpr BaseStats BaseAt(ClassId id, uint8_t level) noexcept
	{
		const ClassRow& c = Get(id);
		BaseStats s{};
		const int l = level > 0 ? level - 1 : 0;
		for (int i = 0; i < 6; ++i)
			s.v[i] = static_cast<int16_t>(c.base[i] + (c.growth10[i] * l) / 10);
		return s;
	}
}

// ── 능력치 → 전투 능력치 ──────────────────────────────────────────────────────
// 각 능력치가 플레이 방식에 닿도록:
//   STR 물리 공격·방어 / DEX 공격속도·명중·회피·치명(작게)·물리 공격(사냥꾼)
//   INT 마법 공격·마법 관통·MP / VIT 최대 HP·방어·상태이상 저항
//   WIS 최대 MP·MP 회복·마법 방어·쿨다운 감소 / LUK 치명타 확률·치명타 피해·회피(작게)
struct GearBonus
{
	int16_t  stats[6] = {};
	uint16_t weaponAttack = 0, weaponMagic = 0;
	uint16_t armor = 0, magicArmor = 0;
	int16_t  maxHp = 0, maxMp = 0;
	int16_t  attackSpeed = 0, moveSpeed = 0;
	int16_t  critChance = 0, critDamage = 0;   // ‰, %
	int16_t  penetration = 0, cooldownReduction = 0, statusResist = 0;
	int16_t  hpRegen = 0, mpRegen = 0;
	int16_t  attackPercent = 0;                // 공격력 %
};

namespace StatRule
{
	[[nodiscard]] constexpr uint16_t Clamp16(int v) noexcept { return static_cast<uint16_t>(v < 0 ? 0 : (v > 65535 ? 65535 : v)); }

	[[nodiscard]] constexpr DerivedStats Compute(ClassId cls, uint8_t level, const BaseStats& base, const GearBonus& gear) noexcept
	{
		const ClassRow& c = ClassTable::Get(cls);
		int s[6];
		for (int i = 0; i < 6; ++i) s[i] = base.v[i] + gear.stats[i];
		const int STR = s[0], DEX = s[1], INT = s[2], VIT = s[3], WIS = s[4], LUK = s[5];
		const int l = level > 0 ? level - 1 : 0;

		DerivedStats d{};
		d.maxHp = Clamp16(c.baseHp + c.hpPerLevel * l + VIT * 6 + gear.maxHp);
		d.maxMp = Clamp16(c.baseMp + c.mpPerLevel * l + WIS * 5 + INT * 2 + gear.maxMp);
		const int atk = gear.weaponAttack + (STR * c.atkStr10) / 10 + (DEX * c.atkDex10) / 10;
		d.attack = Clamp16(atk * (100 + gear.attackPercent) / 100);
		d.magicAttack = Clamp16((gear.weaponMagic + (INT * 15) / 10 + (WIS * 3) / 10) * (100 + gear.attackPercent) / 100);
		d.defense = Clamp16(gear.armor + VIT / 2 + STR / 5);
		d.magicDefense = Clamp16(gear.magicArmor + (WIS * 8) / 10 + INT / 5);
		d.attackSpeed = Clamp16(100 + (DEX * 6) / 10 + gear.attackSpeed);
		d.moveSpeed = Clamp16(100 + DEX / 8 + gear.moveSpeed);
		d.critChance = Clamp16(50 + LUK * 4 + DEX + gear.critChance);
		d.critDamage = Clamp16(150 + LUK + gear.critDamage);
		d.accuracy = Clamp16(80 + DEX * 2 + level * 2);
		d.evasion = Clamp16(DEX + (LUK * 3) / 10);
		d.penetration = Clamp16(gear.penetration + INT / 5);
		d.cooldownReduction = Clamp16((WIS * 3) / 10 + gear.cooldownReduction > 30 ? 30 : (WIS * 3) / 10 + gear.cooldownReduction);
		d.statusResist = Clamp16((VIT * 3 + WIS * 3) / 10 + gear.statusResist > 50 ? 50 : (VIT * 3 + WIS * 3) / 10 + gear.statusResist);
		d.hpRegen = Clamp16(4 + level / 2 + VIT / 4 + gear.hpRegen);
		d.mpRegen = Clamp16(3 + WIS / 2 + level / 4 + gear.mpRegen);
		return d;
	}

	// 받는 피해 배율(방어/마방) — 100/(100+유효 방어)
	[[nodiscard]] constexpr uint32_t Mitigate(uint32_t damage, int defense, int penetration) noexcept
	{
		int eff = defense - penetration;
		if (eff < 0) eff = 0;
		const uint32_t out = damage * 100u / static_cast<uint32_t>(100 + eff);
		return out > 0 ? out : 1;
	}

	// 회피 확률(%) — 몬스터 레벨이 높을수록 줄어든다. 최대 40%.
	[[nodiscard]] constexpr uint32_t EvadeChance(int evasion, int attackerLevel) noexcept
	{
		const int denom = evasion + 100 + attackerLevel * 10;
		const int p = denom > 0 ? evasion * 100 / denom : 0;
		return static_cast<uint32_t>(p > 40 ? 40 : (p < 0 ? 0 : p));
	}

	// 몬스터 방어(레벨 기반)
	[[nodiscard]] constexpr int MonsterDefense(int monsterLevel) noexcept { return monsterLevel * 3 / 2; }
}

// ── 스킬 ──────────────────────────────────────────────────────────────────────
enum class SkillTarget : uint8_t
{
	Self,       // 자기 주변
	Enemy,      // 대상 지정(몬스터/플레이어)
	Ground,     // 지점 지정(마우스 칸)
	Direction,  // 방향(마우스 쪽) — 돌진·회피
};

enum class SkillShape : uint8_t
{
	Single,     // 대상 하나
	Around,     // 시전자 주변 radius
	Circle,     // 지점 주변 radius
	Line,       // 시전자에서 방향으로 radius칸
	Chain,      // 대상 → 주변으로 연쇄
};

enum class DamageKind : uint8_t { Physical, Magic };

struct SkillRow
{
	uint16_t    id;
	ClassId     cls;
	uint8_t     slot;          // 0 = 기본 공격, 1~5
	const char* name;
	const char* desc;
	const char* combo;         // 연계 설명(툴팁)
	uint8_t     unlockLevel;
	uint16_t    mp;
	uint16_t    cooldownMs;    // 기본 공격은 ClassRow.basicIntervalMs를 공격속도로 나눈 값
	uint16_t    castMs;        // 시전(예고) 시간 — 서버가 이만큼 뒤에 판정한다(피할 수 있다)
	uint8_t     range;         // 체비셰프 칸
	SkillTarget target;
	SkillShape  shape;
	uint8_t     radius;
	DamageKind  kind;
	uint16_t    coeff;         // 공격력 × %
	uint8_t     hits;          // 타수
	StatusId    status;        // 적중 시 거는 상태
	uint16_t    statusMs;
	uint8_t     statusStacks;
	StatusId    comboStatus;   // 대상이 이 상태면 연계
	uint16_t    comboBonus;    // 연계 피해 +%
	uint8_t     comboId;       // 연계 이름(클라 표시) — 0 없음
	bool        consumeCombo;  // 연계 후 상태 제거
	uint8_t     knockback;     // 칸
	uint8_t     dash;          // 이동 칸(돌진/도약/회피)
	uint16_t    projectile;    // 투사체 속도(칸/초 ×10, 표시용) 0 = 즉발
	uint32_t    color;         // 표시 색
};

// 연계 이름(클라 표시용, comboId → 이름)
namespace ComboTable
{
	inline constexpr const char* kNames[] =
	{
		"",
		"무너뜨리기",      // 1 기절 대상에게 검격/회전 베기
		"황혼의 처형",     // 2 기절·빈사 대상 처형
		"증발 폭발",       // 3 빙결 + 화염
		"전도",            // 4 번개가 표식/화상 대상으로 연쇄
		"산산조각",        // 5 혜성이 빙결 대상을 부숨
		"기습",            // 6 그림자 돌진 뒤 공격
		"독 폭발",         // 7 월영 난무가 중독을 터뜨림
		"반격",            // 8 철벽 태세 반격
		"벽 충돌",         // 9 넉백이 벽에 부딪힘
		"급소",            // 10 후방 습격
	};
	inline constexpr uint8_t kCount = static_cast<uint8_t>(sizeof(kNames) / sizeof(kNames[0]));
	[[nodiscard]] constexpr const char* Name(uint8_t id) noexcept { return id < kCount ? kNames[id] : ""; }
}

namespace SkillTable
{
	using T = SkillTarget;
	using S = SkillShape;
	using K = DamageKind;
	using St = StatusId;

	inline constexpr SkillRow kRows[] =
	{
		{ 0, ClassId::Knight, 0, "", "", "", 1, 0, 0, 0, 0, T::Self, S::Single, 0, K::Physical, 0, 0, St::None, 0, 0, St::None, 0, 0, false, 0, 0, 0, 0 },

		// ── 황혼의 기사 ── 버티고, 무너뜨리고, 처형한다
		{ 1, ClassId::Knight, 0, "검격", "대검을 크게 휘두른다.", "기절한 적에게 +40% (무너뜨리기)",
		  1, 0, 0, 0, 1, T::Enemy, S::Single, 0, K::Physical, 100, 1, St::None, 0, 0, St::Stun, 40, 1, false, 0, 0, 0, 0xFFE8C0 },
		{ 2, ClassId::Knight, 1, "방패 강타", "방패로 내리쳐 적을 기절시키고 밀어낸다.", "기절 → 검격·회전 베기·처형으로 이어진다",
		  1, 10, 6000, 0, 1, T::Enemy, S::Single, 0, K::Physical, 80, 1, St::Stun, 1400, 1, St::None, 0, 0, false, 1, 0, 0, 0xFFD070 },
		{ 3, ClassId::Knight, 2, "회전 베기", "몸을 한 바퀴 돌려 주변의 모든 적을 벤다.", "기절한 적에게 +60% (무너뜨리기)",
		  2, 14, 5000, 0, 1, T::Self, S::Around, 1, K::Physical, 120, 1, St::None, 0, 0, St::Stun, 60, 1, false, 0, 0, 0, 0xFFC860 },
		{ 4, ClassId::Knight, 3, "돌진", "마우스 방향으로 최대 4칸 돌진해 처음 부딪힌 적을 밀쳐 낸다.", "벽으로 밀면 추가 피해·기절 (벽 충돌)",
		  4, 12, 9000, 0, 4, T::Direction, S::Line, 4, K::Physical, 110, 1, St::Stun, 700, 1, St::None, 0, 0, false, 2, 4, 0, 0xFFB050 },
		{ 5, ClassId::Knight, 4, "철벽 태세", "4초 동안 받는 피해가 절반이 되고, 맞을 때마다 반격한다.", "반격은 공격력의 70%",
		  6, 15, 14000, 0, 0, T::Self, S::Around, 0, K::Physical, 0, 0, St::Guard, 4000, 1, St::None, 0, 8, false, 0, 0, 0, 0xFFC860 },
		{ 6, ClassId::Knight, 5, "황혼의 처형", "잠깐 힘을 모았다가 내려찍는다.", "기절했거나 체력 30% 이하인 적에게 2배 (황혼의 처형)",
		  8, 25, 12000, 380, 1, T::Enemy, S::Single, 0, K::Physical, 240, 1, St::None, 0, 0, St::Stun, 100, 2, true, 1, 0, 0, 0xFF8A3A },

		// ── 룬술사 ── 얼리고, 태우고, 벼락으로 끝낸다
		{ 7, ClassId::RuneCaster, 0, "룬 탄환", "룬을 새긴 마력 탄환을 쏜다.", "빙결된 적에게 +25%",
		  1, 0, 0, 0, 7, T::Enemy, S::Single, 0, K::Magic, 90, 1, St::None, 0, 0, St::Freeze, 25, 0, false, 0, 0, 160, 0x7AD8FF },
		{ 8, ClassId::RuneCaster, 1, "빙결 인장", "지점에 얼음 인장을 새겨 주변 적을 얼린다.", "빙결 → 화염 고리(증발 폭발)·혜성(산산조각)",
		  1, 18, 8000, 0, 7, T::Ground, S::Circle, 1, K::Magic, 70, 1, St::Freeze, 2600, 1, St::None, 0, 0, false, 0, 0, 0, 0x9AE8FF },
		{ 9, ClassId::RuneCaster, 2, "화염 고리", "자신을 중심으로 불꽃의 고리를 터뜨린다.", "빙결된 적은 증발 폭발(+80%, 주변 폭발)",
		  2, 20, 7000, 0, 0, T::Self, S::Around, 2, K::Magic, 130, 1, St::Burn, 3000, 1, St::Freeze, 80, 3, true, 1, 0, 0, 0xFF8A3A },
		{ 10, ClassId::RuneCaster, 3, "공간 도약", "마우스 지점으로 순간이동하고, 떠난 자리에 룬 폭발을 남긴다.", "위험할 때 거리를 벌린다",
		  4, 12, 10000, 0, 4, T::Ground, S::Circle, 1, K::Magic, 60, 1, St::Slow, 2000, 1, St::None, 0, 0, false, 0, 4, 0, 0xB08AFF },
		{ 11, ClassId::RuneCaster, 4, "번개 폭발", "대상에게 벼락을 떨어뜨리고 주변 적으로 튄다.", "화상·표식·빙결 대상으로 더 멀리 튄다(전도 +40%)",
		  6, 22, 9000, 0, 7, T::Enemy, S::Chain, 3, K::Magic, 120, 1, St::Marked, 3000, 1, St::Burn, 40, 4, false, 0, 0, 0, 0xE8F0FF },
		{ 12, ClassId::RuneCaster, 5, "혜성 낙하", "지점에 혜성을 불러 내린다(0.8초 뒤 떨어짐).", "빙결된 적은 산산조각 ×1.5",
		  8, 40, 18000, 800, 7, T::Ground, S::Circle, 2, K::Magic, 300, 1, St::Stun, 1000, 1, St::Freeze, 50, 5, true, 1, 0, 0, 0xFF6A3A },

		// ── 그림자 사냥꾼 ── 뒤를 잡고, 독을 쌓고, 터뜨린다
		{ 13, ClassId::ShadowHunter, 0, "연속 찌르기", "단검으로 두 번 빠르게 찌른다.", "중독된 적에게 +20%",
		  1, 0, 0, 0, 1, T::Enemy, S::Single, 0, K::Physical, 58, 2, St::None, 0, 0, St::Poison, 20, 0, false, 0, 0, 0, 0xD8C8FF },
		{ 14, ClassId::ShadowHunter, 1, "그림자 돌진", "그림자를 타고 대상의 등 뒤로 이동한다. 다음 공격은 확정 치명타.", "돌진 → 후방 습격(급소)·연속 찌르기(기습)",
		  1, 12, 7000, 0, 5, T::Enemy, S::Single, 0, K::Physical, 40, 1, St::Ambush, 2500, 1, St::None, 0, 0, false, 0, 0, 0, 0xB88AFF },
		{ 15, ClassId::ShadowHunter, 2, "독 칼날", "독을 바른 칼날로 벤다. 중독이 최대 5번 쌓인다.", "중독 → 월영 난무(독 폭발)",
		  2, 8, 3500, 0, 1, T::Enemy, S::Single, 0, K::Physical, 110, 1, St::Poison, 6000, 1, St::None, 0, 0, false, 0, 0, 0, 0x8AE85A },
		{ 16, ClassId::ShadowHunter, 3, "잔상 회피", "마우스 반대 방향으로 3칸 물러나며 잠깐 모든 공격을 피한다.", "위험한 예고를 보고 쓰면 된다",
		  4, 10, 9000, 0, 3, T::Direction, S::Line, 3, K::Physical, 0, 0, St::Evade, 900, 1, St::None, 0, 0, false, 0, 3, 0, 0xB0A0FF },
		{ 17, ClassId::ShadowHunter, 4, "후방 습격", "등 뒤에서 급소를 찌른다.", "대상 뒤(또는 기습 중)면 확정 치명타 ×1.5 (급소)",
		  6, 15, 6000, 0, 1, T::Enemy, S::Single, 0, K::Physical, 170, 1, St::None, 0, 0, St::Ambush, 50, 10, true, 0, 0, 0, 0xE06AFF },
		{ 18, ClassId::ShadowHunter, 5, "월영 난무", "달빛 아래 주변 적을 다섯 번 벤다.", "중독 중첩마다 +30%, 독을 터뜨린다 (독 폭발)",
		  8, 35, 16000, 0, 0, T::Self, S::Around, 1, K::Physical, 48, 5, St::None, 0, 0, St::Poison, 30, 7, true, 0, 0, 0, 0xC8B8FF },
	};
	inline constexpr uint16_t kCount = static_cast<uint16_t>(sizeof(kRows) / sizeof(kRows[0]));

	[[nodiscard]] constexpr const SkillRow* Find(uint16_t id) noexcept
	{
		return (id > 0 && id < kCount && kRows[id].id == id) ? &kRows[id] : nullptr;
	}

	// 기본 공격 간격(ms) — 공격속도 %
	[[nodiscard]] constexpr uint32_t BasicIntervalMs(ClassId cls, uint16_t attackSpeed) noexcept
	{
		const uint32_t base = ClassTable::Get(cls).basicIntervalMs;
		const uint32_t spd = attackSpeed < 40 ? 40 : attackSpeed;
		return base * 100u / spd;
	}

	// 실제 쿨다운(ms) — 쿨다운 감소 %
	[[nodiscard]] constexpr uint32_t CooldownMs(const SkillRow& row, uint16_t cdr) noexcept
	{
		const uint32_t r = cdr > 50 ? 50 : cdr;
		return row.cooldownMs * (100u - r) / 100u;
	}

	namespace Detail
	{
		constexpr bool Validate() noexcept
		{
			for (uint16_t i = 0; i < kCount; ++i)
				if (kRows[i].id != i) return false;
			for (const auto& c : ClassTable::kRows)
				for (int s = 0; s < 6; ++s)
				{
					const SkillRow* r = Find(c.skills[s]);
					if (r == nullptr || r->cls != c.id || r->slot != s) return false;
				}
			return true;
		}
	}
	static_assert(Detail::Validate(), "SkillTable/ClassTable: 스킬 id·슬롯·직업이 서로 맞지 않습니다.");
}
