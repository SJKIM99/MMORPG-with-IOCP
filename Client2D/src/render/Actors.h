#pragma once

#include "raylib.h"
#include "net/Shared.h"

// ─────────────────────────────────────────────────────────────────────────────
// 캐릭터/몬스터/NPC 절차적 드로잉. 스프라이트 대신 벡터 도형 + 간단한 리그
// (걷기 주기, 공격 휘두르기, 피격 젖힘, 사망 쓰러짐)로 움직인다.
// 좌표: 발 위치(base, 아이소 px)에서 위로 쌓는다. 로컬 +x = 바라보는 방향.
// ─────────────────────────────────────────────────────────────────────────────
namespace actors
{
	struct Pose
	{
		Vector2 base{};          // 발 위치(아이소 px)
		float scale = 1.f;
		bool  faceRight = true;  // 화면 기준 오른쪽을 보는가
		bool  faceBack = false;  // 카메라 반대쪽(화면 위쪽)을 보는가
		float walk = 0.f;        // 걷기 위상(라디안)
		float moving = 0.f;      // 0 서 있음 ~ 1 걷는 중
		float attack = -1.f;     // 0~1 공격 동작 진행(<0 = 없음)
		float hurt = 0.f;        // 1 → 0 피격 반동
		float dead = 0.f;        // 0~1 쓰러짐
		float cast = 0.f;        // 0~1 기술 시전 자세
		float time = 0.f;        // 전역 시간(대기 동작)
		float hover = 0.f;       // 마우스 올림 강조
		uint32_t seed = 0;       // 개체별 변화(머리색 등)
	};

	struct PlayerLook
	{
		uint16_t weaponId = 0;
		int      enchant = 0;
		Color    armor{ 150, 160, 176, 255 };
		Color    cloth{ 60, 90, 170, 255 };   // 망토/깃털
		Color    trim{ 200, 170, 90, 255 };
	};

	// 캐릭터 외형 기본색(이름 해시로 망토 색 등을 고른다)
	PlayerLook LookForPlayer(uint32_t nameHash, uint16_t weaponId, int enchant);

	void DrawShadow(Vector2 base, float rx, float alpha);
	void DrawPlayer(const Pose& pose, const PlayerLook& look);
	void DrawMonster(const Pose& pose, const MonsterTableRow& row);
	void DrawNpc(const Pose& pose, const WorldData::NpcRow& npc);

	// 머리 위(이름표/체력바 위치) 높이(px, scale 반영 전)
	float PlayerHeight();
	float MonsterHeight(const MonsterTableRow& row);
	float NpcHeight(const WorldData::NpcRow& npc);
	// 그림자 반지름(px, scale 반영 전)
	float MonsterShadow(const MonsterTableRow& row);
}
