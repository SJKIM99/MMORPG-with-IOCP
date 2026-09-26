#pragma once

#include "raylib.h"

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 셰이더 절차 효과(Phase 2-A) — 그림 파일 없이 조각 셰이더로 그리는 "연속 그림".
//
// 한 효과 = 월드의 네모 하나 + 종류 · 나이(0~1) · 씨앗 · 색 램프(뜨거운 색 → 중간 → 식은 색).
// 노이즈(fbm) · 극좌표 · 램프만으로 불꽃 · 폭발 · 연기 · 얼음 파편 · 먹물 · 먼지 · 빛기둥 · 소용돌이 · 번개 ·
// 초승 섬광 · 흐르는 빛줄기 · 허공 각인 글자와, 바닥에 눕는 각인진 · 새김 원 · 균열 · 그을음 · 서리 · 먹물 번짐 ·
// 충격 고리를 만든다. 더하기 혼합 효과는 블룸 빛 버퍼에도 그려져 주변을 물들인다(post::Apply → 빛 전용 모드).
//
//  · Higgsfield 영상 → 프레임 방식 대신 셰이더를 고른 이유: 잔액(크레딧) · 해상도에 묶이지 않고,
//    색 램프로 직업 색을 바꾸며, 나이 곡선을 기술 타이밍(서버 타격 시각)에 정확히 맞출 수 있다.
//  · 셰이더 문자열은 조각(머리 · 공중 · 바닥)으로 나눠 MSVC 문자열 한도(16,380B) 안에 둔다.
//  · 셰이더가 없으면(오래된 드라이버) 아무것도 그리지 않는다 — 부르는 쪽의 입자 · 고리가 남는다.
// ─────────────────────────────────────────────────────────────────────────────
namespace vfx
{
	enum class Kind : uint8_t
	{
		// 공중(세로로 선 네모, 깊이 = 바닥 위치). rot = 네모 회전(운석 꼬리 · 초승 섬광 방향)
		Fire,        // 부풀며 타오르는 불(아랫변 기준 — 위로 탄다)
		Explosion,   // 뜨거운 핵 → 퍼지는 불 공 → 식어 가는 테
		Smoke,       // 뭉게 연기(보통 혼합)
		IceBurst,    // 가운데서 뻗는 얼음 결정 가시
		InkBurst,    // 먹물 튐(보통 혼합)
		Dust,        // 흙먼지 구름(보통 혼합, 아랫변 기준)
		Pillar,      // 빛기둥(위로 흐르는 결, 아랫변 기준)
		Vortex,      // 소용돌이(극좌표 띠)
		Lightning,   // 세로 번개(떨리는 줄기 + 가지, 아랫변 기준 — 땅에 꽂힌다)
		Slash,       // 초승 섬광(오른쪽을 향한 호 — rot로 돌린다)
		Flare,       // 날카로운 섬광 별(타격 중심)
		Beam,        // 흐르는 빛줄기(가로 = 길이, 세로 = 굵기 — 두 점 사이, BeamBetween)
		Glyph,       // 허공에 획 순서대로 쓰이는 각인 글자
		// 바닥(눕힌 네모 — 가로 w, 세로 w/2, 깊이 = 바닥 데칼). rot = 바닥 평면 회전
		Rune,        // 각인진 — 원 · 글자 칸 · 방사선 · 별이 획 순서대로(나이 앞 40%) 그려진다
		Seal,        // 새김 원(서약기사) — 두 겹 원 + 방패 문장 + 종 울림 물결
		Crack,       // 땅 균열(빛나다 식음)
		Scorch,      // 그을음(보통 혼합)
		Frost,       // 서리 결정 번짐
		InkStain,    // 먹물 번짐(보통 혼합)
		GroundRing,  // 바닥을 달리는 충격 고리
		Count
	};

	struct Spec
	{
		Kind    kind = Kind::Fire;
		Vector2 iso{};            // 바닥 위치(아이소 px)
		float   z = 0.f;          // 높이(px, 위로 +): 공중 효과의 가운데(불 · 먼지 · 빛기둥 · 번개는 아랫변)
		float   w = 64.f, h = 64.f;   // 크기(px). 바닥 효과는 w = 지름(세로는 절반, h 무시)
		float   rot = 0.f;        // 라디안
		float   life = 0.6f;      // 초
		float   delay = 0.f;      // 이만큼 뒤에 시작(초)
		Color   hot{ 255, 250, 220, 255 };   // 램프: 뜨거운(가운데) → 중간 → 식은(가장자리)
		Color   mid{ 255, 160, 60, 255 };
		Color   cool{ 120, 40, 20, 255 };
		float   intensity = 1.f;  // 밝기 배율
		float   alpha = 1.f;      // 전체 투명도(다른 사람 기술 50% 등)
		float   seed = -1.f;      // <0 이면 무작위
		float   depthBias = 0.35f;// 공중 효과 깊이 앞당김(타일 X+Y 단위)
		float   grow = 1.f;       // 끝 크기 배율(시작 1 → 끝 grow, 빠르게 부푼다)
		bool    over = false;     // 깊이 무시(모든 것 위에 — 각성기 절정 섬광 등)
	};

	void Spawn(const Spec& s);
	// 두 점 사이 빛줄기(Beam): 가로 = 거리, 세로 = thick
	void BeamBetween(Vector2 aIso, float az, Vector2 bIso, float bz, float thick, float life, const Spec& style);
	void Update(float dt);
	void Clear();
	void DrawGround();   // 바닥 데칼 단계(깊이 테스트 O · 쓰기 X)
	void DrawWorld();    // 반투명 월드 단계
	[[nodiscard]] int Count();
	[[nodiscard]] bool Ready();   // 셰이더를 쓸 수 있나(첫 그리기 뒤에 알 수 있다)
	void Shutdown();

	// 흔히 쓰는 램프 — 「새김」 팔레트 안에서(DESIGN_SYSTEM 10장)
	struct Ramp { Color hot, mid, cool; };
	[[nodiscard]] Ramp FireRamp();      // 불씨 주황
	[[nodiscard]] Ramp GoldRamp();      // 새벽빛 금 · 강철 백(서약기사)
	[[nodiscard]] Ramp TealRamp();      // 옛길 청록(각인술사)
	[[nodiscard]] Ramp FrostRamp();     // 서리 청백
	[[nodiscard]] Ramp BoltRamp();      // 벼락 흰청록
	[[nodiscard]] Ramp MoonRamp();      // 달빛 은백(밤사냥꾼)
	[[nodiscard]] Ramp PoisonRamp();    // 독 녹색
	[[nodiscard]] Ramp InkRamp();       // 먹빛
	[[nodiscard]] Ramp EmberRamp();     // 붉은 불씨(보스 · 몬스터)
	[[nodiscard]] Ramp DustRamp();      // 흙먼지
	[[nodiscard]] Ramp SmokeRamp();     // 잿빛 연기
	void SetRamp(Spec& s, const Ramp& r);
	[[nodiscard]] Spec Make(Kind k, Vector2 iso, float z, float size, float life, const Ramp& r);
}
