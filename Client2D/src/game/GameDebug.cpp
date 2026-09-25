#include "game/Game.h"
#include "fx/Particles.h"
#include "core/Perf.h"
#include "core/Recorder.h"
#include "core/Clock.h"
#include "core/Util.h"
#include "ui/Font.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// F3 — 개발 정보(프레임 시간 그래프 · CPU 시간 · 드로우콜 · 입자 · 네트워크 · 이동 예측).
// 판정 · 조작과는 무관하다. 녹화(--record)에 함께 찍혀 전/후 비교의 근거가 된다.
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	std::string Fmt(const char* f, ...)
	{
		char buf[256];
		va_list ap;
		va_start(ap, f);
		std::vsnprintf(buf, sizeof(buf), f, ap);
		va_end(ap);
		return buf;
	}
}

void Game::DrawDebugOverlay()
{
	// 네트워크 처리량(1초마다)
	if (m_now - m_netRateAt >= 1.0)
	{
		const double span = m_netRateAt > 0.0 ? m_now - m_netRateAt : 1.0;
		m_netInKBs = static_cast<float>((m_net.BytesIn() - m_netBytesInPrev) / 1024.0 / span);
		m_netOutKBs = static_cast<float>((m_net.BytesOut() - m_netBytesOutPrev) / 1024.0 / span);
		m_netPps = static_cast<float>((m_net.PacketsIn() - m_netPacketsPrev) / span);
		m_netBytesInPrev = m_net.BytesIn();
		m_netBytesOutPrev = m_net.BytesOut();
		m_netPacketsPrev = m_net.PacketsIn();
		m_netRateAt = m_now;
	}

	const float W = 330.f;
	const Rectangle r{ 12.f, 196.f, W, 314.f };
	ui::Plate(r, util::WithA(ui::theme::kInk, 0.86f), ui::theme::kLineStrong, 4.f, 1.f);

	ui::TextStyle st;
	st.face = ui::Face::Number;
	st.size = 13.f;
	st.color = ui::theme::kText;
	float y = r.y + 8.f;
	auto line = [&](const std::string& s, Color c = ui::theme::kText)
	{
		st.color = c;
		ui::Fonts().Draw(s, Vector2{ r.x + 10.f, y }, st);
		y += 16.f;
	};

	// ── 프레임 ──
	float avg = 0.f, worst = 0.f, cpuAvg = 0.f;
	const int n = std::min(perf::Count(), 60);
	for (int i = 0; i < n; ++i)
	{
		avg += perf::FrameMs(i);
		cpuAvg += perf::WorkMs(i);
		worst = std::max(worst, perf::FrameMs(i));
	}
	if (n > 0) { avg /= n; cpuAvg /= n; }
	const int dc = perf::DrawCallsLast();
	if (clk::Fixed())
		line(Fmt("FIXED %.0f fps (%s)   render %.0f ms   cpu %.1f ms", 1.f / std::max(0.001f, clk::FrameDt()), clk::Shared() ? "shared clock" : "fixed step", avg, cpuAvg),
			ui::theme::kOldway);
	else
		line(Fmt("FPS %.0f   frame %.2f ms (max %.1f)   cpu %.2f ms", avg > 0.f ? 1000.f / avg : 0.f, avg, worst, cpuAvg),
			avg > 16.8f ? ui::theme::kWarn : ui::theme::kTextHi);
	line(dc >= 0 ? Fmt("draw calls %d   particles %d", dc, static_cast<int>(fx::Particles().Count()))
		: Fmt("draw calls ?   particles %d", static_cast<int>(fx::Particles().Count())));

	// 프레임 시간 그래프(오른쪽이 최신) — 16.6ms · 33.3ms 선
	{
		const Rectangle g{ r.x + 10.f, y + 2.f, W - 20.f, 58.f };
		DrawRectangleRec(g, util::WithA(ui::theme::kSurface1, 0.9f));
		const float maxMs = 50.f;
		auto yOf = [&](float ms) { return g.y + g.height - std::min(ms, maxMs) / maxMs * g.height; };
		DrawLineEx(Vector2{ g.x, yOf(16.67f) }, Vector2{ g.x + g.width, yOf(16.67f) }, 1.f, util::WithA(ui::theme::kGood, 0.7f));
		DrawLineEx(Vector2{ g.x, yOf(33.3f) }, Vector2{ g.x + g.width, yOf(33.3f) }, 1.f, util::WithA(ui::theme::kBad, 0.7f));
		const int cnt = std::min(perf::Count(), perf::kHistory);
		const float bw = g.width / perf::kHistory;
		for (int i = 0; i < cnt; ++i)
		{
			const float x = g.x + g.width - (i + 1) * bw;
			const float fm = perf::FrameMs(i), wm = perf::WorkMs(i);
			const Color fc = fm > 33.3f ? ui::theme::kBad : (fm > 16.8f ? ui::theme::kWarn : ui::theme::kOldway);
			DrawRectangleRec(Rectangle{ x, yOf(fm), std::max(1.f, bw), g.y + g.height - yOf(fm) }, util::WithA(fc, 0.55f));
			DrawRectangleRec(Rectangle{ x, yOf(wm), std::max(1.f, bw), 1.5f }, ui::theme::kTextHi);
		}
		y += 64.f;
	}

	// ── 개체 ──
	int players = 0, monsters = 0, items = 0;
	for (const auto& [id, e] : m_ents)
	{
		if (e.kind == EntityKind::Player) ++players;
		else if (e.kind == EntityKind::Monster) ++monsters;
		else ++items;
	}
	line(Fmt("entities  players %d  monsters %d  items %d  npc %d", players, monsters, items, static_cast<int>(m_npcView.size())));

	// ── 네트워크 ──
	line(Fmt("net in %.1f KB/s  out %.2f KB/s  %.0f pkt/s", m_netInKBs, m_netOutKBs, m_netPps));
	line(Fmt("move rtt %.0f ms (last %.0f)  pending %d", m_moveRttMs, m_moveRttLastMs, m_mover.PendingCount()));
	if (m_net.NetSimOn())
	{
		const auto& sim = m_net.GetNetSim();
		line(Fmt("netsim latency=%d jitter=%d spike=%.1f%%  spikes %u", sim.latencyMs, sim.jitterMs, sim.spikePercent, m_net.SpikesIn()), ui::theme::kWarn);
	}

	// ── 이동 ──
	const auto& ms = m_mover.GetStats();
	const Vector2 lg = m_mover.Logical(), vs = m_mover.Visual();
	line(Fmt("move  srv %d,%d  pred %d,%d  speed %.2f t/s", m_srvX, m_srvY, m_predX, m_predY, m_mover.Speed()));
	line(Fmt("      logical-visual %.3f  steps %d  acks %d", util::Len(util::Sub(lg, vs)), ms.steps, ms.acks));
	line(Fmt("      rejects %d  corrections %d (max %.2f)  tp %d", ms.rejects, ms.corrections, ms.maxCorrection, ms.teleports),
		ms.rejects > 0 || ms.corrections > 0 ? ui::theme::kWarn : ui::theme::kText);
	line(Fmt("zoom %.2f  hover %d,%d", m_cam.Zoom(), m_hoverX, m_hoverY));
	if (rec::Recording())
		line(Fmt("REC %s", rec::Get().dir.c_str()), ui::theme::kBad);
}
