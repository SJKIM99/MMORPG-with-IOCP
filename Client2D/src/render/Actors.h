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
		// V2
		uint16_t skill = 0;      // 재생 중인 직업 기술(Data/CombatData.h SkillTable id)
		float skillT = -1.f;     // 0~1 기술 동작 진행(<0 = 없음)
		float skillHold = 0.f;   // 기술 앞부분의 "힘 모으기" 구간 비율(처형·낙성)
		float windup = 0.f;      // 몬스터 강공격 예고 자세 0~1
		float knock = 0.f;       // 1 → 0 크게 밀려남
		// Phase 1-C 몸(game/Motion Body) — 0이면 faceRight만 쓴다(도감 · 예전 호출)
		float turn = 0.f;        // 가로 배율 −1(왼쪽) ~ +1(오른쪽): 좌우 반전이 60ms에 걸쳐 0을 지난다
		float lean = 0.f;        // 앞기울기(도) — 출발 가속 · 달리는 자세
		float bank = 0.f;        // 도는 쪽 기울기(도)
		float settle = 0.f;      // 멈출 때 한 번 흔들림(−1~1 정도)
		float gait = -1.f;       // 지금 속도 ÷ 규칙 속도(Body::gait). 음수 = 모름(도감 · 예전 호출 — moving만 본다)
	};
	// 좌우 방향(부호)과 돌아서는 중의 가로 배율. 그릴 때는 kTurnMinWidth 밑으로 좁히지 않는다 —
	// 0 근처(한 프레임)에서 종잇장처럼 보이지 않고 "휙 도는" 인상만 남게(1-C 녹화 검토)
	inline constexpr float kTurnMinWidth = 0.32f;
	[[nodiscard]] inline float TurnScale(const Pose& p) { return p.turn != 0.f ? p.turn : (p.faceRight ? 1.f : -1.f); }

	struct PlayerLook
	{
		uint8_t  cls = 0;        // ClassId: 0 서약기사 · 1 각인술사 · 2 밤사냥꾼
		uint16_t weaponId = 0;
		int      enchant = 0;
		Color    armor{ 150, 160, 176, 255 };
		Color    cloth{ 60, 90, 170, 255 };   // 망토/깃털
		Color    trim{ 200, 170, 90, 255 };
		// ── V2 착용 장비 외형(ApplyGear) ──
		uint8_t  weaponRarity = 0;            // Rarity — 영웅 이상은 무기에 희귀도 빛
		int8_t   helmStyle = -1;              // -1 직업 기본 · 0 두건 · 1 철모 · 2 기사 투구 · 3 날개 투구 · 4 이레린 두건
		Color    helmMain{}, helmTrim{}, helmAccent{};
		int8_t   bodyStyle = -1;              // -1 직업 기본 · 0 누빔 · 1 사슬 · 2 판금 · 3 로브 · 4 가죽 · 5 서약 판금 · 6 이레린 로브
		Color    bodyMain{}, bodyTrim{}, bodyAccent{};
		bool     hasCloak = false;
		Color    cloakMain{}, cloakTrim{};
		bool     hasGloves = false;
		Color    gloveMain{};
		bool     hasBoots = false;
		Color    bootMain{};
		uint8_t  auraRarity = 0;              // 가장 높은 방어구 희귀도(발밑 빛)
	};

	// 캐릭터 외형 기본색(이름 해시로 망토 색 등을 고른다)
	PlayerLook LookForPlayer(uint32_t nameHash, uint16_t weaponId, int enchant, uint8_t cls = 0);
	// 착용 장비(EquipSlot - 1 순서의 아이템 id · 희귀도)를 외형에 반영한다.
	void ApplyGear(PlayerLook& look, const uint16_t* items, const uint8_t* rarity, int count);

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
