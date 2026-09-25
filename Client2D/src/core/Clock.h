#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 게임 시계 — 보통은 raylib 시계(GetTime · GetFrameTime) 그대로.
// 녹화의 고정 시간 모드(--record … --fixed-step)에서는 한 프레임 = 정확히 1/fps초로
// 게임 시간을 진행한다(그리기가 느려도 움직임 규칙은 같은 간격으로 재생된다).
// ─────────────────────────────────────────────────────────────────────────────
// 샌드박스 공유 시계(--sandbox-clock /dev/shm/harven_clock): 서버(tools/sandbox, HARVEN_CLOCK)와
// 봇이 같은 파일의 int64 나노초를 "지금"으로 읽는다. 이 클라이언트가 프레임마다 1/fps초씩 올린다
// → 그리기가 아무리 느려도 서버 · 봇 · 클라이언트가 같은 시간 흐름을 본다(녹화 전용, POSIX만).
namespace clk
{
	bool AttachShared(const char* path, int fps);   // 실패하면 false(보통 시계 그대로)
	void BeginFrame();           // 프레임 맨 앞에서 한 번
	[[nodiscard]] double Now();  // 초
	[[nodiscard]] float FrameDt();
	[[nodiscard]] bool Fixed();
	[[nodiscard]] bool Shared();
}
