#include "core/Recorder.h"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#if defined(_WIN32)
#define REC_POPEN _popen
#define REC_PCLOSE _pclose
#define REC_NULL " > NUL 2>&1"
#else
#define REC_POPEN popen
#define REC_PCLOSE pclose
#define REC_NULL " > /dev/null 2>&1"
#endif

namespace rec
{
	namespace
	{
		Options g_opt;
		bool g_armed = false, g_open = false, g_done = false;
		double g_startAt = 0.0;
		int g_written = 0, g_total = 0;
		int g_w = 0, g_h = 0;        // 저장 크기
		std::FILE* g_pipe = nullptr;
		std::vector<unsigned char> g_scaled;

		bool HaveFfmpeg()
		{
			return std::system("ffmpeg -version" REC_NULL) == 0;
		}

		// 상자 필터로 줄이기(RGBA → RGBA)
		void Downscale(const unsigned char* src, int sw, int sh, int dw, int dh, std::vector<unsigned char>& out)
		{
			out.assign(static_cast<size_t>(dw) * dh * 4, 0);
			for (int y = 0; y < dh; ++y)
			{
				const int y0 = y * sh / dh, y1 = std::max(y0 + 1, (y + 1) * sh / dh);
				for (int x = 0; x < dw; ++x)
				{
					const int x0 = x * sw / dw, x1 = std::max(x0 + 1, (x + 1) * sw / dw);
					unsigned sum[4] = { 0, 0, 0, 0 };
					int n = 0;
					for (int yy = y0; yy < y1; ++yy)
						for (int xx = x0; xx < x1; ++xx)
						{
							const unsigned char* p = src + (static_cast<size_t>(yy) * sw + xx) * 4;
							sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; sum[3] += p[3];
							++n;
						}
					unsigned char* d = out.data() + (static_cast<size_t>(y) * dw + x) * 4;
					for (int c = 0; c < 4; ++c) d[c] = static_cast<unsigned char>(sum[c] / static_cast<unsigned>(n));
				}
			}
		}

		void Open()
		{
			g_open = true;
			std::error_code ec;
			std::filesystem::create_directories(g_opt.dir, ec);
			const int sw = GetRenderWidth(), sh = GetRenderHeight();
			const float s = std::clamp(g_opt.scale, 0.25f, 1.f);
			g_w = std::max(16, static_cast<int>(std::lround(sw * s)) & ~1);
			g_h = std::max(16, static_cast<int>(std::lround(sh * s)) & ~1);
			g_total = std::max(1, static_cast<int>(std::lround(g_opt.seconds * g_opt.fps)));
			if (!g_opt.frames && HaveFfmpeg())
			{
				char cmd[1024];
				std::snprintf(cmd, sizeof(cmd),
					"ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - -c:v libx264 -preset veryfast -crf 14 -pix_fmt yuv420p \"%s/capture.mp4\"",
					g_w, g_h, g_opt.fps, g_opt.dir.c_str());
#if defined(_WIN32)
				g_pipe = REC_POPEN(cmd, "wb");
#else
				g_pipe = REC_POPEN(cmd, "w");
#endif
			}
			std::printf("[record] %s %dx%d %dfps %d frames (%s)\n", g_opt.dir.c_str(), g_w, g_h, g_opt.fps, g_total, g_pipe ? "ffmpeg → capture.mp4" : "ppm frames");
			std::fflush(stdout);
		}

		void WriteFrame(const unsigned char* rgba)
		{
			if (g_pipe)
			{
				std::fwrite(rgba, 1, static_cast<size_t>(g_w) * g_h * 4, g_pipe);
				return;
			}
			char path[1024];
			std::snprintf(path, sizeof(path), "%s/frame_%05d.ppm", g_opt.dir.c_str(), g_written + 1);
			std::FILE* f = std::fopen(path, "wb");
			if (!f) return;
			std::fprintf(f, "P6\n%d %d\n255\n", g_w, g_h);
			std::vector<unsigned char> rgb(static_cast<size_t>(g_w) * g_h * 3);
			for (size_t i = 0, n = static_cast<size_t>(g_w) * g_h; i < n; ++i)
			{
				rgb[i * 3 + 0] = rgba[i * 4 + 0];
				rgb[i * 3 + 1] = rgba[i * 4 + 1];
				rgb[i * 3 + 2] = rgba[i * 4 + 2];
			}
			std::fwrite(rgb.data(), 1, rgb.size(), f);
			std::fclose(f);
		}
	}

	void Configure(const Options& o) { g_opt = o; }
	const Options& Get() { return g_opt; }
	bool Enabled() { return !g_opt.dir.empty(); }
	bool Recording() { return Enabled() && g_open && !g_done; }
	bool Finished() { return g_done; }
	bool FixedStep() { return Enabled() && g_opt.fixedStep; }
	float StepSeconds() { return 1.f / static_cast<float>(std::max(1, g_opt.fps)); }

	double StartAt() { return g_armed ? g_startAt : -1.0; }

	void Arm(double now)
	{
		if (!Enabled() || g_armed) return;
		g_armed = true;
		g_startAt = now + g_opt.after;
	}

	void Capture(double now)
	{
		if (!Enabled() || !g_armed || g_done || now < g_startAt)
			return;
		if (!g_open)
			Open();
		// 이번까지 떠야 할 장 수: 고정 시간 모드는 한 장씩, 아니면 벽시계 기준(느리면 되풀이)
		int due = FixedStep() ? g_written + 1 : static_cast<int>(std::floor((now - g_startAt) * g_opt.fps)) + 1;
		due = std::min(due, g_total);
		if (due <= g_written)
			return;
		rlDrawRenderBatchActive();
		const int sw = GetRenderWidth(), sh = GetRenderHeight();
		unsigned char* px = rlReadScreenPixels(sw, sh);
		if (!px) return;
		const unsigned char* frame = px;
		if (sw != g_w || sh != g_h)
		{
			Downscale(px, sw, sh, g_w, g_h, g_scaled);
			frame = g_scaled.data();
		}
		while (g_written < due)
		{
			WriteFrame(frame);
			++g_written;
		}
		RL_FREE(px);
		if (g_written >= g_total)
			Finish();
	}

	void Finish()
	{
		if (g_done) return;
		g_done = true;
		if (g_pipe)
		{
			REC_PCLOSE(g_pipe);
			g_pipe = nullptr;
		}
		if (g_open)
		{
			std::printf("[record] done %d frames → %s\n", g_written, g_opt.dir.c_str());
			std::fflush(stdout);
		}
	}
}
