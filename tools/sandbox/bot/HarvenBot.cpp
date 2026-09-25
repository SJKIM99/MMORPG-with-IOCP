// ─────────────────────────────────────────────────────────────────────────────
// 하르벤 시험용 봇 — 확장 세션(USER_LOGIN_EX_REQ)으로 붙어 정해진 길을 규칙 속도로 걷는다.
// "다른 플레이어" 보간 · 네트워크 흉내(--netsim) 검증과 녹화(따라가기)에 쓴다.
// 서버와 같은 헤더(Protocol.h · Data/MoveRule.h · WorldData.h)와 Client2D의 net 계층을 그대로 쓴다.
//
//   harven_bot --name bot1 [--class 2] [--host 127.0.0.1 --port 4000]
//              [--clock /dev/shm/harven_clock]      샌드박스 공유 시계(녹화 클라이언트와 같은 시간)
//              [--circle cx,cy,r[,cw]]              중심 둘레를 돈다(마을 좌표 기준: 칸 오프셋이면 +로 적는다)
//              [--line x1,y1,x2,y2]                 두 점 사이를 오간다
//              [--start 2.0] [--seconds 60]         시작 지연 · 끝나는 시각(공유 시계 기준 초)
//   좌표 앞에 't'를 붙이면 마을 원점(kTownX0, kTownY0) 기준 오프셋(예: t21.5,t21.5,5).
// ─────────────────────────────────────────────────────────────────────────────
#include "net/NetClient.h"
#include "Data/MoveRule.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace
{
	const volatile int64_t* g_clock = nullptr;

	double Now()
	{
		if (g_clock) return static_cast<double>(g_clock[0]) / 1e9;
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	double Coord(const std::string& s, bool y)
	{
		if (!s.empty() && s[0] == 't')
			return std::atof(s.c_str() + 1) + (y ? WorldData::kTownY0 : WorldData::kTownX0);
		return std::atof(s.c_str());
	}

	std::vector<std::string> Split(const std::string& s)
	{
		std::vector<std::string> out;
		size_t p = 0;
		while (p <= s.size())
		{
			const size_t c = s.find(',', p);
			out.push_back(s.substr(p, c == std::string::npos ? std::string::npos : c - p));
			if (c == std::string::npos) break;
			p = c + 1;
		}
		return out;
	}
}

int main(int argc, char** argv)
{
	std::string host = "127.0.0.1", name = "bot1", clockPath;
	uint16_t port = PORT_NUM;
	uint8_t cls = 2;
	double cx = 0, cy = 0, radius = 0;
	bool clockwise = true, circle = false, line = false;
	double lx1 = 0, ly1 = 0, lx2 = 0, ly2 = 0;
	double startAfter = 2.0, seconds = 1e9;
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "--host") host = next();
		else if (a == "--port") port = static_cast<uint16_t>(std::atoi(next().c_str()));
		else if (a == "--name") name = next();
		else if (a == "--class") cls = static_cast<uint8_t>(std::atoi(next().c_str()));
		else if (a == "--clock") clockPath = next();
		else if (a == "--start") startAfter = std::atof(next().c_str());
		else if (a == "--seconds") seconds = std::atof(next().c_str());
		else if (a == "--circle")
		{
			const auto v = Split(next());
			if (v.size() >= 3) { cx = Coord(v[0], false); cy = Coord(v[1], true); radius = std::atof(v[2].c_str()); circle = true; }
			if (v.size() >= 4) clockwise = v[3] != "ccw";
		}
		else if (a == "--line")
		{
			const auto v = Split(next());
			if (v.size() >= 4) { lx1 = Coord(v[0], false); ly1 = Coord(v[1], true); lx2 = Coord(v[2], false); ly2 = Coord(v[3], true); line = true; }
		}
	}

#if !defined(_WIN32)
	if (!clockPath.empty())
	{
		const int fd = open(clockPath.c_str(), O_RDONLY);
		if (fd >= 0)
		{
			void* p = mmap(nullptr, 16, PROT_READ, MAP_SHARED, fd, 0);
			close(fd);
			if (p != MAP_FAILED) g_clock = static_cast<const volatile int64_t*>(p);
		}
		std::printf("[bot %s] 공유 시계 %s\n", name.c_str(), g_clock ? "사용" : "실패 — 실제 시계");
	}
#endif

	net::NetClient net;
	bool loggedIn = false;
	int x = 0, y = 0;            // 예측 칸
	int pending = 0;
	uint16_t seq = 0;
	double nextStepAt = 0.0;
	int steps = 0, rejects = 0;
	uint32_t speedPercent = 100;   // 서버가 알려 준 이동 속도(민첩 · 장비) — 클라이언트처럼 이 박자로 걷는다
	net.SetHandler([&](const char* data, uint16_t size)
	{
		const auto type = static_cast<PacketType>(static_cast<unsigned char>(data[2]));
		if (type == PacketType::SERVER_CONFIG_INF && size >= sizeof(SERVER_CONFIG_INF_PACKET))
		{
			SERVER_CONFIG_INF_PACKET p{};
			std::memcpy(&p, data, sizeof(p));
			if (p.moveSpeedPercent > 0) speedPercent = p.moveSpeedPercent;
		}
		if (type == PacketType::USER_LOGIN_ACK && size >= sizeof(USER_LOGIN_ACK_PACKET))
		{
			USER_LOGIN_ACK_PACKET p{};
			std::memcpy(&p, data, sizeof(p));
			x = p.x; y = p.y;
			loggedIn = true;
			std::printf("[bot %s] 접속 %d,%d\n", name.c_str(), x, y);
			std::fflush(stdout);
		}
		else if (type == PacketType::USER_MOVE_EX_ACK && size >= sizeof(USER_MOVE_EX_ACK_PACKET))
		{
			USER_MOVE_EX_ACK_PACKET p{};
			std::memcpy(&p, data, sizeof(p));
			--pending;
			if (static_cast<MoveResult>(p.result) != MoveResult::Ok) { ++rejects; x = p.x; y = p.y; pending = 0; }
			else if (pending == 0) { x = p.x; y = p.y; }
		}
	});
	if (!net.Connect(host, port)) { std::printf("[bot] 연결 실패\n"); return 1; }
	bool sentLogin = false;
	const double t0 = Now();
	double lastLog = t0;
	int legDir = 0;   // line: 0 → (x2,y2), 1 → (x1,y1)

	while (true)
	{
		net.Update();
		if (net.GetState() == net::TcpSocket::State::Failed || net.GetState() == net::TcpSocket::State::Closed)
		{
			std::printf("[bot %s] 연결 끊김\n", name.c_str());
			return 1;
		}
		if (net.IsConnected() && !sentLogin)
		{
			net.SendLoginEx(name, "1234", cls);
			sentLogin = true;
		}
		const double now = Now();
		if (now - t0 > seconds) break;
		if (loggedIn && now - t0 >= startAfter && pending < 2 && now >= nextStepAt)
		{
			// 목표 칸 쪽으로 8방향 중 가장 잘 맞는 걸음(막혔으면 다음으로 잘 맞는 것)
			double tx = x, ty = y;
			if (circle)
			{
				const double a = std::atan2(y - cy, x - cx) + (clockwise ? 0.9 : -0.9);
				tx = cx + std::cos(a) * radius;
				ty = cy + std::sin(a) * radius;
			}
			else if (line)
			{
				tx = legDir == 0 ? lx2 : lx1;
				ty = legDir == 0 ? ly2 : ly1;
				if (std::fabs(tx - x) < 0.6 && std::fabs(ty - y) < 0.6) legDir ^= 1;
			}
			const double dx = tx - x, dy = ty - y;
			const double len = std::sqrt(dx * dx + dy * dy);
			if (len > 0.5)
			{
				int best = -1;
				double bestDot = -2.0;
				for (int d = 0; d < 8; ++d)
				{
					int nx = x, ny = y;
					MoveRule::Apply(nx, ny, d);
					if (nx == x && ny == y) continue;
					const double mx = nx - x, my = ny - y;
					const double dot = (mx * dx + my * dy) / (std::sqrt(mx * mx + my * my) * len);
					if (dot > bestDot) { bestDot = dot; best = d; }
				}
				if (best >= 0)
				{
					int nx = x, ny = y;
					MoveRule::Apply(nx, ny, best);
					const int real = MoveRule::DirOf(nx - x, ny - y);
					net.SendMoveEx(real, ++seq);
					++pending;
					++steps;
					x = nx; y = ny;
					nextStepAt = std::max(nextStepAt, now - 0.02) + MoveRule::StepMs(real, speedPercent) / 1000.0;
				}
			}
		}
		if (now - lastLog > 5.0)
		{
			lastLog = now;
			std::printf("[bot %s] t=%.1f pos %d,%d steps %d rejects %d\n", name.c_str(), now - t0, x, y, steps, rejects);
			std::fflush(stdout);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	net.Disconnect();
	return 0;
}
