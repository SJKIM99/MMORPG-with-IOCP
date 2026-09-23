#pragma once

#include "raylib.h"
#include "net/Shared.h"

// ─────────────────────────────────────────────────────────────────────────────
// 아이템 아이콘: 시작할 때 벡터로 한 번 그려 아틀라스(128px/칸, 밉맵)로 굽는다.
// 무기 모양 그리기(DrawWeapon)는 캐릭터가 손에 든 무기에도 그대로 쓴다 —
// 인벤토리 아이콘과 필드/손에 든 모습이 항상 같은 무기로 보이게.
// ─────────────────────────────────────────────────────────────────────────────
namespace icons
{
	void Build();
	void Unload();

	// 화면 공간
	void Draw(uint16_t itemId, Rectangle dst, Color tint = WHITE);
	// 월드 공간(현재 gfx 깊이 사용)
	void DrawWorld(uint16_t itemId, Vector2 center, float size, float rot, Color tint = WHITE);

	// hilt = 손잡이 끝(잡는 곳), angle = 칼끝 방향(라디안, 화면 기준), scale 1 = 아이콘 128px 기준 약 1/3
	void DrawWeapon(uint16_t itemId, Vector2 hilt, float angle, float scale, float time);
	// 무기의 칼끝까지 길이(px, scale 1 기준)
	float WeaponLength(uint16_t itemId);

	Color GradeColor(ItemGrade grade);
	Color ItemNameColor(uint16_t itemId);
	// 강화 수치에 따른 칼날 빛(0이면 투명)
	Color EnchantGlow(int enchant);
}
