#include "game/Motion.h"
#include "game/Entity.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>

namespace motion
{
	namespace
	{
		Vector2 V(int x, int y) { return Vector2{ static_cast<float>(x), static_cast<float>(y) }; }

		float Dist(Vector2 a, Vector2 b)
		{
			const float dx = a.x - b.x, dy = a.y - b.y;
			return std::sqrt(dx * dx + dy * dy);
		}

		// 지수 스무딩 계수(프레임 레이트 무관)
		float Follow(float dt, float tau) { return tau <= 0.f ? 1.f : 1.f - std::exp(-dt / tau); }
	}

	// ─────────────────────────────────────────────────────────────────────────
	// LocalMover
	// ─────────────────────────────────────────────────────────────────────────
	void LocalMover::Reset(int x, int y)
	{
		m_pending.clear();
		m_srvX = m_predX = x;
		m_srvY = m_predY = y;
		m_segFrom = m_segTo = m_logical = m_visual = V(x, y);
		m_segT0 = m_segT1 = 0.0;
		m_velocity = Vector2{ 0, 0 };
		m_nextStepAt = 0.0;
		m_snapped = true;
	}

	uint32_t LocalMover::StepMs(int dir) const
	{
		const uint32_t speed = MoveRule::ClampSpeed(m_speedPercent);
		const uint32_t base = m_baseStepMs * 100u / speed;
		return MoveRule::IsDiagonal(dir) ? base * m_diagPermille / 1000u : base;
	}

	double LocalMover::SentAt(uint16_t seq) const
	{
		for (const Pending& p : m_pending)
			if (p.seq == seq) return p.sentAt;
		return -1.0;
	}

	bool LocalMover::CanStep(double now) const
	{
		return static_cast<int>(m_pending.size()) < kMaxPending && now + 0.004 >= m_nextStepAt;
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

		// 걸음을 이어서 누르고 있으면 직전 걸음이 끝난 순간부터 바로 이어 간다
		// (프레임 단위 늦음으로 칸 중앙에서 멈칫하지 않게). 낮은 FPS(프레임 80ms)도
		// 덮도록 0.12초까지 거슬러 시작한다 — 서버 토큰 버킷의 catch-up(200ms) 안쪽이다.
		constexpr double kChain = 0.12;
		const double t0 = (now - m_nextStepAt) < kChain && m_nextStepAt > 0.0 ? std::max(m_nextStepAt, now - kChain) : now;
		const double dur = StepMs(dir) / 1000.0;

		m_segFrom = LogicalAt(now);
		m_segTo = V(x, y);
		m_segT0 = t0;
		m_segT1 = t0 + dur;
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

	void LocalMover::Retarget(double now)
	{
		const Vector2 target = V(m_predX, m_predY);
		if (target.x == m_segTo.x && target.y == m_segTo.y)
			return;
		const Vector2 cur = LogicalAt(now);
		const float d = Dist(cur, target);
		// 걸음 도중 목표 칸이 바뀐 것 = 예측이 틀렸다
		const float err = Dist(m_segTo, target);
		if (err > 0.01f)
		{
			++m_stats.corrections;
			m_stats.maxCorrection = std::max(m_stats.maxCorrection, err);
		}
		if (d > 3.5f)
		{
			// 크게 틀렸다(순간이동 등) — 스냅
			m_segFrom = m_segTo = m_logical = m_visual = target;
			m_segT0 = m_segT1 = now;
			m_snapped = true;
			return;
		}
		// 짧게 미끄러뜨려 보정(한 칸당 0.12초)
		m_segFrom = cur;
		m_segTo = target;
		m_segT0 = now;
		m_segT1 = now + std::max(0.08, 0.12 * d);
		m_nextStepAt = std::max(m_nextStepAt, m_segT1 - 0.05);
	}

	void LocalMover::OnAck(uint16_t seq, int x, int y, MoveResult result, uint16_t stepMs, double now)
	{
		(void)stepMs;
		// 받은 순서대로 정확히 하나씩 응답이 온다(TCP·서버 FIFO). 순번이 앞선 것들은
		// 이미 지난 응답이므로 버린다.
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
			// 거절(너무 빠름/행동 불가) — 다음 걸음은 조금 쉬었다가
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
		// 순간이동/부활/서버 재배치 — 미응답 입력은 서버가 새 위치에서 처리한다.
		++m_stats.teleports;
		m_srvX = x;
		m_srvY = y;
		Replay();
		const Vector2 target = V(m_predX, m_predY);
		m_segFrom = m_segTo = m_logical = m_visual = target;
		m_segT0 = m_segT1 = now;
		m_velocity = Vector2{ 0, 0 };
		m_snapped = true;
		return true;
	}

	void LocalMover::Dash(int x, int y, double now, double duration)
	{
		m_pending.clear();
		m_srvX = m_predX = x;
		m_srvY = m_predY = y;
		const Vector2 target = V(x, y);
		if (duration <= 0.0)
		{
			// 순간이동형(옛길 도약·그늘 걸음): 몸은 바로 옮기고 카메라는 미끄러지게 둔다
			m_segFrom = m_segTo = m_logical = m_visual = target;
			m_segT0 = m_segT1 = now;
			m_velocity = Vector2{ 0, 0 };
			return;
		}
		m_segFrom = LogicalAt(now);
		m_segTo = target;
		m_segT0 = now;
		m_segT1 = now + duration;
		m_nextStepAt = std::max(m_nextStepAt, m_segT1 - 0.02);
	}

	Vector2 LocalMover::LogicalAt(double now) const
	{
		if (now >= m_segT1 || m_segT1 <= m_segT0)
			return m_segTo;
		if (now <= m_segT0)
			return m_segFrom;
		const float t = static_cast<float>((now - m_segT0) / (m_segT1 - m_segT0));
		return util::LerpV(m_segFrom, m_segTo, t);
	}

	void LocalMover::Update(double now, float visualDt)
	{
		// 응답이 한참 안 오면(연결 이상) 예측을 서버 좌표로 되돌린다.
		if (!m_pending.empty() && now - m_pending.front().sentAt > 3.0)
		{
			m_pending.clear();
			Replay();
			Retarget(now);
		}

		m_logical = LogicalAt(now);
		const Vector2 prev = m_visual;
		const float k = Follow(visualDt, kVisualTau);
		m_visual.x += (m_logical.x - m_visual.x) * k;
		m_visual.y += (m_logical.y - m_visual.y) * k;
		if (visualDt > 1e-4f)
		{
			const Vector2 v{ (m_visual.x - prev.x) / visualDt, (m_visual.y - prev.y) / visualDt };
			const float kv = Follow(visualDt, 0.06f);
			m_velocity.x += (v.x - m_velocity.x) * kv;
			m_velocity.y += (v.y - m_velocity.y) * kv;
		}
	}

	float LocalMover::Speed() const
	{
		return std::sqrt(m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// 다른 개체
	// ─────────────────────────────────────────────────────────────────────────
	void RemoteStep(Entity& e, int x, int y, float durationSec, bool allowFar)
	{
		const Vector2 target = V(x, y);
		const Vector2 lastQueued = e.stepQN > 0 ? e.stepQ[e.stepQN - 1].to : e.to;
		const float far = Dist(lastQueued, target);
		if (far < 0.01f)
			return;   // 이미 그 칸에 있거나 가는 중(기술 이동을 먼저 보간해 둔 경우)
		if (allowFar && durationSec > 0.f && far < 8.f)
		{
			// 여러 칸 이동(돌진·도약): 밀린 걸음을 버리고 지금 위치에서 곧장 간다
			e.stepQN = 0;
			e.from = e.logPos;
			e.to = target;
			e.moveT = 0.f;
			e.moveDur = durationSec;
			return;
		}
		if (durationSec <= 0.f || far > 2.6f)
		{
			// 순간이동/재배치/시야 밖에서 한참 이동 — 스냅
			e.stepQN = 0;
			e.from = e.to = e.logPos = e.pos = target;
			e.moveT = 1.f;
			return;
		}
		if (e.moveT >= 1.f && e.stepQN == 0)
		{
			e.from = e.logPos;
			e.to = target;
			e.moveT = 0.f;
			e.moveDur = durationSec;
			return;
		}
		if (e.stepQN < static_cast<int>(e.stepQ.size()))
			e.stepQ[e.stepQN++] = Entity::QueuedStep{ target, durationSec };
		else
		{
			// 너무 밀렸다(창이 멈췄다 돌아옴 등) — 가장 오래된 걸음을 버리고 새 걸음을 넣는다.
			// 다음 칸들은 빠르게(×1.8) 재생되며 따라잡는다.
			for (int i = 1; i < e.stepQN; ++i) e.stepQ[i - 1] = e.stepQ[i];
			e.stepQ[e.stepQN - 1] = Entity::QueuedStep{ target, durationSec };
			e.to = e.stepQ[0].to;
		}
	}

	void UpdateRemote(Entity& e, float dt, float visualDt)
	{
		// 밀린 걸음이 있으면 조금 빠르게 재생해 따라잡는다.
		const float catchUp = e.stepQN >= 3 ? 1.8f : (e.stepQN == 2 ? 1.35f : (e.stepQN == 1 ? 1.1f : 1.f));
		float remain = dt * catchUp;
		while (remain > 0.f)
		{
			if (e.moveT < 1.f)
			{
				const float dur = std::max(0.04f, e.moveDur);
				const float need = (1.f - e.moveT) * dur;
				if (remain < need) { e.moveT += remain / dur; remain = 0.f; break; }
				remain -= need;
				e.moveT = 1.f;
			}
			if (e.stepQN == 0)
				break;
			// 다음 걸음을 남은 시간만큼 이어서 재생(칸 중앙에서 멈칫하지 않게)
			e.from = e.to;
			e.to = e.stepQ[0].to;
			e.moveDur = e.stepQ[0].dur;
			for (int i = 1; i < e.stepQN; ++i) e.stepQ[i - 1] = e.stepQ[i];
			--e.stepQN;
			e.moveT = 0.f;
		}
		e.logPos = util::LerpV(e.from, e.to, e.moveT);
		const Vector2 prev = e.pos;
		const float k = Follow(visualDt, kRemoteTau);
		e.pos.x += (e.logPos.x - e.pos.x) * k;
		e.pos.y += (e.logPos.y - e.pos.y) * k;
		Animate(e, prev, visualDt);
	}

	void Animate(Entity& e, Vector2 prevVisual, float dt)
	{
		const float dist = Dist(e.pos, prevVisual);
		// 걷기 위상은 이동 거리 기준 — 한 칸에 반 주기(발 미끄러짐 없음)
		e.walkPhase += dist * util::kPi;
		if (dt > 1e-4f)
		{
			const float speed = dist / dt;
			e.speed = util::Damp(e.speed, speed, 12.f, dt);
		}
		// idle ↔ walk: 속도로 섞는다(칸/초 기준 0.6에서 걷기 시작)
		const float walkTarget = util::Clamp01((e.speed - 0.35f) / 1.2f);
		e.moving = util::Damp(e.moving, walkTarget, walkTarget > e.moving ? 14.f : 9.f, dt);
		// run: 기본 속도(약 3.6칸/초)보다 빠르면 달리기 자세
		e.run = util::Damp(e.run, util::Clamp01((e.speed - 3.9f) / 1.2f), 6.f, dt);
		// 진행 방향으로 몸을 돌린다(화면 좌우/앞뒤)
		if (dist > 0.004f)
		{
			const float dx = e.pos.x - prevVisual.x, dy = e.pos.y - prevVisual.y;
			const float sx = dx - dy, sy = dx + dy;
			if (std::fabs(sx) > 0.002f) e.faceRight = sx > 0.f;
			if (std::fabs(sy) > 0.002f) e.faceBack = sy < 0.f;
		}
	}
}
