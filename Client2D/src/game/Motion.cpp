#include "game/Motion.h"
#include "game/Entity.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>

namespace motion
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		Vector2 V(int x, int y) { return Vector2{ static_cast<float>(x), static_cast<float>(y) }; }
		float Dist(Vector2 a, Vector2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
		float Dot(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }
		Vector2 Sub(Vector2 a, Vector2 b) { return Vector2{ a.x - b.x, a.y - b.y }; }
		Vector2 Add(Vector2 a, Vector2 b) { return Vector2{ a.x + b.x, a.y + b.y }; }
		Vector2 Mul(Vector2 a, float k) { return Vector2{ a.x * k, a.y * k }; }
		Vector2 Lerp(Vector2 a, Vector2 b, float t) { return Vector2{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }
		float WrapPi(float a)
		{
			while (a > kPi) a -= 2.f * kPi;
			while (a < -kPi) a += 2.f * kPi;
			return a;
		}
		bool Norm(Vector2 v, Vector2& out)
		{
			const float l = std::hypot(v.x, v.y);
			if (l < 1e-5f) return false;
			out = Vector2{ v.x / l, v.y / l };
			return true;
		}
		// 명시 속도 추종: 출발 kStartSec · 정지 kStopSec에 맞춘 가속도로 목표 속도에 다가간다
		float g_netQuiet = 0.f;   // 마지막으로 패킷을 받은 뒤 흐른 시간(Game이 프레임마다)

		float Approach(float speed, float target, float nominal, float dt)
		{
			const float up = nominal / kStartSec, down = nominal / kStopSec;
			if (target > speed) return std::min(target, speed + up * dt);
			return std::max(target, speed - down * dt);
		}

	}

	void SetNetQuiet(float seconds) { g_netQuiet = seconds; }
	float RuleSpeed(uint32_t stepMs) { return stepMs > 0 ? 1000.f / static_cast<float>(stepMs) : 3.57f; }

	float GridRate(Vector2 u)
	{
		float a = std::fabs(u.x), b = std::fabs(u.y);
		if (a < b) std::swap(a, b);
		if (a < 1e-5f) return 1.f;
		const float r = b / a;   // 대각 걸음 비율
		return std::sqrt(1.f + r * r) / (1.f + 0.41421356f * r);
	}

	float ScreenAngle(Vector2 d)
	{
		// 칸 좌표 → 아이소 화면: sx ∝ X − Y, sy ∝ (X + Y) / 2
		return std::atan2((d.x + d.y) * 0.5f, d.x - d.y);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// 몸
	// ─────────────────────────────────────────────────────────────────────────
	void Body::Face(bool right, bool back)
	{
		faceRight = right;
		faceBack = back;
		sector = right ? (back ? 7 : 1) : (back ? 5 : 3);
		heading = WrapPi(sector * kPi * 0.25f);
	}

	void Body::Update(Vector2 prevPos, Vector2 pos, float dt, float nominalSpeed, bool holdHeading)
	{
		footfall = false;
		if (dt <= 1e-5f) return;
		const Vector2 d = Sub(pos, prevPos);
		const float dist = std::hypot(d.x, d.y);
		const float inst = dist / dt;
		const float prevSpeed = speed;
		speed = util::Damp(speed, inst, 16.f, dt);

		// 진행 각: 움직이는 동안만, 초당 1080°까지. 입력 방향은 짧게(80ms) 거른다 — 한두 프레임의 흔들림이 방향 그림을 바꾸지 않게.
		const float kf = 1.f - std::exp(-dt / 0.1f);
		dirF.x += (d.x / std::max(dt, 1e-5f) - dirF.x) * kf;
		dirF.y += (d.y / std::max(dt, 1e-5f) - dirF.y) * kf;
		float dh = 0.f;
		if (!holdHeading && inst > 0.25f && std::hypot(dirF.x, dirF.y) > 0.2f)
		{
			const float want = ScreenAngle(dirF);
			const float mx = kTurnRate * dt;
			dh = std::clamp(WrapPi(want - heading), -mx, mx);
			heading = WrapPi(heading + dh);
		}
		turnRate = util::Damp(turnRate, dh / dt, 10.f, dt);

		// 8구역(45°) + 경계 15° 히스테리시스 — 경계 근처에서 방향 그림이 깜빡이지 않는다
		if (std::fabs(WrapPi(heading - sector * kPi * 0.25f)) > kPi / 8.f + kSectorHyst)
		{
			const int s = static_cast<int>(std::lround(heading / (kPi * 0.25f)));
			sector = ((s % 8) + 8) % 8;
		}
		// 지금 그림은 오른쪽 아래(앞) · 오른쪽 위(뒤) 두 방향 + 좌우 반전 — 1-D의 다섯 방향이 오면 그대로 끼운다
		switch (sector)
		{
		case 0: case 1: faceRight = true; faceBack = false; break;
		case 2: faceBack = false; break;            // ↓: 좌우는 그대로
		case 3: case 4: faceRight = false; faceBack = false; break;
		case 5: faceRight = false; faceBack = true; break;
		case 6: faceBack = true; break;             // ↑
		default: faceRight = true; faceBack = true; break;
		}
		// 좌우 반전: 가로 배율이 60ms에 걸쳐 0을 지난다(휙 돌아서기)
		const float want = faceRight ? 1.f : -1.f;
		const float step = 2.f / kFlipSec * dt;
		turn = want > turn ? std::min(want, turn + step) : std::max(want, turn - step);

		// 걸음 위상 = 이동 거리 ÷ 보폭 — 속도가 바뀌어도 발이 미끄러지지 않는다. 발이 닿는 순간(보폭이 가장 벌어진 위상)
		// 서 있다가(서기 그림) 출발하면 첫 발 디딤 자세(walk1 · 달리기 0번 = π/2)부터 — 멈췄던 위상의 아무 자세로나 시작하지 않게
		if (gait < 0.2f && inst > 0.3f)
			stride = kPi * 0.5f;
		gait = util::Damp(gait, inst / std::max(0.5f, nominalSpeed), 30.f, dt);
		const float before = stride;
		stride += dist / kStrideTiles * 2.f * kPi;
		if (std::floor((before - kPi * 0.5f) / kPi) != std::floor((stride - kPi * 0.5f) / kPi) && moving > 0.3f)
			footfall = true;
		if (stride > 2000.f * kPi) stride = std::fmod(stride, 2.f * kPi);

		const float walkTarget = util::Clamp01((speed - 0.35f) / 1.2f);
		moving = util::Damp(moving, walkTarget, walkTarget > moving ? 14.f : 9.f, dt);
		const float nom = std::max(0.5f, nominalSpeed);
		run = util::Damp(run, util::Clamp01((speed - nom * 1.12f) / (nom * 0.3f)), 6.f, dt);

		// 자세: 출발 가속 → 앞으로(최대 5°), 달리는 동안 조금 앞으로, 도는 쪽으로 기울기
		const float accel = (speed - prevSpeed) / dt;
		lean = util::Damp(lean, std::clamp(accel * 0.1f, -4.f, 5.f) + 2.f * moving, 12.f, dt);
		bank = util::Damp(bank, std::clamp(turnRate * speed * 0.3f, -7.f, 7.f), 10.f, dt);

		// 멈춤: 달리다 서면 한 번 흔들리며 선다(감쇠 스프링)
		if (speed > nom * 0.6f) running = true;
		else if (running && speed < nom * 0.2f)
		{
			running = false;
			settleV += 6.f;
		}
		// 감쇠 스프링은 1/120초로 잘라 적분한다 — 긴 프레임(느린 PC · 로딩 멈칫 · 소프트웨어 렌더링 170ms)에 한 번에
		// 적분하면 발산해 NaN이 되고, NaN 자세는 몸 그림 전체를 지운다(Phase 4-A 시험에서 찾음: 걷고 난 뒤 캐릭터가 사라졌다).
		constexpr float k = 170.f, c = 13.f, kStep = 1.f / 120.f;
		for (float rem = std::min(dt, 0.5f); rem > 0.f; rem -= kStep)
		{
			const float h = std::min(rem, kStep);
			settleV += (-k * settle - c * settleV) * h;
			settle += settleV * h;
		}
		if (!std::isfinite(settle) || !std::isfinite(settleV) || (std::fabs(settle) < 1e-3f && std::fabs(settleV) < 1e-2f)) settle = settleV = 0.f;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// 내 캐릭터
	// ─────────────────────────────────────────────────────────────────────────
	void LocalMover::Reset(int x, int y)
	{
		m_pending.clear();
		m_segs.clear();
		m_srvX = m_predX = x;
		m_srvY = m_predY = y;
		m_logical = V(x, y);
		m_f.Snap(m_logical);
		m_velocity = Vector2{ 0, 0 };
		m_nextStepAt = 0.0;
		m_dashUntil = -1.0;
		m_snapped = true;
	}

	uint32_t LocalMover::StepMs(int dir) const
	{
		const uint32_t speed = MoveRule::ClampSpeed(m_speedPercent);
		const uint32_t base = m_baseStepMs * 100u / speed;
		return MoveRule::IsDiagonal(dir) ? base * m_diagPermille / 1000u : base;
	}

	float LocalMover::NominalSpeed() const { return RuleSpeed(StepMs(0)); }

	double LocalMover::SentAt(uint16_t seq) const
	{
		for (const Pending& p : m_pending)
			if (p.seq == seq) return p.sentAt;
		return -1.0;
	}

	bool LocalMover::CanStep(double now) const
	{
		return static_cast<int>(m_pending.size()) < kMaxPending && now + kCommitLead >= m_nextStepAt && now + 0.02 >= m_dashUntil;
	}

	bool LocalMover::WouldMove(int dir) const
	{
		int x = m_predX, y = m_predY;
		MoveRule::Apply(x, y, dir);
		return x != m_predX || y != m_predY;
	}

	bool LocalMover::Step(double now, int dir, uint16_t& outSeq)
	{
		if (!MoveRule::IsValidDir(dir) || !CanStep(now))
			return false;
		int x = m_predX, y = m_predY;
		MoveRule::Apply(x, y, dir);
		if (x == m_predX && y == m_predY)
			return false;

		// 이어 걷기: 앞 걸음이 끝나는 순간부터(선확정이면 미래). 프레임이 늦어 조금 지났으면
		// kChain까지 거슬러 직전 걸음 끝에 붙인다(칸 중앙에서 멈칫하지 않게). 멈춰 있다 출발하면 지금부터.
		double t0 = now;
		if (m_nextStepAt >= now || (m_nextStepAt > 0.0 && now - m_nextStepAt < kChain))
			t0 = m_nextStepAt;
		const double dur = StepMs(dir) / 1000.0;
		m_segs.push_back(Seg{ V(m_predX, m_predY), V(x, y), t0, t0 + dur });
		m_nextStepAt = t0 + dur;

		m_predX = x;
		m_predY = y;
		m_lastDir = dir;
		outSeq = ++m_seq;
		m_pending.push_back(Pending{ outSeq, static_cast<uint8_t>(dir), now });
		++m_stats.steps;
		m_stats.maxPending = std::max(m_stats.maxPending, static_cast<int>(m_pending.size()));
		return true;
	}

	void LocalMover::Replay()
	{
		int x = m_srvX, y = m_srvY;
		for (const Pending& p : m_pending)
			MoveRule::Apply(x, y, p.dir);
		m_predX = x;
		m_predY = y;
	}

	Vector2 LocalMover::ChainEnd() const { return m_segs.empty() ? V(m_predX, m_predY) : m_segs.back().to; }

	Vector2 LocalMover::LogicalAt(double now) const
	{
		if (m_segs.empty())
			return V(m_predX, m_predY);
		for (const Seg& s : m_segs)
		{
			if (now < s.t0) return s.from;                 // 선확정된 다음 조각은 아직
			if (now < s.t1)
			{
				const float t = static_cast<float>((now - s.t0) / std::max(1e-6, s.t1 - s.t0));
				return Lerp(s.from, s.to, t);
			}
		}
		return m_segs.back().to;
	}

	Vector2 LocalMover::AheadOnChain(double now, float dist) const
	{
		Vector2 cur = LogicalAt(now);
		float rem = dist;
		for (const Seg& s : m_segs)
		{
			if (s.t1 <= now) continue;
			const float l = Dist(cur, s.to);
			if (l >= rem && l > 1e-5f) return Lerp(cur, s.to, rem / l);
			rem -= l;
			cur = s.to;
		}
		return cur;
	}

	void LocalMover::Retarget(double now)
	{
		const Vector2 target = V(m_predX, m_predY);
		const Vector2 end = ChainEnd();
		if (target.x == end.x && target.y == end.y)
			return;
		// 예측이 틀렸다(거절 · 서버 미끄러짐) — 지금 논리 위치에서 새 예측 칸으로 짧게 이어 준다.
		const float err = Dist(end, target);
		if (err > 0.01f)
		{
			++m_stats.corrections;
			m_stats.maxCorrection = std::max(m_stats.maxCorrection, err);
		}
		const Vector2 cur = LogicalAt(now);
		const float d = Dist(cur, target);
		m_segs.clear();
		if (d > 3.5f)
		{
			m_logical = target;
			m_f.Snap(target);
			m_nextStepAt = now;
			m_snapped = true;
			return;
		}
		const double dur = std::max(0.08, 0.12 * d);
		m_segs.push_back(Seg{ cur, target, now, now + dur });
		m_nextStepAt = now + dur;
	}

	void LocalMover::OnAck(uint16_t seq, int x, int y, MoveResult result, uint16_t stepMs, double now)
	{
		(void)stepMs;
		// 받은 순서대로 정확히 하나씩 응답이 온다(TCP · 서버 FIFO). 순번이 앞선 것들은 이미 지난 응답이다.
		while (!m_pending.empty() && static_cast<int16_t>(m_pending.front().seq - seq) < 0)
			m_pending.pop_front();
		if (!m_pending.empty() && m_pending.front().seq == seq)
			m_pending.pop_front();

		m_srvX = x;
		m_srvY = y;
		Replay();
		++m_stats.acks;
		if (result != MoveResult::Ok)
		{
			++m_stats.rejects;
			// 거절(너무 빠름 · 행동 불가) — 다음 걸음은 조금 쉬었다가
			m_nextStepAt = std::max(m_nextStepAt, now + 0.05);
		}
		Retarget(now);
	}

	bool LocalMover::OnServerPosition(int x, int y, double now)
	{
		// 존 이동 메아리: 서버 좌표나 예측 경로 위의 칸이면 무시해도 된다.
		if (x == m_srvX && y == m_srvY)
			return false;
		{
			int px = m_srvX, py = m_srvY;
			for (const Pending& p : m_pending)
			{
				MoveRule::Apply(px, py, p.dir);
				if (px == x && py == y)
					return false;
			}
		}
		// 순간이동 · 부활 · 서버 재배치 — 미응답 입력은 서버가 새 위치에서 처리한다.
		++m_stats.teleports;
		m_srvX = x;
		m_srvY = y;
		Replay();
		const Vector2 target = V(m_predX, m_predY);
		m_segs.clear();
		m_logical = target;
		m_f.Snap(target);
		m_velocity = Vector2{ 0, 0 };
		m_nextStepAt = now;
		m_snapped = true;
		return true;
	}

	void LocalMover::Dash(int x, int y, double now, double duration)
	{
		m_pending.clear();
		m_srvX = m_predX = x;
		m_srvY = m_predY = y;
		const Vector2 target = V(x, y);
		m_segs.clear();
		if (duration <= 0.0)
		{
			// 순간이동형(옛길 도약 · 그늘 걸음): 몸은 바로 옮기고 카메라는 미끄러지게 둔다
			m_logical = target;
			m_f.Snap(target);
			m_velocity = Vector2{ 0, 0 };
			m_nextStepAt = now;
			m_dashUntil = now;
			return;
		}
		// 화면 위치에서 곧장(논리와 어긋나 있던 만큼 튀지 않게) — 기술 이동 동안은 화면 = 논리
		m_segs.push_back(Seg{ m_f.pos, target, now, now + duration });
		m_nextStepAt = now + duration;
		m_dashUntil = now + duration;
	}

	void LocalMover::Update(double now, float visualDt)
	{
		// 지난 조각 정리(지금 조각 앞의 것은 하나만 남긴다)
		while (m_segs.size() > 1 && m_segs.front().t1 <= now && m_segs[1].t0 <= now)
			m_segs.pop_front();
		// 응답이 한참 안 오면(연결 이상) 예측을 서버 좌표로 되돌린다.
		if (!m_pending.empty() && now - m_pending.front().sentAt > 3.0)
		{
			m_pending.clear();
			Replay();
			Retarget(now);
		}

		m_logical = LogicalAt(now);
		const Vector2 prev = m_f.pos;
		float dt = std::max(0.f, visualDt);
		const float vRule = NominalSpeed();
		// 멈춰 있다 출발한 프레임: 논리 궤적이 시작하기 전의 시간만큼 화면이 먼저 떠나지 않게(프레임이 길 때 앞질러 가지 않는다)
		if (m_f.speed < 0.05f && !m_segs.empty())
		{
			const double started = now - m_segs.front().t0;
			if (started >= 0.0 && started < dt) dt = static_cast<float>(started);
		}

		if (now < m_dashUntil + 1e-4)
		{
			m_f.pos = m_logical;   // 기술 이동: 서버가 정한 곡선 그대로
			if (dt > 1e-5f)
			{
				Vector2 d{};
				if (Norm(Sub(m_f.pos, prev), d)) m_f.dir = d;
				m_f.speed = Dist(m_f.pos, prev) / dt;
			}
		}
		else if (dt > 1e-6f)
		{
			const bool moving = now < m_nextStepAt;
			// 멈추는 중: 다음 걸음을 확정할 시점이 지났는데 새 걸음이 없다(입력이 끝났거나 막혔다)
			const bool stopping = !moving || now > m_nextStepAt - kCommitLead + 0.03;
			const Vector2 end = ChainEnd();
			Vector2 dir = m_f.dir;
			float target = 0.f;

			// 안내선을 쓸 수 있나: 논리 궤적이 가는 쪽과 크게 어긋나지 않을 때(급히 돌아서는 동안은 궤적을 따라 돈다)
			bool guided = false;
			if (m_guide.active && moving && !stopping)
			{
				// 논리 궤적이 앞으로 가는 쪽(지금 위치 → 확정한 궤적의 끝)과 안내선이 50° 안이고, 궤적 끝이 선에서
				// 격자 지그재그(≤ 0.5칸)보다 멀리 벗어나지 않았으면 안내선을 쓴다. 벽 · 나무 모서리를 돌아가느라
				// 궤적이 선을 벗어나면 궤적을 따라 돈다 — 선을 고집하면 돌아가는 동안 느려지거나 끌려간다(1-C 검증).
				Vector2 chainDir{};
				const Vector2 gn{ -m_guide.dir.y, m_guide.dir.x };
				const float offLine = std::fabs(Dot(Sub(end, m_guide.origin), gn));
				if ((!Norm(Sub(end, m_logical), chainDir) || Dot(chainDir, m_guide.dir) > 0.64f) && offLine < kGuideOffLine)
					guided = true;
			}
			// 진행 동기화: 논리가 앞서면 빨리, 화면이 앞서면 천천히(±25%). 크게 앞질렀을 때만 더 늦춘다(드문 경우).
			//   prog는 이번 프레임에 움직인 뒤의 차이(프레임 길이와 무관), 작은 차이(−0.03 ~ +0.06칸)는 그대로 둔다 —
			//   출발 가속(80ms)이 남긴 뒤처짐을 급히 메우면 출발 직후 몇 % 빨라졌다 느려지는 것이 보인다(1-C 검증).
			auto sync = [&](float prog)
			{
				prog -= m_f.speed * dt;
				const float e = prog > kSyncDeadAhead ? prog - kSyncDeadAhead : (prog < -kSyncDeadBehind ? prog + kSyncDeadBehind : 0.f);
				float k = std::clamp(e * kSyncGain, -kSyncMax, kSyncMax);
				if (prog < -0.25f) k = std::max(-0.7f, k + (prog + 0.25f) * 1.5f);
				return 1.f + k;
			};
			if (guided)
			{
				// 안내선을 따라 곧게 — 격자 지그재그(직선 · 대각 걸음이 섞인 것)를 지운다.
				// 벽을 피해 논리 궤적이 선에서 비켜 가면(확정한 궤적의 끝이 선에서 0.3칸 넘게) 그만큼 궤적 쪽으로 휜다.
				const Vector2 u = m_guide.dir;
				const Vector2 n{ -u.y, u.x };
				const float lat = Dot(Sub(m_f.pos, m_guide.origin), n);
				const float latEnd = Dot(Sub(end, m_f.pos), n);
				// 궤적이 선에서 비켜 간 정도를 0.4초로 거른다 — 격자 지그재그(평균 0)는 무시하고 벽을 돈 뒤의 어긋남만 따라간다
				m_latF += (latEnd - m_latF) * (1.f - std::exp(-dt / 0.4f));
				// 격자 지그재그는 선에서 최대 0.5칸(DDA) — 그보다 멀리 비켜 갈 때만(벽) 궤적 쪽으로 휜다
				float k = std::clamp(-lat * 2.5f, -0.6f, 0.6f);
				if (std::fabs(latEnd) > 0.55f)
					k += std::clamp((std::fabs(latEnd) - 0.55f) * 2.f, 0.f, 1.2f) * (latEnd > 0.f ? 1.f : -1.f);
				else if (std::fabs(m_latF) > 0.15f)
					k += std::clamp((std::fabs(m_latF) - 0.15f) * 1.5f, 0.f, 0.4f) * (m_latF > 0.f ? 1.f : -1.f);
				if (!Norm(Vector2{ u.x + n.x * k, u.y + n.y * k }, dir)) dir = u;
				const float prog = Dot(Sub(m_logical, m_f.pos), u);
				target = vRule * GridRate(u) * sync(prog);
			}
			else
			{
				// 논리 궤적을 앞보기로 따라간다 — 멈출 때는 마지막 칸으로
				const Vector2 ahead = stopping ? end : AheadOnChain(now, kLookAhead);
				const Vector2 to = Sub(ahead, m_f.pos);
				const float l = std::hypot(to.x, to.y);
				if (l > 1e-4f) dir = Mul(to, 1.f / l);
				const float prog = Dot(Sub(m_logical, m_f.pos), dir);
				target = (moving || l > 0.004f) ? vRule * sync(prog) : 0.f;
				if (stopping)
				{
					// 마지막 칸까지 남은 거리로 제동: v = √(2·a·d), a = 규칙 속도 / 100ms
					target = std::min(target, std::sqrt(2.f * (vRule / kStopSec) * l));
					if (!moving && l < 0.004f)
					{
						m_f.pos = end;
						m_f.speed = 0.f;
						target = 0.f;
					}
				}
			}
			m_dbg = Debug{ guided, stopping, target, Dot(Sub(m_logical, m_f.pos), dir) };
			m_f.speed = Approach(m_f.speed, target, vRule, dt);
			m_f.dir = dir;
			if (stopping && !guided && Dist(m_f.pos, end) <= m_f.speed * dt)
			{
				// 이번 프레임에 닿는다 — 넘어갔다 되돌아오지 않게 정확히 그 칸에
				m_f.pos = end;
				if (!moving) m_f.speed = 0.f;
			}
			else
			{
				m_f.pos.x += dir.x * m_f.speed * dt;
				m_f.pos.y += dir.y * m_f.speed * dt;
			}
			// 논리 위치에서 너무 멀어지지 않게(벽 파고들기 · 오래 어긋남 방지): 0.38칸부터 부드럽게 당기고 0.48칸에서 막는다.
			// 안내선을 따라 곧게 갈 때는 격자 지그재그(선에서 최대 0.5칸)를 받아 주도록 0.45 · 0.5칸(지시서: 늘 0.5칸 미만).
			const float soft = guided ? 0.45f : 0.38f, hard = guided ? kMaxDevGuided : kMaxDevLocal;
			Vector2 e = Sub(m_f.pos, m_logical);
			float el = std::hypot(e.x, e.y);
			if (el > soft)
			{
				const float pull = std::min(el - soft, (el - soft) * 8.f * dt);
				m_f.pos = Sub(m_f.pos, Mul(e, pull / el));
				e = Sub(m_f.pos, m_logical);
				el = std::hypot(e.x, e.y);
			}
			if (el > hard)
				m_f.pos = Add(m_logical, Mul(e, hard / el));
			m_stats.maxDeviation = std::max(m_stats.maxDeviation, std::min(el, hard));
		}
		if (dt > 1e-4f)
		{
			const Vector2 v{ (m_f.pos.x - prev.x) / dt, (m_f.pos.y - prev.y) / dt };
			const float kv = 1.f - std::exp(-dt / 0.05f);
			m_velocity.x += (v.x - m_velocity.x) * kv;
			m_velocity.y += (v.y - m_velocity.y) * kv;
		}
	}

	float LocalMover::Speed() const { return std::hypot(m_velocity.x, m_velocity.y); }
	float LocalMover::Deviation() const { return Dist(m_f.pos, m_logical); }

	// ─────────────────────────────────────────────────────────────────────────
	// 다른 개체
	// ─────────────────────────────────────────────────────────────────────────
	void TrackSnap(Track& tr, Vector2 p)
	{
		tr.qn = 0;
		tr.from = tr.to = tr.logical = p;
		tr.t = 1.f;
		tr.wait = 0.f;
		tr.extra = 0.f;
		tr.dryT = 0.f;
		tr.merge = 0.f;
		tr.margin = kRemoteDelay;
		tr.coastV = 0.f;
		tr.dash = false;
		tr.lastSpeed = 0.f;
		tr.f.Snap(p);
	}

	namespace
	{
		// 남은 재생 시간(지금 걸음의 나머지 + 쌓인 걸음)
		float Backlog(const Track& tr)
		{
			float b = tr.t < 1.f ? (1.f - tr.t) * tr.dur : 0.f;
			for (int i = 0; i < tr.qn; ++i) b += tr.q[static_cast<size_t>(i)].dur;
			return b;
		}
		// 걸음이 도착한 순간의 버퍼(남은 재생 시간)를 거른다 — 이 값이 100ms 근처에 머물도록 재생 속도를 조절
		void SampleMargin(Track& tr, float sample)
		{
			tr.margin += (std::clamp(sample, -0.2f, 1.f) - tr.margin) * 0.3f;
		}
	}

	void TrackStep(Track& tr, int x, int y, float durationSec, bool allowFar)
	{
		const Vector2 target = V(x, y);
		const Vector2 last = tr.qn > 0 ? tr.q[static_cast<size_t>(tr.qn - 1)].to : tr.to;
		const float far = Dist(last, target);
		if (far < 0.01f)
			return;   // 이미 그 칸에 있거나 가는 중(기술 이동을 먼저 보간해 둔 경우)
		if (allowFar && durationSec > 0.f && far < 8.f)
		{
			// 여러 칸 이동(돌진 · 도약 · 넉백): 밀린 걸음을 버리고 지금 화면 위치에서 곧장
			tr.qn = 0;
			tr.from = tr.f.pos;
			tr.to = target;
			tr.t = 0.f;
			tr.dur = durationSec;
			tr.wait = 0.f;
			tr.extra = 0.f;
			tr.merge = 0.f;
			tr.dash = true;
			return;
		}
		if (durationSec <= 0.f || far > 2.6f)
		{
			TrackSnap(tr, target);   // 순간이동 · 재배치 · 시야 밖에서 한참 이동
			return;
		}
		if (tr.t >= 1.f && tr.qn == 0)
		{
			// 재생할 걸음이 없었다. 걷던 중이면(잠깐 늦었을 뿐) 지금 논리 위치(외삽했으면 그 자리)에서 바로 이어
			// 한 걸음 안에 제 길로 합류하고, 서 있다 출발이면 100ms 뒤에 시작한다(그동안 다음 걸음이 도착해 쌓인다).
			const bool wasMoving = tr.lastSpeed > 0.3f && tr.dryT < 0.6f;
			if (wasMoving)
			{
				SampleMargin(tr, -tr.dryT);   // 늦었다 — 버퍼가 모자란다
				tr.lateMax = std::max(tr.lateMax, std::min(tr.dryT + 0.03f, kLateMax));
			}
			else tr.margin = kRemoteDelay;
			tr.from = tr.logical;
			tr.to = target;
			tr.t = 0.f;
			tr.dur = durationSec;
			if (tr.extra > 0.f && tr.lastSpeed > 0.3f)
			{
				// 외삽한 자리에서 합류: 같은 속도로(남은 거리 ÷ 걷던 속도) — 늦춰지거나 튀지 않게
				tr.dur = std::clamp(Dist(tr.from, target) / tr.lastSpeed, durationSec * 0.5f, durationSec * 1.5f);
			}
			tr.wait = wasMoving ? 0.f : kRemoteDelay;
			tr.extra = 0.f;
			tr.coastV = 0.f;
			tr.dryT = 0.f;
			tr.merge = 0.f;
			tr.dash = false;
			return;
		}
		SampleMargin(tr, Backlog(tr));
		if (tr.qn < static_cast<int>(tr.q.size()))
			tr.q[static_cast<size_t>(tr.qn++)] = Track::Step{ target, durationSec };
		else
		{
			// 너무 밀렸다(창이 멈췄다 돌아옴 등) — 가장 오래된 걸음을 건너뛴다
			for (int i = 1; i < tr.qn; ++i) tr.q[static_cast<size_t>(i - 1)] = tr.q[static_cast<size_t>(i)];
			tr.q[static_cast<size_t>(tr.qn - 1)] = Track::Step{ target, durationSec };
		}
	}

	Vector2 TrackUpdate(Track& tr, float dt, float visualDt, bool allowExtrap)
	{
		// ── 논리 재생: 100ms 뒤 시간표. 걸음이 도착할 때의 버퍼(남은 재생 시간)가 100ms보다 모자라면 조금 늦게(−10%까지),
		//    남으면 조금 빨리(+12%까지) 재생해 버퍼를 지킨다 — 스파이크 뒤 몰려 온 걸음도 이 값이 커져서 천천히 따라잡는다.
		//    (예전: 쌓인 재생 시간 전체로 따라잡기를 정했더니 걸음이 막 도착한 직후마다 5~10% 빨라져 버퍼를 스스로 갉아먹었다) ──
		const float backlog = Backlog(tr);
		tr.lateMax = std::max(0.f, tr.lateMax - dt * kLateDecay);
		const float want = kRemoteDelay + (allowExtrap ? tr.lateMax : 0.f);   // 지킬 버퍼(나쁜 망에서는 늘어난다)
		float rate = 1.f + std::clamp((tr.margin - want) * 1.0f, -kSlowMax, kCatchupMax);
		// 부드러운 착지: 쌓인 걸음 없이 지금 걸음이 끝나 가면 재생을 절반까지 늦춘다 — 다음 걸음이 조금 늦게 와도
		// 칸에서 멈춰 섰다 떠나지 않고, 정말 멈춘 것이면 자연스럽게 감속하며 선다
		// (통신 전체가 멈춘 동안의 플레이어는 늦추지 않는다 — 곧 외삽으로 같은 속도로 이어 간다)
		const bool netStall = allowExtrap && g_netQuiet > kQuietSec && tr.lastSpeed > 0.3f;
		if (!tr.dash && tr.qn == 0 && tr.t < 1.f && !netStall)
			rate *= std::clamp(kLandMin + (1.f - kLandMin) * backlog / kLandSec, kLandMin, 1.f);
		float remain = dt;
		if (tr.wait > 0.f)
		{
			const float w = std::min(tr.wait, remain);
			tr.wait -= w;
			remain -= w;
		}
		remain *= tr.dash ? 1.f : rate;
		while (remain > 0.f)
		{
			if (tr.t < 1.f)
			{
				const float dur = std::max(0.04f, tr.dur);
				const float need = (1.f - tr.t) * dur;
				if (remain < need) { tr.t += remain / dur; remain = 0.f; break; }
				remain -= need;
				tr.t = 1.f;
				const Vector2 d = Sub(tr.to, tr.from);
				const float l = std::hypot(d.x, d.y);
				if (l > 1e-4f && !tr.dash)
				{
					tr.lastDir = Mul(d, 1.f / l);
					tr.lastSpeed = l / dur;
				}
				tr.dash = false;
			}
			if (tr.qn == 0)
				break;
			// 다음 걸음을 남은 시간만큼 이어서(칸 중앙에서 멈칫하지 않게)
			tr.from = tr.to;
			tr.to = tr.q[0].to;
			tr.dur = tr.q[0].dur;
			for (int i = 1; i < tr.qn; ++i) tr.q[static_cast<size_t>(i - 1)] = tr.q[static_cast<size_t>(i)];
			--tr.qn;
			tr.t = 0.f;
		}
		const bool dry = tr.t >= 1.f && tr.qn == 0;
		if (!dry)
			tr.logical = Lerp(tr.from, tr.to, tr.t);
		else
		{
			tr.dryT += dt;
			// 걸음이 바닥났다(플레이어). 통신 전체가 멈춘 동안(TCP 재전송 — 아무 패킷도 안 옴)만 착지 속도로 이어 미끄러진다
			// (외삽, 최대 0.4칸). 다른 패킷이 오면 그 자리에 선다 — 되돌아가지 않는다(예전: 외삽 ↔ 되돌림이 번갈아 흔들렸다).
			// 늦은 걸음이 오면 이 자리에서 곧장 이어 가고(TrackStep), 0.5초가 지나도 없으면 멈춘 것 — 제 칸으로 천천히
			// 돌아간다(몸 방향은 그대로 — UpdateRemote).
			const bool coast = allowExtrap && tr.lastSpeed > 0.3f && tr.dryT < kCoastHold && tr.merge <= 0.f;
			if (coast)
			{
				if (g_netQuiet > kQuietSec) tr.coastV = tr.lastSpeed * kCoastRate;
				else if (tr.dryT < kCreepSec && tr.extra < kCreepTiles) tr.coastV = tr.lastSpeed * kCreepRate;   // 막 바닥남 — 아직 모른다: 착지 속도로 조금만
				else if (tr.extra > kCreepTiles) tr.coastV *= std::exp(-dt / kCoastStopTau);   // 외삽하던 중 통신이 살아났다 — 짧게 줄어들며 선다
				else tr.coastV = 0.f;                                                            // 통신은 살아 있는데 이 걸음만 늦다 — 그 자리에서 기다린다
				if (tr.coastV > 0.02f && tr.extra < kExtrapTiles)
					tr.extra = std::min(kExtrapTiles, tr.extra + tr.coastV * dt);
				tr.logical = Add(tr.to, Mul(tr.lastDir, tr.extra));   // 마지막 칸에서 걷던 방향으로
			}
			else if (tr.extra > 0.f)
			{
				if (tr.merge <= 0.f) { tr.merge = 1.f; tr.mergeFrom = tr.logical; }
				tr.merge = std::max(0.f, tr.merge - dt / kSettleSec);
				tr.logical = Lerp(tr.to, tr.mergeFrom, util::EaseInOutCubic(tr.merge));
				if (tr.merge <= 0.f) { tr.extra = 0.f; tr.logical = tr.to; }
			}
			else
				tr.logical = tr.to;
			if (tr.dryT > 0.6f) tr.lastSpeed = 0.f;
		}

		// ── 화면 추종자: 논리 궤적을 앞보기로, 명시 속도로 ──
		const float vdt = std::max(0.f, visualDt);
		if (tr.dash)
		{
			tr.f.pos = tr.logical;
			return tr.f.pos;
		}
		if (vdt <= 1e-6f)
			return tr.f.pos;
		// 첫 걸음을 100ms 기다리는 동안은 화면도 기다린다(논리보다 먼저 떠나지 않게)
		const bool coasting = dry && allowExtrap && tr.merge <= 0.f && tr.lastSpeed > 0.3f && tr.dryT < kCoastHold && tr.coastV > 0.02f
			&& tr.extra < kExtrapTiles;
		const bool moving = tr.wait <= 0.f && (!dry || coasting);
		// vStep: 걷는 속도(착지 · 미끄러짐으로 줄이기 전) — 가감속의 기준. vNom: 지금 따라갈 속도
		const float vStep = std::max(0.5f, !dry ? Dist(tr.from, tr.to) / std::max(0.04f, tr.dur) : tr.lastSpeed);
		const float vNom = !dry ? vStep * rate : (coasting ? std::max(0.3f, tr.coastV) : vStep);
		// 앞보기 점: 지금 걸음의 끝 → 밀린 걸음들
		Vector2 ahead = tr.logical;
		if (!dry)
		{
			float rem = kLookAhead;
			Vector2 cur = tr.logical;
			bool found = false;
			for (int i = -1; i < tr.qn && !found; ++i)
			{
				const Vector2 nxt = i < 0 ? tr.to : tr.q[static_cast<size_t>(i)].to;
				const float l = Dist(cur, nxt);
				if (l >= rem && l > 1e-5f) { ahead = Lerp(cur, nxt, rem / l); found = true; }
				else { rem -= l; cur = nxt; }
			}
			if (!found) ahead = cur;
		}
		else if (moving)
			ahead = Add(tr.logical, Mul(tr.lastDir, kLookAhead * 0.5f));
		const Vector2 to = Sub(ahead, tr.f.pos);
		const float l = std::hypot(to.x, to.y);
		if (l > 1e-4f) tr.f.dir = Mul(to, 1.f / l);
		// 진행 차이는 이번 프레임에 움직인 뒤 기준(프레임 길이와 무관 — 화면이 논리보다 한 프레임 앞서 끝 칸에 먼저 닿아 서지 않게)
		const float prog = Dot(Sub(tr.logical, tr.f.pos), tr.f.dir) - tr.f.speed * vdt;
		float target = moving ? vNom * (1.f + std::clamp(prog * kSyncGain, -kSyncMax, kSyncMax)) : vNom;
		// 제동: 알고 있는 길의 끝(쌓인 걸음이 없으면 지금 걸음의 끝 칸, 걸음이 바닥났으면 논리 위치 — 미끄러지는 중이면
		// 그 자리)을 넘지 않게 v = √(2·a·d)로. 가속도는 걷는 속도 기준(착지 · 미끄러짐으로 줄어든 속도로 정하면 제동이 약해 넘어간다).
		const bool brake = tr.qn == 0 && !(allowExtrap && g_netQuiet > kQuietSec && tr.lastSpeed > 0.3f);
		const Vector2 stopAt = dry ? tr.logical : tr.to;
		if (brake)
		{
			const float d = std::max(0.f, Dot(Sub(stopAt, tr.f.pos), tr.f.dir));
			target = std::min(target, std::sqrt(2.f * (vStep / kStopSec) * d));
			if (!moving && Dist(tr.f.pos, stopAt) < 0.004f) { tr.f.pos = stopAt; tr.f.speed = 0.f; target = 0.f; }
		}
		tr.f.speed = Approach(tr.f.speed, target, vStep, vdt);
		if (brake && Dist(tr.f.pos, stopAt) <= tr.f.speed * vdt)
		{
			tr.f.pos = stopAt;   // 이번 프레임에 닿는다 — 넘어가지 않게
			if (!moving) tr.f.speed = 0.f;
		}
		else
		{
			tr.f.pos.x += tr.f.dir.x * tr.f.speed * vdt;
			tr.f.pos.y += tr.f.dir.y * tr.f.speed * vdt;
		}
		const Vector2 e = Sub(tr.f.pos, tr.logical);
		const float el = std::hypot(e.x, e.y);
		if (el > kMaxDevRemote)
			tr.f.pos = Add(tr.logical, Mul(e, kMaxDevRemote / el));
		return tr.f.pos;
	}

	void RemoteStep(Entity& e, int x, int y, float durationSec, bool allowFar)
	{
		TrackStep(e.track, x, y, durationSec, allowFar);
	}

	void RemoteSnap(Entity& e, Vector2 p)
	{
		TrackSnap(e.track, p);
		e.pos = e.logPos = p;
	}

	void UpdateRemote(Entity& e, float dt, float visualDt)
	{
		const Vector2 prev = e.pos;
		// 이어 미끄러지기 · 버퍼 늘리기는 플레이어만(몬스터 · NPC는 AI가 자주 멈춰 서서 넘어갔다 돌아오는 것이 더 거슬린다)
		e.pos = TrackUpdate(e.track, dt, visualDt, e.kind == EntityKind::Player);
		e.logPos = e.track.logical;
		const float nominal = e.track.lastSpeed > 0.3f ? e.track.lastSpeed : 3.57f;
		Animate(e, prev, visualDt, nominal, e.track.merge > 0.f);
	}

	void Animate(Entity& e, Vector2 prevVisual, float dt, float nominalSpeed, bool holdHeading)
	{
		// 서 있는 동안 밖에서 정한 방향(공격 · 대화 · 순간이동 뒤)을 몸이 받아들인다
		if (e.body.moving < 0.25f && (e.faceRight != e.body.faceRight || e.faceBack != e.body.faceBack))
			e.body.Face(e.faceRight, e.faceBack);
		e.body.Update(prevVisual, e.pos, dt, nominalSpeed, holdHeading);
		e.faceRight = e.body.faceRight;
		e.faceBack = e.body.faceBack;
		e.walkPhase = e.body.stride;
		e.moving = e.body.moving;
		e.run = e.body.run;
		e.speed = e.body.speed;
	}
}
