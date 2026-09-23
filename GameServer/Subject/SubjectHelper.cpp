#include "pch.h"
#include "SubjectHelper.h"
#include "Collision.h"

namespace SubjectHelper
{
	// 한 번에 한 타일씩 옮긴다. 좌표는 float 이지만 **값은 여전히 정수 타일**이다 —
	// 연속 이동(서버 고정 틱 적분)은 Week 1 의 6번 단계에서 이 함수를 대체한다.
	void MovePositionByDirection(float& fx, float& fz, char direction)
	{
		short x = ToLegacyTile(fx);
		short y = ToLegacyTile(fz);

		// 0:UP  1:DOWN  2:LEFT  3:RIGHT
		// 4:UP-LEFT  5:UP-RIGHT  6:DOWN-LEFT  7:DOWN-RIGHT
		// 대각선은 각 축을 독립적으로 처리 → 벽에 자연스럽게 슬라이딩
		switch (direction)
		{
		case 0: if (y > 0)            { --y; if (isCollision(x, y)) ++y; } break;
		case 1: if (y < W_HEIGHT - 1) { ++y; if (isCollision(x, y)) --y; } break;
		case 2: if (x > 0)            { --x; if (isCollision(x, y)) ++x; } break;
		case 3: if (x < W_WIDTH - 1)  { ++x; if (isCollision(x, y)) --x; } break;
		case 4: // UP-LEFT
			if (y > 0)           { --y; if (isCollision(x, y)) ++y; }
			if (x > 0)           { --x; if (isCollision(x, y)) ++x; }
			break;
		case 5: // UP-RIGHT
			if (y > 0)           { --y; if (isCollision(x, y)) ++y; }
			if (x < W_WIDTH - 1) { ++x; if (isCollision(x, y)) --x; }
			break;
		case 6: // DOWN-LEFT
			if (y < W_HEIGHT - 1){ ++y; if (isCollision(x, y)) --y; }
			if (x > 0)           { --x; if (isCollision(x, y)) ++x; }
			break;
		case 7: // DOWN-RIGHT
			if (y < W_HEIGHT - 1){ ++y; if (isCollision(x, y)) --y; }
			if (x < W_WIDTH - 1) { ++x; if (isCollision(x, y)) --x; }
			break;
		}

		fx = static_cast<float>(x);
		fz = static_cast<float>(y);
	}

	bool CanSee(const Subject::SharedPtr& a, const Subject::SharedPtr& b)
	{
		if (!a || !b) return false;
		if (abs(a->GetX() - b->GetX()) > VIEW_RANGE) return false;
		return abs(a->GetZ() - b->GetZ()) <= VIEW_RANGE;
	}

	// 인접 = 맨해튼 거리 1타일. 아직 타일 판정이다 — 미터 단위 사거리는
	// Week 1 의 6번 단계에서 들어온다.
	bool IsAdjacent(const Subject::SharedPtr& a, const Subject::SharedPtr& b)
	{
		if (!a || !b) return false;
		const int dx = ToLegacyTile(b->GetX()) - ToLegacyTile(a->GetX());
		const int dz = ToLegacyTile(b->GetZ()) - ToLegacyTile(a->GetZ());
		return abs(dx) + abs(dz) == 1;
	}

	bool CanAttack(const Subject::SharedPtr& a, const Subject::SharedPtr& b)
	{
		if (!a || !b) return false;
		const int dx = ToLegacyTile(b->GetX()) - ToLegacyTile(a->GetX());
		const int dz = ToLegacyTile(b->GetZ()) - ToLegacyTile(a->GetZ());

		if (abs(dx) + abs(dz) != 1) return false;
		if (dx > 0 && a->IsFacingLeft())  return false;
		if (dx < 0 && !a->IsFacingLeft()) return false;

		return true;
	}

}
