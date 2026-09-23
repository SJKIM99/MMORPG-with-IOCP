#include "pch.h"
#include "DropTable.h"

#include "ItemTable.h"
#include "ItemTableData.h"
#include "CoreTLS.h"

namespace
{
	using namespace ItemTableData::Id;

	struct DropTableEntry
	{
		ItemTableId id;
		ItemType    type;    // ItemTable과 반드시 일치해야 한다(ValidateDropTable가 시작 시점에 검증) —
		                      // 여기 명시해두면 이 파일만 보고도 무슨 아이템인지 바로 알 수 있다.
		ItemGrade   grade;   // 장비가 아니면 ItemGrade::eNone. type과 마찬가지로 ItemTable과 일치해야 한다.
		uint32_t    weight;  // 상대 가중치. noDropWeight를 포함한 전체 합 대비 비율이 곧 확률이다.
		uint16_t    minCount;
		uint16_t    maxCount;  // inclusive
	};

	struct DropTableDef
	{
		uint32_t              noDropWeight;  // "아무것도 안 나옴"의 가중치
		const DropTableEntry* entries;
		size_t                count;
	};

	constexpr ItemType  C = ItemType::eConsumable;
	constexpr ItemType  E = ItemType::eEquipment;
	constexpr ItemGrade N = ItemGrade::eNone;

	// 이 값들은 전부 조정 가능한 밸런싱 데이터다. 등급이 높을수록 weight를 큰 폭으로
	// 낮춰 "고등급일수록 현저히 희귀함"을 유지했다(기존 표의 취지 그대로).
	// 표마다 noDropWeight + weight 합 = 1000으로 맞춰 두면 weight가 곧 천분율이다.

	// 1단계: 기존 전역 표(물약 150 / 나무검 80 / 철검 50 / 강철검 15 / 화염검 4 / 드슬 1)를
	// 바탕으로, 초반 사냥터답게 물약·주문서를 늘리고 고등급 무기는 아래 단계로 넘겼다.
	constexpr DropTableEntry kTier1[] =
	{
		{ HealthPotion,   C, N,                   180, 1, 2 },
		{ OrangePotion,   C, N,                    20, 1, 1 },
		{ ReturnScroll,   C, N,                    30, 1, 1 },
		{ TeleportScroll, C, N,                    14, 1, 1 },
		{ WoodenSword,    E, N,                    40, 1, 1 },
		{ IronSword,      E, ItemGrade::eRare,     12, 1, 1 },
		{ EnchantScroll,  C, N,                     4, 1, 1 },
	};
	constexpr DropTableEntry kTier2[] =
	{
		{ HealthPotion,     C, N,                 160, 1, 3 },
		{ OrangePotion,     C, N,                  60, 1, 2 },
		{ ReturnScroll,     C, N,                  35, 1, 1 },
		{ TeleportScroll,   C, N,                  30, 1, 1 },
		{ HastePotion,      C, N,                  25, 1, 1 },
		{ IronSword,        E, ItemGrade::eRare,   40, 1, 1 },
		{ OrichalcumDagger, E, ItemGrade::eRare,   10, 1, 1 },
		{ SteelSword,       E, ItemGrade::eEpic,    8, 1, 1 },
		{ EnchantScroll,    C, N,                  12, 1, 1 },
	};
	constexpr DropTableEntry kTier3[] =
	{
		{ OrangePotion,     C, N,                 140, 1, 2 },
		{ ClearPotion,      C, N,                  40, 1, 1 },
		{ ReturnScroll,     C, N,                  40, 1, 1 },
		{ TeleportScroll,   C, N,                  35, 1, 1 },
		{ HastePotion,      C, N,                  40, 1, 1 },
		{ SteelSword,       E, ItemGrade::eEpic,   40, 1, 1 },
		{ OrichalcumDagger, E, ItemGrade::eRare,   25, 1, 1 },
		{ Katana,           E, ItemGrade::eEpic,    6, 1, 1 },
		{ FlameSword,       E, ItemGrade::eUnique,  4, 1, 1 },
		{ EnchantScroll,    C, N,                  25, 1, 1 },
		{ BlessedEnchant,   C, N,                   5, 1, 1 },
	};
	constexpr DropTableEntry kTier4[] =
	{
		{ OrangePotion,     C, N,                    120, 1, 3 },
		{ ClearPotion,      C, N,                     90, 1, 2 },
		{ ReturnScroll,     C, N,                     35, 1, 1 },
		{ TeleportScroll,   C, N,                     30, 1, 1 },
		{ HastePotion,      C, N,                     45, 1, 1 },
		{ SteelSword,       E, ItemGrade::eEpic,      50, 1, 1 },
		{ Katana,           E, ItemGrade::eEpic,      20, 1, 1 },
		{ OrichalcumDagger, E, ItemGrade::eRare,       9, 1, 1 },
		{ FlameSword,       E, ItemGrade::eUnique,    12, 1, 1 },
		{ DragonSlayer,     E, ItemGrade::eLegendary,  2, 1, 1 },
		{ EnchantScroll,    C, N,                     55, 1, 1 },
		{ BlessedEnchant,   C, N,                     12, 1, 1 },
	};
	constexpr DropTableEntry kTier5[] =
	{
		{ ClearPotion,      C, N,                    180, 1, 2 },
		{ OrangePotion,     C, N,                     60, 1, 3 },
		{ ReturnScroll,     C, N,                     14, 1, 1 },
		{ HastePotion,      C, N,                     50, 1, 1 },
		{ Katana,           E, ItemGrade::eEpic,      40, 1, 1 },
		{ FlameSword,       E, ItemGrade::eUnique,    30, 1, 1 },
		{ DragonSlayer,     E, ItemGrade::eLegendary,  6, 1, 1 },
		{ EnchantScroll,    C, N,                    110, 1, 1 },
		{ BlessedEnchant,   C, N,                     30, 1, 1 },
	};
	constexpr DropTableEntry kBossCommon[] =
	{
		{ EnchantScroll,    C, N,                    350, 1, 3 },
		{ BlessedEnchant,   C, N,                    150, 1, 1 },
		{ Katana,           E, ItemGrade::eEpic,     150, 1, 1 },
		{ FlameSword,       E, ItemGrade::eUnique,   120, 1, 1 },
		{ ClearPotion,      C, N,                    150, 3, 5 },
		{ DragonSlayer,     E, ItemGrade::eLegendary, 20, 1, 1 },
		{ HastePotion,      C, N,                     60, 1, 2 },
	};
	constexpr DropTableEntry kDeathKnight[] =
	{
		{ EnchantScroll,    C, N,                    300, 1, 3 },
		{ BlessedEnchant,   C, N,                    150, 1, 1 },
		{ DkFlameSword,     E, ItemGrade::eUnique,   200, 1, 1 },
		{ FlameSword,       E, ItemGrade::eUnique,   150, 1, 1 },
		{ Katana,           E, ItemGrade::eEpic,     100, 1, 1 },
		{ DragonSlayer,     E, ItemGrade::eLegendary, 40, 1, 1 },
		{ ClearPotion,      C, N,                     60, 3, 5 },
	};
	constexpr DropTableEntry kFireDrake[] =
	{
		{ EnchantScroll,    C, N,                    300, 2, 4 },
		{ BlessedEnchant,   C, N,                    200, 1, 1 },
		{ DragonSlayer,     E, ItemGrade::eLegendary,120, 1, 1 },
		{ FlameSword,       E, ItemGrade::eUnique,   180, 1, 1 },
		{ DkFlameSword,     E, ItemGrade::eUnique,   100, 1, 1 },
		{ Executioner,      E, ItemGrade::eLegendary, 20, 1, 1 },
		{ ClearPotion,      C, N,                     80, 3, 5 },
	};
	constexpr DropTableEntry kBaphomet[] =
	{
		{ EnchantScroll,    C, N,                    300, 2, 4 },
		{ BlessedEnchant,   C, N,                    200, 1, 1 },
		{ Executioner,      E, ItemGrade::eLegendary, 60, 1, 1 },
		{ DragonSlayer,     E, ItemGrade::eLegendary,120, 1, 1 },
		{ DkFlameSword,     E, ItemGrade::eUnique,   120, 1, 1 },
		{ FlameSword,       E, ItemGrade::eUnique,   120, 1, 1 },
		{ ClearPotion,      C, N,                     80, 3, 5 },
	};

	template<size_t Count>
	constexpr DropTableDef MakeDef(uint32_t noDrop, const DropTableEntry (&entries)[Count]) { return { noDrop, entries, Count }; }

	// 인덱스 = 표 번호 - 1
	constexpr DropTableDef kTables[DropTable::kTableCount] =
	{
		MakeDef(700, kTier1),
		MakeDef(620, kTier2),
		MakeDef(600, kTier3),
		MakeDef(520, kTier4),
		MakeDef(480, kTier5),
		MakeDef(0,   kBossCommon),
		MakeDef(0,   kDeathKnight),
		MakeDef(0,   kFireDrake),
		MakeDef(0,   kBaphomet),
	};

	// DropTable(여기)과 ItemTable(진짜 데이터 소유자)이 서로 어긋나면
	// 조용히 잘못된 등급/타입으로 취급하는 대신 프로세스 시작 시점에 바로 크래시로
	// 드러낸다. ItemTable::Find가 constexpr이 아니라(다른 번역 단위의 배열을
	// 컴파일 타임에 참조할 수 없음) static_assert 대신 이 방식을 쓴다 — 그래도
	// main() 진입 전(정적 초기화 시점)에 실행되므로 실질적으로는 컴파일 타임 검증과
	// 다름없이 항상 걸러진다.
	bool ValidateDropTable() noexcept
	{
		for (const auto& table : kTables)
		{
			uint32_t total = table.noDropWeight;
			for (size_t i = 0; i < table.count; ++i)
			{
				const auto& entry = table.entries[i];
				const ItemTableRow* row = ItemTable::Find(entry.id);
				ASSERT_CRASH(row != nullptr);
				ASSERT_CRASH(row->type == entry.type);
				ASSERT_CRASH(row->grade == entry.grade);
				ASSERT_CRASH(entry.minCount >= 1 && entry.minCount <= entry.maxCount);
				ASSERT_CRASH(entry.maxCount <= row->maxStack);
				total += entry.weight;
			}
			ASSERT_CRASH(total > 0);
		}
		return true;
	}

	const bool GDropTableValidated = ValidateDropTable();

	std::vector<double> BuildWeights(const DropTableDef& table)
	{
		std::vector<double> weights;
		weights.reserve(table.count + 1);
		weights.push_back(static_cast<double>(table.noDropWeight));  // index 0 = 드롭 없음
		for (size_t i = 0; i < table.count; ++i)
			weights.push_back(static_cast<double>(table.entries[i].weight));
		return weights;
	}
}

DropTable::RollResult DropTable::Roll(uint8_t tableId) noexcept
{
	if (tableId == 0 || tableId > kTableCount)
		tableId = 1;
	const DropTableDef& table = kTables[tableId - 1];

	// discrete_distribution은 생성 비용이 있으므로(가중치 정규화 테이블을 미리
	// 계산) 매 호출마다 새로 만들지 않고 스레드마다 표별로 한 번만 만들어 재사용한다.
	// LRng(Core/CoreTLS.h)도 이미 스레드마다 독립적으로 시드되므로, 이 함수는
	// 몇 개의 Zone 스레드가 동시에 호출해도 서로 간섭하지 않는다.
	thread_local std::array<std::discrete_distribution<size_t>, kTableCount> LDropDists = []()
	{
		std::array<std::discrete_distribution<size_t>, kTableCount> dists;
		for (size_t i = 0; i < kTableCount; ++i)
		{
			const std::vector<double> weights = BuildWeights(kTables[i]);
			dists[i] = std::discrete_distribution<size_t>(weights.begin(), weights.end());
		}
		return dists;
	}();

	const size_t index = LDropDists[tableId - 1](LRng);
	if (index == 0)
		return {};  // 드롭 없음

	const DropTableEntry& entry = table.entries[index - 1];
	std::uniform_int_distribution<uint16_t> countRoll(entry.minCount, entry.maxCount);

	RollResult result;
	result.hasItem = true;
	result.itemId  = entry.id;
	result.count   = countRoll(LRng);
	return result;
}
