#pragma once

class GameSession;

struct NODE {
    short _x, _y;
    int _gCost, _hCost, _fCost;
    bool operator<(const NODE& rhs) const {
        return _fCost > rhs._fCost;
    }
};

struct hash_pair {
    template <class T1, class T2>
    size_t operator()(const pair<T1, T2>& p) const {
        size_t h1 = hash<T1>{}(p.first);
        size_t h2 = hash<T2>{}(p.second);
        // Boost hash_combine style: reduces collisions for grid coordinates
        h1 ^= h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        return h1;
    }
};

vector<NODE> FindPath(short startX, short startY, short goalX, short goalY);
