#pragma once

#include <cstdint>

#include "Data/CombatData.h"
#include "Data/LevelTable.h"
#include "Item/ItemTableData.h"

// ─────────────────────────────────────────────────────────────────────────────
// 아이템 성장 규칙 — 희귀도 · 옵션 · 특수 효과 · 세트. 서버·클라 공용.
//
// 한 장비 "인스턴스"는 표의 행(ItemTableRow: 기본 성능)과 64비트 인스턴스 값
// (ItemInstance: 희귀도 · 옵션 4줄 · 특수 효과)으로 이루어진다. 인스턴스 값은
// DB Inventory.extra(BIGINT)와 ITEM_DETAIL_INF에 그대로 실린다.
//
// 희귀도: 일반 → 고급 → 희귀 → 영웅 → 전설 → 신화
//   · 표의 grade(예전 등급)는 "적어도 이 희귀도"라는 바닥이다(FloorRarity).
//   · 바닥보다 높게 나오면 기본 성능이 단계마다 +8% 오르고, 옵션 줄 수는 희귀도가 정한다.
//   · 전설 이상은 특수 효과(적중 시 벼락 등)가 붙는다. 신화의 특수 효과는 더 강하다.
// 보상 루프(굴림은 서버만): 몬스터 단계가 높을수록 높은 희귀도가 잘 나오고,
// "희귀 이상"을 오래 못 보면 반드시 한 번은 준다(천장, User::m_pity*).
// 옵션 값은 단계(tier)·희귀도로 범위가 정해진 안에서만 굴린다(제한된 무작위).
// ─────────────────────────────────────────────────────────────────────────────

enum class Rarity : uint8_t
{
	Common,      // 일반
	Uncommon,    // 고급
	Rare,        // 희귀
	Heroic,      // 영웅
	Legendary,   // 전설
	Mythic,      // 신화
	Count
};

// 옵션 종류. DB에 번호로 저장되므로 이미 배정된 값은 바꾸지 않는다(끝에만 추가, 최대 31).
enum class ItemOption : uint8_t
{
	None = 0,
	Str, Dex, Int, Vit, Wis, Luk,          // 1~6  능력치 +N
	Attack, Magic, Armor, MagicArmor,      // 7~10 +N
	MaxHp, MaxMp,                          // 11~12 +N×5 / +N×3
	CritChance,                            // 13 +N‰
	CritDamage, AttackSpeed, MoveSpeed,    // 14~16 +N%
	CooldownReduction,                     // 17 +N%
	Penetration, HpRegen, MpRegen,         // 18~20 +N
	AttackPercent, StatusResist,           // 21~22 +N%
	SkillBoost,                            // 23 값 = 기술 id(하위 5비트) | 단계(상위 3비트) → 그 기술 피해 +(15 + 5×단계)%
	AdenaFind, ItemFind,                   // 24~25 +N%
	Count
};
static_assert(static_cast<int>(ItemOption::Count) <= 32, "옵션 종류는 5비트(최대 31)에 들어가야 한다");

// 특수 효과(전설 이상). DB에 번호로 저장되므로 값은 바꾸지 않는다(최대 15).
enum class ItemProc : uint8_t
{
	None = 0,
	Thunder,      // 천둥의 일격: 적중 시 확률로 벼락(마법 피해)
	Lifesteal,    // 피의 갈증: 준 피해 일부를 HP로
	FrostTouch,   // 서리 손길: 적중 시 확률로 둔화(강화: 빙결)
	Execution,    // 처형자: HP 30% 이하 대상에게 피해 증가
	Echo,         // 메아리: 기술 적중 시 확률로 한 번 더
	Guardian,     // 수호의 맹세: 맞을 때 확률로 철벽 태세
	Ember,        // 잔불: 적중 시 확률로 화상
	Venom,        // 맹독: 적중 시 확률로 중독 1중첩
	Count
};
static_assert(static_cast<int>(ItemProc::Count) <= 16, "특수 효과는 4비트에 들어가야 한다");

struct OptionRoll
{
	ItemOption kind  = ItemOption::None;
	uint8_t    value = 0;
};

// ── 인스턴스(64비트) ─────────────────────────────────────────────────────────
//  bit 0-2  희귀도 | 3-6 특수 효과 | 7 강화된 특수 효과(신화) | 8-9 예약
//  bit 10-61 옵션 4줄 × 13비트(종류 5 + 값 8) | 62-63 예약(BIGINT가 음수가 되지 않게 0)
// 0 = "굴린 적 없음"(예전 아이템/소모품) — 희귀도는 표의 바닥을 쓰고 옵션은 없다.
struct ItemInstance
{
	Rarity     rarity    = Rarity::Common;
	ItemProc   proc      = ItemProc::None;
	bool       empowered = false;
	OptionRoll opts[4]{};

	[[nodiscard]] constexpr uint64_t Pack() const noexcept
	{
		uint64_t v = static_cast<uint64_t>(rarity) & 0x7u;
		v |= (static_cast<uint64_t>(proc) & 0xFu) << 3;
		v |= (empowered ? 1ull : 0ull) << 7;
		for (int i = 0; i < 4; ++i)
		{
			const uint64_t o = (static_cast<uint64_t>(opts[i].kind) & 0x1Fu) | (static_cast<uint64_t>(opts[i].value) << 5);
			v |= (o & 0x1FFFu) << (10 + i * 13);
		}
		return v;
	}

	[[nodiscard]] static constexpr ItemInstance Unpack(uint64_t v) noexcept
	{
		ItemInstance inst;
		const uint8_t r = static_cast<uint8_t>(v & 0x7u);
		inst.rarity = r < static_cast<uint8_t>(Rarity::Count) ? static_cast<Rarity>(r) : Rarity::Common;
		const uint8_t p = static_cast<uint8_t>((v >> 3) & 0xFu);
		inst.proc = p < static_cast<uint8_t>(ItemProc::Count) ? static_cast<ItemProc>(p) : ItemProc::None;
		inst.empowered = ((v >> 7) & 1u) != 0;
		for (int i = 0; i < 4; ++i)
		{
			const uint64_t o = (v >> (10 + i * 13)) & 0x1FFFu;
			const uint8_t k = static_cast<uint8_t>(o & 0x1Fu);
			if (k == 0 || k >= static_cast<uint8_t>(ItemOption::Count))
				continue;
			inst.opts[i].kind = static_cast<ItemOption>(k);
			inst.opts[i].value = static_cast<uint8_t>(o >> 5);
		}
		return inst;
	}

	[[nodiscard]] constexpr int OptionCount() const noexcept
	{
		int n = 0;
		for (const auto& o : opts) if (o.kind != ItemOption::None) ++n;
		return n;
	}
};

// 착용 효과 중 GearBonus(능력치 계산)에 들어가지 않는 것들 — 특수 효과·기술 강화·획득 보너스.
struct GearExtras
{
	uint8_t  skillBoost[24] = {};        // 기술 id별 피해 +%
	uint16_t adenaFind = 0;              // 아데나 획득 +%
	uint16_t itemFind  = 0;              // 희귀품 발견 +%
	uint8_t  procPower[static_cast<size_t>(ItemProc::Count)] = {};   // 0 없음 · 1 보통 · 2 강화(신화)
	uint8_t  setCount[4] = {};           // 세트별 착용 부위 수
};

// 착용 중인 장비 하나(능력치 계산 입력)
struct EquippedPiece
{
	const ItemTableRow* row = nullptr;
	uint64_t extra   = 0;
	uint8_t  enchant = 0;
};

namespace ItemRules
{
	// ── 희귀도 ────────────────────────────────────────────────────────────────
	inline constexpr const char* kRarityName[] = { "일반", "고급", "희귀", "영웅", "전설", "신화" };
	inline constexpr uint32_t kRarityColor[]   = { 0xDCD6C8, 0x7BDC6A, 0x4FA8FF, 0xB87AFF, 0xFFA43A, 0xFF4F62 };
	inline constexpr uint8_t  kOptionCount[]   = { 0, 1, 2, 3, 3, 4 };
	// 상점 매입가 배율(%) — 쓸 수 없는 좋은 물건도 팔면 보상이 된다
	inline constexpr uint16_t kSellPercent[]   = { 100, 130, 170, 260, 420, 700 };

	[[nodiscard]] constexpr int Idx(Rarity r) noexcept
	{
		const int i = static_cast<int>(r);
		return i < 0 ? 0 : (i >= static_cast<int>(Rarity::Count) ? static_cast<int>(Rarity::Count) - 1 : i);
	}
	[[nodiscard]] constexpr const char* RarityName(Rarity r) noexcept { return kRarityName[Idx(r)]; }
	[[nodiscard]] constexpr uint32_t RarityColor(Rarity r) noexcept { return kRarityColor[Idx(r)]; }

	// 표의 예전 등급 → 이 물건이 나오는 가장 낮은 희귀도
	[[nodiscard]] constexpr Rarity FloorRarity(const ItemTableRow& row) noexcept
	{
		if (row.type != ItemType::eEquipment) return Rarity::Common;
		switch (row.grade)
		{
		case ItemGrade::eRare:      return Rarity::Uncommon;
		case ItemGrade::eEpic:      return Rarity::Rare;
		case ItemGrade::eUnique:    return Rarity::Heroic;
		case ItemGrade::eLegendary: return Rarity::Legendary;
		default:                    return Rarity::Common;
		}
	}

	[[nodiscard]] constexpr Rarity EffectiveRarity(const ItemTableRow& row, const ItemInstance& inst) noexcept
	{
		const Rarity f = FloorRarity(row);
		return static_cast<int>(inst.rarity) > static_cast<int>(f) ? inst.rarity : f;
	}
	[[nodiscard]] constexpr Rarity EffectiveRarity(const ItemTableRow& row, uint64_t extra) noexcept
	{
		return EffectiveRarity(row, ItemInstance::Unpack(extra));
	}

	// 기본 성능 배율(%): 바닥보다 한 단계 높을 때마다 +8%
	[[nodiscard]] constexpr int BasePercent(const ItemTableRow& row, Rarity r) noexcept
	{
		const int above = static_cast<int>(r) - static_cast<int>(FloorRarity(row));
		return 100 + 8 * (above > 0 ? above : 0);
	}

	[[nodiscard]] constexpr uint32_t SellPrice(const ItemTableRow& row, uint64_t extra, uint8_t enchant) noexcept
	{
		if (row.type != ItemType::eEquipment)
			return row.sellPrice;
		const uint32_t base = row.sellPrice * (10u + enchant) / 10u;
		return base * kSellPercent[Idx(EffectiveRarity(row, extra))] / 100u;
	}

	// ── 옵션 ──────────────────────────────────────────────────────────────────
	struct OptionRow
	{
		const char* name;
		uint8_t     lo, hi;     // 1단계·희귀 기준 굴림 범위
		uint8_t     cap;        // 옵션 한 줄의 최대 값(0 = 제한 없음)
		uint8_t     unit;       // 적용 배수(최대 HP ×5 등)
		uint8_t     display;    // 0: +N · 1: +N% · 2: +N‰(소수 한 자리 %로 표시)
	};
	inline constexpr OptionRow kOptions[] =
	{
		{ "",               0,  0,  0, 1, 0 },   // None
		{ "힘",             1,  3,  0, 1, 0 },
		{ "민첩",           1,  3,  0, 1, 0 },
		{ "지능",           1,  3,  0, 1, 0 },
		{ "체력",           1,  3,  0, 1, 0 },
		{ "지혜",           1,  3,  0, 1, 0 },
		{ "행운",           1,  3,  0, 1, 0 },
		{ "공격력",         1,  4,  0, 1, 0 },
		{ "마법 공격력",    1,  4,  0, 1, 0 },
		{ "방어력",         1,  4,  0, 1, 0 },
		{ "마법 방어력",    1,  4,  0, 1, 0 },
		{ "최대 HP",        2,  5,  0, 5, 0 },
		{ "최대 MP",        2,  5,  0, 3, 0 },
		{ "치명타 확률",    5, 12, 60, 1, 2 },
		{ "치명타 피해",    4, 10, 60, 1, 1 },
		{ "공격 속도",      2,  5, 14, 1, 1 },
		{ "이동 속도",      2,  4, 10, 1, 1 },
		{ "재사용 대기시간 감소", 1, 3, 8, 1, 1 },
		{ "관통",           1,  4,  0, 1, 0 },
		{ "HP 회복",        1,  3,  0, 1, 0 },
		{ "MP 회복",        1,  2,  0, 1, 0 },
		{ "모든 공격력",    2,  5, 20, 1, 1 },   // AttackPercent(물리·마법 모두 %)
		{ "상태이상 저항",  3,  8, 25, 1, 1 },
		{ "기술 강화",      0,  0,  0, 1, 0 },   // SkillBoost(값이 기술·단계를 담는다)
		{ "아데나 획득",    5, 15, 60, 1, 1 },
		{ "희귀품 발견",    3, 10, 40, 1, 1 },
	};
	static_assert(sizeof(kOptions) / sizeof(kOptions[0]) == static_cast<size_t>(ItemOption::Count));

	[[nodiscard]] constexpr const OptionRow& Option(ItemOption k) noexcept
	{
		const auto i = static_cast<size_t>(k);
		return kOptions[i < static_cast<size_t>(ItemOption::Count) ? i : 0];
	}

	[[nodiscard]] constexpr uint32_t Bit(ItemOption o) noexcept { return 1u << static_cast<uint32_t>(o); }

	// 기술 강화 값 = 기술 id | 단계 << 5
	[[nodiscard]] constexpr uint8_t SkillBoostValue(uint16_t skillId, int step) noexcept
	{
		return static_cast<uint8_t>((skillId & 0x1Fu) | ((static_cast<unsigned>(step) & 0x7u) << 5));
	}
	[[nodiscard]] constexpr uint16_t SkillBoostSkill(uint8_t value) noexcept { return static_cast<uint16_t>(value & 0x1Fu); }
	[[nodiscard]] constexpr int SkillBoostPercent(uint8_t value) noexcept { return 15 + 5 * ((value >> 5) & 0x7); }

	// 부위별로 붙을 수 있는 옵션(서로 겹치지 않게 뽑는다)
	[[nodiscard]] constexpr uint32_t OptionPool(const ItemTableRow& row) noexcept
	{
		using O = ItemOption;
		switch (ItemTableData::SlotOf(row))
		{
		case EquipSlot::Weapon:
			if (row.weaponKind == WeaponKind::Staff)
				return Bit(O::Int) | Bit(O::Wis) | Bit(O::Magic) | Bit(O::CritChance) | Bit(O::CritDamage)
					| Bit(O::CooldownReduction) | Bit(O::Penetration) | Bit(O::AttackPercent) | Bit(O::MaxMp);
			return Bit(O::Str) | Bit(O::Dex) | Bit(O::Luk) | Bit(O::Attack) | Bit(O::CritChance) | Bit(O::CritDamage)
				| Bit(O::AttackSpeed) | Bit(O::Penetration) | Bit(O::AttackPercent);
		case EquipSlot::Helmet:
			return Bit(O::Int) | Bit(O::Wis) | Bit(O::Vit) | Bit(O::MaxHp) | Bit(O::MaxMp) | Bit(O::MagicArmor)
				| Bit(O::CooldownReduction) | Bit(O::StatusResist);
		case EquipSlot::Armor:
			return Bit(O::Str) | Bit(O::Vit) | Bit(O::Int) | Bit(O::MaxHp) | Bit(O::Armor) | Bit(O::MagicArmor)
				| Bit(O::HpRegen) | Bit(O::StatusResist);
		case EquipSlot::Gloves:
			return Bit(O::Str) | Bit(O::Dex) | Bit(O::Int) | Bit(O::AttackSpeed) | Bit(O::CritChance) | Bit(O::Attack) | Bit(O::Magic);
		case EquipSlot::Boots:
			return Bit(O::Dex) | Bit(O::Luk) | Bit(O::MoveSpeed) | Bit(O::Armor) | Bit(O::MaxHp) | Bit(O::StatusResist) | Bit(O::HpRegen);
		case EquipSlot::Cloak:
			return Bit(O::Luk) | Bit(O::Wis) | Bit(O::MagicArmor) | Bit(O::Armor) | Bit(O::CooldownReduction) | Bit(O::ItemFind) | Bit(O::MpRegen);
		case EquipSlot::Necklace:
			return Bit(O::Str) | Bit(O::Int) | Bit(O::Wis) | Bit(O::MaxHp) | Bit(O::MaxMp) | Bit(O::MpRegen)
				| Bit(O::AttackPercent) | Bit(O::CritDamage);
		case EquipSlot::Ring:
			return Bit(O::Str) | Bit(O::Dex) | Bit(O::Int) | Bit(O::Luk) | Bit(O::CritChance) | Bit(O::CritDamage)
				| Bit(O::Penetration) | Bit(O::AdenaFind) | Bit(O::ItemFind);
		default:
			return 0;
		}
	}

	// 단계·희귀도에 따른 굴림 범위. 단계마다 +45%, 영웅부터 희귀도마다 +10%.
	struct Range { uint8_t lo, hi; };
	[[nodiscard]] constexpr Range OptionRange(ItemOption k, int tier, Rarity r) noexcept
	{
		const OptionRow& o = Option(k);
		const int t = tier < 1 ? 1 : (tier > 5 ? 5 : tier);
		const int above = static_cast<int>(r) - static_cast<int>(Rarity::Rare);
		const int pct = (100 + 45 * (t - 1)) * (100 + 10 * (above > 0 ? above : 0)) / 100;
		int lo = o.lo * pct / 100, hi = o.hi * pct / 100;
		if (lo < 1) lo = 1;
		if (hi < lo) hi = lo;
		if (o.cap != 0) { if (hi > o.cap) hi = o.cap; if (lo > hi) lo = hi; }
		if (hi > 255) hi = 255;
		if (lo > 255) lo = 255;
		return Range{ static_cast<uint8_t>(lo), static_cast<uint8_t>(hi) };
	}

	// 직업이 좋아하는 옵션은 잘 붙는다(제한된 무작위 — 쓸모없는 옵션만 잔뜩 붙는 일을 줄인다)
	[[nodiscard]] constexpr int OptionWeight(ItemOption k, ClassId cls) noexcept
	{
		using O = ItemOption;
		switch (cls)
		{
		case ClassId::Knight:
			if (k == O::Str || k == O::Vit) return 30;
			if (k == O::Magic || k == O::Int) return 3;
			break;
		case ClassId::RuneCaster:
			if (k == O::Int || k == O::Wis) return 30;
			if (k == O::Attack || k == O::Str) return 3;
			break;
		case ClassId::ShadowHunter:
			if (k == O::Dex || k == O::Luk) return 30;
			if (k == O::Magic || k == O::Int) return 3;
			break;
		default:
			break;
		}
		return 10;
	}

	constexpr void AddI16(int16_t& dst, int v) noexcept { dst = static_cast<int16_t>(dst + v); }
	constexpr void AddU16(uint16_t& dst, int v) noexcept { const int s = dst + v; dst = static_cast<uint16_t>(s < 0 ? 0 : (s > 65535 ? 65535 : s)); }

	constexpr void ApplyOption(GearBonus& g, GearExtras& ex, const OptionRoll& o) noexcept
	{
		const int v = o.value * Option(o.kind).unit;
		switch (o.kind)
		{
		case ItemOption::Str:               AddI16(g.stats[0], v); break;
		case ItemOption::Dex:               AddI16(g.stats[1], v); break;
		case ItemOption::Int:               AddI16(g.stats[2], v); break;
		case ItemOption::Vit:               AddI16(g.stats[3], v); break;
		case ItemOption::Wis:               AddI16(g.stats[4], v); break;
		case ItemOption::Luk:               AddI16(g.stats[5], v); break;
		case ItemOption::Attack:            AddU16(g.weaponAttack, v); break;
		case ItemOption::Magic:             AddU16(g.weaponMagic, v); break;
		case ItemOption::Armor:             AddU16(g.armor, v); break;
		case ItemOption::MagicArmor:        AddU16(g.magicArmor, v); break;
		case ItemOption::MaxHp:             AddI16(g.maxHp, v); break;
		case ItemOption::MaxMp:             AddI16(g.maxMp, v); break;
		case ItemOption::CritChance:        AddI16(g.critChance, v); break;
		case ItemOption::CritDamage:        AddI16(g.critDamage, v); break;
		case ItemOption::AttackSpeed:       AddI16(g.attackSpeed, v); break;
		case ItemOption::MoveSpeed:         AddI16(g.moveSpeed, v); break;
		case ItemOption::CooldownReduction: AddI16(g.cooldownReduction, v); break;
		case ItemOption::Penetration:       AddI16(g.penetration, v); break;
		case ItemOption::HpRegen:           AddI16(g.hpRegen, v); break;
		case ItemOption::MpRegen:           AddI16(g.mpRegen, v); break;
		case ItemOption::AttackPercent:     AddI16(g.attackPercent, v); break;
		case ItemOption::StatusResist:      AddI16(g.statusResist, v); break;
		case ItemOption::SkillBoost:
		{
			const uint16_t sk = SkillBoostSkill(o.value);
			if (sk < sizeof(ex.skillBoost))
			{
				const int sum = ex.skillBoost[sk] + SkillBoostPercent(o.value);
				ex.skillBoost[sk] = static_cast<uint8_t>(sum > 200 ? 200 : sum);
			}
			break;
		}
		case ItemOption::AdenaFind:         ex.adenaFind = static_cast<uint16_t>(ex.adenaFind + v); break;
		case ItemOption::ItemFind:          ex.itemFind = static_cast<uint16_t>(ex.itemFind + v); break;
		default: break;
		}
	}

	// ── 특수 효과 ─────────────────────────────────────────────────────────────
	struct ProcRow
	{
		const char* name;
		uint8_t     chance, chanceEmp;   // 발동 확률(%) — 100 = 항상
		uint8_t     power, powerEmp;     // 효과 크기(%)
		uint32_t    color;
	};
	inline constexpr ProcRow kProcs[] =
	{
		{ "",            0,   0,   0,  0, 0xFFFFFF },
		{ "천둥의 일격", 12,  18,  60, 90, 0x9AD8FF },   // 벼락: 공격력의 power% 마법 피해
		{ "피의 갈증",  100, 100,   5,  8, 0xFF5A6A },   // 준 피해의 power% 흡수
		{ "서리 손길",   12,  18,   0,  0, 0x8AD8FF },   // 둔화 2.5초(강화: 빙결 1.2초)
		{ "처형자",     100, 100,  30, 50, 0xFF8A3A },   // HP 30% 이하 대상 피해 +power%
		{ "메아리",      12,  18,  40, 60, 0xC8A8FF },   // 기술 적중 시 power% 피해로 한 번 더
		{ "수호의 맹세", 10,  15,   0,  0, 0xFFD870 },   // 맞을 때 철벽 태세 3초(12초마다)
		{ "잔불",        15,  22,   0,  0, 0xFF7A2A },   // 화상
		{ "맹독",        20,  30,   0,  0, 0x7ADF4A },   // 중독 1중첩
	};
	static_assert(sizeof(kProcs) / sizeof(kProcs[0]) == static_cast<size_t>(ItemProc::Count));

	[[nodiscard]] constexpr const ProcRow& Proc(ItemProc p) noexcept
	{
		const auto i = static_cast<size_t>(p);
		return kProcs[i < static_cast<size_t>(ItemProc::Count) ? i : 0];
	}
	[[nodiscard]] constexpr int ProcChance(ItemProc p, int power) noexcept { return power >= 2 ? Proc(p).chanceEmp : Proc(p).chance; }
	[[nodiscard]] constexpr int ProcPower(ItemProc p, int power) noexcept { return power >= 2 ? Proc(p).powerEmp : Proc(p).power; }

	inline constexpr uint32_t kGuardianCooldownMs = 12000;

	// 부위별로 붙을 수 있는 특수 효과
	[[nodiscard]] constexpr uint32_t ProcPool(const ItemTableRow& row) noexcept
	{
		auto b = [](ItemProc p) { return 1u << static_cast<uint32_t>(p); };
		switch (ItemTableData::SlotOf(row))
		{
		case EquipSlot::Weapon:
			return b(ItemProc::Thunder) | b(ItemProc::Lifesteal) | b(ItemProc::FrostTouch) | b(ItemProc::Execution)
				| b(ItemProc::Echo) | b(ItemProc::Ember) | b(ItemProc::Venom);
		case EquipSlot::Helmet: case EquipSlot::Armor: case EquipSlot::Cloak: case EquipSlot::Boots:
			return b(ItemProc::Guardian) | b(ItemProc::Lifesteal);
		case EquipSlot::Gloves:
			return b(ItemProc::Lifesteal) | b(ItemProc::Echo) | b(ItemProc::Execution);
		case EquipSlot::Necklace: case EquipSlot::Ring:
			return b(ItemProc::Thunder) | b(ItemProc::Lifesteal) | b(ItemProc::Echo) | b(ItemProc::Execution);
		default:
			return 0;
		}
	}

	// ── 세트 ──────────────────────────────────────────────────────────────────
	struct SetRow
	{
		uint8_t     id;
		const char* name;
		ItemTableId pieces[4];
		OptionRoll  two[2];      // 2부위 효과
		OptionRoll  four[2];     // 4부위 효과
		ItemProc    fourProc;    // 4부위 특수 효과
		uint32_t    color;
	};
	inline constexpr SetRow kSets[] =
	{
		{ 1, "여명의 파수꾼", { 47, 48, 49, 50 },
		  { { ItemOption::Vit, 4 }, { ItemOption::MaxHp, 12 } },
		  { { ItemOption::Armor, 15 }, { ItemOption::StatusResist, 10 } }, ItemProc::Guardian, 0xFFD890 },
		{ 2, "별빛 룬", { 51, 52, 53, 54 },
		  { { ItemOption::Int, 5 }, { ItemOption::MaxMp, 15 } },
		  { { ItemOption::CooldownReduction, 8 }, { ItemOption::Magic, 20 } }, ItemProc::Thunder, 0x8AB8FF },
		{ 3, "달그림자", { 55, 56, 57, 58 },
		  { { ItemOption::Dex, 5 }, { ItemOption::CritChance, 30 } },
		  { { ItemOption::CritDamage, 25 }, { ItemOption::MoveSpeed, 6 } }, ItemProc::Venom, 0xB88AFF },
	};
	inline constexpr int kSetCount = static_cast<int>(sizeof(kSets) / sizeof(kSets[0]));

	[[nodiscard]] constexpr const SetRow* FindSet(uint8_t setId) noexcept
	{
		for (const auto& s : kSets) if (s.id == setId) return &s;
		return nullptr;
	}

	// ── 착용 장비 → 능력치 보너스(서버 RecomputeStats · 클라 비교 툴팁이 같은 함수를 쓴다) ──
	constexpr void Accumulate(const EquippedPiece* pieces, int count, GearBonus& g, GearExtras& ex) noexcept
	{
		for (int i = 0; i < count; ++i)
		{
			const EquippedPiece& p = pieces[i];
			if (p.row == nullptr || p.row->type != ItemType::eEquipment)
				continue;
			const ItemTableRow& row = *p.row;
			const ItemInstance inst = ItemInstance::Unpack(p.extra);
			const Rarity r = EffectiveRarity(row, inst);
			const int pct = BasePercent(row, r);
			if (ItemTableData::SlotOf(row) == EquipSlot::Weapon)
			{
				const int bonus = row.attackBonus * pct / 100 + EnchantRule::AttackBonus(p.enchant);
				if (row.weaponKind == WeaponKind::Staff) AddU16(g.weaponMagic, bonus);
				else AddU16(g.weaponAttack, bonus);
			}
			AddU16(g.armor, row.armorBonus * pct / 100);
			AddU16(g.magicArmor, row.magicArmorBonus * pct / 100);
			for (const auto& o : inst.opts)
				if (o.kind != ItemOption::None)
					ApplyOption(g, ex, o);
			if (inst.proc != ItemProc::None)
			{
				uint8_t& pw = ex.procPower[static_cast<size_t>(inst.proc)];
				const uint8_t want = inst.empowered ? 2 : 1;
				if (pw < want) pw = want;
			}
			if (row.setId > 0 && row.setId < sizeof(ex.setCount))
				++ex.setCount[row.setId];
		}
		for (const auto& s : kSets)
		{
			const int n = s.id < sizeof(ex.setCount) ? ex.setCount[s.id] : 0;
			if (n >= 2) for (const auto& o : s.two) ApplyOption(g, ex, o);
			if (n >= 4)
			{
				for (const auto& o : s.four) ApplyOption(g, ex, o);
				if (s.fourProc != ItemProc::None && ex.procPower[static_cast<size_t>(s.fourProc)] < 1)
					ex.procPower[static_cast<size_t>(s.fourProc)] = 1;
			}
		}
	}

	// ── 드롭 희귀도 굴림(서버) ─────────────────────────────────────────────────
	// 바닥 위로 +0 ~ +4 단계 가중치(천분율). 몬스터 단계(1~5)별, 보스는 따로.
	inline constexpr uint16_t kStepWeights[6][5] =
	{
		{ 820, 150,  26,   4,  0 },   // (0 = 1단계와 같음)
		{ 820, 150,  26,   4,  0 },   // 1단계
		{ 790, 170,  33,   6,  1 },   // 2단계
		{ 750, 190,  47,  11,  2 },   // 3단계
		{ 700, 215,  63,  18,  4 },   // 4단계
		{ 650, 240,  80,  24,  6 },   // 5단계
	};
	inline constexpr uint16_t kBossStepWeights[5] = { 360, 340, 200, 80, 20 };

	// 천장: "희귀 이상"을 이 횟수 동안 못 보면 다음 장비는 적어도 희귀, "영웅 이상"도 따로 센다.
	inline constexpr uint16_t kPityRare   = 10;
	inline constexpr uint16_t kPityHeroic = 40;
	inline constexpr int      kMythicGate     = 8;    // %
	inline constexpr int      kMythicGateBoss = 15;   // %

	// rng() → uint32_t 난수. 굴린 뒤 천장 카운터를 갱신한다.
	template <class Rng>
	[[nodiscard]] Rarity RollRarity(const ItemTableRow& row, int monsterTier, bool boss, int itemFind,
		uint16_t& pityRare, uint16_t& pityHeroic, Rng&& rng)
	{
		const int t = monsterTier < 0 ? 0 : (monsterTier > 5 ? 5 : monsterTier);
		const uint16_t* w = boss ? kBossStepWeights : kStepWeights[t];
		uint32_t weights[5];
		uint32_t total = 0;
		for (int i = 0; i < 5; ++i)
		{
			weights[i] = w[i];
			if (i > 0) weights[i] = weights[i] * static_cast<uint32_t>(100 + (itemFind > 0 ? itemFind : 0)) / 100u;
			total += weights[i];
		}
		uint32_t roll = total > 0 ? static_cast<uint32_t>(rng()) % total : 0;
		int step = 0;
		for (; step < 4; ++step)
		{
			if (roll < weights[step]) break;
			roll -= weights[step];
		}
		int r = static_cast<int>(FloorRarity(row)) + step;
		if (r >= static_cast<int>(Rarity::Count)) r = static_cast<int>(Rarity::Count) - 1;
		// 신화는 한 번 더 문을 통과해야 한다(바닥이 높은 전설 무기가 너무 쉽게 신화가 되지 않게)
		if (r == static_cast<int>(Rarity::Mythic) && static_cast<int>(rng() % 100u) >= (boss ? kMythicGateBoss : kMythicGate))
			r = static_cast<int>(Rarity::Legendary);

		// 천장
		if (r < static_cast<int>(Rarity::Rare) && pityRare + 1 >= kPityRare) r = static_cast<int>(Rarity::Rare);
		if (r < static_cast<int>(Rarity::Heroic) && pityHeroic + 1 >= kPityHeroic) r = static_cast<int>(Rarity::Heroic);
		pityRare   = r >= static_cast<int>(Rarity::Rare)   ? 0 : static_cast<uint16_t>(pityRare + 1);
		pityHeroic = r >= static_cast<int>(Rarity::Heroic) ? 0 : static_cast<uint16_t>(pityHeroic + 1);
		return static_cast<Rarity>(r);
	}

	// 희귀도에 맞춰 옵션·특수 효과를 굴린다. cls: 주운 사람 직업(옵션 가중치·기술 강화 대상).
	template <class Rng>
	[[nodiscard]] ItemInstance RollInstance(const ItemTableRow& row, Rarity rarity, ClassId cls, Rng&& rng)
	{
		ItemInstance inst;
		inst.rarity = rarity;
		const int want = kOptionCount[Idx(rarity)];
		const EquipSlot slot = ItemTableData::SlotOf(row);
		int filled = 0;

		// 전설 이상 무기·목걸이: 절반 확률로 "기술 강화" 한 줄
		if (static_cast<int>(rarity) >= static_cast<int>(Rarity::Legendary) && (slot == EquipSlot::Weapon || slot == EquipSlot::Necklace)
			&& (rng() % 2u) == 0)
		{
			ClassId target = cls;
			for (int c = 0; c < static_cast<int>(ClassId::Count); ++c)
				if (row.classMask == (1u << c)) target = static_cast<ClassId>(c);
			const ClassRow& cr = ClassTable::Get(target);
			const uint16_t skill = cr.skills[1 + rng() % 5u];
			const int step = rarity == Rarity::Mythic ? 4 + static_cast<int>(rng() % 4u) : 2 + static_cast<int>(rng() % 3u);
			inst.opts[filled++] = OptionRoll{ ItemOption::SkillBoost, SkillBoostValue(skill, step) };
		}

		uint32_t pool = OptionPool(row);
		while (filled < want && pool != 0)
		{
			int total = 0;
			for (int k = 1; k < static_cast<int>(ItemOption::Count); ++k)
				if (pool & (1u << k)) total += OptionWeight(static_cast<ItemOption>(k), cls);
			if (total <= 0) break;
			int roll = static_cast<int>(rng() % static_cast<uint32_t>(total));
			int pick = 0;
			for (int k = 1; k < static_cast<int>(ItemOption::Count); ++k)
			{
				if (!(pool & (1u << k))) continue;
				const int wk = OptionWeight(static_cast<ItemOption>(k), cls);
				if (roll < wk) { pick = k; break; }
				roll -= wk;
			}
			if (pick == 0) break;
			pool &= ~(1u << pick);
			const Range rg = OptionRange(static_cast<ItemOption>(pick), row.tier, rarity);
			const uint8_t value = static_cast<uint8_t>(rg.lo + rng() % static_cast<uint32_t>(rg.hi - rg.lo + 1));
			inst.opts[filled++] = OptionRoll{ static_cast<ItemOption>(pick), value };
		}

		if (static_cast<int>(rarity) >= static_cast<int>(Rarity::Legendary))
		{
			const uint32_t procs = ProcPool(row);
			int n = 0;
			for (int p = 1; p < static_cast<int>(ItemProc::Count); ++p) if (procs & (1u << p)) ++n;
			if (n > 0)
			{
				int k = static_cast<int>(rng() % static_cast<uint32_t>(n));
				for (int p = 1; p < static_cast<int>(ItemProc::Count); ++p)
				{
					if (!(procs & (1u << p))) continue;
					if (k-- == 0) { inst.proc = static_cast<ItemProc>(p); break; }
				}
			}
			inst.empowered = rarity == Rarity::Mythic;
		}
		return inst;
	}
}
