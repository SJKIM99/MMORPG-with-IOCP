#pragma once

#include "raylib.h"
#include "net/Shared.h"

struct Entity;

// ─────────────────────────────────────────────────────────────────────────────
// 직업 스킬 연출 — 시전(모션 앞의 준비 동작·투사체·돌진 잔상) · 적중(임팩트) ·
// 상태이상 표시(기절 별·빙결 얼음·화상 불꽃·중독 거품·철벽 방패) · 몬스터 예고.
//
// 원칙: 강한 기술일수록 크게, 기본 공격은 짧고 가볍게. 화면이 난잡해지지 않도록
// 동시에 도는 큰 연출 수에 상한을 둔다(파티클 수·흔들림은 Game이 합산 상한).
// ─────────────────────────────────────────────────────────────────────────────
namespace skillfx
{
	struct Feel { float trauma = 0.f; float kick = 0.f; float hitstop = 0.f; };

	struct CastInfo
	{
		Vector2 casterIso{};     // 시전 위치(아이소 px)
		Vector2 targetIso{};     // 대상/지점(아이소 px)
		Vector2 landIso{};       // 돌진·도약 도착(아이소 px)
		float   casterHeight = 60.f;
		float   targetHeight = 50.f;
		bool    faceRight = true;
		float   castSec = 0.f;   // 예고 시간
		bool    self = false;
		uint8_t classId = 0;
	};

	void Update(float dt);
	void Clear();
	void DrawGround();           // 바닥 문양(각인·인장·불 고리)
	void DrawWorld();            // 투사체·번개·방패 막(가산 합성)

	// 시전 연출. 투사체면 도착까지 걸리는 시간(초)을 돌려준다(적중 표시를 그만큼 늦춘다).
	float OnCast(uint16_t skillId, const CastInfo& c);
	// 적중 연출(피해 숫자는 Game이 따로)
	void OnHit(uint16_t skillId, Vector2 iso, float height, bool crit, uint8_t comboId, uint8_t flags2, bool dot);
	// 적중 체감(히트스톱·흔들림) — 연계/치명타면 더 크게
	[[nodiscard]] Feel HitFeel(uint16_t skillId, bool crit, bool combo, bool killed);
	[[nodiscard]] Feel CastFeel(uint16_t skillId);
	// 투사체 이동 시간(표시용)
	[[nodiscard]] float TravelTime(uint16_t skillId, Vector2 fromTile, Vector2 toTile);

	// 연쇄 번개(두 점 사이)
	void Bolt(Vector2 a, float za, Vector2 b, float zb, Color color, float life);
	// 서버 이펙트 알림(각인 폭발·증발 폭발·반격·벽 충돌·독 폭발)
	void OnServerEffect(EffectId id, Vector2 iso, int param);

	// 상태이상: 파티클은 Emit*, 모양은 Draw*
	void EmitStatus(const Entity& e, Vector2 iso, float height, float dt, float now);
	void DrawStatus(const Entity& e, Vector2 iso, float height, float now);
	// 몬스터 강공격 예고(바닥 모양이 차오른다)
	void DrawTelegraph(const Entity& e, float now);
	// 예고 테두리만(후처리 뒤 · 모든 연출 위 — 화면 좌표계가 아니라 월드 카메라 안에서)
	void DrawTelegraphEdge(const Entity& e);
	// 몬스터 강공격이 떨어지는 순간(예고 끝) — pattern: MonsterPattern
	void MonsterImpact(uint8_t pattern, Vector2 fromIso, Vector2 toIso, int radius, Color bodyColor);
	// 던지는 물체(몬스터 투척 등): from → to를 dur초 동안
	void Shot(Vector2 fromIso, float fromZ, Vector2 toIso, float toZ, float dur, Color color);

	// ── Phase 2-B/2-C 기술 구조 V3 연출(Data/SkillData.h의 시각표를 따른다) ──
	struct ExCtx
	{
		uint16_t skillId = 0;
		uint8_t  phase = 0;          // SkillExPhase
		uint8_t  stage = 0;          // 콤보 타 · 차지 단계 · 체인 단
		uint8_t  flags = 0;          // SkillExFlag(완벽)
		Vector2  casterIso{};
		Vector2  aimIso{};
		Vector2  targetIso{};
		Vector2  landIso{};          // 돌진 · 도약 도착
		Vector2  dir{ 1.f, 0.f };    // 화면 방향(단위)
		Vector2  dirTile{ 1.f, 0.f };// 칸 방향(단위)
		float    casterHeight = 60.f;
		float    targetHeight = 50.f;
		bool     faceRight = true;
		bool     mine = true;
		uint8_t  classId = 0;
		float    castSec = 0.f;
		// 예측한 대상들(아이소 px) — 방패 투척 튕김 · 그믐 베기 순간이동 · 비수 · 창의 첫 적
		Vector2  pts[8]{};
		float    ptsZ[8]{};
		int      nPts = 0;
	};
	// 단계 연출(시작 · 차지 시작/놓음 · 홀딩 시작/끝 · 체인 단 · 토글 · 고유 기술 · 취소) — 예고 · 발동.
	// 투사체가 대상에 닿기까지 걸리는 시간(초)을 돌려준다(피해 숫자를 그만큼 늦춘다 — 0이면 없음)
	float OnExPhase(const ExCtx& c);
	// 타격 시각(표의 hits[i].atMs) — 모양 가운데에서. targets = 모양 안의 적 수(예측, 0이면 헛친 모양만)
	void OnExBeat(const ExCtx& c, int hitIdx, Vector2 centerIso, int targets);
	// 서버가 확정한 한 대상의 피해(작은 겹 — 큰 연출은 OnExBeat가 이미 했다)
	void OnExVictim(uint16_t skillId, uint8_t hitIdx, Vector2 iso, float height, bool crit, uint8_t comboId, uint8_t flags2);
	// 차지 · 홀딩 동안(매 프레임): 모이는 빛 · 도는 칼날
	void OnExHeld(const ExCtx& c, float held, uint8_t stage, bool perfectWindow, float dt);
	// 연계 10종(무너뜨리기 · 단죄 · 증발 폭발 · 전도 · 산산조각 · 기습 · 독 폭발 · 반격 · 벽 충돌 · 급소) — T3 연출
	void ComboFx(uint8_t comboId, Vector2 iso, float height, bool crit);
	// 갈래 「장판」(Phase 2-D): 자리에 seconds초 동안 남는 바닥(색 · 상태이상 모양). 서버 틱(0.5초)마다 테가 울린다
	void Linger(Vector2 iso, float radiusTiles, float seconds, uint32_t color, StatusId status);
	// 연출 등급(1~4)
	[[nodiscard]] int TierOf(uint16_t skillId);
	// 소리 거리 감쇠 · 좌우(내 캐릭터 위치, 아이소 px) — Game이 매 프레임
	void SetListener(Vector2 iso);
	// 떠 있는 기술 오브젝트 수(F3 계측)
	[[nodiscard]] int ExCount();

	// 스킬 모션 길이(초) — 시전자 애니메이션
	[[nodiscard]] float AnimDuration(uint16_t skillId);
	// 스킬 아이콘(스킬바) — 절차적
	void DrawSkillIcon(uint16_t skillId, Vector2 center, float size, float time, bool locked);
}
