#pragma once

#include "ItemTableRow.h"

// ─────────────────────────────────────────────────────────────────────────────
// 아이템 테이블 원본 데이터.
//
// 예전에는 ItemTable.cpp 안에 숨겨 두었지만, 새 클라이언트(Client2D)가 아이템
// 이름·아이콘·가격을 같은 표에서 읽도록 헤더로 꺼냈다 — "서버와 클라가 같은
// 데이터를 읽는다"는 원칙으로 두 쪽 표가 어긋나는 일을 원천 차단한다.
// 서버 코드는 여전히 ItemTable::Find()로만 조회한다(이 배열을 직접 순회/수정하는
// 서버 코드를 만들지 않는다 — 조회 지점을 하나로 유지하기 위함).
//
// 규칙(기존과 동일): 새 아이템은 배열 끝에만 추가하고, 이미 등록된 id는 절대
// 바꾸거나 재사용하지 않는다 — Inventory DB가 id를 영구 식별자로 저장한다.
// ─────────────────────────────────────────────────────────────────────────────
namespace ItemTableData
{
	// 자주 쓰는 id에 이름을 붙여 둔다(서버 로직/클라 UI 양쪽에서 사용).
	namespace Id
	{
		inline constexpr ItemTableId HerbPotion      = 1;
		inline constexpr ItemTableId PracticeSword   = 2;
		inline constexpr ItemTableId IronSword       = 3;
		inline constexpr ItemTableId SteelSword      = 4;
		inline constexpr ItemTableId EmberSword      = 5;
		inline constexpr ItemTableId Dawnkeeper      = 6;
		inline constexpr ItemTableId Adena           = 7;
		inline constexpr ItemTableId StrongHerbPotion = 8;
		inline constexpr ItemTableId SapElixir       = 9;
		inline constexpr ItemTableId ReturnScroll    = 10;
		inline constexpr ItemTableId OldwayScroll    = 11;
		inline constexpr ItemTableId WindstepPotion  = 12;
		inline constexpr ItemTableId EngravingScroll = 13;
		inline constexpr ItemTableId ConsecratedScroll = 14;
		inline constexpr ItemTableId DobranDagger     = 15;
		inline constexpr ItemTableId CavalrySaber    = 16;
		inline constexpr ItemTableId SeverinBlade    = 17;
		inline constexpr ItemTableId KaldunExecutioner = 18;
		// V2 — 직업별 초보자 무기
		inline constexpr ItemTableId ApprenticeStaff = 19;
		inline constexpr ItemTableId HunterDagger    = 20;
		// V2 — 직업 무기(각인술사 지팡이 · 사냥꾼 단검)
		inline constexpr ItemTableId OakStaff        = 21;
		inline constexpr ItemTableId EngravedStaff   = 22;
		inline constexpr ItemTableId EmberStaff      = 23;
		inline constexpr ItemTableId HarvenBranch    = 24;
		inline constexpr ItemTableId BlackironDagger = 25;
		inline constexpr ItemTableId MoonlightDagger = 26;
		inline constexpr ItemTableId VenomFang       = 27;
		inline constexpr ItemTableId Nightfang       = 28;
		// V2 — 방어구 · 장신구
		inline constexpr ItemTableId LeatherHood     = 29;
		inline constexpr ItemTableId IronHelm        = 30;
		inline constexpr ItemTableId OrderHelm       = 31;
		inline constexpr ItemTableId PaddedArmor     = 32;
		inline constexpr ItemTableId ChainMail       = 33;
		inline constexpr ItemTableId PlateArmor      = 34;
		inline constexpr ItemTableId EngraverRobe    = 35;
		inline constexpr ItemTableId HunterLeather   = 36;
		inline constexpr ItemTableId LeatherGloves   = 37;
		inline constexpr ItemTableId IronGauntlets   = 38;
		inline constexpr ItemTableId LeatherBoots    = 39;
		inline constexpr ItemTableId IronGreaves     = 40;
		inline constexpr ItemTableId TravelerCloak   = 41;
		inline constexpr ItemTableId NightCloak      = 42;
		inline constexpr ItemTableId BoneCharm       = 43;
		inline constexpr ItemTableId SilverPendant   = 44;
		inline constexpr ItemTableId CopperRing      = 45;
		inline constexpr ItemTableId SilverRing      = 46;
		// V2 — 세트(서약의 갑주 · 이레린의 노래 · 은늑대)
		inline constexpr ItemTableId OathHelm        = 47;
		inline constexpr ItemTableId OathPlate       = 48;
		inline constexpr ItemTableId OathGauntlets   = 49;
		inline constexpr ItemTableId OathGreaves     = 50;
		inline constexpr ItemTableId IrerinHood      = 51;
		inline constexpr ItemTableId IrerinRobe      = 52;
		inline constexpr ItemTableId IrerinAmulet    = 53;
		inline constexpr ItemTableId IrerinBand      = 54;
		inline constexpr ItemTableId SilverwolfGrips = 55;
		inline constexpr ItemTableId SilverwolfBoots = 56;
		inline constexpr ItemTableId SilverwolfCloak = 57;
		inline constexpr ItemTableId SilverwolfRing  = 58;
	}

	// 직업 비트(classMask)
	inline constexpr uint8_t kMaskKnight = 1 << 0;
	inline constexpr uint8_t kMaskRune   = 1 << 1;
	inline constexpr uint8_t kMaskHunter = 1 << 2;

	// { id, type, maxStack, name, grade, attackBonus, healAmount,
	//   effect, effectValue, buyPrice, sellPrice, displayName, iconId,
	//   equipSlot, weaponKind, armorBonus, magicArmorBonus, tier, setId, classMask, reqLevel }
	//
	// attackBonus는 레벨 기본 공격력(LevelTable::BaseAttack, 1레벨 10)에 더해진다.
	// 1~6번은 기존 값(보너스 3/6/12/22/40, 물약 회복 30)을 그대로 유지했다.
	// grade(예전 등급)는 V2에서 "이 물건이 적어도 이 희귀도로 나온다"는 바닥이 된다
	// (Data/ItemRules.h FloorRarity) — 없음=일반, 레어=고급, 에픽=희귀, 유니크=영웅, 전설=전설.
	// 지팡이의 attackBonus는 마법 공격력으로 들어간다(WeaponKind::Staff).
	inline constexpr ItemTableRow kRows[] =
	{
		{ 1,   ItemType::eConsumable,  99,     "Herb Potion",         ItemGrade::eNone,       0,   30,   ItemEffect::eHeal,            0,    25,    8,      "약초 물약",             1 },
		{ 2,   ItemType::eEquipment,   1,      "Practice Sword",      ItemGrade::eNone,       3,   0,    ItemEffect::eNone,            0,    100,   30,     "연습용 목검",           10,  EquipSlot::Weapon,    WeaponKind::Sword,       0,   0,   1 },
		{ 3,   ItemType::eEquipment,   1,      "Iron Sword",          ItemGrade::eRare,       6,   0,    ItemEffect::eNone,            0,    2000,  300,    "철검",                  11,  EquipSlot::Weapon,    WeaponKind::Sword,       0,   0,   2 },
		{ 4,   ItemType::eEquipment,   1,      "Steel Longsword",     ItemGrade::eEpic,       12,  0,    ItemEffect::eNone,            0,    0,     1500,   "강철 장검",             12,  EquipSlot::Weapon,    WeaponKind::Sword,       0,   0,   3 },
		{ 5,   ItemType::eEquipment,   1,      "Ember Longsword",     ItemGrade::eUnique,     22,  0,    ItemEffect::eNone,            0,    0,     6000,   "잉걸 장검",             13,  EquipSlot::Weapon,    WeaponKind::Sword,       0,   0,   4 },
		{ 6,   ItemType::eEquipment,   1,      "Dawnkeeper",          ItemGrade::eLegendary,  40,  0,    ItemEffect::eNone,            0,    0,     30000,  "새벽지기",              14,  EquipSlot::Weapon,    WeaponKind::Greatsword,  0,   0,   5 },
		{ 7,   ItemType::eCurrency,    65535,  "Grosz",               ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    0,     0,      "그로시",                30 },
		{ 8,   ItemType::eConsumable,  99,     "Strong Herb Potion",  ItemGrade::eNone,       0,   70,   ItemEffect::eHeal,            0,    90,    25,     "진한 약초 물약",        2 },
		{ 9,   ItemType::eConsumable,  99,     "Sap Elixir",          ItemGrade::eNone,       0,   150,  ItemEffect::eHeal,            0,    300,   90,     "수액 영약",             3 },
		{ 10,  ItemType::eConsumable,  99,     "Return Scroll",       ItemGrade::eNone,       0,   0,    ItemEffect::eReturnTown,      0,    60,    15,     "귀환 두루마리",         20 },
		{ 11,  ItemType::eConsumable,  99,     "Oldway Scroll",       ItemGrade::eNone,       0,   0,    ItemEffect::eTeleportRandom,  0,    100,   25,     "옛길 두루마리",         21 },
		{ 12,  ItemType::eConsumable,  99,     "Windstep Potion",     ItemGrade::eNone,       0,   0,    ItemEffect::eHaste,           300,  200,   50,     "바람걸음 물약",         4 },
		{ 13,  ItemType::eConsumable,  99,     "Engraving Scroll",    ItemGrade::eNone,       0,   0,    ItemEffect::eEnchantWeapon,   0,    5000,  1000,   "각인 두루마리",         22 },
		{ 14,  ItemType::eConsumable,  99,     "Consecrated Scroll",  ItemGrade::eNone,       0,   0,    ItemEffect::eEnchantWeapon,   1,    0,     5000,   "축성된 각인 두루마리",  23 },
		{ 15,  ItemType::eEquipment,   1,      "Dobran's Dagger",     ItemGrade::eRare,       9,   0,    ItemEffect::eNone,            0,    6000,  800,    "도브란의 단검",         15,  EquipSlot::Weapon,    WeaponKind::Dagger,      0,   0,   2 },
		{ 16,  ItemType::eEquipment,   1,      "Cavalry Saber",       ItemGrade::eEpic,       16,  0,    ItemEffect::eNone,            0,    0,     2500,   "기병 곡도",             16,  EquipSlot::Weapon,    WeaponKind::Saber,      0,   0,   3 },
		{ 17,  ItemType::eEquipment,   1,      "Severin's Blade",     ItemGrade::eUnique,     30,  0,    ItemEffect::eNone,            0,    0,     12000,  "세베린의 장검",         17,  EquipSlot::Weapon,    WeaponKind::Sword,       0,   0,   4 },
		{ 18,  ItemType::eEquipment,   1,      "Kaldun Executioner",  ItemGrade::eLegendary,  55,  0,    ItemEffect::eNone,            0,    0,     50000,  "칼둔의 처형검",         18,  EquipSlot::Weapon,    WeaponKind::Greatsword,  0,   0,   5 },
		{ 19,  ItemType::eEquipment,   1,      "Apprentice Staff",    ItemGrade::eNone,       3,   0,    ItemEffect::eNone,            0,    100,   30,     "견습생의 지팡이",       40,  EquipSlot::Weapon,    WeaponKind::Staff,       0,   0,   1 },
		{ 20,  ItemType::eEquipment,   1,      "Hunting Dagger",      ItemGrade::eNone,       3,   0,    ItemEffect::eNone,            0,    100,   30,     "사냥용 단검",           41,  EquipSlot::Weapon,    WeaponKind::Dagger,      0,   0,   1 },

		// ── V2 직업 무기 ──────────────────────────────────────────────────────────
		{ 21,  ItemType::eEquipment,   1,      "Oak Staff",           ItemGrade::eRare,       8,   0,    ItemEffect::eNone,            0,    1800,  260,    "참나무 지팡이",         42,  EquipSlot::Weapon,    WeaponKind::Staff,       0,   0,   2,  0,  kMaskRune,    5 },
		{ 22,  ItemType::eEquipment,   1,      "Engraved Staff",      ItemGrade::eEpic,       15,  0,    ItemEffect::eNone,            0,    0,     1300,   "각인 지팡이",           43,  EquipSlot::Weapon,    WeaponKind::Staff,       0,   0,   3,  0,  kMaskRune,    12 },
		{ 23,  ItemType::eEquipment,   1,      "Ember Staff",         ItemGrade::eUnique,     26,  0,    ItemEffect::eNone,            0,    0,     5500,   "잿불 지팡이",           44,  EquipSlot::Weapon,    WeaponKind::Staff,       0,   0,   4,  0,  kMaskRune,    18 },
		{ 24,  ItemType::eEquipment,   1,      "Harven Branch",       ItemGrade::eLegendary,  44,  0,    ItemEffect::eNone,            0,    0,     26000,  "하르벤 가지",           45,  EquipSlot::Weapon,    WeaponKind::Staff,       0,   0,   5,  0,  kMaskRune,    26 },
		{ 25,  ItemType::eEquipment,   1,      "Blackiron Dagger",    ItemGrade::eRare,       8,   0,    ItemEffect::eNone,            0,    1800,  260,    "흑철 단검",             46,  EquipSlot::Weapon,    WeaponKind::Dagger,      0,   0,   2,  0,  kMaskHunter,  5 },
		{ 26,  ItemType::eEquipment,   1,      "Moonlight Dagger",    ItemGrade::eEpic,       15,  0,    ItemEffect::eNone,            0,    0,     1300,   "월광 단검",             47,  EquipSlot::Weapon,    WeaponKind::Dagger,      0,   0,   3,  0,  kMaskHunter,  12 },
		{ 27,  ItemType::eEquipment,   1,      "Venom Fang",          ItemGrade::eUnique,     25,  0,    ItemEffect::eNone,            0,    0,     5500,   "독니 단검",             48,  EquipSlot::Weapon,    WeaponKind::Dagger,      0,   0,   4,  0,  kMaskHunter,  18 },
		{ 28,  ItemType::eEquipment,   1,      "Nightfang",           ItemGrade::eLegendary,  42,  0,    ItemEffect::eNone,            0,    0,     26000,  "밤송곳니",              49,  EquipSlot::Weapon,    WeaponKind::Dagger,      0,   0,   5,  0,  kMaskHunter,  26 },

		// ── V2 방어구 ──────────────────────────────────────────────────────────────
		{ 29,  ItemType::eEquipment,   1,      "Leather Hood",        ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    300,   60,     "가죽 두건",             50,  EquipSlot::Helmet,    WeaponKind::None,        2,   1,   1 },
		{ 30,  ItemType::eEquipment,   1,      "Iron Helm",           ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    0,     260,    "철 투구",               51,  EquipSlot::Helmet,    WeaponKind::None,        4,   0,   2,  0,  0,            5 },
		{ 31,  ItemType::eEquipment,   1,      "Order Helm",          ItemGrade::eRare,       0,   0,    ItemEffect::eNone,            0,    0,     1200,   "기사단 투구",           52,  EquipSlot::Helmet,    WeaponKind::None,        8,   1,   4,  0,  0,            18 },
		{ 32,  ItemType::eEquipment,   1,      "Padded Armor",        ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    500,   90,     "누빔 갑옷",             53,  EquipSlot::Armor,     WeaponKind::None,        3,   0,   1 },
		{ 33,  ItemType::eEquipment,   1,      "Chain Mail",          ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    0,     420,    "사슬 갑옷",             54,  EquipSlot::Armor,     WeaponKind::None,        7,   0,   2,  0,  0,            5 },
		{ 34,  ItemType::eEquipment,   1,      "Plate Armor",         ItemGrade::eRare,       0,   0,    ItemEffect::eNone,            0,    0,     2200,   "판금 갑옷",             55,  EquipSlot::Armor,     WeaponKind::None,        14,  1,   4,  0,  0,            18 },
		{ 35,  ItemType::eEquipment,   1,      "Engraver Robe",       ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    0,     420,    "각인사의 법복",         56,  EquipSlot::Armor,     WeaponKind::None,        3,   6,   2,  0,  0,            5 },
		{ 36,  ItemType::eEquipment,   1,      "Hunter Leather",      ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    0,     800,    "사냥꾼 가죽 갑옷",      57,  EquipSlot::Armor,     WeaponKind::None,        6,   3,   3,  0,  0,            12 },
		{ 37,  ItemType::eEquipment,   1,      "Leather Gloves",      ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    250,   50,     "가죽 장갑",             58,  EquipSlot::Gloves,    WeaponKind::None,        1,   0,   1 },
		{ 38,  ItemType::eEquipment,   1,      "Iron Gauntlets",      ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    0,     500,    "철 장갑",               59,  EquipSlot::Gloves,    WeaponKind::None,        3,   0,   3,  0,  0,            12 },
		{ 39,  ItemType::eEquipment,   1,      "Leather Boots",       ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    250,   50,     "가죽 장화",             60,  EquipSlot::Boots,     WeaponKind::None,        1,   0,   1 },
		{ 40,  ItemType::eEquipment,   1,      "Iron Greaves",        ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    0,     500,    "철 장화",               61,  EquipSlot::Boots,     WeaponKind::None,        3,   0,   3,  0,  0,            12 },
		{ 41,  ItemType::eEquipment,   1,      "Traveler Cloak",      ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    400,   70,     "여행자의 망토",         62,  EquipSlot::Cloak,     WeaponKind::None,        0,   2,   1 },
		{ 42,  ItemType::eEquipment,   1,      "Night Cloak",         ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    0,     700,    "야행 망토",             63,  EquipSlot::Cloak,     WeaponKind::None,        1,   4,   3,  0,  0,            12 },
		{ 43,  ItemType::eEquipment,   1,      "Bone Charm",          ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    0,     80,     "뼈 부적",               64,  EquipSlot::Necklace,  WeaponKind::None,        0,   1,   1 },
		{ 44,  ItemType::eEquipment,   1,      "Silver Pendant",      ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    0,     900,    "은 목걸이",             65,  EquipSlot::Necklace,  WeaponKind::None,        0,   3,   3,  0,  0,            12 },
		{ 45,  ItemType::eEquipment,   1,      "Copper Ring",         ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    0,     80,     "구리 반지",             66,  EquipSlot::Ring,      WeaponKind::None,        0,   0,   1 },
		{ 46,  ItemType::eEquipment,   1,      "Silver Ring",         ItemGrade::eNone,       0,   0,    ItemEffect::eNone,            0,    0,     900,    "은 반지",               67,  EquipSlot::Ring,      WeaponKind::None,        0,   1,   3,  0,  0,            12 },

		// ── V2 세트(2부위 · 4부위 효과: Data/ItemRules.h kSets) ────────────────────
		{ 47,  ItemType::eEquipment,   1,      "Oath Helm",           ItemGrade::eEpic,       0,   0,    ItemEffect::eNone,            0,    0,     3000,   "서약의 투구",           68,  EquipSlot::Helmet,    WeaponKind::None,        7,   2,   4,  1,  0,            18 },
		{ 48,  ItemType::eEquipment,   1,      "Oath Plate",          ItemGrade::eEpic,       0,   0,    ItemEffect::eNone,            0,    0,     3000,   "서약의 흉갑",           69,  EquipSlot::Armor,     WeaponKind::None,        13,  2,   4,  1,  0,            18 },
		{ 49,  ItemType::eEquipment,   1,      "Oath Gauntlets",      ItemGrade::eEpic,       0,   0,    ItemEffect::eNone,            0,    0,     3000,   "서약의 장갑",           70,  EquipSlot::Gloves,    WeaponKind::None,        4,   0,   4,  1,  0,            18 },
		{ 50,  ItemType::eEquipment,   1,      "Oath Greaves",        ItemGrade::eEpic,       0,   0,    ItemEffect::eNone,            0,    0,     3000,   "서약의 각반",           71,  EquipSlot::Boots,     WeaponKind::None,        4,   0,   4,  1,  0,            18 },
		{ 51,  ItemType::eEquipment,   1,      "Irerin Hood",         ItemGrade::eEpic,       0,   0,    ItemEffect::eNone,            0,    0,     3000,   "이레린 두건",           72,  EquipSlot::Helmet,    WeaponKind::None,        3,   6,   4,  2,  0,            18 },
		{ 52,  ItemType::eEquipment,   1,      "Irerin Robe",         ItemGrade::eEpic,       0,   0,    ItemEffect::eNone,            0,    0,     3000,   "이레린 법복",           73,  EquipSlot::Armor,     WeaponKind::None,        5,   11,  4,  2,  0,            18 },
		{ 53,  ItemType::eEquipment,   1,      "Irerin Amulet",       ItemGrade::eEpic,       0,   0,    ItemEffect::eNone,            0,    0,     3000,   "이레린 목걸이",         74,  EquipSlot::Necklace,  WeaponKind::None,        0,   5,   4,  2,  0,            18 },
		{ 54,  ItemType::eEquipment,   1,      "Irerin Band",         ItemGrade::eEpic,       0,   0,    ItemEffect::eNone,            0,    0,     3000,   "이레린 반지",           75,  EquipSlot::Ring,      WeaponKind::None,        0,   2,   4,  2,  0,            18 },
		{ 55,  ItemType::eEquipment,   1,      "Silverwolf Grips",    ItemGrade::eEpic,       0,   0,    ItemEffect::eNone,            0,    0,     3000,   "은늑대 장갑",           76,  EquipSlot::Gloves,    WeaponKind::None,        3,   1,   4,  3,  0,            18 },
		{ 56,  ItemType::eEquipment,   1,      "Silverwolf Boots",    ItemGrade::eEpic,       0,   0,    ItemEffect::eNone,            0,    0,     3000,   "은늑대 장화",           77,  EquipSlot::Boots,     WeaponKind::None,        3,   1,   4,  3,  0,            18 },
		{ 57,  ItemType::eEquipment,   1,      "Silverwolf Cloak",    ItemGrade::eEpic,       0,   0,    ItemEffect::eNone,            0,    0,     3000,   "은늑대 망토",           78,  EquipSlot::Cloak,     WeaponKind::None,        2,   4,   4,  3,  0,            18 },
		{ 58,  ItemType::eEquipment,   1,      "Silverwolf Ring",     ItemGrade::eEpic,       0,   0,    ItemEffect::eNone,            0,    0,     3000,   "은늑대 반지",           79,  EquipSlot::Ring,      WeaponKind::None,        1,   1,   4,  3,  0,            18 },
	};

	inline constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

	// 클라이언트/툴에서 쓰는 constexpr 조회(서버는 ItemTable::Find를 쓴다).
	[[nodiscard]] constexpr const ItemTableRow* Find(ItemTableId id) noexcept
	{
		for (const auto& row : kRows)
			if (row.id == id)
				return &row;
		return nullptr;
	}

	// 장비 부위. 예전 행(equipSlot을 적지 않은 장비)은 무기다.
	[[nodiscard]] constexpr EquipSlot SlotOf(const ItemTableRow& row) noexcept
	{
		if (row.type != ItemType::eEquipment)
			return EquipSlot::None;
		return row.equipSlot == EquipSlot::None ? EquipSlot::Weapon : row.equipSlot;
	}

	[[nodiscard]] constexpr EquipSlot SlotOf(ItemTableId id) noexcept
	{
		const ItemTableRow* row = Find(id);
		return row != nullptr ? SlotOf(*row) : EquipSlot::None;
	}

	// 무기(각인 두루마리 대상 · 손에 드는 외형)
	[[nodiscard]] constexpr bool IsWeapon(ItemTableId id) noexcept
	{
		return SlotOf(id) == EquipSlot::Weapon;
	}

	// 지팡이(마법 무기) — 공격력 보너스가 마법 공격력으로 들어간다.
	[[nodiscard]] constexpr bool IsStaff(ItemTableId id) noexcept
	{
		const ItemTableRow* row = Find(id);
		return row != nullptr && row->weaponKind == WeaponKind::Staff;
	}

	// 직업 제한(확장 세션만 검사). mask 0 = 모두.
	[[nodiscard]] constexpr bool ClassCanUse(const ItemTableRow& row, uint8_t classId) noexcept
	{
		return row.classMask == 0 || (classId < 8 && (row.classMask & (1u << classId)) != 0);
	}

	namespace Detail
	{
		constexpr bool ValidateV2Rows() noexcept
		{
			for (const auto& row : kRows)
			{
				if (row.type != ItemType::eEquipment)
				{
					if (row.equipSlot != EquipSlot::None || row.armorBonus != 0 || row.magicArmorBonus != 0 || row.setId != 0)
						return false;
					continue;
				}
				if (row.tier < 1 || row.tier > 5)
					return false;
				// 무기만 공격력/무기 종류를 갖는다
				const bool weapon = SlotOf(row) == EquipSlot::Weapon;
				if (weapon != (row.weaponKind != WeaponKind::None))
					return false;
				if (!weapon && row.attackBonus != 0)
					return false;
			}
			return true;
		}
		static_assert(ValidateV2Rows(), "ItemTableData: V2 장비 필드(부위/무기 종류/단계)가 어긋났습니다.");
	}
}
