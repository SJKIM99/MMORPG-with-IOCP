#pragma once

#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 클릭 이동용 A*. 서버와 같은 충돌(WorldData::IsBlocked)을 쓰고, 서버의 대각선
// 이동 규칙(축별로 따로 막힘 검사 → 벽을 타고 미끄러짐)과 어긋나지 않도록
// 대각선은 "두 직교 이웃이 모두 비어 있을 때"만 쓴다. 그러면 클라가 예측한 칸과
// 서버가 실제로 옮긴 칸이 항상 같다.
// ─────────────────────────────────────────────────────────────────────────────
namespace path
{
	struct Step { int x, y; };

	// (sx,sy) → (gx,gy). stopWithin > 0이면 목표까지 체비셰프 거리가 그 이하인
	// 칸에 도착하면 끝(몬스터/NPC처럼 목표 칸 자체가 막혀 있거나 점유된 경우).
	// 찾으면 출발 칸을 뺀 경로를 돌려준다. 못 찾으면 가장 가까이 갈 수 있는 곳까지.
	std::vector<Step> Find(int sx, int sy, int gx, int gy, int stopWithin = 0, int maxRadius = 96);   // 96: 새 마을(84×72)을 가로지르는 계단 우회까지

	// 서버 방향 코드(0:UP 1:DOWN 2:LEFT 3:RIGHT 4:UL 5:UR 6:DL 7:DR). 인접하지 않으면 -1
	int DirectionOf(int dx, int dy);
	void DeltaOf(int dir, int& dx, int& dy);
	// 한 칸 이동이 서버 규칙상 정확히 (dx,dy)만큼 움직이는가(대각선은 양쪽 직교 칸이 모두 비어야)
	bool CanStep(int x, int y, int dx, int dy);
}
