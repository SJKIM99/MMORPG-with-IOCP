#pragma once

#include <cstdint>
#include "Data/WorldData.h"

// ─────────────────────────────────────────────────────────────────────────────
// 이동 규칙 — 서버·클라 공용.
//
// 클라이언트는 이 규칙으로 서버 결과를 "그대로" 예측한다. 서버의
// SubjectHelper::MovePositionByDirection과 결과가 한 칸이라도 다르면 예측이 틀려
// 고무줄 현상이 생기므로, 두 쪽 모두 아래 Apply를 기준으로 삼는다.
//
// 속도: 걸음 비용은 **실제 이동 거리**에 비례한다. 대각선(두 축을 동시에 한 칸)은
// 직선보다 √2배 길다 — 예전처럼 같은 시간을 주면 대각선이 41% 빠른 이동이 된다.
//
// 검증(서버, 확장 세션): 토큰 버킷.
//   start = max(clock, now - kCatchupMs)   ← 늦게 도착한 패킷이 잃은 시간은 되찾는다
//   start <= now + kSlackMs 이면 허용      ← 지터로 몰려 온 패킷도 정상 속도면 통과
//   clock = start + 걸음 비용
// 정상 클라이언트는 몇 백 ms 지터에도 거절되지 않고, 속도 핵은 slack만큼만 앞설 수 있다.
//
// kCatchupMs = 500(Phase 1-C, 예전 200): TCP 재전송 한 번(200~450ms)이면 그 뒤 걸음들이 한꺼번에 도착한다.
//   200ms로는 몰려 온 대각선 걸음(395ms)이 TooFast로 거절돼 1~1.4칸 되돌림이 생겼다
//   (모의 시험 --netsim 150/40/2: 6분에 11번 → 500ms에서 0번). 오래 쉬었다가 한꺼번에 보낼 수 있는 양이
//   350ms → 650ms어치(직선 두세 걸음)로 늘 뿐 평균 속도의 상한은 그대로다. 확장 세션(USER_MOVE_EX_REQ)만 쓰는 값 —
//   레거시 USER_MOVE_REQ(STRESS_TEST 봇)는 따로 쿨타임을 본다.
// ─────────────────────────────────────────────────────────────────────────────
namespace MoveRule
{
	inline constexpr uint32_t kBaseStepMs       = 280;    // 이동속도 100% 기준 직선 한 칸
	inline constexpr uint32_t kDiagPermille     = 1414;   // 대각선 = 직선 × 1.414
	inline constexpr uint32_t kHasteBonusPercent = 35;    // 바람걸음 물약
	inline constexpr uint32_t kMinSpeedPercent  = 40;
	inline constexpr uint32_t kMaxSpeedPercent  = 200;
	inline constexpr uint32_t kSlackMs          = 150;
	inline constexpr uint32_t kCatchupMs        = 500;

	// 0:UP 1:DOWN 2:LEFT 3:RIGHT 4:UP-LEFT 5:UP-RIGHT 6:DOWN-LEFT 7:DOWN-RIGHT
	[[nodiscard]] constexpr bool IsValidDir(int dir) noexcept { return dir >= 0 && dir <= 7; }
	[[nodiscard]] constexpr bool IsDiagonal(int dir) noexcept { return dir >= 4 && dir <= 7; }

	constexpr void Delta(int dir, int& dx, int& dy) noexcept
	{
		constexpr int kDx[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
		constexpr int kDy[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };
		dx = IsValidDir(dir) ? kDx[dir] : 0;
		dy = IsValidDir(dir) ? kDy[dir] : 0;
	}

	[[nodiscard]] constexpr int DirOf(int dx, int dy) noexcept
	{
		if (dx == 0 && dy < 0) return 0;
		if (dx == 0 && dy > 0) return 1;
		if (dx < 0 && dy == 0) return 2;
		if (dx > 0 && dy == 0) return 3;
		if (dx < 0 && dy < 0) return 4;
		if (dx > 0 && dy < 0) return 5;
		if (dx < 0 && dy > 0) return 6;
		if (dx > 0 && dy > 0) return 7;
		return -1;
	}

	[[nodiscard]] constexpr uint32_t ClampSpeed(uint32_t speedPercent) noexcept
	{
		return speedPercent < kMinSpeedPercent ? kMinSpeedPercent : (speedPercent > kMaxSpeedPercent ? kMaxSpeedPercent : speedPercent);
	}

	// 한 걸음 비용(ms)
	[[nodiscard]] constexpr uint32_t StepMs(int dir, uint32_t speedPercent) noexcept
	{
		const uint32_t base = kBaseStepMs * 100u / ClampSpeed(speedPercent);
		return IsDiagonal(dir) ? base * kDiagPermille / 1000u : base;
	}

	// 서버와 같은 규칙: 대각선은 y축 → x축 순서로 한 축씩 따로 막힘을 검사한다
	// (한 축이 막혀도 다른 축으로 미끄러진다).
	template <class BlockedFn>
	constexpr void Apply(int& x, int& y, int dir, BlockedFn&& blocked) noexcept
	{
		int dx = 0, dy = 0;
		Delta(dir, dx, dy);
		if (dy != 0 && !blocked(x, y + dy)) y += dy;
		if (dx != 0 && !blocked(x + dx, y)) x += dx;
	}

	constexpr void Apply(int& x, int& y, int dir) noexcept
	{
		Apply(x, y, dir, [](int bx, int by) { return WorldData::IsBlocked(bx, by); });
	}

	static_assert(StepMs(0, 100) == 280 && StepMs(4, 100) == 395);
}
