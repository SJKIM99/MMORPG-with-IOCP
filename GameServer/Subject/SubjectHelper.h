#pragma once

#include "ObjID.h"

namespace SubjectHelper
{
	void MovePositionByDirection(short& x, short& y, char direction);
	[[nodiscard]] bool CanSee(const ObjID& a, const ObjID& b);
	[[nodiscard]] bool CanAttack(const ObjID& a, const ObjID& b);
	[[nodiscard]] bool IsAdjacent(const ObjID& a, const ObjID& b);
}
