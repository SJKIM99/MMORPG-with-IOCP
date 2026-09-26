#pragma once

#include "Data/SkillData.h"
#include "User.h"

class Monster;
class Subject;
struct SKILL_EX_REQ_PACKET;

// ─────────────────────────────────────────────────────────────────────────────
// 기술 구조 V3(Phase 2-B) — 확장 요청 SKILL_EX_REQ를 SkillDefTable(Data/SkillData.h)대로 판정한다.
//
//  · 유형: 콤보 · 일반 · 차지(누름/뗌 + 완벽 구간) · 홀딩(틱 + 마무리) · 캐스팅(이동하면 끊김) · 체인 · 토글 · 이동기 · 고유 기술 · 각성기.
//  · 타격은 표의 시각(ms)에 TimerThread(EV_SKILL_IMPACT) → 시전자 소유 Zone 스레드에서 모양을 다시 계산해 판정한다
//    (예고를 보고 피할 수 있다). 끊긴 시전(castSeq가 바뀜)의 예약은 버린다.
//  · 알림: SKILL_EX_NFY(주변 확장 세션) — 클라이언트는 같은 표의 시각으로 연출을 재생한다.
//  · 스레드: 모든 함수는 시전자의 소유 Zone 스레드에서 불린다(요청 경로 · 타이머 경로 모두). 다른 플레이어 상태는 바꾸지 않는다
//    (PvP 피해는 기존 ContentHelper 경로 — 같은 Zone 안).
// ─────────────────────────────────────────────────────────────────────────────
namespace SkillEx
{
	enum class ExEvent : uint8_t
	{
		None = 0,
		Hit,            // 타격표 한 줄(hit = 번호)
		Legacy,         // 예전 판정(Execute)
		HoldTick,
		HoldTimeout,
		ChargeTimeout,
		ToggleTick,
		MistTick,       // 맹독 안개 틱(extra = 남은 틱)
		Resonance,      // 새김 울림 폭발(target)
		Bounce,         // 방패 투척 튕김(extra = 몇 번째)
		MarkHit,        // 기억한 대상(그믐 베기 · 달 가르기)에게 타격표 한 줄
		Linger,         // Phase 2-D 갈래 「장판」 틱(stage = 반경, extra = 남은 틱)
	};

	void HandleRequest(const std::shared_ptr<User>& u, const SKILL_EX_REQ_PACKET& req);
	void HandleEvent(const std::shared_ptr<User>& u, const User::PendingImpact& ev);

	// DealHit 뒤(확장 타격): 고유 기술 게이지 · 잔향(서약 해방) · 분신
	void OnHitDealt(const std::shared_ptr<User>& u, const SkillRow& row, const std::shared_ptr<Subject>& target, uint32_t dealt, uint8_t flags2, uint32_t castSeq);
	// 보스 무력화 게이지
	void ApplyStagger(const std::shared_ptr<User>& u, const std::shared_ptr<Monster>& m, uint8_t amount);
	void AddIdentity(const std::shared_ptr<User>& u, int amount);
	void SendGauge(const std::shared_ptr<User>& u, bool force);
	// 몬스터 공격을 피했다(잔상 회피 직후면 완벽 회피)
	void OnEvaded(const std::shared_ptr<User>& u, uint32_t now);
	// 걸었다 — 캐스팅 · (움직일 수 없는) 차지 · 홀딩을 끊는다
	void OnMoved(const std::shared_ptr<User>& u);
}
