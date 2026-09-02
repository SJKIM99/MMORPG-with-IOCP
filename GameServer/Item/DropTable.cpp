#include "pch.h"
#include "DropTable.h"

#include "ItemTable.h"
#include "CoreTLS.h"

namespace
{
	struct DropTableEntry
	{
		ItemTableId id;
		ItemType    type;    // ItemTable과 반드시 일치해야 한다(ValidateDropTable가 시작 시점에 검증) —
		                      // 여기 명시해두면 이 파일만 보고도 무슨 아이템인지 바로 알 수 있다.
		ItemGrade   grade;   // 장비가 아니면 ItemGrade::eNone. type과 마찬가지로 ItemTable과 일치해야 한다.
		uint32_t    weight;  // 상대 가중치. kNoDropWeight를 포함한 전체 합 대비 비율이 곧 확률이다.
		uint16_t    minCount;
		uint16_t    maxCount;  // inclusive
	};

	// "아무것도 안 나옴"의 가중치. 아래 kEntries의 weight 합과 더한 값이 전체
	// 표본공간이다 — 예: 700 + (150+80+50+15+4+1=300) = 1000이면 전체 드롭 확률은
	// 30%, 그 중 legendary는 1/1000 = 0.1%.
	//
	// 이 값들은 전부 예시/조정 가능한 밸런싱 데이터다 — 등급이 높을수록 weight를
	// 큰 폭(이번 표에서는 대략 3~5배씩)으로 낮춰서 "고등급일수록 현저히 희귀함"을
	// 구현했다. 새 아이템은 이 배열 끝에만 추가한다.
	constexpr uint32_t kNoDropWeight = 700;

	constexpr DropTableEntry kEntries[] =
	{
		// { id, type,               grade,                weight, minCount, maxCount }
		{ 1, ItemType::eConsumable, ItemGrade::eNone,      150,   1,  3 },  // Health Potion
		{ 2, ItemType::eEquipment,  ItemGrade::eNone,       80,   1,  1 },  // Wooden Sword
		{ 3, ItemType::eEquipment,  ItemGrade::eRare,       50,   1,  1 },  // Iron Sword
		{ 4, ItemType::eEquipment,  ItemGrade::eEpic,       15,   1,  1 },  // Steel Sword
		{ 5, ItemType::eEquipment,  ItemGrade::eUnique,      4,   1,  1 },  // Flame Sword
		{ 6, ItemType::eEquipment,  ItemGrade::eLegendary,   1,   1,  1 },  // Dragon Slayer
	};

	// DropTable(여기)과 ItemTable(진짜 데이터 소유자, ItemTable.cpp)이 서로 어긋나면
	// 조용히 잘못된 등급/타입으로 취급하는 대신 프로세스 시작 시점에 바로 크래시로
	// 드러낸다. ItemTable::Find가 constexpr이 아니라(다른 번역 단위의 배열을
	// 컴파일 타임에 참조할 수 없음) static_assert 대신 이 방식을 쓴다 — 그래도
	// main() 진입 전(정적 초기화 시점)에 실행되므로 실질적으로는 컴파일 타임 검증과
	// 다름없이 항상 걸러진다.
	bool ValidateDropTable() noexcept
	{
		for (const auto& entry : kEntries)
		{
			const ItemTableRow* row = ItemTable::Find(entry.id);
			ASSERT_CRASH(row != nullptr);
			ASSERT_CRASH(row->type == entry.type);
			ASSERT_CRASH(row->grade == entry.grade);
			ASSERT_CRASH(entry.minCount >= 1 && entry.minCount <= entry.maxCount);
			ASSERT_CRASH(entry.maxCount <= row->maxStack);
		}
		return true;
	}

	const bool GDropTableValidated = ValidateDropTable();

	std::vector<double> BuildWeights()
	{
		std::vector<double> weights;
		weights.reserve(std::size(kEntries) + 1);
		weights.push_back(static_cast<double>(kNoDropWeight));  // index 0 = 드롭 없음
		for (const auto& entry : kEntries)
			weights.push_back(static_cast<double>(entry.weight));
		return weights;
	}
}

DropTable::RollResult DropTable::Roll() noexcept
{
	// discrete_distribution은 생성 비용이 있으므로(가중치 정규화 테이블을 미리
	// 계산) 매 호출마다 새로 만들지 않고 스레드마다 한 번만 만들어 재사용한다.
	// LRng(Core/CoreTLS.h)도 이미 스레드마다 독립적으로 시드되므로, 이 함수는
	// 몇 개의 Zone 스레드가 동시에 호출해도 서로 간섭하지 않는다.
	thread_local std::discrete_distribution<size_t> LDropDist = []()
	{
		const std::vector<double> weights = BuildWeights();
		return std::discrete_distribution<size_t>(weights.begin(), weights.end());
	}();

	const size_t index = LDropDist(LRng);
	if (index == 0)
		return {};  // 드롭 없음

	const DropTableEntry& entry = kEntries[index - 1];
	std::uniform_int_distribution<uint16_t> countRoll(entry.minCount, entry.maxCount);

	RollResult result;
	result.hasItem = true;
	result.itemId  = entry.id;
	result.count   = countRoll(LRng);
	return result;
}
