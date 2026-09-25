#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// 샌드박스 전용 시계 — build_server.sh가 복사한 서버 소스의 std::chrono::steady_clock을
// 이 시계로 바꿔 넣는다(-include). MSVC 빌드는 이 파일을 모른다.
//
//  · 환경 변수 HARVEN_CLOCK이 없으면: steady_clock 그대로(보통 실행).
//  · HARVEN_CLOCK=<파일>이면: 그 파일 첫 8바이트(int64 나노초)를 "지금"으로 읽는다.
//    녹화하는 클라이언트(--sandbox-clock)가 프레임마다 1/fps초씩 이 값을 올린다 →
//    서버 · 봇 · 클라이언트가 한 시계로 움직인다(느린 소프트웨어 렌더링에서도 30fps 영상의
//    시간 간격이 실제 게임과 같다). 클라이언트가 멈추면 세상도 멈춘다.
// ─────────────────────────────────────────────────────────────────────────────
#include <chrono>
#include <cstdint>

struct sandbox_clock
{
	using rep = long long;
	using period = std::nano;
	using duration = std::chrono::nanoseconds;
	using time_point = std::chrono::time_point<sandbox_clock>;
	static constexpr bool is_steady = true;
	static time_point now() noexcept;
};
