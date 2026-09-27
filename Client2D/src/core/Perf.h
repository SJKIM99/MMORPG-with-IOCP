#pragma once

#include <cstdint>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// 프레임 성능 계측 — F3 오버레이와 자동 검증(--script · --record) 로그가 같이 쓴다.
//
//  · 프레임 시간: 한 프레임 전체(GetFrameTime — 수직 동기 · 대기 포함)
//  · CPU 시간: 프레임 시작 → EndDrawing 직전(갱신 + 그리기 명령 제출). 소프트웨어 렌더링
//    샌드박스에서는 이것 · 드로우콜 · 입자 수로 비교하고, 실제 GPU 시간은 실기에서 본다.
//  · 드로우콜: raylib가 쓰는 glad 함수 포인터(glDrawArrays · glDrawElements)를 한 번 감싸 센다.
//    raylib를 공유 라이브러리로 빌드했거나 심볼이 없으면 -1(모름).
// ─────────────────────────────────────────────────────────────────────────────
namespace perf
{
	inline constexpr int kHistory = 240;   // 그래프에 남기는 프레임 수(144fps에서 1.7초)

	void InstallGlHooks();                 // InitWindow 뒤 한 번
	void BeginFrame();                     // 프레임 맨 앞
	void MarkWorkEnd();                    // EndDrawing 직전(여기까지가 CPU 시간)
	void EndFrame(float frameSec);         // EndDrawing 뒤(GetFrameTime)

	[[nodiscard]] int DrawCallsLast();     // 직전 프레임 드로우콜(-1 = 모름)
	[[nodiscard]] int DrawCallsNow();      // 이번 프레임 지금까지
	[[nodiscard]] float FrameMs(int ago);  // ago = 0 직전 프레임
	[[nodiscard]] float WorkMs(int ago);
	[[nodiscard]] int Count();             // 기록된 프레임 수(최대 kHistory)

	// 실행 전체 요약(시작 1초는 버린다 — 에셋 로딩 · 셰이더 컴파일)
	struct Summary
	{
		int   frames = 0;
		float avgMs = 0, p95Ms = 0, p99Ms = 0, maxMs = 0;
		float avgWorkMs = 0, p95WorkMs = 0, maxWorkMs = 0;
		int   avgDrawCalls = 0, maxDrawCalls = 0;
		int   maxParticles = 0;
		int   over33 = 0;      // 33ms를 넘긴 프레임(눈에 보이는 끊김)
	};
	void NoteParticles(int n);
	[[nodiscard]] Summary Summarize();
	void Print(std::FILE* out, const char* tag);
	// 메모리(리눅스 /proc/self/status의 VmRSS · VmHWM — Windows에서는 "?")를 [mem] 한 줄로
	void PrintMem(const char* tag);
}
