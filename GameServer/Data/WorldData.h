#pragma once

#include <cstdint>
#include <cstddef>

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
		int16_t     landX        = 0;      // 텔레포터 도착 지점
		int16_t     landY        = 0;
		uint32_t    teleportFee  = 0;      // 0 = 텔레포트 목적지가 아님(마을 지역)
	};

	// zoneId = zoneY * 4 + zoneX (ZoneLayout::GetZoneId와 동일한 배치)
	//   [ 0 서리 설원 ][ 1 고요한 숲 ][ 2 용의 협곡 ][ 3 화염 둥지 ]
	//   [ 4 요정의 숲 ][ 5 여명의 평원][ 6 황혼의 폐허][ 7 오크 요새 ]
	//   [ 8 안개 늪지 ][ 9 푸른 초원 ][10 모래바람 사막][11 붉은 황무지]
	//   [12 망자의 묘지][13 파도 해안][14 개미굴     ][15 심연의 문 ]
	inline constexpr RegionRow kRegions[kRegionCount] =
	{
		{  0, "서리 설원",     Biome::eSnow,      3, { {14, 50}, {15, 30}, { 5, 20}, { 0, 0} }, 29,  266,  268,  250,  250,  800 },
		{  1, "고요한 숲",     Biome::eForest,    2, { { 5, 35}, { 6, 30}, { 3, 20}, { 4, 15} },  0,    0,    0,  749,  251,  300 },
		{  2, "용의 협곡",     Biome::eCanyon,    4, { {24, 35}, {19, 35}, {23, 30}, { 0, 0} },  0,    0,    0, 1249,  249, 1500 },
		{  3, "화염 둥지",     Biome::eVolcanic,  5, { {25, 40}, {26, 40}, {27, 20}, { 0, 0} }, 33, 1765,  265, 1750,  250, 3000 },
		{  4, "요정의 숲",     Biome::eFairy,     2, { {11, 35}, { 4, 25}, { 7, 25}, { 6, 15} },  0,    0,    0,  249,  751,  300 },
		{  5, "여명의 평원",   Biome::eMeadow,    1, { { 1, 40}, { 2, 25}, { 4, 20}, { 3, 15} },  0,    0,    0,  749,  754,    0 },
		{  6, "황혼의 폐허",   Biome::eRuins,     3, { {18, 35}, {13, 40}, { 8, 25}, { 0, 0} },  0,    0,    0, 1249,  751,  800 },
		{  7, "오크 요새",     Biome::eOrcFort,   3, { {12, 60}, { 8, 25}, { 5, 15}, { 0, 0} }, 30, 1766,  766, 1750,  750,  800 },
		{  8, "안개 늪지",     Biome::eSwamp,     3, { {16, 45}, { 7, 30}, {18, 25}, { 0, 0} },  0,    0,    0,  249, 1249,  800 },
		{  9, "푸른 초원",     Biome::eMeadow,    1, { { 1, 35}, { 2, 30}, { 3, 20}, { 4, 15} },  0,    0,    0,  751, 1249,  100 },
		{ 10, "모래바람 사막", Biome::eDesert,    2, { { 9, 50}, { 8, 30}, { 3, 20}, { 0, 0} },  0,    0,    0, 1250, 1250,  300 },
		{ 11, "붉은 황무지",   Biome::eWasteland, 4, { {23, 40}, {19, 30}, {21, 30}, { 0, 0} },  0,    0,    0, 1750, 1250, 1500 },
		{ 12, "망자의 묘지",   Biome::eGraveyard, 4, { {20, 40}, {21, 30}, {13, 30}, { 0, 0} }, 31,  265, 1765,  250, 1750, 1500 },
		{ 13, "파도 해안",     Biome::eBeach,     2, { {10, 50}, { 7, 30}, { 9, 20}, { 0, 0} },  0,    0,    0,  750, 1750,  300 },
		{ 14, "개미굴",        Biome::eAntHill,   3, { {17, 70}, { 9, 30}, { 0, 0}, { 0, 0} }, 32, 1268, 1768, 1250, 1750,  800 },
		{ 15, "심연의 문",     Biome::eHell,      5, { {28, 40}, {27, 40}, {26, 20}, { 0, 0} }, 34, 1763, 1763, 1751, 1751, 3000 },
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
	// 마을(안전지대) — 여명의 마을. Zone 5 한가운데 44x44칸.
	// ─────────────────────────────────────────────────────────────────────────
	inline constexpr int kTownZoneId = 5;
	inline constexpr int kTownX0 = 728;
	inline constexpr int kTownY0 = 728;
	inline constexpr int kTownSize = 44;
	inline constexpr const char* kTownName = "여명의 마을";

	// 행 = y, 열 = x. 막힌 칸: # 성벽, O 망루, B 큰 건물, H 집, F 분수, T 나무,
	// L 가로등, * 꽃밭, N NPC가 서 있는 칸, S 가판대. 나머지(. = , G)는 걸을 수 있다.
	// (. 풀밭, = 길, , 광장 포석, G 성문 — 차이는 클라이언트 표시용)
	inline constexpr const char* kTownLayout[kTownSize] =
	{
		"OO##################GGGG##################OO",
		"OO..................====..................OO",
		"#.................N.====.N.................#",
		"#.T.........T.......====.......T.........T.#",
		"#..............HHHH.====.HHHH..............#",
		"#...BBBBBBBB...HHHH.====.HHHH...BBBBBBBB...#",
		"#...BBBBBBBB...HHHH.====.HHHH...BBBBBBBB...#",
		"#...BBBBBBBB...HHHH.====.HHHH...BBBBBBBB...#",
		"#...BBBBBBBB........====........BBBBBBBB...#",
		"#...BBBBBBBB........====........BBBBBBBB...#",
		"#...BBBBBBBB.....**.====.**.....BBBBBBBB...#",
		"#...BBBBBBBB.....**.====.**.....BBBBBBBB...#",
		"#......==...........====...........==......#",
		"#......==...........====...........==......#",
		"#.............L,,,,,,,,,,,,,,L.............#",
		"#..T..........,,,,,,,,,,,,,,,,..........T..#",
		"#.....T...T...,,N,,,,,,,,,,N,,...T.........#",
		"#.............,,,,,,,N,,,,,,,,.......T.....#",
		"#.............,,,,,,,,,,,,,,,,.............#",
		"#.............,,,,,,,,,,,,,,,,.............#",
		"G=============,,,,,,FFFF,,,,,,=============G",
		"G=============,,,,,,FFFF,,,,,,=============G",
		"G=============,,,,,,FFFF,,,,,,=============G",
		"G=============,,,,,,FFFF,,,,,,=============G",
		"#.............,SS,,,,,,,,,,SS,.............#",
		"#.............,SS,,,,,,,,,,SS,.............#",
		"#.............,,,,,,,,,,,,,,,,.......T.....#",
		"#..T......T...,,,,,,,,,,,,,,,,...T......T..#",
		"#.....T.......,,,,,,,,,,,,,,,,.............#",
		"#.............L,,,,,,,,,,,,,,L.............#",
		"#......==...........====...........==......#",
		"#......==...........====...........==......#",
		"#...BBBBBBBB.....**.====.**.....BBBBBBBB...#",
		"#...BBBBBBBB.....**.====.**.....BBBBBBBB...#",
		"#...BBBBBBBB........====........BBBBBBBB...#",
		"#...BBBBBBBB........====........BBBBBBBB...#",
		"#...BBBBBBBB...HHHH.====.HHHH...BBBBBBBB...#",
		"#...BBBBBBBB...HHHH.====.HHHH...BBBBBBBB...#",
		"#...BBBBBBBB...HHHH.====.HHHH...BBBBBBBB...#",
		"#..............HHHH.====.HHHH..............#",
		"#.T.........T.......====.......T.........T.#",
		"#...................====...................#",
		"OO..................====..................OO",
		"OO##################GGGG##################OO",
	};

	[[nodiscard]] constexpr bool IsInTown(int x, int y) noexcept
	{
		return x >= kTownX0 && x < kTownX0 + kTownSize && y >= kTownY0 && y < kTownY0 + kTownSize;
	}

	// 마을 칸 문자(마을 밖이면 '\0')
	[[nodiscard]] constexpr char TownCell(int x, int y) noexcept
	{
		return IsInTown(x, y) ? kTownLayout[y - kTownY0][x - kTownX0] : '\0';
	}

	[[nodiscard]] constexpr bool IsTownCellBlocked(char c) noexcept
	{
		switch (c)
		{
		case '#': case 'O': case 'B': case 'H': case 'F': case 'T': case 'L': case '*': case 'N': case 'S':
			return true;
		default:
			return false;
		}
	}

	// 안전지대: 마을 사각형 전체(성벽 포함). PvP 불가, 몬스터 진입/피해 불가, 회복 2배.
	[[nodiscard]] constexpr bool IsSafeZone(int x, int y) noexcept
	{
		return IsInTown(x, y);
	}

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

	// ── 마을 건물(클라이언트가 입체로 그리기 위한 사각형. 충돌은 레이아웃이 담당) ──
	enum class BuildingKind : uint8_t { eStore, eWeaponShop, eTemple, eInn, eHouse };

	struct BuildingRow
	{
		BuildingKind kind;
		int16_t x0, y0, x1, y1;   // 월드 좌표, 양끝 포함
		const char* sign;         // 간판
	};

	inline constexpr BuildingRow kBuildings[] =
	{
		{ BuildingKind::eStore,      kTownX0 + 4,  kTownY0 + 5,  kTownX0 + 11, kTownY0 + 11, "잡화점" },
		{ BuildingKind::eWeaponShop, kTownX0 + 32, kTownY0 + 5,  kTownX0 + 39, kTownY0 + 11, "대장간" },
		{ BuildingKind::eTemple,     kTownX0 + 4,  kTownY0 + 32, kTownX0 + 11, kTownY0 + 38, "신전" },
		{ BuildingKind::eInn,        kTownX0 + 32, kTownY0 + 32, kTownX0 + 39, kTownY0 + 38, "여관" },
		{ BuildingKind::eHouse,      kTownX0 + 15, kTownY0 + 4,  kTownX0 + 18, kTownY0 + 7,  "" },
		{ BuildingKind::eHouse,      kTownX0 + 25, kTownY0 + 4,  kTownX0 + 28, kTownY0 + 7,  "" },
		{ BuildingKind::eHouse,      kTownX0 + 15, kTownY0 + 36, kTownX0 + 18, kTownY0 + 39, "" },
		{ BuildingKind::eHouse,      kTownX0 + 25, kTownY0 + 36, kTownX0 + 28, kTownY0 + 39, "" },
	};

	// ── NPC — 서버는 "플레이어가 NPC 근처인가"만 검증하고, 모양/대사는 클라이언트 몫이다.
	// NPC는 움직이지 않으므로 서버 월드(Sector)에 오브젝트로 올리지 않는다 — 기존
	// 봇/레거시 클라이언트가 모르는 카테고리의 SUBJECT_ADD_NFY를 받을 일이 없다.
	enum class NpcKind : uint8_t { eMerchant, eWeaponSmith, eTeleporter, eGuard };

	struct NpcRow
	{
		uint16_t    id;
		NpcKind     kind;
		const char* name;
		int16_t     x, y;          // 월드 좌표(레이아웃에서 'N' 칸)
		uint8_t     facing;        // 클라 표시용: 0=남서 1=남동 2=북동 3=북서
		const char* greeting;
	};

	inline constexpr NpcRow kNpcs[] =
	{
		{ 1, NpcKind::eMerchant,    "잡화상인 그레타",  kTownX0 + 16, kTownY0 + 16, 1, "어서 오세요! 물약과 주문서가 가득하답니다." },
		{ 2, NpcKind::eWeaponSmith, "대장장이 브론",    kTownX0 + 27, kTownY0 + 16, 0, "좋은 무기는 좋은 사냥의 시작이지. 쓰던 물건도 사 주마." },
		{ 3, NpcKind::eTeleporter,  "텔레포터 루시엔",  kTownX0 + 21, kTownY0 + 17, 1, "어디로 모셔다 드릴까요? 먼 곳일수록 값이 비쌉니다." },
		{ 4, NpcKind::eGuard,       "마을 경비병",      kTownX0 + 18, kTownY0 + 2,  1, "마을 안에서는 싸움이 금지되어 있소." },
		{ 5, NpcKind::eGuard,       "마을 경비병",      kTownX0 + 25, kTownY0 + 2,  0, "성 밖은 위험하니 물약을 챙기시오." },
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

	// ── 상점: NPC별 판매 목록(아이템 id). 매입은 상인(eMerchant/eWeaponSmith) 누구나 한다.
	struct ShopRow
	{
		uint16_t npcId;
		uint16_t items[8];   // 0 = 빈 칸
	};

	inline constexpr ShopRow kShops[] =
	{
		{ 1, { 1, 8, 9, 10, 11, 12, 13, 0 } },   // 물약/주문서
		{ 2, { 2, 3, 15, 0, 0, 0, 0, 0 } },       // 무기
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

	// ── 마을 부활/귀환 지점(광장 남쪽 포석) — 여러 곳 중 무작위로 고른다.
	struct Point16 { int16_t x, y; };
	inline constexpr Point16 kTownSpawnPoints[] =
	{
		{ kTownX0 + 17, kTownY0 + 26 }, { kTownX0 + 19, kTownY0 + 27 }, { kTownX0 + 21, kTownY0 + 26 },
		{ kTownX0 + 23, kTownY0 + 27 }, { kTownX0 + 25, kTownY0 + 26 }, { kTownX0 + 20, kTownY0 + 28 },
		{ kTownX0 + 22, kTownY0 + 28 }, { kTownX0 + 24, kTownY0 + 28 }, { kTownX0 + 18, kTownY0 + 19 },
		{ kTownX0 + 25, kTownY0 + 19 },
	};
	inline constexpr size_t kTownSpawnPointCount = sizeof(kTownSpawnPoints) / sizeof(kTownSpawnPoints[0]);

	// 필드 보스 재등장 시간
	inline constexpr uint32_t kBossRespawnSeconds = 300;

	// ── 컴파일 타임 검증: 레이아웃/건물/NPC/스폰 지점이 서로 어긋나면 빌드가 실패한다.
	namespace Detail
	{
		constexpr bool ValidateTown() noexcept
		{
			for (int y = 0; y < kTownSize; ++y)
			{
				int len = 0;
				while (kTownLayout[y][len] != '\0') ++len;
				if (len != kTownSize) return false;
			}
			for (const auto& b : kBuildings)
				for (int y = b.y0; y <= b.y1; ++y)
					for (int x = b.x0; x <= b.x1; ++x)
					{
						const char c = TownCell(x, y);
						if (c != 'B' && c != 'H') return false;
					}
			for (const auto& npc : kNpcs)
				if (TownCell(npc.x, npc.y) != 'N') return false;
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
