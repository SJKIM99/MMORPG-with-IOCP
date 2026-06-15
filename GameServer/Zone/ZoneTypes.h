#pragma once

#include <cstdint>

using ZoneId = uint16_t;

inline constexpr ZoneId InvalidZoneId = UINT16_MAX;

struct ZoneCoord
{
	short x = -1;
	short y = -1;

	[[nodiscard]] constexpr bool IsAssigned() const noexcept
	{
		return x >= 0 && y >= 0;
	}

	constexpr bool operator==(const ZoneCoord&) const noexcept = default;
};
