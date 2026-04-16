#include "pch.h"
#include "AStar.h"
#include "Collision.h"

vector<NODE> FindPath(short startX, short startY, short goalX, short goalY) 
{
    if (startX == goalX && startY == goalY) return {};

    priority_queue<NODE> openSet;
    unordered_map<pair<short, short>, pair<short, short>, hash_pair> cameFrom;
    unordered_map<pair<short, short>, int, hash_pair> gScores;

    auto Heuristic = [](short x1, short y1, short x2, short y2) {
        return abs(x1 - x2) + abs(y1 - y2);
    };

    int hCost = Heuristic(startX, startY, goalX, goalY);
    openSet.push({ startX, startY, 0, hCost, hCost });
    cameFrom[{startX, startY}] = { startX, startY };
    gScores[{startX, startY}] = 0;

    short dx[] = { 0, 0, -1, 1 };
    short dy[] = { -1, 1, 0, 0 };

    unordered_set<pair<short, short>, hash_pair> closedSet;

    while (!openSet.empty()) {
        NODE current = openSet.top();
        openSet.pop();

        if (closedSet.count({ current._x, current._y }))
            continue;
        closedSet.insert({ current._x, current._y });

        if (current._x == goalX && current._y == goalY) {
            vector<NODE> path;
            while (!(current._x == startX && current._y == startY)) {
                path.push_back({ current._x, current._y, 0, 0, 0 });
                auto& prev = cameFrom[{current._x, current._y}];
                current._x = prev.first;
                current._y = prev.second;
            }
            return path;
        }

        for (int i = 0; i < 4; ++i) {
            short nextX = current._x + dx[i];
            short nextY = current._y + dy[i];

            if (nextX < 0 || nextX >= W_WIDTH || nextY < 0 || nextY >= W_HEIGHT) continue;
            if (isCollision(nextX, nextY)) continue;

            int newCost = gScores[{current._x, current._y}] + 1;
            if (!gScores.count({ nextX, nextY }) || newCost < gScores[{nextX, nextY}]) {
                gScores[{nextX, nextY}] = newCost;
                int hCost = Heuristic(nextX, nextY, goalX, goalY);
                openSet.push({ nextX, nextY, newCost, hCost, newCost + hCost });
                cameFrom[{nextX, nextY}] = { current._x, current._y };
            }
        }
    }

    return {};
}
