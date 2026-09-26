#pragma once

#include <cstdint>
#include <cstddef>

#include "TownMap.h"   // 마을 칸 · 층 · 건물 · 이름 있는 자리(생성물 — tools/mapgen/gen_town.py)

// ─────────────────────────────────────────────────────────────────────────────
// 월드 데이터 — 지역(=Zone) 테마/난이도, 마을 레이아웃, NPC, 상점, 텔레포트, 충돌.
//
// 서버와 클라이언트가 같은 파일을 읽는다. 특히 충돌 판정(IsBlocked)을 한 곳에서
// 정의해서 "클라에선 길인데 서버에선 벽" 같은 불일치가 구조적으로 생기지 않게 했다.
// Windows/서버 전용 헤더에 의존하지 않는다(<cstdint>만 사용).
//
// 설계 메모: 서버의 Zone(스레드 분할 단위, 500x500칸)을 그대로 "지역"으로 삼았다.
// 존 경계에서 시야가 끊기는 기존 구조(Sector 격자가 Zone마다 따로 있음)가 게임적으로는
// "다른 지역으로 넘어간다"는 자연스러운 연출이 된다 — 새 경계를 만들 필요가 없다.
// ─────────────────────────────────────────────────────────────────────────────
namespace WorldData
{
	// Protocol.h의 W_WIDTH/W_HEIGHT, ZoneLayout의 Zone 크기(50섹터 x 10칸)와 같아야 한다.
	// 서버 쪽 Collision.cpp에서 static_assert로 일치를 확인한다.
	inline constexpr int kWorldWidth  = 2000;
	inline constexpr int kWorldHeight = 2000;
	inline constexpr int kRegionSize  = 500;
	inline constexpr int kRegionCountX = kWorldWidth / kRegionSize;   // 4
	inline constexpr int kRegionCount  = kRegionCountX * (kWorldHeight / kRegionSize);  // 16

	enum class Biome : uint8_t
	{
		eMeadow, eForest, eSnow, eCanyon, eVolcanic, eFairy, eRuins, eOrcFort,
		eSwamp, eDesert, eWasteland, eGraveyard, eBeach, eAntHill, eHell,
	};

	struct RegionSpawn
	{
		uint16_t species = 0;  // MonsterTable id
		uint8_t  weight  = 0;
	};

	struct RegionRow
	{
		uint16_t    zoneId       = 0;
		const char* name         = "";
		Biome       biome        = Biome::eMeadow;
		uint8_t     tier         = 1;      // 1~5 (★ 개수)
		RegionSpawn spawns[4]    = {};
		uint16_t    bossSpecies  = 0;      // 0 = 필드 보스 없음
		int16_t     bossX        = 0;
		int16_t     bossY        = 0;
		int16_t     landX        = 0;      // 길잡이 도착 지점
		int16_t     landY        = 0;
		uint32_t    teleportFee  = 0;      // 0 = 텔레포트 목적지가 아님(마을 지역)
	};

	// zoneId = zoneY * 4 + zoneX (ZoneLayout::GetZoneId와 동일한 배치)
	//   [ 0 벨고라 설원 ][ 1 올벤 숲     ][ 2 크라스나 협곡][ 3 바트라 화산 ]
	//   [ 4 이레린 숲   ][ 5 데니차 들녘 ][ 6 캄파나 폐허 ][ 7 그락 요새   ]
	//   [ 8 모크라 늪지 ][ 9 야스나 초원 ][10 아스린 사막 ][11 가르 황무지 ]
	//   [12 서약의 언덕 ][13 베레그 해안 ][14 굴개미 둔덕 ][15 칼둔 균열   ]
	//   이름의 유래와 언어 규칙은 docs/WORLD.md — 새 지역도 그 규칙으로 짓는다.
	inline constexpr RegionRow kRegions[kRegionCount] =
	{
		{  0, "벨고라 설원",   Biome::eSnow,      3, { {14, 50}, {15, 30}, { 5, 20}, { 0, 0} }, 29,  266,  268,  250,  250,  800 },
		{  1, "올벤 숲",       Biome::eForest,    2, { { 5, 35}, { 6, 30}, { 3, 20}, { 4, 15} },  0,    0,    0,  749,  251,  300 },
		{  2, "크라스나 협곡", Biome::eCanyon,    4, { {24, 35}, {19, 35}, {23, 30}, { 0, 0} },  0,    0,    0, 1249,  249, 1500 },
		{  3, "바트라 화산",   Biome::eVolcanic,  5, { {25, 40}, {26, 40}, {27, 20}, { 0, 0} }, 33, 1765,  265, 1750,  250, 3000 },
		{  4, "이레린 숲",     Biome::eFairy,     2, { {11, 35}, { 4, 25}, { 7, 25}, { 6, 15} },  0,    0,    0,  249,  751,  300 },
		{  5, "데니차 들녘",   Biome::eMeadow,    1, { { 1, 40}, { 2, 25}, { 4, 20}, { 3, 15} },  0,    0,    0,  749,  754,    0 },
		{  6, "캄파나 폐허",   Biome::eRuins,     3, { {18, 35}, {13, 40}, { 8, 25}, { 0, 0} },  0,    0,    0, 1249,  751,  800 },
		{  7, "그락 요새",     Biome::eOrcFort,   3, { {12, 60}, { 8, 25}, { 5, 15}, { 0, 0} }, 30, 1766,  766, 1750,  750,  800 },
		{  8, "모크라 늪지",   Biome::eSwamp,     3, { {16, 45}, { 7, 30}, {18, 25}, { 0, 0} },  0,    0,    0,  249, 1249,  800 },
		{  9, "야스나 초원",   Biome::eMeadow,    1, { { 1, 35}, { 2, 30}, { 3, 20}, { 4, 15} },  0,    0,    0,  751, 1249,  100 },
		{ 10, "아스린 사막",   Biome::eDesert,    2, { { 9, 50}, { 8, 30}, { 3, 20}, { 0, 0} },  0,    0,    0, 1250, 1250,  300 },
		{ 11, "가르 황무지",   Biome::eWasteland, 4, { {23, 40}, {19, 30}, {21, 30}, { 0, 0} },  0,    0,    0, 1750, 1250, 1500 },
		{ 12, "서약의 언덕",   Biome::eGraveyard, 4, { {20, 40}, {21, 30}, {13, 30}, { 0, 0} }, 31,  265, 1765,  250, 1750, 1500 },
		{ 13, "베레그 해안",   Biome::eBeach,     2, { {10, 50}, { 7, 30}, { 9, 20}, { 0, 0} },  0,    0,    0,  750, 1750,  300 },
		{ 14, "굴개미 둔덕",   Biome::eAntHill,   3, { {17, 70}, { 9, 30}, { 0, 0}, { 0, 0} }, 32, 1268, 1768, 1250, 1750,  800 },
		{ 15, "칼둔 균열",     Biome::eHell,      5, { {28, 40}, {27, 40}, {26, 20}, { 0, 0} }, 34, 1763, 1763, 1751, 1751, 3000 },
	};

	[[nodiscard]] constexpr int RegionIdAt(int x, int y) noexcept
	{
		if (x < 0 || y < 0 || x >= kWorldWidth || y >= kWorldHeight)
			return -1;
		return (y / kRegionSize) * kRegionCountX + (x / kRegionSize);
	}

	[[nodiscard]] constexpr const RegionRow* FindRegion(int zoneId) noexcept
	{
		return (zoneId >= 0 && zoneId < kRegionCount) ? &kRegions[zoneId] : nullptr;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// 마을(안전지대) — 데니차(변경말로 '샛별'). 칸 · 층 · 건물은 TownMap.h(생성물 —
	// 원본 tools/mapgen/denica/*.txt, 생성기 tools/mapgen/gen_town.py). Phase 4-A.
	// ─────────────────────────────────────────────────────────────────────────
	inline constexpr int kTownZoneId = TownMap::kZoneId;
	inline constexpr int kTownX0 = TownMap::kX0;
	inline constexpr int kTownY0 = TownMap::kY0;
	inline constexpr int kTownW = TownMap::kW;
	inline constexpr int kTownH = TownMap::kH;
	inline constexpr const char* kTownName = TownMap::kName;

	[[nodiscard]] constexpr bool IsInTown(int x, int y) noexcept
	{
		return x >= kTownX0 && x < kTownX0 + kTownW && y >= kTownY0 && y < kTownY0 + kTownH;
	}

	// 마을 칸 문자(마을 밖이면 '\0') — 글자 뜻은 tools/mapgen/gen_town.py의 LEGEND
	[[nodiscard]] constexpr char TownCell(int x, int y) noexcept
	{
		return IsInTown(x, y) ? TownMap::kGround[y - kTownY0][x - kTownX0] : '\0';
	}

	// 마을 칸의 층(0~4, 계단은 낮은 쪽 층). 마을 밖은 0(들판)
	[[nodiscard]] constexpr int TownLevel(int x, int y) noexcept
	{
		return IsInTown(x, y) ? TownMap::kLevel[y - kTownY0][x - kTownX0] - '0' : 0;
	}

	// 계단 문자 → 오르는 방향(없으면 false)
	[[nodiscard]] constexpr bool StairsDir(char c, int& dx, int& dy) noexcept
	{
		switch (c)
		{
		case '^': dx = 0; dy = -1; return true;
		case 'v': dx = 0; dy = 1; return true;
		case '<': dx = -1; dy = 0; return true;
		case '>': dx = 1; dy = 0; return true;
		default: return false;
		}
	}

	[[nodiscard]] constexpr bool IsTownCellBlocked(char c) noexcept
	{
		switch (c)
		{
		case '.': case '=': case ',': case ':': case 's': case 'G': case 'b': case 'g': case 'N':
		case '^': case 'v': case '<': case '>':
			return false;   // 풀밭 · 길 · 광장 · 흙길 · 모래 · 성문 · 다리 · 묘지 풀 · 계단
		default:
			return true;    // 성벽 · 망루 · 건물 · 분수 · 나무 · 가로등 · 꽃밭 · 가판대 · 가장자리(R) · 물 · 바위 …
		}
	}

	// 안전지대: 마을 사각형 전체(성벽 포함). PvP 불가, 몬스터 진입/피해 불가, 회복 2배.
	[[nodiscard]] constexpr bool IsSafeZone(int x, int y) noexcept
	{
		return IsInTown(x, y);
	}

	// ── Phase 2-E 훈련장(마을 동쪽 풀밭) — 안전지대 안이지만 여기서는 기술을 쓸 수 있다(맞는 것은 허수아비뿐:
	//    몬스터는 마을에 들어오지 못하고, 안전지대의 사람은 강제 공격으로도 맞지 않는다). Phase 4에서 훈련장 건물로 옮긴다.
	inline constexpr int kYardX0 = TownMap::kYardX0, kYardY0 = TownMap::kYardY0, kYardX1 = TownMap::kYardX1, kYardY1 = TownMap::kYardY1;   // 포함
	[[nodiscard]] constexpr bool IsTrainingYard(int x, int y) noexcept
	{
		return x >= kYardX0 && x <= kYardX1 && y >= kYardY0 && y <= kYardY1;
	}
	using DummySpot = TownMap::Point16;
	inline constexpr auto& kDummySpots = TownMap::kDummies;
	inline constexpr int kDummyCount = static_cast<int>(sizeof(kDummySpots) / sizeof(kDummySpots[0]));

	// 기존 서버의 장애물 패턴(Collision.cpp의 checkCollision과 동일한 식).
	// 마을 밖 월드는 이 패턴을 그대로 유지한다 — 기존 A*/몬스터 이동/저장 좌표와의 호환.
	[[nodiscard]] constexpr bool LegacyPatternBlocked(int x, int y) noexcept
	{
		if ((x / 3 + y / 3) % 3 == 0) return false;
		if ((x / 3 + y / 3) % 3 == 1) return false;
		return (x / 2 + y / 2) % 3 == 0;
	}

	// 서버/클라 공용 충돌 판정. (x, y) = (열, 행) — 월드 밖은 막힘.
	[[nodiscard]] constexpr bool IsBlocked(int x, int y) noexcept
	{
		if (x < 0 || y < 0 || x >= kWorldWidth || y >= kWorldHeight)
			return true;
		if (IsInTown(x, y))
			return IsTownCellBlocked(TownCell(x, y));
		return LegacyPatternBlocked(x, y);
	}

	namespace Detail
	{
		constexpr bool ValidateYard() noexcept
		{
			for (const auto& d : kDummySpots)
				if (!IsTrainingYard(d.x, d.y) || IsBlocked(d.x, d.y) || !IsSafeZone(d.x, d.y)) return false;
			return true;
		}
	}
	static_assert(Detail::ValidateYard(), "허수아비 자리는 훈련장 안의 걸을 수 있는 칸이어야 합니다.");

	// ── 마을 건물(클라이언트가 입체로 그리기 위한 사각형. 충돌은 칸 문자 'B'가 담당) ──
	using BuildingKind = TownMap::BuildingKind;
	using BuildingRow = TownMap::BuildingRow;
	inline constexpr auto& kBuildings = TownMap::kBuildings;

	// ── NPC ─────────────────────────────────────────────────────────────────
	// V2: 마을 NPC는 서버가 배회시킨다(Npc/NpcHelper — 마을 Zone 스레드, 확장 세션에만
	// NPC_* 패킷). 기존 봇/레거시 클라이언트가 모르는 카테고리의 SUBJECT_ADD_NFY를 받지
	// 않도록 Sector의 오브젝트로 올리지는 않는다. 여기의 x, y는 "집"(배회의 중심).
	enum class NpcKind : uint8_t
	{
		eMerchant, eWeaponSmith, eTeleporter, eGuard,
		// V2 — 살아 있는 마을
		eElder,       // 촌장(이야기의 시작)
		ePriest,      // 사제(예배당 — 치유의 기도)
		eInnkeeper,   // 여관 주인(수프·소문)
		eBard,        // 음유시인(노래)
		eChild,       // 뛰노는 아이
		eVillager,    // 주민
		eFarmer,      // 농부
		eDog, eCat, eChicken,   // 동물(쓰다듬기)
	};

	[[nodiscard]] constexpr bool IsAnimal(NpcKind k) noexcept
	{
		return k == NpcKind::eDog || k == NpcKind::eCat || k == NpcKind::eChicken;
	}

	// NPC 서비스(NPC_SERVICE_REQ)
	enum class NpcService : uint8_t { None, Heal, Soup };

	struct NpcRow
	{
		uint16_t    id;
		NpcKind     kind;
		const char* name;
		int16_t     x, y;          // 집(배회의 중심) — 걸을 수 있는 칸
		uint8_t     facing;        // 클라 표시용: 0=남서 1=남동 2=북동 3=북서
		const char* greeting;
		uint8_t     wander = 0;    // 배회 반경(칸). 0 = 제자리(가끔 돌아본다)
		uint16_t    stepMs = 650;  // 한 칸 걷는 시간(ms)
		uint32_t    color = 0;     // 옷 색(클라, 0 = 종류 기본)
	};

	inline constexpr NpcRow kNpcs[] =
	{
		// 이름 = 직함 + 변경말 이름(docs/WORLD.md 3.2). 길잡이 마테오만 남부 출신이다.
		{ 1, NpcKind::eMerchant,    "잡화상 라다",      kTownX0 + 16, kTownY0 + 16, 1, "어서 오세요. 물약도 두루마리도 오늘 들어온 거예요.", 1, 900 },
		{ 2, NpcKind::eWeaponSmith, "대장장이 도브란",  kTownX0 + 27, kTownY0 + 16, 0, "좋은 무기가 좋은 사냥을 만들지. 쓰던 물건도 값을 쳐 주겠네.", 1, 900 },
		{ 3, NpcKind::eTeleporter,  "길잡이 마테오",    kTownX0 + 21, kTownY0 + 17, 1, "어디로 가시겠어요? 옛길은 멀수록 값이 오릅니다.", 1, 900 },
		{ 4, NpcKind::eGuard,       "경비대장 야로",    kTownX0 + 18, kTownY0 + 2,  1, "성벽 안에서 칼을 뽑는 자는 내가 상대하겠소." },
		{ 5, NpcKind::eGuard,       "경비병 브란코",    kTownX0 + 25, kTownY0 + 2,  0, "성 밖은 위험하오. 물약을 챙기시오." },
		// ── V2: 살아 있는 마을 ──
		{ 6,  NpcKind::eElder,     "촌장 고란",        kTownX0 + 18, kTownY0 + 19, 1, "어서 오게, 젊은이. 요즘은 새벽이 늦어도 너무 늦는다네.", 3, 1100 },
		{ 7,  NpcKind::ePriest,    "사제 밀레나",      kTownX0 + 10, kTownY0 + 30, 1, "새벽이 함께하기를. 다친 곳이 있으면 기도를 올려 드릴게요.", 2, 900 },
		{ 8,  NpcKind::eInnkeeper, "여관 주인 베스나", kTownX0 + 33, kTownY0 + 30, 0, "따뜻한 수프 한 그릇 어때요? 30 그로시면 속이 든든해요.", 2, 800 },
		{ 9,  NpcKind::eBard,      "음유시인 보얀",    kTownX0 + 26, kTownY0 + 22, 0, "서약 기사단의 마지막 노래를 들어 보시겠소?", 1, 1000 },
		{ 10, NpcKind::eChild,     "꼬마 미샤",        kTownX0 + 22, kTownY0 + 26, 1, "형도 모험가야? 나도 크면 서약기사 될 거야!", 6, 380, 0x3A7AD8 },
		{ 11, NpcKind::eChild,     "꼬마 안카",        kTownX0 + 19, kTownY0 + 25, 0, "쉿! 지금 미샤랑 숨바꼭질하는 중이야.", 6, 400, 0xD85A7A },
		{ 12, NpcKind::eVillager,  "마르파 할멈",      kTownX0 + 20, kTownY0 + 8,  1, "아이고, 젊은이. 이 늙은이 말 좀 들어 보겠수?", 3, 1300, 0x6A5A7A },
		{ 13, NpcKind::eFarmer,    "농부 다닐로",      kTownX0 + 6,  kTownY0 + 21, 1, "허허, 닭 좀 봐 주시오. 자꾸 도망가서 말이오.", 4, 800, 0x7A6A3A },
		{ 14, NpcKind::eGuard,     "경비병 페타르",    kTownX0 + 18, kTownY0 + 41, 3, "성문 밖 들녘엔 수액덩이와 그락 약탈꾼이 많소. 처음이라면 거기서 시작하시오." },
		{ 15, NpcKind::eGuard,     "경비병 드라고",    kTownX0 + 25, kTownY0 + 41, 2, "짐승 몸이 붉게 달아오르면 강공격이오. 바닥을 보고 피하시오." },
		{ 16, NpcKind::eGuard,     "순찰병 일리야",    kTownX0 + 21, kTownY0 + 12, 1, "순찰 중이오. 무슨 일이오?", 9, 700 },
		{ 17, NpcKind::eDog,       "누렁이",           kTownX0 + 23, kTownY0 + 27, 1, "멍!", 10, 300 },
		{ 18, NpcKind::eCat,       "나비",             kTownX0 + 30, kTownY0 + 31, 0, "야옹~", 2, 700 },
		{ 19, NpcKind::eChicken,   "닭",               kTownX0 + 4,  kTownY0 + 18, 1, "꼬꼬!", 3, 450 },
		{ 20, NpcKind::eChicken,   "닭",               kTownX0 + 6,  kTownY0 + 17, 0, "꼬꼬댁!", 3, 450 },
	};
	inline constexpr size_t kNpcCount = sizeof(kNpcs) / sizeof(kNpcs[0]);

	// NPC와 상호작용할 수 있는 거리(체비셰프). 클라이언트 표시값이 아니라 서버 판정 기준이다.
	inline constexpr int kNpcInteractRange = 3;

	[[nodiscard]] constexpr const NpcRow* FindNpc(uint16_t id) noexcept
	{
		for (const auto& npc : kNpcs)
			if (npc.id == id)
				return &npc;
		return nullptr;
	}

	[[nodiscard]] constexpr bool IsNearNpc(const NpcRow& npc, int x, int y) noexcept
	{
		const int dx = x - npc.x;
		const int dy = y - npc.y;
		return (dx < 0 ? -dx : dx) <= kNpcInteractRange && (dy < 0 ? -dy : dy) <= kNpcInteractRange;
	}

	namespace Detail
	{
		constexpr bool ValidateNpcs() noexcept
		{
			for (size_t i = 0; i < kNpcCount; ++i)
			{
				if (!IsInTown(kNpcs[i].x, kNpcs[i].y) || IsBlocked(kNpcs[i].x, kNpcs[i].y)) return false;
				for (size_t j = i + 1; j < kNpcCount; ++j)
					if (kNpcs[i].id == kNpcs[j].id) return false;
			}
			return kNpcCount <= 32;
		}
	}
	static_assert(Detail::ValidateNpcs(), "WorldData::kNpcs: NPC 집은 마을 안의 걸을 수 있는 칸이어야 하고 id는 겹치면 안 됩니다(최대 32).");

	// ── 상점: NPC별 판매 목록(아이템 id). 매입은 상인(eMerchant/eWeaponSmith) 누구나 한다.
	struct ShopRow
	{
		uint16_t npcId;
		uint16_t items[12];  // 0 = 빈 칸
	};

	inline constexpr ShopRow kShops[] =
	{
		{ 1, { 1, 8, 9, 10, 11, 12, 13, 0 } },                    // 물약/두루마리
		// 대장장이: 무기(직업별 초보자·직업 무기) + 기본 방어구(V2 장비 부위)
		{ 2, { 2, 19, 20, 3, 15, 21, 25, 29, 32, 37, 39, 41 } },
	};

	[[nodiscard]] constexpr bool ShopSells(uint16_t npcId, uint16_t itemId) noexcept
	{
		if (itemId == 0)
			return false;
		for (const auto& shop : kShops)
		{
			if (shop.npcId != npcId)
				continue;
			for (uint16_t id : shop.items)
				if (id == itemId)
					return true;
		}
		return false;
	}

	[[nodiscard]] constexpr bool NpcBuysItems(const NpcRow& npc) noexcept
	{
		return npc.kind == NpcKind::eMerchant || npc.kind == NpcKind::eWeaponSmith;
	}

	// ── 마을 부활/귀환 지점 — 여러 곳 중 무작위로 고른다(TownMap anchors.txt의 spawn).
	using Point16 = TownMap::Point16;
	inline constexpr auto& kTownSpawnPoints = TownMap::kSpawns;
	inline constexpr size_t kTownSpawnPointCount = sizeof(TownMap::kSpawns) / sizeof(TownMap::kSpawns[0]);

	// 필드 보스 재등장 시간
	inline constexpr uint32_t kBossRespawnSeconds = 300;

	// ── 컴파일 타임 검증: 레이아웃/건물/NPC/스폰 지점이 서로 어긋나면 빌드가 실패한다.
	namespace Detail
	{
		constexpr bool ValidateTown() noexcept
		{
			// 칸 · 층 격자의 줄 길이(생성기가 이미 검사한다 — 손으로 고친 TownMap.h를 막는다)
			for (int y = 0; y < kTownH; ++y)
			{
				int a = 0, b = 0;
				while (TownMap::kGround[y][a] != '\0') ++a;
				while (TownMap::kLevel[y][b] != '\0') ++b;
				if (a != kTownW || b != kTownW) return false;
			}
			// 건물 발자국은 모두 막힌 'B' 칸
			for (const auto& b : kBuildings)
			{
				if (!IsInTown(b.x0, b.y0) || !IsInTown(b.x1, b.y1)) return false;
				for (int y = b.y0; y <= b.y1; ++y)
					for (int x = b.x0; x <= b.x1; ++x)
						if (TownCell(x, y) != 'B') return false;
			}
			// NPC 집 칸은 ValidateNpcs(걸을 수 있는 마을 칸)가 따로 검사한다.
			// 칸 사이 층 규칙(절벽 · 계단)은 생성기가 모든 칸을 검사하고, 서버가 켤 때 한 번 더 센다(Collision.cpp).
			for (const auto& p : kTownSpawnPoints)
				if (IsBlocked(p.x, p.y) || !IsInTown(p.x, p.y)) return false;
			return true;
		}

		constexpr bool ValidateRegions() noexcept
		{
			for (int i = 0; i < kRegionCount; ++i)
			{
				const auto& r = kRegions[i];
				if (r.zoneId != i) return false;
				if (RegionIdAt(r.landX, r.landY) != i || IsBlocked(r.landX, r.landY)) return false;
				if (r.bossSpecies != 0 && (RegionIdAt(r.bossX, r.bossY) != i || IsBlocked(r.bossX, r.bossY))) return false;
				int total = 0;
				for (const auto& s : r.spawns) total += s.weight;
				if (total == 0) return false;
			}
			return true;
		}
	}

	static_assert(Detail::ValidateTown(), "WorldData: 마을 레이아웃/건물/NPC/스폰 지점이 서로 맞지 않습니다.");
	static_assert(Detail::ValidateRegions(), "WorldData: 지역 데이터(도착 지점/보스 위치/스폰 가중치)가 잘못되었습니다.");
}
