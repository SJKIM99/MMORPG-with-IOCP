#pragma once

class User;

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3-B 채널 채팅(CS_CHAT_EX · 확장 세션 전용)
//
//  스레드: 모든 함수는 보낸 사람을 소유한 Zone 스레드에서(Route가 세션의 Zone 큐로 넘긴다).
//   · 보낸 사람의 도배 상태(토큰 버킷 · 되풀이 · 채널 재사용 대기)는 User 안 — 단일 작성자.
//   · 일반 = 같은 Zone(지역) 전체: 그 Zone 스레드의 섹터 표만 읽는다(GSector). 레거시 세션에는 예전처럼
//     시야 안(섹터 3×3 · CanSee)에서 SC_CHAT — 봇이 받는 패킷 · 부하 조건은 그대로.
//   · 전체 · 거래 = 서버 전체 확장 세션(SnapshotSessions — 월드 공지와 같은 경로). 귓속말 = 이름 색인.
//     다른 Zone의 세션에는 PostSend(세션별 송신 락)만 한다 — 그 사용자의 상태는 건드리지 않는다.
//  받는 쪽 패킷은 기존 SC_CHAT_EX(168B) 그대로, 채널 값만 끝에 추가(Protocol.h ChatChannel).
// ─────────────────────────────────────────────────────────────────────────────
namespace ChatHelper
{
	// 검사 규칙(서버 상수 — 문서 PHASE_3.md 2.2)
	inline constexpr int      kBucketMax       = 5;       // 5줄을 몰아서
	inline constexpr uint32_t kBucketRefillMs  = 1000;    // 1초에 1줄씩 찬다 → 5줄/5초
	inline constexpr uint32_t kWorldCooldownMs = 10000;   // 전체 10초
	inline constexpr uint32_t kTradeCooldownMs = 5000;    // 거래 5초
	inline constexpr uint32_t kRepeatWindowMs  = 30000;   // 같은 말: 30초 안의 세 번째부터 막는다
	inline constexpr size_t   kRecentMax       = 20;      // 접속 직후 보내는 최근 대화

	void HandleChatEx(const shared_ptr<User>& sender, uint8_t channel, const char* targetName, const char* mess);
	// 로그인 직후(확장 세션): 이름 색인에 넣고 최근 전체 · 거래 · 같은 지역 일반 대화를 보낸다
	void OnEnterWorld(const shared_ptr<User>& user);

	// 시험용(샌드박스 회귀): UTF-8 검사
	[[nodiscard]] bool IsValidUtf8(const char* s, size_t n);
}
