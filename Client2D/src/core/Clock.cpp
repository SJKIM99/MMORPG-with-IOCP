#include "core/Clock.h"
#include "core/Recorder.h"

#include "raylib.h"

#include <cstdint>
#include <cstdio>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace clk
{
	namespace
	{
		double g_now = 0.0;
		float g_dt = 0.f;
		bool g_started = false;
		volatile int64_t* g_shared = nullptr;   // 공유 시계(나노초)
		float g_sharedStep = 1.f / 30.f;
	}

	bool AttachShared(const char* path, int fps)
	{
#if defined(_WIN32)
		(void)path; (void)fps;
		std::printf("[sandbox-clock] Windows에서는 쓰지 않는다(샌드박스 녹화 전용)\n");
		return false;
#else
		const int fd = open(path, O_RDWR);
		if (fd < 0)
		{
			std::printf("[sandbox-clock] %s 을(를) 열 수 없음\n", path);
			return false;
		}
		void* p = mmap(nullptr, 16, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		close(fd);
		if (p == MAP_FAILED)
			return false;
		g_shared = static_cast<volatile int64_t*>(p);
		g_sharedStep = 1.f / static_cast<float>(fps > 0 ? fps : 30);
		g_now = static_cast<double>(g_shared[0]) / 1e9;
		g_started = true;
		std::printf("[sandbox-clock] %s 공유 — %.3f초부터 %d fps로 진행\n", path, g_now, fps);
		std::fflush(stdout);
		return true;
#endif
	}

	void BeginFrame()
	{
		if (g_shared != nullptr)
		{
			g_dt = g_sharedStep;
			g_now += g_dt;
			g_shared[0] = static_cast<int64_t>(g_now * 1e9);
			return;
		}
		if (rec::FixedStep())
		{
			g_dt = rec::StepSeconds();
			g_now = g_started ? g_now + g_dt : GetTime();
			g_started = true;
			return;
		}
		g_now = GetTime();
		g_dt = GetFrameTime();
		g_started = true;
	}

	double Now() { return g_started ? g_now : GetTime(); }
	float FrameDt() { return g_dt; }
	bool Fixed() { return g_shared != nullptr || rec::FixedStep(); }
	bool Shared() { return g_shared != nullptr; }
}
