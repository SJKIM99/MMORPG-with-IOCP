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
		bool g_follow = false;                  // 따라가기만(FollowShared)
		float g_jitter = 0.f, g_hitch = 0.f;
		uint32_t g_rng = 0x9E3779B9u;
		float Rand01() { g_rng = g_rng * 1664525u + 1013904223u; return static_cast<float>(g_rng >> 8) / 16777216.f; }
		float Shake(float step)
		{
			float dt = step;
			if (g_jitter > 0.f) dt *= 1.f + (Rand01() * 2.f - 1.f) * g_jitter;
			if (g_hitch > 0.f && Rand01() * 100.f < g_hitch) dt *= 3.f;
			return dt;
		}
	}

	void SetFrameJitter(float jitter, float hitchPercent) { g_jitter = jitter; g_hitch = hitchPercent; }

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

	bool FollowShared(const char* path)
	{
		if (!AttachShared(path, 30))
			return false;
		g_follow = true;
		std::printf("[sandbox-clock] 따라가기(시계를 올리지 않는다)\n");
		return true;
	}

	void BeginFrame()
	{
		if (g_shared != nullptr && g_follow)
		{
			const double t = static_cast<double>(g_shared[0]) / 1e9;
			const double d = t - g_now;
			g_dt = static_cast<float>(d < 0.0 ? 0.0 : (d > 0.25 ? 0.25 : d));
			if (t > g_now) g_now = t;
			return;
		}
		if (g_shared != nullptr)
		{
			g_dt = Shake(g_sharedStep);
			g_now += g_dt;
			g_shared[0] = static_cast<int64_t>(g_now * 1e9);
			return;
		}
		if (rec::FixedStep())
		{
			g_dt = Shake(rec::StepSeconds());
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
