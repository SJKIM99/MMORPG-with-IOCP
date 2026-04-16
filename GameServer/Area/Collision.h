#pragma once

bool checkCollision(short x_pos, short y_pos);
bool isCollision(short x_pos, short y_pos);
void InitCollisionTile();

extern array<array<bool, W_HEIGHT>, W_WIDTH> GCollision;