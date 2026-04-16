#include "pch.h"
#include "SubjectHelper.h"
#include "Collision.h"

namespace SubjectHelper
{
	void MovePositionByDirection(short& x, short& y, char direction)
	{
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
	}

	bool CanSee(const ObjID& a, const ObjID& b)
	{
		const auto aSubject = ::GetGameObject<Subject>(a);
		const auto bSubject = ::GetGameObject<Subject>(b);
		if (aSubject == nullptr || bSubject == nullptr)
			return false;

		if (abs(aSubject->GetX() - bSubject->GetX()) >= VIEW_RANGE)
			return false;

		return abs(aSubject->GetY() - bSubject->GetY()) <= VIEW_RANGE;
	}

	bool IsAdjacent(const ObjID& a, const ObjID& b)
	{
		const auto aSubject = ::GetGameObject<Subject>(a);
		const auto bSubject = ::GetGameObject<Subject>(b);
		if (aSubject == nullptr || bSubject == nullptr)
			return false;

		const int dx = bSubject->GetX() - aSubject->GetX();
		const int dy = bSubject->GetY() - aSubject->GetY();
		return abs(dx) + abs(dy) == 1;
	}

	bool CanAttack(const ObjID& a, const ObjID& b)
	{
		const auto aSubject = ::GetGameObject<Subject>(a);
		const auto bSubject = ::GetGameObject<Subject>(b);
		if (aSubject == nullptr || bSubject == nullptr)
			return false;

		const int dx = bSubject->GetX() - aSubject->GetX();
		const int dy = bSubject->GetY() - aSubject->GetY();

		// Must be exactly 1 cardinal tile away (no diagonals, no same tile)
		if (abs(dx) + abs(dy) != 1)
			return false;

		// Horizontal attack: attacker must be facing the target's direction
		if (dx > 0 && aSubject->IsFacingLeft())  return false; // target is right, facing left
		if (dx < 0 && !aSubject->IsFacingLeft()) return false; // target is left, facing right

		// Vertical attack (dx == 0): always valid regardless of horizontal facing
		return true;
	}
}
