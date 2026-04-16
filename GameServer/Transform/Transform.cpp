#include "pch.h"
#include "Transform.h"

void Transform::Reset() noexcept
{
	m_x       = -1;
	m_y       = -1;
	m_sectorX = -1;
	m_sectorY = -1;
}
