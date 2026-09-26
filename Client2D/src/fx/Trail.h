#pragma once

#include "raylib.h"

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 무기 궤적 리본(Phase 2-A) — 휘두른 호를 따라가는 삼각형 띠.
//
//  · 호(Arc): 머리가 a0 → a1로 빠르게(이징) 달리고, 꼬리가 조금 늦게 따라와 띠가 자랐다가 줄어든다.
//    띠의 두께 = 칼날 길이(바깥 테가 칼끝, 안쪽이 손잡이 쪽). 바깥 테는 뜨겁고, 안쪽 · 꼬리로 갈수록 투명하다.
//  · 흐름 무늬: 띠 방향으로 흐르는 결 텍스처(시작할 때 절차적으로 만든다 — 그림 파일 없음)를 머리 쪽으로 흘린다.
//  · 따라가기(Follow): 움직이는 점(돌진 · 투사체 · 순간이동)의 지나간 자리를 띠로 — 오래된 점일수록 가늘고 흐리다.
//  · 모든 궤적은 더하기 혼합(빛) 또는 보통 혼합(먹물)이다. 빛 궤적은 블룸 빛 버퍼에도 그려진다.
// ─────────────────────────────────────────────────────────────────────────────
namespace trail
{
	struct ArcSpec
	{
		Vector2 iso{};         // 휘두르는 중심(시전자 발, 아이소 px)
		float   z = 34.f;      // 휘두르는 평면 높이(px)
		float   radius = 64.f; // 칼끝 반지름(바닥 평면 px — 화면에서는 가로 1 : 세로 ½)
		float   blade = 38.f;  // 띠 두께(칼날 길이, px)
		float   a0 = 0.f, a1 = 3.f;   // 시작 · 끝 각(라디안, 바닥 평면. 0 = 화면 오른쪽, +π/2 = 화면 아래)
		float   tilt = 0.f;    // 0 = 수평 베기, π/2 = 세로 내려치기(화면 평면)
		float   sweep = 0.14f; // 머리가 a0 → a1까지 걸리는 시간(초)
		float   lag = 0.09f;   // 꼬리가 머리를 따라오는 늦음(초) — 띠 길이
		float   delay = 0.f;
		Color   edge{ 255, 250, 235, 255 };   // 칼끝 테(뜨거운 색)
		Color   body{ 242, 196, 124, 255 };   // 띠 몸통
		float   alpha = 1.f;
		bool    additive = true;
		float   depthBias = 0.5f;
	};
	void Arc(const ArcSpec& s);

	// 따라가기: 핸들을 받고 매 프레임 Push, 끝나면 End(남은 띠는 fade초에 걸쳐 사라진다)
	struct FollowSpec
	{
		float width = 16.f;
		float fade = 0.22f;    // 점 하나가 사라지는 시간
		Color edge{ 255, 255, 255, 255 };
		Color body{ 111, 181, 172, 255 };
		float alpha = 1.f;
		bool  additive = true;
	};
	[[nodiscard]] int Begin(const FollowSpec& s);
	void Push(int handle, Vector2 iso, float z);
	void End(int handle);
	// 한 번에: a → b 직선 궤적(순간이동 · 돌진 잔광선)
	// age0: 이미 흐른 시간(초) — 늦게 도착한 알림 · 확인용 표에서 나이를 맞출 때
	void Streak(Vector2 aIso, float az, Vector2 bIso, float bz, const FollowSpec& s, float age0 = 0.f);

	void Update(float dt);
	void Draw();   // 반투명 월드 단계(깊이 테스트 O · 쓰기 X). 빛 전용 모드에서는 더하기 궤적만
	void Clear();
	[[nodiscard]] int Count();
	void Shutdown();
}
