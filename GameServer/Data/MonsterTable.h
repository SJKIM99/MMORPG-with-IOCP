#pragma once

#include <cstdint>
#include <cstddef>

// ─────────────────────────────────────────────────────────────────────────────
// 몬스터 종족 테이블 — 서버와 클라이언트가 같은 표를 읽는다.
//
// 종족 id는 몬스터 ObjID의 KIND(ContentID::m_KIND, uint16)에 실려 나간다.
// 그래서 SUBJECT_ADD_NFY 같은 기존 패킷 구조를 한 바이트도 바꾸지 않고도
// 클라이언트가 "이 몬스터가 무엇인지"(모양/이름/레벨/최대 HP)를 알 수 있다.
// 기존 STRESS_TEST/레거시 클라이언트는 ObjID를 통째로 비교만 하므로 영향이 없다.
//
// 규칙: 새 종족은 끝에만 추가하고, 이미 배정된 id는 바꾸거나 재사용하지 않는다.
// ─────────────────────────────────────────────────────────────────────────────

// 클라이언트가 어떤 모양으로 그릴지(절차적 드로잉 루틴) 고르는 값. 서버는 쓰지 않는다.
enum class MonsterArchetype : uint8_t
{
	eBlob,        // 젤리/슬라임
	eQuadruped,   // 늑대/곰/개
	eHumanoid,    // 고블린/오크/코볼트/좀비
	eInsect,      // 개미/전갈/거미
	eGolem,       // 골렘/거인
	eGhost,       // 정령/망령
	eDragon,      // 드레이크
	eDemon,       // 악마
	eCrab,        // 게
	eMushroom,    // 버섯
	eSkeleton,    // 해골/망령 기사
};

// V2: 몬스터 강공격 패턴 — 모두 예고(telegraph) 뒤에 판정되어 피할 수 있다.
enum class MonsterPattern : uint8_t
{
	None,     // 평타만
	Charge,   // 거리 2~4에서 직선 예고 → 돌진(선 위 대상 피해 + 둔화)
	Slam,     // 붙어 있을 때 주변 원 예고 → 내려찍기
	Ranged,   // 거리 3~5에서 대상 칸 예고 → 투척/주문
	Leap,     // 거리 2~4에서 대상 주변 원 예고 → 도약
};

enum class MonsterVariant : uint8_t
{
	eNormal,
	eScorpion,    // eInsect 변형: 집게 + 꼬리
	eSpider,      // eInsect 변형: 다리 8개
	eArmored,     // eSkeleton/eHumanoid 변형: 갑옷 + 투구
	eZombie,      // eHumanoid 변형
	eLava,        // eGolem 변형: 균열에서 빛남
	eIce,         // eGolem 변형
};

struct MonsterTableRow
{
	uint16_t         id           = 0;
	const char*      name         = "";     // UTF-8, SUBJECT_ADD_NFY.name[20]에 들어가야 하므로 19바이트 이하
	bool             aggressive   = false;  // true = AGGRO(시야에 들어오면 선공), false = PASSIVE(맞으면 반격)
	uint8_t          level        = 1;
	uint16_t         maxHp        = 60;
	uint16_t         attack       = 3;
	uint32_t         exp          = 5;
	uint32_t         adenaMin     = 0;
	uint32_t         adenaMax     = 0;
	uint8_t          adenaChance  = 0;      // %
	uint8_t          dropTable    = 0;      // DropTable 번호(서버 DropTable.cpp)
	uint8_t          dropRolls    = 1;      // 한 번 처치에 드롭을 몇 번 굴리는가(보스는 여러 번)
	uint16_t         attackIntervalMs = 1000;
	uint16_t         moveIntervalMs   = 500;
	uint8_t          leash        = 7;      // 스폰 지점에서 이 칸 수를 넘으면 추격 포기(기존 7칸)
	bool             boss         = false;
	// ── 클라이언트 표시 전용 ──
	MonsterArchetype archetype    = MonsterArchetype::eBlob;
	MonsterVariant   variant      = MonsterVariant::eNormal;
	uint32_t         color1       = 0xFFFFFF;  // 0xRRGGBB
	uint32_t         color2       = 0x000000;
	uint16_t         scale        = 100;       // 100 = 1.0배
};

namespace MonsterTable
{
	namespace Id
	{
		inline constexpr uint16_t FrostGiant   = 29;
		inline constexpr uint16_t OrcChief     = 30;
		inline constexpr uint16_t DeathKnight  = 31;
		inline constexpr uint16_t QueenAnt     = 32;
		inline constexpr uint16_t FireDrake    = 33;
		inline constexpr uint16_t Baphomet     = 34;
	}

	using A = MonsterArchetype;
	using V = MonsterVariant;

	// { id, name, aggressive, level, maxHp, attack, exp, adenaMin, adenaMax, adenaChance,
	//   dropTable, dropRolls, atkMs, moveMs, leash, boss, archetype, variant, color1, color2, scale }
	inline constexpr MonsterTableRow kRows[] =
	{
		// ── 1단계(여명의 평원/푸른 초원) ──
		{  1, "초록 젤리",   false,  1,    50,  3,     6,    5,    15,  70, 1, 1, 1000, 600,  7, false, A::eBlob,      V::eNormal,   0x6BD66B, 0x2E8B57, 90 },
		{  2, "고블린",      false,  2,    65,  4,     8,    8,    20,  75, 1, 1, 1000, 550,  7, false, A::eHumanoid,  V::eNormal,   0x7CA35A, 0x8B5A2B, 80 },
		{  3, "들개",        true,   3,    70,  5,    10,    8,    22,  70, 1, 1, 1000, 500,  7, false, A::eQuadruped, V::eNormal,   0x9C7A54, 0x5C4033, 80 },
		{  4, "버섯돌이",    false,  2,    60,  4,     8,    6,    18,  70, 1, 1, 1000, 650,  7, false, A::eMushroom,  V::eNormal,   0xD94A3A, 0xF2E3C6, 85 },
		// ── 2단계 ──
		{  5, "늑대",        true,   7,   160,  8,    24,   20,    50,  75, 2, 1, 1000, 450,  7, false, A::eQuadruped, V::eNormal,   0x8A8F99, 0x4A4E57, 100 },
		{  6, "곰",          false,  8,   220, 10,    30,   25,    60,  75, 2, 1, 1100, 550,  7, false, A::eQuadruped, V::eNormal,   0x7A5230, 0x4A2F1A, 130 },
		{  7, "푸른 젤리",   true,   6,   150,  7,    22,   20,    45,  75, 2, 1, 1000, 550,  7, false, A::eBlob,      V::eNormal,   0x5AA9F0, 0x2B5FA0, 95 },
		{  8, "코볼트",      true,   9,   190,  9,    28,   25,    60,  80, 2, 1, 1000, 500,  7, false, A::eHumanoid,  V::eNormal,   0xB5653A, 0x5A3D2B, 85 },
		{  9, "모래 전갈",   true,   8,   180,  9,    27,   25,    55,  75, 2, 1, 1000, 500,  7, false, A::eInsect,    V::eScorpion, 0xD9B26A, 0x8C6A2F, 95 },
		{ 10, "거대 게",     false,  7,   200,  8,    25,   20,    55,  75, 2, 1, 1100, 600,  7, false, A::eCrab,      V::eNormal,   0xE0603A, 0x9A2F1C, 105 },
		{ 11, "숲의 정령",   false,  6,   140,  7,    22,   20,    50,  75, 2, 1, 1000, 550,  7, false, A::eGhost,     V::eNormal,   0x9BF2B0, 0x3FB67A, 90 },
		// ── 3단계 ──
		{ 12, "오크 전사",   true,  14,   380, 14,    60,   60,   140,  80, 3, 1, 1000, 500,  7, false, A::eHumanoid,  V::eArmored,  0x5E8C3A, 0x6B4A2E, 115 },
		{ 13, "해골 병사",   true,  13,   340, 13,    55,   50,   130,  80, 3, 1, 1000, 500,  7, false, A::eSkeleton,  V::eNormal,   0xE8E0C8, 0x7A6F5A, 100 },
		{ 14, "설원 늑대",   true,  15,   360, 15,    62,   60,   140,  80, 3, 1, 1000, 450,  7, false, A::eQuadruped, V::eNormal,   0xEEF3F8, 0x9FB3C8, 105 },
		{ 15, "얼음 골렘",   false, 16,   480, 16,    70,   70,   160,  80, 3, 1, 1200, 650,  7, false, A::eGolem,     V::eIce,      0xA8D8F0, 0x5A8AB0, 125 },
		{ 16, "늪 괴물",     true,  13,   400, 12,    56,   50,   130,  80, 3, 1, 1000, 550,  7, false, A::eBlob,      V::eNormal,   0x5A6B3A, 0x2F3A1F, 120 },
		{ 17, "거대 개미",   true,  15,   350, 15,    60,   55,   140,  80, 3, 1, 1000, 450,  7, false, A::eInsect,    V::eNormal,   0x8A2E1F, 0x3D140D, 100 },
		{ 18, "폐허의 망령", true,  16,   330, 17,    66,   60,   150,  80, 3, 1, 1000, 500,  7, false, A::eGhost,     V::eNormal,   0xB08BE0, 0x5A3A8A, 105 },
		// ── 4단계 ──
		{ 19, "스톤 골렘",   true,  22,   800, 22,   115,  150,   300,  85, 4, 1, 1200, 650,  7, false, A::eGolem,     V::eNormal,   0x9A968C, 0x5C5850, 135 },
		{ 20, "좀비",        true,  20,   650, 20,   100,  130,   260,  85, 4, 1, 1100, 650,  7, false, A::eHumanoid,  V::eZombie,   0x8FA37A, 0x4A5A3A, 105 },
		{ 21, "망령 기사",   true,  24,   760, 25,   130,  160,   320,  85, 4, 1, 1000, 500,  7, false, A::eSkeleton,  V::eArmored,  0x3A3F4A, 0x9FB0C8, 115 },
		{ 22, "거대 거미",   true,  23,   700, 24,   125,  150,   300,  85, 4, 1, 1000, 450,  7, false, A::eInsect,    V::eSpider,   0x3A2A4A, 0x7A4AA0, 115 },
		{ 23, "붉은 전갈",   true,  22,   720, 23,   120,  150,   300,  85, 4, 1, 1000, 500,  7, false, A::eInsect,    V::eScorpion, 0xC0392B, 0x6A1A12, 110 },
		{ 24, "새끼 드레이크", true, 25,  780, 26,   140,  170,   340,  85, 4, 1, 1000, 500,  7, false, A::eDragon,    V::eNormal,   0xD9642A, 0x7A2E12, 90 },
		// ── 5단계 ──
		{ 25, "드레이크",    true,  30,  1500, 36,   250,  350,   650,  90, 5, 1, 1000, 500,  7, false, A::eDragon,    V::eNormal,   0xB0302A, 0x5A1410, 130 },
		{ 26, "화염 골렘",   true,  29,  1600, 34,   240,  330,   620,  90, 5, 1, 1200, 650,  7, false, A::eGolem,     V::eLava,     0x4A2A1A, 0xFF7A1A, 140 },
		{ 27, "지옥견",      true,  28,  1250, 33,   220,  300,   600,  90, 5, 1, 1000, 400,  7, false, A::eQuadruped, V::eNormal,   0x2A1A1A, 0xE0401A, 115 },
		{ 28, "악마 전사",   true,  31,  1450, 38,   260,  360,   680,  90, 5, 1, 1000, 500,  7, false, A::eDemon,     V::eNormal,   0x7A1F2A, 0x2A0A10, 125 },
		// ── 필드 보스 ──
		{ 29, "서리 거인",   true,  22, 12000, 40,  3000, 3000,  6000, 100, 6, 4, 1500, 700, 12, true,  A::eGolem,     V::eIce,      0xCFEFFF, 0x6FA8D0, 240 },
		{ 30, "오크 대족장", true,  20, 10000, 36,  2500, 2500,  5000, 100, 6, 4, 1400, 600, 12, true,  A::eHumanoid,  V::eArmored,  0x4F7A2E, 0xB08A3A, 200 },
		{ 31, "데스나이트",  true,  30, 22000, 55,  7000, 8000, 15000, 100, 7, 5, 1400, 550, 12, true,  A::eSkeleton,  V::eArmored,  0x1F2229, 0xFF5A1F, 190 },
		{ 32, "여왕 개미",   true,  24, 15000, 45,  4500, 4000,  8000, 100, 6, 4, 1500, 650, 12, true,  A::eInsect,    V::eNormal,   0xA0301F, 0xF0C040, 230 },
		{ 33, "화염 드레이크", true, 36, 30000, 70, 12000, 12000, 25000, 100, 8, 6, 1500, 600, 12, true, A::eDragon,    V::eNormal,   0xE0501A, 0xFFD040, 260 },
		{ 34, "바포메트",    true,  40, 35000, 80, 15000, 15000, 30000, 100, 9, 6, 1500, 600, 12, true,  A::eDemon,     V::eNormal,   0x3A1A2A, 0xC02030, 250 },
	};

	inline constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

	[[nodiscard]] constexpr const MonsterTableRow* Find(uint16_t id) noexcept
	{
		// id가 1부터 연속이라 인덱스로 바로 찾을 수 있지만, 누군가 중간을 비우는
		// 실수를 해도 틀린 행을 돌려주지 않도록 id를 확인한다.
		if (id >= 1 && id <= kRowCount && kRows[id - 1].id == id)
			return &kRows[id - 1];
		for (const auto& row : kRows)
			if (row.id == id)
				return &row;
		return nullptr;
	}

	namespace Detail
	{
		constexpr size_t StrLen(const char* s) noexcept
		{
			size_t n = 0;
			while (s[n] != '\0') ++n;
			return n;
		}

		constexpr bool Validate() noexcept
		{
			for (size_t i = 0; i < kRowCount; ++i)
			{
				const auto& r = kRows[i];
				if (r.id != i + 1) return false;                  // 1부터 연속
				if (StrLen(r.name) == 0 || StrLen(r.name) > 19) return false;  // name[20] (널 포함)
				if (r.maxHp == 0 || r.attack == 0) return false;
				if (r.adenaMin > r.adenaMax || r.adenaMax > 65535) return false;  // 필드 아이템 수량은 uint16
				if (r.dropRolls == 0) return false;
			}
			return true;
		}
	}

	static_assert(Detail::Validate(), "MonsterTable: id는 1부터 연속, 이름은 19바이트 이하여야 합니다.");

	// 종족 → 패턴. 초반 몬스터(초록 젤리·고블린)는 평타만 — 처음엔 쉽게, 뒤로 갈수록 읽어야 한다.
	[[nodiscard]] constexpr MonsterPattern PatternOf(const MonsterTableRow& row) noexcept
	{
		if (row.boss)
			return MonsterPattern::None;   // 보스는 전용 광역기 루프
		switch (row.id)
		{
		case 1: case 2: case 13: case 20: return MonsterPattern::None;
		case 7: return MonsterPattern::Leap;        // 푸른 젤리: 통통 튀어 덮친다
		case 8: return MonsterPattern::Ranged;      // 코볼트: 창 던지기
		case 12: return MonsterPattern::Slam;       // 오크 전사
		default: break;
		}
		switch (row.archetype)
		{
		case MonsterArchetype::eQuadruped: return MonsterPattern::Charge;
		case MonsterArchetype::eGolem:     return MonsterPattern::Slam;
		case MonsterArchetype::eDemon:     return MonsterPattern::Slam;
		case MonsterArchetype::eMushroom:  return MonsterPattern::Slam;
		case MonsterArchetype::eInsect:    return MonsterPattern::Leap;
		case MonsterArchetype::eDragon:    return MonsterPattern::Ranged;
		case MonsterArchetype::eGhost:     return MonsterPattern::Ranged;
		default:                           return MonsterPattern::None;
		}
	}
}
