#pragma once

#include "raylib.h"
#include "net/Shared.h"
#include "Data/MoveRule.h"

#include <cstdint>
#include <deque>

struct Entity;

// ─────────────────────────────────────────────────────────────────────────────
// 이동 표현 — "서버는 칸, 화면은 연속".
//
// 내 캐릭터(LocalMover)
//   · 입력이 들어오면 서버 규칙(Data/MoveRule.h)으로 결과 칸을 **그대로** 예측하고
//     순번(seq)을 붙여 보낸다. 응답을 기다리지 않고 최대 kMaxPending걸음까지 앞서
//     나가므로 지연이 커도 멈추지 않는다.
//   · ACK가 오면 서버 좌표 + 아직 응답 안 온 입력을 다시 재생해 예측 칸을 맞춘다
//     (server reconciliation). 틀렸으면 논리 위치만 짧게 미끄러뜨려 보정한다.
//   · 논리 위치(칸 사이를 걸음 시간 동안 직선 이동)를 표시 위치가 지수 스무딩으로
//     따라간다 → 출발 가속·정지 감속·방향 전환 모서리가 자연스럽게 둥글어진다.
// 다른 개체(RemoteStep/UpdateRemote)
//   · SUBJECT_MOVE_NFY.move_time(서버가 실은 걸음 시간)대로 보간하고, 밀린 걸음이
//     쌓이면 조금 빨리 재생해 따라잡는다.
// ─────────────────────────────────────────────────────────────────────────────
namespace motion
{
	inline constexpr int   kMaxPending = 3;       // 응답 없이 앞서 보낼 수 있는 걸음 수
	inline constexpr float kVisualTau = 0.055f;   // 표시 위치 스무딩 시간 상수(초)
	inline constexpr float kRemoteTau = 0.045f;

	class LocalMover
	{
	public:
		void Reset(int x, int y);                         // 로그인·순간이동·부활(스냅)
		void SetConfig(uint32_t baseStepMs, uint32_t diagPermille) { m_baseStepMs = baseStepMs; m_diagPermille = diagPermille; }
		void SetSpeedPercent(uint32_t percent) { m_speedPercent = percent; }
		[[nodiscard]] uint32_t SpeedPercent() const { return m_speedPercent; }

		// 지금 한 걸음을 더 보낼 수 있는가(걸음 시간·미응답 수)
		[[nodiscard]] bool CanStep(double now) const;
		// dir 방향으로 움직였을 때 실제로 칸이 바뀌는가(서버 규칙: 축별 미끄러짐 포함)
		[[nodiscard]] bool WouldMove(int dir) const;
		// 걸음을 예측 적용하고 보낼 순번을 돌려준다. 못 움직이면 false.
		bool Step(double now, int dir, uint16_t& outSeq);

		void OnAck(uint16_t seq, int x, int y, MoveResult result, uint16_t stepMs, double now);
		// 본인에 대한 SUBJECT_MOVE_NFY(존 이동 메아리·순간이동·부활). 순간이동이면 true.
		bool OnServerPosition(int x, int y, double now);
		// 기술 이동(돌진·잔상 회피·그림자 돌진·공간 도약) — 서버가 정한 도착 칸으로
		// duration초 동안 빠르게 옮긴다(0이면 순간이동). 기술을 쓰는 동안은 걸음을 보내지
		// 않으므로(Game의 행동 잠금) 미응답 입력이 없다.
		void Dash(int x, int y, double now, double duration);

		// 논리/표시 위치 갱신. visualDt는 히트스톱이 반영된 시간(멈춤 연출).
		void Update(double now, float visualDt);

		[[nodiscard]] int PredX() const { return m_predX; }
		[[nodiscard]] int PredY() const { return m_predY; }
		[[nodiscard]] int SrvX() const { return m_srvX; }
		[[nodiscard]] int SrvY() const { return m_srvY; }
		[[nodiscard]] int PendingCount() const { return static_cast<int>(m_pending.size()); }
		[[nodiscard]] Vector2 Visual() const { return m_visual; }
		[[nodiscard]] Vector2 Logical() const { return m_logical; }
		[[nodiscard]] Vector2 Velocity() const { return m_velocity; }   // 칸/초(표시 기준)
		[[nodiscard]] float Speed() const;                             // 칸/초
		[[nodiscard]] bool IsWalking(double now) const { return now < m_segT1 + 0.02; }
		[[nodiscard]] int LastDir() const { return m_lastDir; }
		[[nodiscard]] bool Snapped() { const bool s = m_snapped; m_snapped = false; return s; }
		[[nodiscard]] double NextStepAt() const { return m_nextStepAt; }
		[[nodiscard]] uint32_t StepMs(int dir) const;

		// 검증용 통계
		struct Stats { int steps = 0, acks = 0, rejects = 0, corrections = 0, maxPending = 0, teleports = 0; float maxCorrection = 0.f; };
		[[nodiscard]] const Stats& GetStats() const { return m_stats; }

	private:
		struct Pending { uint16_t seq; uint8_t dir; double sentAt; };

		void Replay();                                       // srv + 미응답 입력 → pred
		void Retarget(double now);                           // pred가 바뀌면 논리 위치를 부드럽게 이어 준다
		[[nodiscard]] Vector2 LogicalAt(double now) const;

		std::deque<Pending> m_pending;
		int      m_srvX = 0, m_srvY = 0;
		int      m_predX = 0, m_predY = 0;
		uint16_t m_seq = 0;
		uint32_t m_baseStepMs = 280, m_diagPermille = 1414, m_speedPercent = 100;
		double   m_nextStepAt = 0.0;
		int      m_lastDir = 1;

		// 논리 위치 = segFrom → segTo 를 [segT0, segT1] 동안
		Vector2  m_segFrom{}, m_segTo{};
		double   m_segT0 = 0.0, m_segT1 = 0.0;
		Vector2  m_logical{}, m_visual{}, m_velocity{};
		bool     m_snapped = false;
		Stats    m_stats;
	};

	// 다른 개체의 한 걸음(서버 알림). durationSec <= 0 이거나 너무 멀면 스냅.
	// allowFar: 돌진·도약처럼 여러 칸을 한 번에 가는 이동(멀어도 보간한다).
	void RemoteStep(Entity& e, int x, int y, float durationSec, bool allowFar = false);
	// 다른 개체의 논리/표시 위치·걷기 애니메이션 갱신
	void UpdateRemote(Entity& e, float dt, float visualDt);
	// 걷기 위상/속도(애니메이션) — 표시 위치가 움직인 거리 기준
	void Animate(Entity& e, Vector2 prevVisual, float dt);
}
