#pragma once

#include "raylib.h"
#include "net/Shared.h"
#include "Data/MoveRule.h"

#include <array>
#include <cstdint>
#include <deque>

struct Entity;

// ─────────────────────────────────────────────────────────────────────────────
// 이동 표현(Phase 1-C) — "서버는 칸, 화면은 길". 설계: docs/upgrade/PHASE_1.md 4장.
//
//   의도(입력) ─▶ 걸음 계획(칸, Game) ─▶ 논리 궤적 Lg(t) ─▶ 화면 추종자 P(t) ─▶ 몸(방향 · 걸음 · 자세)
//
// 내 캐릭터(LocalMover)
//   · 서버 규칙(Data/MoveRule.h)으로 결과 칸을 그대로 예측하고 순번(seq)을 붙여 보낸다.
//     다음 걸음은 **지금 걸음이 0.11초 남았을 때** 확정 · 전송한다 — 논리 궤적이 늘 조금 앞까지 정해져 있어
//     칸 경계에서 멈출 구조가 없다. 응답 없이 앞선 걸음은 최대 3개(서버 토큰 버킷 허용량 안).
//   · 논리 궤적 = 확정한 걸음 조각을 이어 붙인 시간 함수(걸음 시간대로 직선).
//   · 화면 추종자 = 위치 · 속도를 가진 점. 속도는 **명시적으로** 정하고(규칙 속도 × 격자 진행 비율 × 진행 동기화 ±25%,
//     출발 80ms · 정지 100ms 곡선), 방향은 안내선(걸음 계획과 같은 곧은 선 — Game::RunLine) 또는 논리 궤적의 0.55칸
//     앞을 본다. 궤적이 선에서 0.6칸 넘게 비켜 가면(장애물을 도는 중) 궤적을 따른다.
//     논리 위치에서 0.5칸(궤적을 따를 때 0.48칸) 넘게 벗어나지 않는다(벽 파고들기 없음).
// 다른 개체(RemoteStep/UpdateRemote) — 플레이어 · 몬스터 · NPC 공통(Track)
//   · 100ms 뒤를 그리는 재생 시간표(걸음 시간 = SUBJECT_MOVE_NFY.move_time). 걸음이 도착할 때 남은 재생 시간(버퍼)을
//     100ms 근처로 지키도록 재생 속도를 −8% ~ +12%로 천천히 조절한다(보내는 쪽 박자가 조금 느려도 칸마다 서지 않게).
//     쌓인 걸음 없이 걸음이 끝나 가면 재생을 50%까지 늦춰 부드럽게 착지한다.
//   · 플레이어만: 걸음이 바닥나면 착지 속도로 0.1칸까지 이어 가고, 통신 전체가 멈춘 동안(TCP 재전송)은 걷던 속도의 90%로
//     0.4칸까지(외삽 — 지시서 상한 1칸). 다른 패킷이 오면 그 자리에서 짧게 줄어들며 선다(되돌아가지 않는다). 늦게 온 걸음은
//     그 자리에서 곧장 이어 합류하고, 늦었던 만큼 버퍼를 늘린다(최대 +250ms, 천천히 원래대로). 0.5초가 지나도 안 오면 멈춘 것 —
//     제 칸으로 천천히 돌아간다(몸 방향은 그대로). 순간이동류만 스냅.
// 몸(Body) — 진행 각(초당 1080°) → 8구역(경계 15° 히스테리시스) → 그림 방향, 좌우 반전은 60ms에 걸쳐,
//   걸음 위상은 이동 거리 ÷ 보폭, 발이 닿는 순간 알림(발소리 · 먼지), 출발 앞기울기 · 도는 쪽 기울기 · 멈춤 흔들림.
// ─────────────────────────────────────────────────────────────────────────────
namespace motion
{
	inline constexpr int    kMaxPending = 3;        // 응답 없이 앞서 보낼 수 있는 걸음 수
	inline constexpr double kCommitLead = 0.11;     // 지금 걸음이 이만큼 남으면 다음 걸음을 확정(직선 61% · 대각 72% 지점)
	inline constexpr double kChain = 0.12;          // 늦은 프레임: 직전 걸음 끝으로 이만큼까지 거슬러 이어 붙인다
	inline constexpr float  kLookAhead = 0.55f;     // 논리 궤적 앞보기(칸)
	inline constexpr float  kStartSec = 0.08f;      // 출발: 0 → 규칙 속도
	inline constexpr float  kStopSec = 0.10f;       // 정지: 규칙 속도 → 0
	inline constexpr float  kSyncGain = 0.30f;      // 진행 차이 1칸 → 속도 30%(상한 kSyncMax)
	inline constexpr float  kSyncMax = 0.25f;
	inline constexpr float  kSyncDeadAhead = 0.06f; // 논리가 이만큼 앞서는 것은 그대로(출발 가속이 남긴 뒤처짐)
	inline constexpr float  kSyncDeadBehind = 0.03f;
	inline constexpr float  kMaxDevLocal = 0.48f;   // 화면 ↔ 논리 최대 차이(칸)
	inline constexpr float  kMaxDevGuided = 0.499f; // 안내선을 따라 곧게 갈 때(격자 지그재그 ≤ 0.5칸)
	inline constexpr float  kGuideOffLine = 0.6f;   // 확정한 궤적 끝이 안내선에서 이보다 멀면(장애물을 도는 중) 궤적을 따른다
	inline constexpr float  kMaxDevRemote = 1.0f;
	inline constexpr float  kTurnRate = 18.85f;     // 진행 각 회전(라디안/초) = 1080°/s
	inline constexpr float  kSectorHyst = 0.2618f;  // 8구역 경계 히스테리시스 15°
	inline constexpr float  kFlipSec = 0.06f;       // 좌우 반전 1 → 0 → −1
	inline constexpr float  kRemoteDelay = 0.10f;   // 원격: 100ms 뒤를 그린다
	inline constexpr float  kCatchupMax = 0.12f;    // 원격: 밀리면 최대 +12%
	inline constexpr float  kSlowMax = 0.08f;       // 원격: 버퍼가 바닥나 가면 최대 −8%(멈춰 서지 않게 늘여 재생)
	inline constexpr float  kExtrapTiles = 0.4f;    // 원격: 걸음이 늦으면 이어 미끄러지는 거리 상한(지시서 상한 1칸)
	inline constexpr float  kLandSec = 0.06f;       // 원격: 쌓인 걸음 없이 지금 걸음이 이만큼 남으면 재생을 늦추기 시작(버퍼 100ms의 지터 몫은 넘긴 뒤)
	inline constexpr float  kLandMin = 0.5f;        //       늦추는 하한(재생 속도 50%)
	inline constexpr float  kQuietSec = 0.07f;      // 원격(플레이어): 아무 패킷도 이만큼 안 오면 통신이 멈춘 것(외삽해도 된다)
	inline constexpr float  kCreepSec = 0.1f;       //       걸음이 막 바닥난 뒤 이 시간 동안은(통신이 멈춘 건지 아직 모른다) 착지 속도(50%)로
	inline constexpr float  kCreepRate = 0.5f;      //       0.1칸까지만 — 정말 멈춘 경우에도 넘어가는 거리가 0.1칸 안
	inline constexpr float  kCreepTiles = 0.1f;
	inline constexpr float  kCoastRate = 0.9f;      //       외삽 속도(걷던 속도의 90%)
	inline constexpr float  kCoastStopTau = 0.06f;  //       외삽하다 다른 패킷이 오면(통신은 살아 있는데 이 걸음만 늦음) 이 시간 상수로 줄어들며 선다
	inline constexpr float  kCoastHold = 0.5f;      //       이만큼 기다려도 걸음이 없으면 멈춘 것 — 제 칸으로 천천히(kSettleSec) 돌아간다
	inline constexpr float  kSettleSec = 0.4f;
	inline constexpr float  kLateMax = 0.25f;       // 원격(플레이어): 늦게 온 걸음만큼 버퍼를 늘리는 상한(100ms + 이 값)
	inline constexpr float  kLateDecay = 0.02f;     //       늘린 버퍼가 줄어드는 속도(초당)
	inline constexpr float  kStrideTiles = 1.9f;    // 달리기 한 주기(두 걸음)에 가는 칸

	// 통신 상태(원격 외삽 판단): 마지막으로 패킷을 받은 뒤 흐른 시간. Game이 프레임마다 알려 준다.
	void SetNetQuiet(float seconds);
	// 규칙 속도(칸/초) — 직선 · 대각 모두 같다(MoveRule: 대각 = 직선 × √2 시간)
	[[nodiscard]] float RuleSpeed(uint32_t stepMs);
	// 격자 위에서 방향 u로 갈 때 평균 진행 속도 비율(0°·45° = 1, 26.6° ≈ 0.92): 직선 · 대각 걸음을 섞은 길이 대비 직선 길이
	[[nodiscard]] float GridRate(Vector2 u);

	// ── 몸 ──
	struct Body
	{
		float heading = 0.7854f;   // 화면 진행 각(라디안, 0 = →, +π/2 = ↓)
		int   sector = 1;          // 0 → · 1 ↘ · 2 ↓ · 3 ↙ · 4 ← · 5 ↖ · 6 ↑ · 7 ↗
		float turn = 1.f;          // 가로 배율: +1 오른쪽 · −1 왼쪽(60ms에 걸쳐 0을 지난다)
		bool  faceRight = true, faceBack = false;
		float stride = 0.f;        // 걸음 위상(라디안) = 이동 거리 ÷ 보폭 × 2π
		float moving = 0.f;        // 0~1 서기 ↔ 걷기
		float run = 0.f;           // 0~1 달리기 ↔ 빠른 달리기(바람걸음)
		float speed = 0.f;         // 화면 속도(칸/초)
		float lean = 0.f;          // 앞기울기(도, + 앞) — 출발 가속 · 달리는 자세
		float bank = 0.f;          // 도는 쪽 기울기(도)
		float settle = 0.f, settleV = 0.f;   // 멈출 때 한 번 흔들림(감쇠 스프링)
		bool  footfall = false;    // 이번 프레임에 발이 땅에 닿았다(발소리 · 먼지)
		float turnRate = 0.f;      // 진행 각 변화(라디안/초)
		bool  running = false;     // 멈춤 흔들림 판정(달리던 중이었나)
		Vector2 dirF{};            // 거른 진행 속도(칸/초) — 진행 각의 입력

		// 서 있는 동안 밖에서 방향을 정한다(공격 · 대화) — 그 구역 한가운데 각으로
		void Face(bool right, bool back);
		// holdHeading: 진행 각을 바꾸지 않는다(원격 개체가 제 칸으로 조금 되돌아가는 동안 뒤돌아보지 않게)
		void Update(Vector2 prevPos, Vector2 pos, float dt, float nominalSpeed, bool holdHeading = false);
	};

	// 화면 속도(칸 좌표 변위) → 화면 각(라디안)
	[[nodiscard]] float ScreenAngle(Vector2 tileDelta);

	// ── 논리 궤적 조각 ──
	struct Seg { Vector2 from, to; double t0, t1; };

	// ── 화면 추종자 ──
	struct Follower
	{
		Vector2 pos{};
		Vector2 dir{ 0.7071f, 0.7071f };   // 진행 방향(단위)
		float   speed = 0.f;               // 칸/초
		void Snap(Vector2 p) { pos = p; speed = 0.f; }
		[[nodiscard]] Vector2 Velocity() const { return Vector2{ dir.x * speed, dir.y * speed }; }
	};

	// 안내선: 누른 채 커서 쪽 · 목적지로 가는 선. 화면 추종자는 이 선을 따라 곧게 간다(격자 지그재그를 지운다).
	struct Guide
	{
		bool    active = false;
		Vector2 origin{};         // 선 위의 한 점(보통 지금 화면 위치)
		Vector2 dir{ 1.f, 0.f };  // 단위 방향(칸 좌표)
	};

	class LocalMover
	{
	public:
		void Reset(int x, int y);                         // 로그인 · 순간이동 · 부활(스냅)
		void SetConfig(uint32_t baseStepMs, uint32_t diagPermille) { m_baseStepMs = baseStepMs; m_diagPermille = diagPermille; }
		void SetSpeedPercent(uint32_t percent) { m_speedPercent = percent; }
		[[nodiscard]] uint32_t SpeedPercent() const { return m_speedPercent; }

		// 지금 한 걸음을 더 확정할 수 있는가(선확정 시점 · 미응답 수)
		[[nodiscard]] bool CanStep(double now) const;
		// dir 방향으로 움직였을 때 실제로 칸이 바뀌는가(서버 규칙: 축별 미끄러짐 포함)
		[[nodiscard]] bool WouldMove(int dir) const;
		// 걸음을 예측 적용하고 보낼 순번을 돌려준다. 못 움직이면 false.
		bool Step(double now, int dir, uint16_t& outSeq);

		void OnAck(uint16_t seq, int x, int y, MoveResult result, uint16_t stepMs, double now);
		// 본인에 대한 SUBJECT_MOVE_NFY(존 이동 메아리 · 순간이동 · 부활). 순간이동이면 true.
		bool OnServerPosition(int x, int y, double now);
		// 기술 이동(돌진 · 잔상 회피 · 그늘 걸음 · 옛길 도약) — 서버가 정한 도착 칸으로 duration초 동안(0이면 순간이동)
		void Dash(int x, int y, double now, double duration);

		// 안내선(Game이 매 프레임). 없으면 논리 궤적을 앞보기로 따라간다.
		void SetGuide(const Guide& g) { m_guide = g; }
		// 논리/화면 위치 갱신. visualDt는 히트스톱이 반영된 시간(멈춤 연출).
		void Update(double now, float visualDt);

		[[nodiscard]] int PredX() const { return m_predX; }
		[[nodiscard]] int PredY() const { return m_predY; }
		[[nodiscard]] int SrvX() const { return m_srvX; }
		[[nodiscard]] int SrvY() const { return m_srvY; }
		[[nodiscard]] int PendingCount() const { return static_cast<int>(m_pending.size()); }
		[[nodiscard]] Vector2 Visual() const { return m_f.pos; }
		[[nodiscard]] Vector2 Logical() const { return m_logical; }
		[[nodiscard]] Vector2 Velocity() const { return m_velocity; }   // 칸/초(화면 기준)
		[[nodiscard]] float Speed() const;                             // 칸/초
		[[nodiscard]] bool IsWalking(double now) const { return now < m_nextStepAt + 0.02; }
		[[nodiscard]] int LastDir() const { return m_lastDir; }
		[[nodiscard]] bool Snapped() { const bool s = m_snapped; m_snapped = false; return s; }
		[[nodiscard]] double NextStepAt() const { return m_nextStepAt; }
		[[nodiscard]] uint32_t StepMs(int dir) const;
		[[nodiscard]] float NominalSpeed() const;                      // 규칙 속도(칸/초)
		// 아직 응답이 오지 않은 요청의 보낸 시각(왕복 시간 계산용). 없으면 -1
		[[nodiscard]] double SentAt(uint16_t seq) const;
		// 화면 추종자 상태(계측)
		[[nodiscard]] float Deviation() const;                          // |P − Lg|
		[[nodiscard]] bool Guided() const { return m_guide.active; }
		struct Debug { bool guided = false, stopping = false; float target = 0.f, prog = 0.f; };
		[[nodiscard]] const Debug& GetDebug() const { return m_dbg; }

		// 검증용 통계
		struct Stats { int steps = 0, acks = 0, rejects = 0, corrections = 0, maxPending = 0, teleports = 0; float maxCorrection = 0.f; float maxDeviation = 0.f; };
		[[nodiscard]] const Stats& GetStats() const { return m_stats; }

	private:
		struct Pending { uint16_t seq; uint8_t dir; double sentAt; };

		void Replay();                                       // srv + 미응답 입력 → pred
		void Retarget(double now);                           // pred가 바뀌면 논리 궤적을 부드럽게 이어 준다
		[[nodiscard]] Vector2 LogicalAt(double now) const;
		[[nodiscard]] Vector2 AheadOnChain(double now, float dist) const;   // 논리 궤적을 따라 dist칸 앞
		[[nodiscard]] Vector2 ChainEnd() const;

		std::deque<Pending> m_pending;
		std::deque<Seg> m_segs;
		int      m_srvX = 0, m_srvY = 0;
		int      m_predX = 0, m_predY = 0;
		uint16_t m_seq = 0;
		uint32_t m_baseStepMs = 280, m_diagPermille = 1414, m_speedPercent = 100;
		double   m_nextStepAt = 0.0;                         // 논리 궤적이 끝나는 시각(= 예측 칸에 닿는 시각)
		int      m_lastDir = 1;
		double   m_dashUntil = -1.0;                         // 기술 이동 중: 화면 = 논리(따라가기 없음)

		Vector2  m_logical{}, m_velocity{};
		Follower m_f;
		Guide    m_guide;
		float    m_latF = 0.f;                               // 안내선 ↔ 궤적 옆 어긋남(거른 값)
		bool     m_snapped = false;
		Stats    m_stats;
		Debug    m_dbg;
	};

	// ── 다른 개체(플레이어 · 몬스터 · NPC) ──
	struct Track
	{
		struct Step { Vector2 to; float dur; };
		std::array<Step, 8> q{};      // 아직 재생하지 않은 걸음
		int      qn = 0;
		Vector2  from{}, to{};        // 지금 재생 중인 걸음
		float    t = 1.f, dur = 0.3f; // 진행(0~1) · 걸음 시간(초)
		float    wait = 0.f;          // 멈춰 있다 첫 걸음: 100ms 뒤에 시작
		Vector2  logical{};           // 논리 위치
		Vector2  lastDir{};           // 마지막 걸음 방향(외삽)
		float    lastSpeed = 0.f;
		float    extra = 0.f;         // 외삽한 거리(칸)
		float    dryT = 0.f;          // 걸음이 바닥난 뒤 흐른 시간
		float    merge = 0.f;         // 1 → 0: 합류 중(외삽한 자리 → 논리 위치)
		float    margin = 0.1f;       // 걸음이 도착할 때 남아 있던 재생 시간(거른 값) — 재생 속도 조절(버퍼 유지)
		float    lateMax = 0.f;       // 최근에 걸음이 늦었던 정도(초, 천천히 줄어든다) — 그만큼 버퍼를 늘린다(플레이어)
		float    coastV = 0.f;        // 외삽 속도(통신이 멈춘 동안 걷던 속도의 90%, 다른 패킷이 오면 짧게 줄어들며 선다)
		Vector2  mergeFrom{};
		bool     dash = false;        // 여러 칸 이동(돌진 · 넉백 · 도약) — 따라가기 없이 그대로
		Follower f;
	};

	// 다른 개체의 한 걸음(서버 알림). durationSec <= 0 이거나 너무 멀면 스냅.
	// allowFar: 돌진 · 도약처럼 여러 칸을 한 번에 가는 이동(멀어도 곧장 보간한다).
	void TrackStep(Track& tr, int x, int y, float durationSec, bool allowFar = false);
	void TrackSnap(Track& tr, Vector2 p);
	// 논리 · 화면 위치 갱신 — 화면 위치를 돌려준다
	Vector2 TrackUpdate(Track& tr, float dt, float visualDt, bool allowExtrap);

	// Entity 편의(예전 이름 유지)
	void RemoteStep(Entity& e, int x, int y, float durationSec, bool allowFar = false);
	void RemoteSnap(Entity& e, Vector2 p);
	void UpdateRemote(Entity& e, float dt, float visualDt);
	// 몸(방향 · 걸음 · 자세) 갱신 후 예전 필드(walkPhase · faceRight …)에 비춘다
	void Animate(Entity& e, Vector2 prevVisual, float dt, float nominalSpeed = 3.57f, bool holdHeading = false);
}
