#include "core/Perf.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <vector>

#if defined(_WIN32)
#define PERF_GLAPIENTRY __stdcall
#else
#define PERF_GLAPIENTRY
#endif

// raylib(rlgl)가 쓰는 glad 함수 포인터 — 정적 라이브러리 안의 전역 변수다(external/glad.h).
extern "C"
{
	extern void (PERF_GLAPIENTRY* glad_glDrawArrays)(unsigned int mode, int first, int count);
	extern void (PERF_GLAPIENTRY* glad_glDrawElements)(unsigned int mode, int count, unsigned int type, const void* indices);
}

namespace perf
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		void (PERF_GLAPIENTRY* g_realDrawArrays)(unsigned int, int, int) = nullptr;
		void (PERF_GLAPIENTRY* g_realDrawElements)(unsigned int, int, unsigned int, const void*) = nullptr;
		int g_calls = 0;
		int g_callsLast = -1;
		bool g_hooked = false;

		void PERF_GLAPIENTRY CountDrawArrays(unsigned int mode, int first, int count)
		{
			++g_calls;
			g_realDrawArrays(mode, first, count);
		}
		void PERF_GLAPIENTRY CountDrawElements(unsigned int mode, int count, unsigned int type, const void* indices)
		{
			++g_calls;
			g_realDrawElements(mode, count, type, indices);
		}

		Clock::time_point g_frameStart{};
		Clock::time_point g_runStart = Clock::now();
		float g_workMs = 0.f;
		std::array<float, kHistory> g_frame{}, g_work{};
		int g_head = 0, g_count = 0;

		// 요약용(시작 1초 뒤부터 전부)
		std::vector<float> g_allFrame, g_allWork;
		long long g_callSum = 0;
		int g_callMax = 0, g_callFrames = 0;
		int g_partMax = 0;

		float Percentile(std::vector<float> v, float p)
		{
			if (v.empty()) return 0.f;
			const size_t k = std::min(v.size() - 1, static_cast<size_t>(p * static_cast<float>(v.size() - 1) + 0.5f));
			std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
			return v[k];
		}
	}

	void InstallGlHooks()
	{
		if (g_hooked || glad_glDrawArrays == nullptr || glad_glDrawElements == nullptr)
			return;
		g_realDrawArrays = glad_glDrawArrays;
		g_realDrawElements = glad_glDrawElements;
		glad_glDrawArrays = &CountDrawArrays;
		glad_glDrawElements = &CountDrawElements;
		g_hooked = true;
	}

	void BeginFrame()
	{
		g_frameStart = Clock::now();
		g_callsLast = g_hooked ? g_calls : -1;
		g_calls = 0;
	}

	void MarkWorkEnd()
	{
		g_workMs = std::chrono::duration<float, std::milli>(Clock::now() - g_frameStart).count();
	}

	void EndFrame(float frameSec)
	{
		const float ms = frameSec * 1000.f;
		g_head = (g_head + 1) % kHistory;
		g_frame[static_cast<size_t>(g_head)] = ms;
		g_work[static_cast<size_t>(g_head)] = g_workMs;
		g_count = std::min(g_count + 1, kHistory);
		if (std::chrono::duration<float>(Clock::now() - g_runStart).count() > 1.f)
		{
			g_allFrame.push_back(ms);
			g_allWork.push_back(g_workMs);
			if (g_hooked)
			{
				g_callSum += g_calls;
				g_callMax = std::max(g_callMax, g_calls);
				++g_callFrames;
			}
		}
	}

	int DrawCallsLast() { return g_callsLast; }
	int DrawCallsNow() { return g_hooked ? g_calls : -1; }
	int Count() { return g_count; }

	float FrameMs(int ago)
	{
		if (ago < 0 || ago >= g_count) return 0.f;
		return g_frame[static_cast<size_t>((g_head - ago + kHistory) % kHistory)];
	}

	float WorkMs(int ago)
	{
		if (ago < 0 || ago >= g_count) return 0.f;
		return g_work[static_cast<size_t>((g_head - ago + kHistory) % kHistory)];
	}

	void NoteParticles(int n) { g_partMax = std::max(g_partMax, n); }

	Summary Summarize()
	{
		Summary s;
		s.frames = static_cast<int>(g_allFrame.size());
		if (s.frames == 0) return s;
		double sum = 0.0, wsum = 0.0;
		for (float f : g_allFrame) { sum += f; s.maxMs = std::max(s.maxMs, f); if (f > 33.4f) ++s.over33; }
		for (float w : g_allWork) { wsum += w; s.maxWorkMs = std::max(s.maxWorkMs, w); }
		s.avgMs = static_cast<float>(sum / s.frames);
		s.avgWorkMs = static_cast<float>(wsum / s.frames);
		s.p95Ms = Percentile(g_allFrame, 0.95f);
		s.p99Ms = Percentile(g_allFrame, 0.99f);
		s.p95WorkMs = Percentile(g_allWork, 0.95f);
		s.avgDrawCalls = g_callFrames > 0 ? static_cast<int>(g_callSum / g_callFrames) : -1;
		s.maxDrawCalls = g_callFrames > 0 ? g_callMax : -1;
		s.maxParticles = g_partMax;
		return s;
	}

	void Print(std::FILE* out, const char* tag)
	{
		const Summary s = Summarize();
		std::fprintf(out, "[%s] frames=%d frame avg=%.2fms p95=%.2fms p99=%.2fms max=%.2fms over33=%d | cpu avg=%.2fms p95=%.2fms max=%.2fms | drawcalls avg=%d max=%d | particles max=%d\n",
			tag, s.frames, s.avgMs, s.p95Ms, s.p99Ms, s.maxMs, s.over33, s.avgWorkMs, s.p95WorkMs, s.maxWorkMs, s.avgDrawCalls, s.maxDrawCalls, s.maxParticles);
		std::fflush(out);
	}
}
