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
//              [--crowd 30]                         한 프로세스에서 여럿(이름01 …) — 원을 세 겹으로 나눠 돈다(Phase 6 붐비는 마을)
//   좌표 앞에 't'를 붙이면 마을 원점(kTownX0, kTownY0) 기준 오프셋(예: t21.5,t21.5,5).
// ─────────────────────────────────────────────────────────────────────────────
#include "net/NetClient.h"
#include "Data/MoveRule.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
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

namespace
{
	// 봇 하나 = 접속 하나. NetClient가 처리기에서 자기 상태를 잡으므로 옮기지 않는다(unique_ptr).
	struct Bot
	{
		std::string name;
		net::NetClient net;
		bool sentLogin = false, loggedIn = false;
		int x = 0, y = 0;            // 예측 칸
		int pending = 0, steps = 0, rejects = 0;
		uint16_t seq = 0;
		double nextStepAt = 0.0, startAfter = 2.0;
		uint32_t speedPercent = 100;   // 서버가 알려 준 이동 속도(민첩 · 장비) — 클라이언트처럼 이 박자로 걷는다
		double cx = 0, cy = 0, radius = 0;
		bool clockwise = true, circle = false, line = false, closed = false;
		double lx1 = 0, ly1 = 0, lx2 = 0, ly2 = 0;
		int legDir = 0;   // line: 0 → (x2,y2), 1 → (x1,y1)
	};

	void Attach(Bot& b, bool quiet)
	{
		b.net.SetHandler([&b, quiet](const char* data, uint16_t size)
		{
			const auto type = static_cast<PacketType>(static_cast<unsigned char>(data[2]));
			if (type == PacketType::SERVER_CONFIG_INF && size >= sizeof(SERVER_CONFIG_INF_PACKET))
			{
				SERVER_CONFIG_INF_PACKET p{};
				std::memcpy(&p, data, sizeof(p));
				if (p.moveSpeedPercent > 0) b.speedPercent = p.moveSpeedPercent;
			}
			if (type == PacketType::USER_LOGIN_ACK && size >= sizeof(USER_LOGIN_ACK_PACKET))
			{
				USER_LOGIN_ACK_PACKET p{};
				std::memcpy(&p, data, sizeof(p));
				b.x = p.x; b.y = p.y;
				b.loggedIn = true;
				if (!quiet) { std::printf("[bot %s] 접속 %d,%d\n", b.name.c_str(), b.x, b.y); std::fflush(stdout); }
			}
			else if (type == PacketType::USER_MOVE_EX_ACK && size >= sizeof(USER_MOVE_EX_ACK_PACKET))
			{
				USER_MOVE_EX_ACK_PACKET p{};
				std::memcpy(&p, data, sizeof(p));
				--b.pending;
				if (static_cast<MoveResult>(p.result) != MoveResult::Ok) { ++b.rejects; b.x = p.x; b.y = p.y; b.pending = 0; }
				else if (b.pending == 0) { b.x = p.x; b.y = p.y; }
			}
		});
	}

	// 목표 칸 쪽으로 8방향 중 가장 잘 맞는 걸음(막혔으면 다음으로 잘 맞는 것)
	void Step(Bot& b, double now)
	{
		double tx = b.x, ty = b.y;
		if (b.circle)
		{
			const double a = std::atan2(b.y - b.cy, b.x - b.cx) + (b.clockwise ? 0.9 : -0.9);
			tx = b.cx + std::cos(a) * b.radius;
			ty = b.cy + std::sin(a) * b.radius;
		}
		else if (b.line)
		{
			tx = b.legDir == 0 ? b.lx2 : b.lx1;
			ty = b.legDir == 0 ? b.ly2 : b.ly1;
			if (std::fabs(tx - b.x) < 0.6 && std::fabs(ty - b.y) < 0.6) b.legDir ^= 1;
		}
		const double dx = tx - b.x, dy = ty - b.y;
		const double len = std::sqrt(dx * dx + dy * dy);
		if (len <= 0.5) return;
		int best = -1;
		double bestDot = -2.0;
		for (int d = 0; d < 8; ++d)
		{
			int nx = b.x, ny = b.y;
			MoveRule::Apply(nx, ny, d);
			if (nx == b.x && ny == b.y) continue;
			const double mx = nx - b.x, my = ny - b.y;
			const double dot = (mx * dx + my * dy) / (std::sqrt(mx * mx + my * my) * len);
			if (dot > bestDot) { bestDot = dot; best = d; }
		}
		if (best < 0) return;
		int nx = b.x, ny = b.y;
		MoveRule::Apply(nx, ny, best);
		const int real = MoveRule::DirOf(nx - b.x, ny - b.y);
		b.net.SendMoveEx(real, ++b.seq);
		++b.pending;
		++b.steps;
		b.x = nx; b.y = ny;
		b.nextStepAt = std::max(b.nextStepAt, now - 0.02) + MoveRule::StepMs(real, b.speedPercent) / 1000.0;
	}
}

int main(int argc, char** argv)
{
	std::string host = "127.0.0.1", name = "bot1", clockPath;
	uint16_t port = PORT_NUM;
	uint8_t cls = 2;
	Bot proto;
	double seconds = 1e9;
	int crowd = 0;   // --crowd N: 한 프로세스에서 N명(이름 뒤에 두 자리 번호) — 붐비는 마을 성능 시험(Phase 6)
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "--host") host = next();
		else if (a == "--port") port = static_cast<uint16_t>(std::atoi(next().c_str()));
		else if (a == "--name") name = next();
		else if (a == "--class") cls = static_cast<uint8_t>(std::atoi(next().c_str()));
		else if (a == "--clock") clockPath = next();
		else if (a == "--start") proto.startAfter = std::atof(next().c_str());
		else if (a == "--seconds") seconds = std::atof(next().c_str());
		else if (a == "--crowd") crowd = std::max(0, std::atoi(next().c_str()));
		else if (a == "--circle")
		{
			const auto v = Split(next());
			if (v.size() >= 3) { proto.cx = Coord(v[0], false); proto.cy = Coord(v[1], true); proto.radius = std::atof(v[2].c_str()); proto.circle = true; }
			if (v.size() >= 4) proto.clockwise = v[3] != "ccw";
		}
		else if (a == "--line")
		{
			const auto v = Split(next());
			if (v.size() >= 4) { proto.lx1 = Coord(v[0], false); proto.ly1 = Coord(v[1], true); proto.lx2 = Coord(v[2], false); proto.ly2 = Coord(v[3], true); proto.line = true; }
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

	// 한 명(기존) 또는 여럿(--crowd): 여럿이면 원을 세 겹(반지름 r · r+2 · r+4)으로 나누고 방향을 번갈아, 접속은 0.15초씩 늦춘다
	std::vector<std::unique_ptr<Bot>> bots;
	const int n = crowd > 0 ? crowd : 1;
	for (int k = 0; k < n; ++k)
	{
		auto b = std::make_unique<Bot>();
		b->name = name;
		b->startAfter = proto.startAfter;
		b->cx = proto.cx; b->cy = proto.cy; b->radius = proto.radius; b->circle = proto.circle; b->clockwise = proto.clockwise;
		b->lx1 = proto.lx1; b->ly1 = proto.ly1; b->lx2 = proto.lx2; b->ly2 = proto.ly2; b->line = proto.line;
		if (crowd > 0)
		{
			char suffix[8];
			std::snprintf(suffix, sizeof(suffix), "%02d", k + 1);
			b->name = name + suffix;
			b->radius = proto.radius + (k % 3) * 2.0;
			b->clockwise = (k % 2) == 0;
			b->startAfter = proto.startAfter + k * 0.15;
		}
		Attach(*b, crowd > 0);
		if (!b->net.Connect(host, port)) { std::printf("[bot %s] 연결 실패\n", b->name.c_str()); return 1; }
		bots.push_back(std::move(b));
	}
	const double t0 = Now();
	double lastLog = t0;

	while (true)
	{
		const double now = Now();
		if (now - t0 > seconds) break;
		int alive = 0, logged = 0, steps = 0, rejects = 0;
		for (auto& bp : bots)
		{
			Bot& b = *bp;
			if (b.closed) continue;
			b.net.Update();
			if (b.net.GetState() == net::TcpSocket::State::Failed || b.net.GetState() == net::TcpSocket::State::Closed)
			{
				std::printf("[bot %s] 연결 끊김\n", b.name.c_str());
				b.closed = true;
				continue;
			}
			++alive;
			if (b.net.IsConnected() && !b.sentLogin)
			{
				b.net.SendLoginEx(b.name, "1234", cls);
				b.sentLogin = true;
			}
			if (b.loggedIn) ++logged;
			if (b.loggedIn && now - t0 >= b.startAfter && b.pending < 2 && now >= b.nextStepAt)
				Step(b, now);
			steps += b.steps;
			rejects += b.rejects;
		}
		if (alive == 0) return 1;
		if (now - lastLog > 5.0)
		{
			lastLog = now;
			if (crowd > 0)
				std::printf("[crowd] t=%.1f 접속 %d/%d · 걸음 %d · 거절 %d\n", now - t0, logged, n, steps, rejects);
			else
				std::printf("[bot %s] t=%.1f pos %d,%d steps %d rejects %d\n", bots[0]->name.c_str(), now - t0, bots[0]->x, bots[0]->y, bots[0]->steps, bots[0]->rejects);
			std::fflush(stdout);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(crowd > 0 ? 4 : 1));
	}
	for (auto& b : bots) b->net.Disconnect();
	return 0;
}
