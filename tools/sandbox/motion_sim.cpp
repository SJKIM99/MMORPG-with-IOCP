// ─────────────────────────────────────────────────────────────────────────────
// 이동 모의 시험(Phase 1-C) — 게임 · 서버 없이 game/Motion을 30fps로 돌려 이동 기록(csv)을 만든다.
//   tools/record/analyze_trace.py로 멈칫 · 속도 · 방향 깜빡임을 잰다(녹화 전에 빨리 조율하는 용도).
//
//   motion_sim ray <각도°(칸 좌표)> [초]          누른 채 곧게(커서 광선) — 직선 · 대각 · 임의 각
//   motion_sim circle [초]                        원 그리기(runcircle과 같은 조준: 반지름 5, 50° 앞)
//   motion_sim remote [초] [지연ms] [지터ms] [스파이크%]   원을 도는 다른 플레이어를 --netsim처럼 받아 그린다
//   공통 옵션: --latency ms --jitter ms --spike % (내 이동의 왕복), --out 파일
//
// 서버 쪽은 확장 세션의 토큰 버킷(Data/MoveRule.h)을 그대로 흉내 낸다 — 거절(TooFast)도 센다.
// 빌드: tools/sandbox/build_motion_sim.sh
// ─────────────────────────────────────────────────────────────────────────────
#include "game/Motion.h"
#include "game/Path.h"
#include "Data/MoveRule.h"
#include "Data/WorldData.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <random>
#include <string>
#include <vector>

namespace
{
	constexpr double kDt = 1.0 / 30.0;
	std::mt19937 g_rng{ 1234 };
	double U(double a, double b) { return std::uniform_real_distribution<double>(a, b)(g_rng); }

	struct NetSim { double latency = 0.1, jitter = 0.0, spike = 0.0; };

	// 순서를 지키는 한 방향 통로(TCP 흉내): 늦은 패킷 뒤의 패킷은 앞지르지 못한다
	struct Pipe
	{
		NetSim sim;
		double lastDeliver = 0.0;
		double DeliverAt(double sendAt)
		{
			double d = sim.latency + U(-sim.jitter, sim.jitter);
			if (sim.spike > 0.0 && U(0, 100) < sim.spike) d += U(0.2, 0.45);
			const double at = std::max(lastDeliver, sendAt + std::max(0.0, d));
			lastDeliver = at;
			return at;
		}
	};


	// Game::PlanDir와 같은 DDA
	int PlanDir(int x, int y, Vector2 origin, Vector2 u)
	{
		int best = -1;
		float bestScore = 1e9f;
		for (int d = 0; d < 8; ++d)
		{
			int dx = 0, dy = 0;
			MoveRule::Delta(d, dx, dy);
			const float len = (dx != 0 && dy != 0) ? 1.41421356f : 1.f;
			const float fwd = (dx * u.x + dy * u.y) / len;
			if (fwd < 0.38f || !path::CanStep(x, y, dx, dy)) continue;
			const float rx = static_cast<float>(x + dx) - origin.x, ry = static_cast<float>(y + dy) - origin.y;
			const float score = std::fabs(rx * u.y - ry * u.x) - fwd * 0.05f;
			if (score < bestScore) { bestScore = score; best = d; }
		}
		return best;
	}

	// Game::PickMoveDir와 같은 미끄러짐: 원하는 8방향이 막혔으면 45° 안쪽의 가장 가까운 열린 방향
	int PickMoveDir(int px, int py, int dx, int dy)
	{
		const int want = MoveRule::DirOf(dx, dy);
		if (want < 0) return -1;
		{
			int x = px, y = py;
			MoveRule::Apply(x, y, want);
			if (x != px || y != py)
			{
				const int straight = MoveRule::DirOf(x - px, y - py);
				return straight >= 0 ? straight : want;
			}
		}
		const float wl = std::sqrt(static_cast<float>(dx * dx + dy * dy));
		int best = -1;
		float bestDot = 0.55f;
		for (int d = 0; d < 8; ++d)
		{
			if (d == want) continue;
			int x = px, y = py;
			MoveRule::Apply(x, y, d);
			const int mx = x - px, my = y - py;
			if (mx == 0 && my == 0) continue;
			const float ml = std::sqrt(static_cast<float>(mx * mx + my * my));
			const float dot = (mx * dx + my * dy) / (ml * wl);
			if (dot > bestDot) { bestDot = dot; best = d; }
		}
		return best;
	}

	int PlanOrSlide(int x, int y, Vector2 origin, Vector2 u)
	{
		int d = PlanDir(x, y, origin, u);
		if (d >= 0) return d;
		const float a = std::atan2(u.y, u.x);
		const int k = static_cast<int>(std::lround(a / (3.14159265f * 0.25f)));
		const int dx = static_cast<int>(std::lround(std::cos(k * 3.14159265f * 0.25f))), dy = static_cast<int>(std::lround(std::sin(k * 3.14159265f * 0.25f)));
		return PickMoveDir(x, y, dx, dy);
	}

	// 서버: 확장 세션 이동 검증(토큰 버킷)
	double g_catchupMs = MoveRule::kCatchupMs;   // --catchup: 서버 규칙 값을 바꿔 보는 실험용
	bool g_botPace = false;
	bool g_noGuide = false;                      // --noguide: 안내선 없이(따라가기 · 쫓아가기처럼 논리 궤적 앞보기)
	double g_bgHz = 10.0;                         // --bg <Hz>: 배경 패킷(다른 개체 · 내 응답) 빈도
	bool g_bgPoisson = false;                     // --bgpoisson: 배경 패킷 간격을 들쭉날쭉하게(게임과 비슷하게)                       // --botpace: 원격 개체를 샌드박스 봇 박자로(공유 시계 경계 · 늦음 누적)
	struct Server
	{
		int x = 0, y = 0;
		double clock = -10.0;
		int rejects = 0;
		bool Move(int dir, double now, uint32_t speed, int& ox, int& oy)
		{
			const double cost = MoveRule::StepMs(dir, speed) / 1000.0;
			const double start = std::max(clock, now - g_catchupMs / 1000.0);
			if (start > now + MoveRule::kSlackMs / 1000.0) { ++rejects; ox = x; oy = y; return false; }
			clock = start + cost;
			MoveRule::Apply(x, y, dir);
			ox = x; oy = y;
			return true;
		}
	};

	Vector2 Norm(Vector2 v) { const float l = std::hypot(v.x, v.y); return l > 1e-6f ? Vector2{ v.x / l, v.y / l } : Vector2{ 1, 0 }; }

	// Game::RunLine과 같은 곧은 선: 방향이 5° 넘게 바뀌거나 칸 · 화면 위치가 선에서 벗어날 때만 화면 위치에서 다시 긋는다
	struct RunLine
	{
		bool valid = false;
		Vector2 o{}, u{};
		Vector2 prevWant{};
		bool hasPrev = false;
		float turn = 0.f;   // Game::m_wantTurn
		// Game::PlanU
		Vector2 PlanU() const
		{
			const float a = std::clamp(turn * 0.1f, -0.35f, 0.35f);
			const float c = std::cos(a), s = std::sin(a);
			return Vector2{ u.x * c - u.y * s, u.x * s + u.y * c };
		}
		void Update(Vector2 P, Vector2 want, int px, int py)
		{
			if (hasPrev)
			{
				const float da = std::atan2(prevWant.x * want.y - prevWant.y * want.x, prevWant.x * want.x + prevWant.y * want.y);
				turn += (da / static_cast<float>(kDt) - turn) * (1.f - std::exp(-static_cast<float>(kDt) / 0.15f));
			}
			prevWant = want;
			hasPrev = true;
			bool keep = valid && want.x * u.x + want.y * u.y > 0.9962f;
			if (keep)
			{
				const Vector2 n{ -u.y, u.x };
				const float latL = std::fabs((px - o.x) * n.x + (py - o.y) * n.y);
				const float latP = std::fabs((P.x - o.x) * n.x + (P.y - o.y) * n.y);
				keep = latL < 0.75f && latP < 0.45f;
			}
			if (!keep) { o = P; u = want; valid = true; }
		}
	};

	// 커서 쪽 방향: ray = 고정 각, circle = runcircle과 같은 조준
	struct Aim
	{
		bool circle = false;
		float angle = 0.f;
		Vector2 c{};
		Vector2 Dir(Vector2 p) const
		{
			if (!circle) return Vector2{ std::cos(angle), std::sin(angle) };
			const float a = std::atan2(p.y - c.y, p.x - c.x) + 0.9f;
			const Vector2 t{ c.x + std::cos(a) * 5.f, c.y + std::sin(a) * 5.f };
			return Norm(Vector2{ t.x - p.x, t.y - p.y });
		}
	};

	// 빈 들판(막힘 없는 곳) — 데니차 동쪽 들녘. 원 그리기는 광장 분수 둘레(막힌 칸 포함, 게임과 같은 곳)
	constexpr int kFieldX = 900, kFieldY = 760;

	void Header(FILE* f)
	{
		std::fprintf(f, "t,lx,ly,vx,vy,speed,pending,keyx,keyy,rx,ry,camx,camy,zoom,face,rface,heading,sector,turn,dev,guided,rlx,rly\n");
	}

	int RunLocal(const Aim& aim, double secs, NetSim upSim, NetSim downSim, const char* out, int sx, int sy)
	{
		FILE* f = std::fopen(out, "w");
		if (!f) return 1;
		Header(f);
		motion::LocalMover mover;
		motion::Body body;
		mover.Reset(sx, sy);
		Server srv{ sx, sy };
		Pipe up{ upSim }, down{ downSim };
		struct Msg { double at; uint16_t seq; int dir; };
		struct Ack { double at; uint16_t seq; int x, y; bool ok; };
		std::deque<Msg> toServer;
		std::deque<Ack> toClient;
		double now = 10.0;
		const double start = now + 0.5, stop = start + secs, end = stop + 1.2;
		RunLine line;
		while (now < end)
		{
			now += kDt;
			while (!toServer.empty() && toServer.front().at <= now)
			{
				const Msg m = toServer.front();
				toServer.pop_front();
				int ox = 0, oy = 0;
				const bool ok = srv.Move(m.dir, m.at, 100, ox, oy);
				toClient.push_back(Ack{ down.DeliverAt(m.at), m.seq, ox, oy, ok });
			}
			while (!toClient.empty() && toClient.front().at <= now)
			{
				const Ack a = toClient.front();
				toClient.pop_front();
				mover.OnAck(a.seq, a.x, a.y, a.ok ? MoveResult::Ok : MoveResult::TooFast, 0, now);
			}
			const Vector2 P = mover.Visual();
			motion::Guide g;
			if (now >= start && now < stop)
			{
				line.Update(P, aim.Dir(P), mover.PredX(), mover.PredY());
				g.active = !g_noGuide;
				g.origin = line.o;
				g.dir = line.u;
				if (mover.CanStep(now))
				{
					const int dir = PlanOrSlide(mover.PredX(), mover.PredY(), line.o, line.PlanU());
					if (std::getenv("MOTION_PLAN_DEBUG"))
						std::fprintf(stderr, "[plan] t=%.3f pred(%d,%d) P(%.3f,%.3f) L(%.3f,%.3f) line o(%.3f,%.3f) u(%.3f,%.3f) dir=%d\n", now - 10.0,
							mover.PredX() - WorldData::kTownX0, mover.PredY() - WorldData::kTownY0, P.x - WorldData::kTownX0, P.y - WorldData::kTownY0,
							mover.Logical().x - WorldData::kTownX0, mover.Logical().y - WorldData::kTownY0, line.o.x - WorldData::kTownX0, line.o.y - WorldData::kTownY0, line.u.x, line.u.y, dir);
					uint16_t seq = 0;
					if (dir >= 0 && mover.Step(now, dir, seq))
						toServer.push_back(Msg{ up.DeliverAt(now), seq, dir });
				}
			}
			mover.SetGuide(g);
			const Vector2 prev = mover.Visual();
			mover.Update(now, static_cast<float>(kDt));
			body.Update(prev, mover.Visual(), static_cast<float>(kDt), mover.NominalSpeed());
			const Vector2 l = mover.Logical(), v = mover.Visual();
			if (std::getenv("MOTION_DEBUG"))
			{
				const auto& d = mover.GetDebug();
				std::fprintf(stderr, "%.3f g=%d stop=%d target=%.3f prog=%.3f speed=%.3f next=%.3f\n", now - 10.0, d.guided ? 1 : 0, d.stopping ? 1 : 0, d.target, d.prog, mover.Speed(), mover.NextStepAt() - 10.0);
			}
			std::fprintf(f, "%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%d,0,0,-1,-1,0,0,1,%d,-1,%.4f,%d,%.3f,%.4f,%d,-1,-1\n", now, l.x, l.y, v.x, v.y, mover.Speed(),
				mover.PendingCount(), (body.faceRight ? 1 : 0) | (body.faceBack ? 2 : 0), body.heading, body.sector, body.turn, mover.Deviation(), mover.Guided() ? 1 : 0);
		}
		std::fclose(f);
		const auto& st = mover.GetStats();
		std::printf("[sim] steps=%d acks=%d rejects=%d(server %d) corrections=%d maxPending=%d maxDev=%.3f\n", st.steps, st.acks, st.rejects, srv.rejects,
			st.corrections, st.maxPending, st.maxDeviation);
		return 0;
	}

	int RunRemote(double secs, NetSim sim, const char* out)
	{
		// 서버에서 원을 도는 플레이어(봇과 같은 조준)의 걸음을 시간대로 만들고, 순서를 지키는 통로로 보낸다.
		// 배경 패킷(다른 개체 · 내 응답)도 같은 통로로 10Hz — 스파이크 때 함께 밀려 몰려 오는 모양을 흉내 낸다.
		FILE* f = std::fopen(out, "w");
		if (!f) return 1;
		Header(f);
		Pipe pipe{ sim };
		struct Pkt { double at; bool step; int x, y; float dur; };
		std::deque<Pkt> q;
		const Vector2 c{ TownMap::kFountain.x, TownMap::kFountain.y };
		int x = static_cast<int>(c.x + 5.f), y = static_cast<int>(c.y);
		double t = 10.0, nextBg = 10.0;
		const double end = 10.0 + secs + 1.0;
		// 서버 시간표 먼저 만들기(걸음 끝 = 다음 걸음 시작). g_botPace면 샌드박스 봇과 같은 박자:
		// 공유 시계가 1/30초씩 뛰므로 걸음은 그 경계에서 나가고, 20ms 넘게 늦은 만큼은 되찾지 않는다(평균이 조금 느리다).
		double next = t;
		while (t < end - 1.0)
		{
			if (g_botPace)
			{
				t = std::ceil(next / kDt - 1e-9) * kDt;
			}
			while (nextBg < t) { q.push_back(Pkt{ pipe.DeliverAt(nextBg), false, 0, 0, 0 }); nextBg += g_bgPoisson ? -std::log(std::max(1e-6, U(0, 1))) / g_bgHz : 1.0 / g_bgHz; }
			const Vector2 p{ static_cast<float>(x), static_cast<float>(y) };
			const float a = std::atan2(p.y - c.y, p.x - c.x) + 0.9f;
			const Vector2 tg{ c.x + std::cos(a) * 5.f, c.y + std::sin(a) * 5.f };
			const int dir = PlanOrSlide(x, y, p, Norm(Vector2{ tg.x - p.x, tg.y - p.y }));
			if (dir < 0) break;
			MoveRule::Apply(x, y, dir);
			const float dur = MoveRule::StepMs(dir, 100) / 1000.f;
			q.push_back(Pkt{ pipe.DeliverAt(t), true, x, y, dur });
			if (g_botPace) next = std::max(next, t - 0.02) + dur;
			else t += dur;
		}
		std::stable_sort(q.begin(), q.end(), [](const Pkt& a, const Pkt& b) { return a.at < b.at; });
		motion::Track tr;
		motion::Body body;
		motion::TrackSnap(tr, Vector2{ c.x + 5.f, c.y });
		double now = 10.0, lastRecv = 10.0;
		while (now < end)
		{
			now += kDt;
			while (!q.empty() && q.front().at <= now)
			{
				const Pkt p = q.front();
				q.pop_front();
				lastRecv = p.at;
				if (p.step) motion::TrackStep(tr, p.x, p.y, p.dur);
			}
			motion::SetNetQuiet(static_cast<float>(now - lastRecv));
			const Vector2 prev = tr.f.pos;
			const Vector2 pos = motion::TrackUpdate(tr, static_cast<float>(kDt), static_cast<float>(kDt), true);
			body.Update(prev, pos, static_cast<float>(kDt), 3.57f, tr.merge > 0.f);
			std::fprintf(f, "%.4f,-1,-1,-1,-1,0,0,0,0,%.4f,%.4f,0,0,1,-1,%d,%.4f,%d,%.3f,0,0,%.4f,%.4f\n", now, pos.x, pos.y,
				(body.faceRight ? 1 : 0) | (body.faceBack ? 2 : 0), body.heading, body.sector, body.turn, tr.logical.x, tr.logical.y);
		}
		std::fclose(f);
		std::printf("[sim] remote done\n");
		return 0;
	}
}

int main(int argc, char** argv)
{
	if (argc < 2) { std::printf("usage: motion_sim ray <deg> [secs] | circle [secs] | remote [secs] [lat] [jit] [spike%%]\n"); return 1; }
	NetSim up, down;
	up.latency = down.latency = 0.05;
	const char* out = "sim.csv";
	int atX = -1, atY = -1;   // --at x,y: 마을 기준 칸(데니차 왼쪽 위 = 0,0)
	std::vector<std::string> pos;
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		if (a == "--latency" && i + 1 < argc) up.latency = down.latency = std::atof(argv[++i]) / 1000.0;
		else if (a == "--jitter" && i + 1 < argc) up.jitter = down.jitter = std::atof(argv[++i]) / 1000.0;
		else if (a == "--spike" && i + 1 < argc) up.spike = down.spike = std::atof(argv[++i]);
		else if (a == "--out" && i + 1 < argc) out = argv[++i];
		else if (a == "--at" && i + 1 < argc) std::sscanf(argv[++i], "%d,%d", &atX, &atY);
		else if (a == "--catchup" && i + 1 < argc) g_catchupMs = std::atof(argv[++i]);
		else if (a == "--botpace") g_botPace = true;
		else if (a == "--noguide") g_noGuide = true;
		else if (a == "--bg" && i + 1 < argc) g_bgHz = std::max(0.1, std::atof(argv[++i]));
		else if (a == "--bgpoisson") g_bgPoisson = true;
		else if (a == "--seed" && i + 1 < argc) g_rng.seed(static_cast<unsigned>(std::atoi(argv[++i])));
		else pos.push_back(a);
	}
	const std::string mode = pos.empty() ? "ray" : pos[0];
	if (mode == "ray")
	{
		Aim aim;
		aim.angle = static_cast<float>((pos.size() > 1 ? std::atof(pos[1].c_str()) : 0.0) * 3.14159265 / 180.0);
		const int sx = atX >= 0 ? WorldData::kTownX0 + atX : kFieldX, sy = atY >= 0 ? WorldData::kTownY0 + atY : kFieldY;
		return RunLocal(aim, pos.size() > 2 ? std::atof(pos[2].c_str()) : 6.0, up, down, out, sx, sy);
	}
	if (mode == "circle")
	{
		Aim aim;
		aim.circle = true;
		aim.c = Vector2{ TownMap::kFountain.x, TownMap::kFountain.y };
		const int sx = atX >= 0 ? WorldData::kTownX0 + atX : static_cast<int>(aim.c.x + 5.f);
		const int sy = atY >= 0 ? WorldData::kTownY0 + atY : static_cast<int>(aim.c.y);
		return RunLocal(aim, pos.size() > 1 ? std::atof(pos[1].c_str()) : 30.0, up, down, out, sx, sy);
	}
	if (mode == "remote")
	{
		NetSim sim;
		sim.latency = (pos.size() > 2 ? std::atof(pos[2].c_str()) : 150.0) / 1000.0;
		sim.jitter = (pos.size() > 3 ? std::atof(pos[3].c_str()) : 40.0) / 1000.0;
		sim.spike = pos.size() > 4 ? std::atof(pos[4].c_str()) : 2.0;
		return RunRemote(pos.size() > 1 ? std::atof(pos[1].c_str()) : 20.0, sim, out);
	}
	std::printf("unknown mode\n");
	return 1;
}
