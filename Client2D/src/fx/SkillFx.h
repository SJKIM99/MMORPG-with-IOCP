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
	void DrawGround();           // 바닥 문양(룬·인장·불 고리)
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
	// 서버 이펙트 알림(룬 폭발·증발 폭발·반격·벽 충돌·독 폭발)
	void OnServerEffect(EffectId id, Vector2 iso, int param);

	// 상태이상: 파티클은 Emit*, 모양은 Draw*
	void EmitStatus(const Entity& e, Vector2 iso, float height, float dt, float now);
	void DrawStatus(const Entity& e, Vector2 iso, float height, float now);
	// 몬스터 강공격 예고(바닥 모양이 차오른다)
	void DrawTelegraph(const Entity& e, float now);
	// 몬스터 강공격이 떨어지는 순간(예고 끝) — pattern: MonsterPattern
	void MonsterImpact(uint8_t pattern, Vector2 fromIso, Vector2 toIso, int radius, Color bodyColor);
	// 던지는 물체(몬스터 투척 등): from → to를 dur초 동안
	void Shot(Vector2 fromIso, float fromZ, Vector2 toIso, float toZ, float dur, Color color);

	// 스킬 모션 길이(초) — 시전자 애니메이션
	[[nodiscard]] float AnimDuration(uint16_t skillId);
	// 스킬 아이콘(스킬바) — 절차적
	void DrawSkillIcon(uint16_t skillId, Vector2 center, float size, float time, bool locked);
}
