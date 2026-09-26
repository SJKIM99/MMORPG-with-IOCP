#pragma once

#include "Data/SkillGrowth.h"
#include "User.h"

struct SKILL_GROW_REQ_PACKET;

// ─────────────────────────────────────────────────────────────────────────────
// 기술 성장(Phase 2-D) — 기술 레벨 · 점수 · 갈래의 서버 권위(Data/SkillGrowth.h 규칙).
//
//  · 상태: User::m_skillGrow(기술 id → 한 바이트). 소유 Zone 스레드만 읽고 쓴다(요청은 Route → 세션의 Zone 스레드).
//  · 저장: 바뀔 때마다 그 기술 한 줄을 절대값으로 DB 샤드(playerId)에 넣는다 — SavePlayerSkill(upsert, 한 트랜잭션).
//    초기화는 ResetPlayerSkills(행 삭제). 같은 샤드라 넣은 순서대로 커밋된다.
//  · 멱등: SKILL_GROW_REQ.seq가 직전과 같으면 다시 처리하지 않고 지금 상태로 결과만 다시 보낸다.
//  · 남은 점수 = 캐릭터 레벨의 점수 − 기술 레벨들의 값(저장하지 않는다 — 어긋날 수 없다).
//  · 확장 세션 전용(봇 · 레거시 세션은 이 경로에 들어오지 않고, 기술 레벨 1 = 예전과 같은 피해).
// ─────────────────────────────────────────────────────────────────────────────
namespace SkillGrow
{
	// 로그인: DB 행을 적용한다(직업 · 범위 검사). 점수보다 많이 쓴 기록이면 모두 초기화하고 true
	bool Load(const std::shared_ptr<User>& u, const std::vector<DB_SKILL_INFO>& rows);
	[[nodiscard]] uint16_t Spent(const std::shared_ptr<User>& u);
	[[nodiscard]] uint16_t Points(const std::shared_ptr<User>& u);   // 남은 점수
	void SendTree(const std::shared_ptr<User>& u);
	void HandleRequest(const std::shared_ptr<User>& u, const SKILL_GROW_REQ_PACKET& req);
}
