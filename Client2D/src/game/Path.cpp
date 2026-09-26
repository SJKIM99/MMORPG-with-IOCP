#include "game/Path.h"
#include "net/Shared.h"
#include "Data/MoveRule.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>
#include <unordered_map>

namespace path
{
	int DirectionOf(int dx, int dy)
	{
		if (dx == 0 && dy == -1) return 0;
		if (dx == 0 && dy == 1) return 1;
		if (dx == -1 && dy == 0) return 2;
		if (dx == 1 && dy == 0) return 3;
		if (dx == -1 && dy == -1) return 4;
		if (dx == 1 && dy == -1) return 5;
		if (dx == -1 && dy == 1) return 6;
		if (dx == 1 && dy == 1) return 7;
		return -1;
	}

	void DeltaOf(int dir, int& dx, int& dy)
	{
		static const int tbl[8][2] = { {0,-1},{0,1},{-1,0},{1,0},{-1,-1},{1,-1},{-1,1},{1,1} };
		if (dir < 0 || dir > 7) { dx = dy = 0; return; }
		dx = tbl[dir][0];
		dy = tbl[dir][1];
	}

	bool CanStep(int x, int y, int dx, int dy)
	{
		// 서버 강제 이동과 같은 모서리 규칙(Data/MoveRule.h CleanStep) — 대각은 두 옆이 다 열려야 한다
		return MoveRule::CleanStep(x, y, dx, dy);
	}

	std::vector<Step> Find(int sx, int sy, int gx, int gy, int stopWithin, int maxRadius)
	{
		std::vector<Step> out;
		if (sx == gx && sy == gy)
			return out;

		auto key = [](int x, int y) { return (static_cast<long long>(y) << 16) | static_cast<long long>(x); };
		auto cheb = [](int ax, int ay, int bx, int by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); };
		auto done = [&](int x, int y)
		{
			if (stopWithin > 0) return cheb(x, y, gx, gy) <= stopWithin;
			return x == gx && y == gy;
		};
		if (done(sx, sy))
			return out;

		// 목표 칸이 막혀 있으면 "가장 가까운 곳까지"로 끝난다(아래 best 추적)
		struct Node { int x, y; int g; int f; };
		struct Cmp { bool operator()(const Node& a, const Node& b) const { return a.f > b.f || (a.f == b.f && a.g < b.g); } };
		std::priority_queue<Node, std::vector<Node>, Cmp> open;
		std::unordered_map<long long, long long> parent;
		std::unordered_map<long long, int> gScore;
		parent.reserve(4096);
		gScore.reserve(4096);

		// 휴리스틱: 옥타일(직선 10, 대각 14)
		auto h = [&](int x, int y)
		{
			const int dx = std::abs(x - gx), dy = std::abs(y - gy);
			return 10 * (dx + dy) - 6 * std::min(dx, dy);
		};

		open.push({ sx, sy, 0, h(sx, sy) });
		gScore[key(sx, sy)] = 0;
		parent[key(sx, sy)] = -1;
		int bestX = sx, bestY = sy, bestH = h(sx, sy);
		int expanded = 0;
		bool found = false;
		int fx = sx, fy = sy;

		while (!open.empty() && expanded < 12000)
		{
			const Node n = open.top();
			open.pop();
			const auto it = gScore.find(key(n.x, n.y));
			if (it != gScore.end() && it->second < n.g)
				continue;
			++expanded;
			if (done(n.x, n.y))
			{
				found = true;
				fx = n.x; fy = n.y;
				break;
			}
			const int hn = h(n.x, n.y);
			if (hn < bestH)
			{
				bestH = hn; bestX = n.x; bestY = n.y;
			}
			for (int dir = 0; dir < 8; ++dir)
			{
				int dx, dy;
				DeltaOf(dir, dx, dy);
				const int nx = n.x + dx, ny = n.y + dy;
				if (std::abs(nx - sx) > maxRadius || std::abs(ny - sy) > maxRadius)
					continue;
				if (!CanStep(n.x, n.y, dx, dy))
					continue;
				const int cost = n.g + ((dx != 0 && dy != 0) ? 14 : 10);
				const long long k = key(nx, ny);
				const auto g2 = gScore.find(k);
				if (g2 != gScore.end() && g2->second <= cost)
					continue;
				gScore[k] = cost;
				parent[k] = key(n.x, n.y);
				open.push({ nx, ny, cost, cost + h(nx, ny) });
			}
		}

		if (!found)
		{
			fx = bestX;
			fy = bestY;
			if (fx == sx && fy == sy)
				return out;
		}

		for (long long k = key(fx, fy); k != -1; )
		{
			const int x = static_cast<int>(k & 0xFFFF), y = static_cast<int>(k >> 16);
			if (x == sx && y == sy)
				break;
			out.push_back({ x, y });
			const auto p = parent.find(k);
			if (p == parent.end())
				break;
			k = p->second;
		}
		std::reverse(out.begin(), out.end());
		return out;
	}
}
