#pragma once

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 마을 데이터 형(손으로 쓰는 쪽). 값은 생성기(tools/mapgen/gen_town.py)가 만드는 TownMap.h에 있다.
// 서버와 클라가 같이 include한다 — 열거형은 끝에만 추가한다.
// ─────────────────────────────────────────────────────────────────────────────
namespace TownMap
{
	struct Point16 { int16_t x, y; };
	struct PointF { float x, y; };

	// 건물 종류 — 클라이언트 그리기(props::DrawBuilding)가 실루엣 · 재질을 고른다(서버는 쓰지 않는다)
	enum class BuildingKind : uint8_t
	{
		eStore, eWeaponShop, eTemple, eInn, eHouse,   // Phase 4 전부터
	};

	struct BuildingRow
	{
		BuildingKind kind;
		int16_t x0, y0, x1, y1;   // 월드 칸, 양끝 포함 — 발자국 칸은 모두 막힘('B')
		uint8_t level;            // 발자국의 층(0~4)
		char    door;             // 문이 난 면 'S' 'E' 'N' 'W'
		uint8_t variant;          // 같은 종류의 모양 변형
		const char* sign;         // 간판 글(없으면 "")
	};

	struct DistrictRow
	{
		const char* name;
		const char* sub;          // 한 줄 설명(투어 자막)
		float x, y;               // 투어 · 지도 이름 자리(월드 칸)
		float zoom;               // 투어 줌
	};

	// 소리 구역(클라이언트 — audio/Ambient)
	enum class AmbientKind : uint8_t { eFountain, eMarket, eSmithy, eBell, eRiver, eWind };
	struct AmbientRow
	{
		AmbientKind kind;
		float x, y;               // 월드 칸
		float radius;             // 이 거리 안에서 들린다(칸)
	};
}
