#pragma once

#include "Subject.h"

namespace SubjectHelper
{
	void MovePositionByDirection(short& x, short& y, char direction);
	[[nodiscard]] bool CanSee(const Subject::SharedPtr& a, const Subject::SharedPtr& b);
	[[nodiscard]] bool CanAttack(const Subject::SharedPtr& a, const Subject::SharedPtr& b);
	[[nodiscard]] bool IsAdjacent(const Subject::SharedPtr& a, const Subject::SharedPtr& b);
}
