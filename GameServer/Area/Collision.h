#pragma once

bool checkCollision(short x_pos, short y_pos);
bool isCollision(short x_pos, short y_pos);
// isCollision의 int 판 — MoveRule::CleanStep 같은 공용 규칙 함수에 넘긴다
bool CollisionAt(int x, int y);
void InitCollisionTile();

// ── Phase 4-A 마을 높낮이(Data/TownRules.h) ──
// 켤 때 한 번: 모든 걷는 칸 × 8방향 전수 검사 결과를 찍는다(절벽 통과 · 허공 걷기 0이어야 한다).
void CheckTownRules();
// 서버가 실제로 옮기는 이동마다(한 칸 걸음 · 돌진 끝점 · NPC 걸음): 곧은 선 위 지면이 끊기면 로그(처음 20건) + 수 세기.
// 마을 밖이면 바로 돌아간다. 옮기는 것을 막지는 않는다(규칙은 이동 판정이 지킨다 — 이것은 어긋남을 드러내는 덫).
void GuardTownMove(const char* who, int fromX, int fromY, int toX, int toY);
[[nodiscard]] int TownRuleViolations();

extern array<array<bool, W_HEIGHT>, W_WIDTH> GCollision;