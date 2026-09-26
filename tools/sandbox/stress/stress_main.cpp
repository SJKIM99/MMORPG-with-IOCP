// ─────────────────────────────────────────────────────────────────────────────
// STRESS_TEST 머리 없는 실행(샌드박스 전용) — STRESS_TEST/NetworkModule.cpp를 그대로 링크하고,
// 그리기(DrawModule · OpenGL) 대신 1초마다 수치를 찍는다. 서버 변경 전/후를 같은 조건에서 비교하려고 만든다.
//
//   stress.exe <초> [출력 csv]
//     csv: 초, 접속 수, 활성 봇, 지연 EMA(ms), 지연 평균, 단계(0 증가 · 1 안정 · 2 감소), 보이는 몬스터 · 플레이어 평균
//
// 원래 프로그램과 같은 봇 행동 · 같은 증가 규칙(지연 100ms에서 멈추고 150ms에서 줄인다)이다.
// 샌드박스는 CPU 2개 + Wine이라 숫자의 크기는 실제 서버와 다르다 — 같은 샌드박스에서의 전/후 비율만 본다.
// ─────────────────────────────────────────────────────────────────────────────
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "NetworkModule.h"

HWND hWnd = nullptr;

int main(int argc, char** argv)
{
	const int seconds = argc > 1 ? std::atoi(argv[1]) : 60;
	FILE* csv = argc > 2 ? std::fopen(argv[2], "w") : nullptr;
	if (csv) std::fprintf(csv, "sec,connections,active,delay_ema,delay_avg,phase,vis_monsters,vis_players\n");
	InitializeNetwork();
	int peakStable = 0;
	long long delaySum = 0;
	int delayN = 0;
	for (int s = 1; s <= seconds; ++s)
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
		const int conn = num_connections.load(), act = active_clients.load();
		const int ema = global_delay.load(), avg = delay_avg_val.load();
		const int ph = static_cast<int>(current_phase.load());
		if (ema <= 100 && act > peakStable) peakStable = act;
		if (s > seconds / 2) { delaySum += ema; ++delayN; }
		std::printf("[stress] %4ds conn=%d active=%d delay=%dms avg=%dms phase=%d monsters=%.1f players=%.1f\n",
			s, conn, act, ema, avg, ph, g_avg_visible_monsters.load(), g_avg_visible_players.load());
		std::fflush(stdout);
		if (csv) { std::fprintf(csv, "%d,%d,%d,%d,%d,%d,%.2f,%.2f\n", s, conn, act, ema, avg, ph, g_avg_visible_monsters.load(), g_avg_visible_players.load()); std::fflush(csv); }
	}
	std::printf("[stress] summary peak_active_at_delay<=100ms=%d mean_delay_2nd_half=%.1fms\n", peakStable, delayN ? static_cast<double>(delaySum) / delayN : 0.0);
	if (csv) std::fclose(csv);
	std::fflush(stdout);
	std::_Exit(0);
}
