#include "pch.h"
#include "Transform.h"

void Transform::Reset() noexcept
{
	m_pos     = Vec3{};
	m_yaw     = 0.0f;
	m_sectorX = -1;
	m_sectorZ = -1;
}
