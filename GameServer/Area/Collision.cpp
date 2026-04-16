#include "pch.h"
#include "Collision.h"

array<array<bool, W_HEIGHT>, W_WIDTH> GCollision;

constexpr int SCREEN_WDITH = 16;
constexpr int SCREEN_HEIGHT = 16;

bool checkCollision(short x_pos, short y_pos)
{
    if (x_pos < 0 || y_pos < 0 || x_pos >= W_WIDTH || y_pos >= W_HEIGHT) return false;
    if ((x_pos / 3 + y_pos / 3) % 3 == 0) return false;
    else if ((x_pos / 3 + y_pos / 3) % 3 == 1) return false;
    else {
        if ((x_pos / 2 + y_pos / 2) % 3 == 0) return true;
        else if ((x_pos / 2 + y_pos / 2) % 3 == 1) return false;
        else return false;
    }
}

bool isCollision(short x_pos, short y_pos)
{
    if (x_pos < 0 || y_pos < 0 || x_pos >= W_WIDTH || y_pos >= W_HEIGHT)
        return true;
    return GCollision[y_pos][x_pos];
}

void InitCollisionTile()
{
    for (int i = 0; i < W_WIDTH; ++i) {
        for (int j = 0; j < W_HEIGHT; ++j) {
            if (checkCollision(i, j) == true)
                GCollision[i][j] = true;
            else
                GCollision[i][j] = false;
        }
    }

    cout << "Init Collision Tile" << endl;
}
