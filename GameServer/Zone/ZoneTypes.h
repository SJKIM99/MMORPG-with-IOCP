#pragma once

#include <cstdint>

using ZoneId = uint16_t;

inline constexpr ZoneId InvalidZoneId = UINT16_MAX;

// Zone 격자 좌표. Sector 와 마찬가지로 **XZ 평면만** 나눈다 (CLAUDE.md 3장).
// 두 번째 축을 y 로 두면 새 좌표계의 높이와 이름이 겹쳐 조용히 틀린다.
struct ZoneCoord
{
	short x = -1;
	short z = -1;

	[[nodiscard]] constexpr bool IsAssigned() const noexcept
	{
		return x >= 0 && z >= 0;
	}

	constexpr bool operator==(const ZoneCoord&) const noexcept = default;
};
