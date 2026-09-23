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
		inline constexpr ItemTableId HealthPotion    = 1;
		inline constexpr ItemTableId WoodenSword     = 2;
		inline constexpr ItemTableId IronSword       = 3;
		inline constexpr ItemTableId SteelSword      = 4;
		inline constexpr ItemTableId FlameSword      = 5;
		inline constexpr ItemTableId DragonSlayer    = 6;
		inline constexpr ItemTableId Adena           = 7;
		inline constexpr ItemTableId OrangePotion    = 8;
		inline constexpr ItemTableId ClearPotion     = 9;
		inline constexpr ItemTableId ReturnScroll    = 10;
		inline constexpr ItemTableId TeleportScroll  = 11;
		inline constexpr ItemTableId HastePotion     = 12;
		inline constexpr ItemTableId EnchantScroll   = 13;
		inline constexpr ItemTableId BlessedEnchant  = 14;
		inline constexpr ItemTableId OrichalcumDagger = 15;
		inline constexpr ItemTableId Katana          = 16;
		inline constexpr ItemTableId DkFlameSword    = 17;
		inline constexpr ItemTableId Executioner     = 18;
	}

	// { id, type, maxStack, name, grade, attackBonus, healAmount,
	//   effect, effectValue, buyPrice, sellPrice, displayName, iconId }
	//
	// attackBonus는 레벨 기본 공격력(LevelTable::BaseAttack, 1레벨 10)에 더해진다.
	// 1~6번은 기존 값(보너스 3/6/12/22/40, 물약 회복 30)을 그대로 유지했다.
	inline constexpr ItemTableRow kRows[] =
	{
		{ 1,  ItemType::eConsumable,    99, "Health Potion",     ItemGrade::eNone,       0,  30, ItemEffect::eHeal,            0,    25,     8, "빨간 물약",               1 },
		{ 2,  ItemType::eEquipment,      1, "Wooden Sword",      ItemGrade::eNone,       3,   0, ItemEffect::eNone,            0,   100,    30, "나무 검",                 10 },
		{ 3,  ItemType::eEquipment,      1, "Iron Sword",        ItemGrade::eRare,       6,   0, ItemEffect::eNone,            0,  2000,   300, "철검",                    11 },
		{ 4,  ItemType::eEquipment,      1, "Steel Sword",       ItemGrade::eEpic,      12,   0, ItemEffect::eNone,            0,     0,  1500, "강철검",                  12 },
		{ 5,  ItemType::eEquipment,      1, "Flame Sword",       ItemGrade::eUnique,    22,   0, ItemEffect::eNone,            0,     0,  6000, "화염검",                  13 },
		{ 6,  ItemType::eEquipment,      1, "Dragon Slayer",     ItemGrade::eLegendary, 40,   0, ItemEffect::eNone,            0,     0, 30000, "드래곤 슬레이어",         14 },
		{ 7,  ItemType::eCurrency,   65535, "Adena",             ItemGrade::eNone,       0,   0, ItemEffect::eNone,            0,     0,     0, "아데나",                  30 },
		{ 8,  ItemType::eConsumable,    99, "Orange Potion",     ItemGrade::eNone,       0,  70, ItemEffect::eHeal,            0,    90,    25, "주홍 물약",               2 },
		{ 9,  ItemType::eConsumable,    99, "Clear Potion",      ItemGrade::eNone,       0, 150, ItemEffect::eHeal,            0,   300,    90, "맑은 물약",               3 },
		{ 10, ItemType::eConsumable,    99, "Return Scroll",     ItemGrade::eNone,       0,   0, ItemEffect::eReturnTown,      0,    60,    15, "마을 귀환 주문서",        20 },
		{ 11, ItemType::eConsumable,    99, "Teleport Scroll",   ItemGrade::eNone,       0,   0, ItemEffect::eTeleportRandom,  0,   100,    25, "순간이동 주문서",         21 },
		{ 12, ItemType::eConsumable,    99, "Haste Potion",      ItemGrade::eNone,       0,   0, ItemEffect::eHaste,         300,   200,    50, "초록 물약",               4 },
		{ 13, ItemType::eConsumable,    99, "Enchant Scroll",    ItemGrade::eNone,       0,   0, ItemEffect::eEnchantWeapon,   0,  5000,  1000, "무기 강화 주문서",        22 },
		{ 14, ItemType::eConsumable,    99, "Blessed Enchant",   ItemGrade::eNone,       0,   0, ItemEffect::eEnchantWeapon,   1,     0,  5000, "축복받은 무기 강화 주문서", 23 },
		{ 15, ItemType::eEquipment,      1, "Orichalcum Dagger", ItemGrade::eRare,       9,   0, ItemEffect::eNone,            0,  6000,   800, "오리하루콘 단검",         15 },
		{ 16, ItemType::eEquipment,      1, "Katana",            ItemGrade::eEpic,      16,   0, ItemEffect::eNone,            0,     0,  2500, "일본도",                  16 },
		{ 17, ItemType::eEquipment,      1, "DK Flame Sword",    ItemGrade::eUnique,    30,   0, ItemEffect::eNone,            0,     0, 12000, "데스나이트의 불검",       17 },
		{ 18, ItemType::eEquipment,      1, "Executioner Sword", ItemGrade::eLegendary, 55,   0, ItemEffect::eNone,            0,     0, 50000, "파멸의 집행검",           18 },
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

	[[nodiscard]] constexpr bool IsWeapon(ItemTableId id) noexcept
	{
		const ItemTableRow* row = Find(id);
		return row != nullptr && row->type == ItemType::eEquipment;
	}
}
