#pragma once

#include "raylib.h"
#include "core/Json.h"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 상시 채팅창(Phase 3-B) — 자료 · 순수 함수(명령 해석 · 아이템 고리 · 탭). 그리기 · 보내기는 game/GameChat.cpp.
//
//   · 줄 = {채널, 보낸 이, 글, 시각}. 탭마다 500줄 고리(같은 줄을 여러 탭이 공유 — shared_ptr).
//   · 채널: 일반(같은 지역) · 전체 · 거래 · 결사 · 귓속말(받음 · 보냄) · 시스템 · 공지.
//   · 아이템 고리: 글 안의 짧은 표식 {i:아이템id:강화:희귀도} — 128바이트 안에 싣고, 보이는 곳에서 [이름 +n]으로 그린다.
// ─────────────────────────────────────────────────────────────────────────────
namespace chat
{
	enum class Ch : uint8_t { Normal, World, System, Trade, Guild, WhisperIn, WhisperOut, Notice, Count };
	inline constexpr int kChCount = static_cast<int>(Ch::Count);
	inline constexpr uint32_t Bit(Ch c) { return 1u << static_cast<uint32_t>(c); }
	inline constexpr uint32_t kAllMask = (1u << kChCount) - 1u;
	inline constexpr size_t kTabLines = 500;
	inline constexpr size_t kMaxBytes = 127;           // CHAT_SIZE 128 − 널

	[[nodiscard]] const char* ChName(Ch c);             // 탭 설정 · 입력칸 머리("일반" "전체" …)
	[[nodiscard]] const char* ChPrefix(Ch c);           // 줄 앞 표시("[전체]" · 일반은 "")
	[[nodiscard]] Color ChColor(Ch c);                  // DESIGN_SYSTEM 3.4 채널 색

	struct Msg
	{
		uint32_t    serial = 0;
		Ch          ch = Ch::System;
		std::string sender;          // 보낸 이(귓속말 보냄이면 받는 이)
		uint64_t    senderKey = 0;   // ObjID 값(말풍선 · 이름 클릭)
		std::string text;            // 본문(아이템 고리 표식 포함)
		Color       color{ 0, 0, 0, 0 };   // 시스템 줄의 개별 색(a = 0이면 채널 색)
		double      time = 0.0;      // 게임 시각(m_now) — 밀려 들어오는 연출
		std::string clock;           // 도착한 실제 시각 "13:05"
		bool        history = false; // 접속 직후 받은 최근 대화
	};
	using MsgPtr = std::shared_ptr<const Msg>;

	struct Tab
	{
		std::string name;
		uint32_t    mask = kAllMask;
		bool        flash = true;     // 새 줄이 오면 탭 이름이 반짝인다(보고 있지 않을 때)
		bool        user = false;     // "+"로 만든 탭(지울 수 있다)
		std::deque<MsgPtr> lines;     // ≤ kTabLines
		int         unread = 0;       // 보고 있지 않을 때 온 줄
		float       scroll = 0.f;     // 아래에서 위로 올려 본 거리(px) — 0이면 맨 아래(새 줄을 따라간다)
		int         newBelow = 0;     // 올려 보는 동안 온 줄("▼ 새 메시지 N")
		double      lastAt = -10.0;   // 마지막 줄이 온 시각(반짝임)
	};

	// 보내기 대상(입력칸 머리)
	struct Target
	{
		Ch          ch = Ch::Normal;  // Normal · World · Trade · Guild · WhisperOut
		std::string whisperTo;        // 귓속말 받는 이
	};

	// 입력 한 줄 해석: "/귓 이름 내용" "/r 내용" "/전 내용" "&내용" …
	struct Parsed
	{
		enum class Kind { Send, Switch, Invite, Unknown, Empty };
		Kind        kind = Kind::Empty;
		Target      target;           // Send · Switch
		std::string body;             // Send
		std::string arg;              // Invite(이름) · Unknown(명령)
	};
	[[nodiscard]] Parsed ParseInput(const std::string& line, const Target& current, const std::string& lastWhisperFrom);

	// 자동 완성: 입력이 "/"로 시작하면 맞는 명령들(보이는 이름 · 설명)
	struct CmdHint { const char* cmd; const char* help; };
	[[nodiscard]] std::vector<CmdHint> CommandHints(std::string_view typed);

	// 아이템 고리 표식
	[[nodiscard]] std::string ItemToken(uint16_t itemId, uint8_t enchant, uint8_t rarity);
	// 글을 조각으로: 보통 글 / 아이템 고리
	struct Piece { bool item = false; std::string text; uint16_t itemId = 0; uint8_t enchant = 0; uint8_t rarity = 0; };
	[[nodiscard]] std::vector<Piece> SplitPieces(const std::string& text);

	// UTF-8 경계에서 maxBytes 안으로 자르기
	[[nodiscard]] std::string ClipUtf8(const std::string& s, size_t maxBytes);
	[[nodiscard]] bool ValidUtf8(std::string_view s);

	// 탭 · 설정(저장: config/ui_<캐릭터>.json의 "chat")
	struct Settings
	{
		float idleAlpha = 0.35f;      // 평소 배경 투명도
		float activeAlpha = 0.85f;    // 마우스를 올리거나 입력 중
		int   font = 1;               // 0 작게 · 1 보통 · 2 크게
		bool  clock = false;          // 줄 앞 시각
		std::vector<std::string> blocked;   // 차단한 이름(클라만)
	};
	[[nodiscard]] float FontSize(int font);
	void DefaultTabs(std::vector<Tab>& tabs);
	[[nodiscard]] json::Value ToJson(const std::vector<Tab>& tabs, int active, const Settings& s);
	void FromJson(const json::Value& v, std::vector<Tab>& tabs, int& active, Settings& s);
}
