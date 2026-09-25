// 샌드박스 전용 시계 구현 — SandboxClock.h 참고.
#include "SandboxClock.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace
{
	const volatile int64_t* g_shared = nullptr;
	std::once_flag g_once;

	void Attach()
	{
		const char* path = std::getenv("HARVEN_CLOCK");
		if (path == nullptr || path[0] == '\0')
			return;
		// 리눅스 경로(/dev/shm/...)는 Wine의 Z: 드라이브로 연다
		std::string p = path;
		if (!p.empty() && p[0] == '/')
		{
			p = "Z:" + p;
			for (char& c : p) if (c == '/') c = '\\';
		}
		HANDLE f = CreateFileA(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
		if (f == INVALID_HANDLE_VALUE)
		{
			std::printf("[sandbox-clock] %s 을(를) 열 수 없음 — 실제 시계를 쓴다\n", p.c_str());
			return;
		}
		HANDLE m = CreateFileMappingA(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
		if (m == nullptr)
			return;
		g_shared = static_cast<const volatile int64_t*>(MapViewOfFile(m, FILE_MAP_READ, 0, 0, 16));
		std::printf("[sandbox-clock] 공유 시계 %s (지금 %.3f초)\n", p.c_str(), g_shared ? static_cast<double>(g_shared[0]) / 1e9 : -1.0);
		std::fflush(stdout);
	}
}

sandbox_clock::time_point sandbox_clock::now() noexcept
{
	std::call_once(g_once, Attach);
	if (g_shared != nullptr)
		return time_point(duration(g_shared[0]));
	return time_point(std::chrono::duration_cast<duration>(std::chrono::steady_clock::now().time_since_epoch()));
}
