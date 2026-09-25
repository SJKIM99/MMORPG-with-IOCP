#pragma once

#include "raylib.h"
#include "net/Shared.h"

#include <string>
#include <string_view>

// ─────────────────────────────────────────────────────────────────────────────
// 아이템 아이콘: 시작할 때 벡터로 한 번 그려 아틀라스(128px/칸, 밉맵)로 굽는다.
// 무기 모양 그리기(DrawWeapon)는 캐릭터가 손에 든 무기에도 그대로 쓴다 —
// 인벤토리 아이콘과 필드/손에 든 모습이 항상 같은 무기로 보이게.
// ─────────────────────────────────────────────────────────────────────────────
namespace icons
{
	void Build();
	void Unload();

	// 그린 아이콘(Higgsfield — assets/icons/icons.png + icons.txt). 있으면 절차적 아이콘 대신 쓴다.
	//   이름: item_<아이템 id> · quest_<수집물> · skill_<기술 id> · status_<상태 id> · ui_<이름>
	void LoadPainted(const std::string& dir);
	[[nodiscard]] bool HasPainted(std::string_view name);
	bool DrawPainted(std::string_view name, Rectangle dst, Color tint = WHITE);

	// 화면 공간
	void Draw(uint16_t itemId, Rectangle dst, Color tint = WHITE);
	// 월드 공간(현재 gfx 깊이 사용)
	void DrawWorld(uint16_t itemId, Vector2 center, float size, float rot, Color tint = WHITE);
	// 퀘스트 수집물(가방 밖 물건) — questIcon = QuestData::QuestIcon 값
	void DrawQuestItem(uint8_t questIcon, Rectangle dst, Color tint = WHITE);

	// hilt = 손잡이 끝(잡는 곳), angle = 칼끝 방향(라디안, 화면 기준), scale 1 = 아이콘 128px 기준 약 1/3
	void DrawWeapon(uint16_t itemId, Vector2 hilt, float angle, float scale, float time);
	// 무기의 칼끝까지 길이(px, scale 1 기준)
	float WeaponLength(uint16_t itemId);

	Color GradeColor(ItemGrade grade);
	Color ItemNameColor(uint16_t itemId);
	// V2: 인스턴스 희귀도까지 반영한 이름 색(장비), 희귀도 색
	Color ItemNameColor(uint16_t itemId, uint64_t extra);
	Color RarityColor(Rarity r);
	// 무기의 빛 색(지팡이 수정 · 칼날 끝) — 캐릭터가 든 지팡이를 같은 색으로 그린다
	Color WeaponAccent(uint16_t itemId);
	// 방어구·장신구의 모양 번호와 색(아이콘과 캐릭터 외형이 같은 색을 쓰게). 없으면 false.
	bool GearStyle(uint16_t itemId, int& style, Color& main, Color& trim, Color& accent);
	// 강화 수치에 따른 칼날 빛(0이면 투명)
	Color EnchantGlow(int enchant);
}
