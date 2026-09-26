#include "pch.h"
#include "Collision.h"
#include "Data/WorldData.h"
#include "Data/TownRules.h"
#include "Zone/ZoneLayout.h"

array<array<bool, W_HEIGHT>, W_WIDTH> GCollision;

// 충돌 규칙의 원본은 Data/WorldData.h(서버·클라 공용)다. 두 쪽이 같은 식을 쓰므로
// "클라에선 길인데 서버에선 벽" 같은 불일치가 생기지 않는다.
static_assert(WorldData::kWorldWidth == W_WIDTH && WorldData::kWorldHeight == W_HEIGHT,
	"WorldData의 월드 크기가 Protocol.h와 다릅니다.");
static_assert(WorldData::kRegionSize == ZoneLayout::SectorsPerZoneX * SECTOR_RANGE
	&& WorldData::kRegionSize == ZoneLayout::SectorsPerZoneY * SECTOR_RANGE,
	"WorldData의 지역 크기가 Zone 크기와 다릅니다.");
static_assert(WorldData::kRegionCount == ZoneLayout::ZoneCount, "지역 수와 Zone 수가 다릅니다.");

bool checkCollision(short x_pos, short y_pos)
{
    // 기존 동작(월드 밖 = false)을 유지한다 — 이 함수는 격자를 채울 때만 쓰인다.
    if (x_pos < 0 || y_pos < 0 || x_pos >= W_WIDTH || y_pos >= W_HEIGHT) return false;
    return WorldData::IsBlocked(x_pos, y_pos);
}

bool isCollision(short x_pos, short y_pos)
{
    if (x_pos < 0 || y_pos < 0 || x_pos >= W_WIDTH || y_pos >= W_HEIGHT)
        return true;
    return GCollision[y_pos][x_pos];
}

bool CollisionAt(int x, int y)
{
    if (x < 0 || y < 0 || x >= W_WIDTH || y >= W_HEIGHT)
        return true;
    return GCollision[y][x];
}

void InitCollisionTile()
{
    // isCollision이 GCollision[y][x]로 읽으므로 같은 순서로 채운다.
    // (예전에는 [x][y]로 채우고 [y][x]로 읽었다 — 기존 장애물 패턴은 x/y 대칭이라
    //  드러나지 않았지만, 대칭이 아닌 마을 레이아웃을 넣으면서 순서를 맞췄다.)
    for (int y = 0; y < W_HEIGHT; ++y) {
        for (int x = 0; x < W_WIDTH; ++x) {
            GCollision[y][x] = checkCollision(static_cast<short>(x), static_cast<short>(y));
        }
    }

    cout << "Init Collision Tile" << endl;
    CheckTownRules();
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 4-A 마을 높낮이 — 층 규칙 검사(Data/TownRules.h, 클라 --script terraincheck와 같은 함수)
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    std::atomic<int> g_townViolations{ 0 };
}

void CheckTownRules()
{
    const TownRules::Report r = TownRules::Check();
    cout << "[Town] level rules: walkable=" << r.walkable << " (L0 " << r.levelCells[0] << ", L1 " << r.levelCells[1]
         << ", L2 " << r.levelCells[2] << ", L3 " << r.levelCells[3] << ", L4 " << r.levelCells[4] << ")"
         << " steps=" << r.steps << " forced=" << r.forced
         << " cliffCross=" << r.cliff << " airWalk=" << r.air << (r.Ok() ? " OK" : " FAIL") << endl;
    if (!r.Ok())
        cout << "[Town]   first: (" << r.firstX << ", " << r.firstY << ") dir " << r.firstDir
             << (r.firstForced ? " forced" : " step") << endl;
}

void GuardTownMove(const char* who, int fromX, int fromY, int toX, int toY)
{
    if (TownRules::PathOk(fromX, fromY, toX, toY))
        return;
    const int n = ++g_townViolations;
    if (n <= 20)
        cout << "[Town] level-rule violation #" << n << " " << (who ? who : "?") << " (" << fromX << ", " << fromY
             << ") -> (" << toX << ", " << toY << ")" << endl;
}

int TownRuleViolations()
{
    return g_townViolations.load();
}
