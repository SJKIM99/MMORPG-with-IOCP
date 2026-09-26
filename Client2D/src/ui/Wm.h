#pragma once

#include "raylib.h"
#include "core/Json.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 창 관리자(Phase 3-A) — 즉시 모드 UI는 그대로, "창 상태"만 한곳에 보관한다.
//
//   · 창(소지품 · 캐릭터 · 퀘스트 일지 …)과 HUD 덩어리(전투 판 · 미니맵 …)마다 id(문자열) →
//     기본 자리에서 옮긴 거리 · 크기 · 배율 · 잠금 · 겹침 순서 · 열기/닫기 애니메이션.
//   · 그리는 코드는 거의 그대로: 매 프레임 기본 사각형(지금 해상도로 계산)을 주면 wm::Window가
//     이번 프레임의 사각형을 돌려준다. 옮긴 거리는 "기본 자리 기준"이라 해상도가 바뀌어도
//     기본 자리가 붙어 있던 모서리 · 가운데를 따라간다(1280 ↔ 2560에서 미니맵이 가운데로 가지 않는다).
//   · 프레임 시작에 "마우스 아래 맨 위 창"을 ui 층 주인으로 정한다 → 겹친 창 뒤의 단추는 눌리지 않는다.
//   · 끌기(제목줄) · 크기(가장자리 · 모서리 6px) · 8px 자석(화면 가장자리 · 다른 창) · 누른 창이 맨 위 ·
//     Esc = 맨 위 열린 창부터 · 열기 0.14초(96% → 100% · 투명 → 불투명) · 닫기 0.12초.
//   · 저장: ToJson/FromJson — Game이 config/ui_<캐릭터>.json에 쓴다.
// ─────────────────────────────────────────────────────────────────────────────
namespace wm
{
	enum Flag : uint32_t
	{
		kMove     = 1u << 0,   // 제목줄(위 38px, 오른쪽 닫기 단추 자리 제외)로 끈다
		kResize   = 1u << 1,   // 가장자리 · 모서리로 크기(minSize ~ maxSize)
		kNoAnim   = 1u << 2,   // 열기 · 닫기 애니메이션 없음
		kLockable = 1u << 3,   // 잠글 수 있다(잠그면 끌기 · 크기 막음) — 채팅
		kPinned   = 1u << 4,   // Esc로 닫히지 않는다(늘 떠 있는 창 — 채팅)
	};
	inline constexpr int kModalLayer = 1000;

	// 창 목록(그리는 함수) — 겹침 순서대로 그린다. closeAnim: 닫힐 때 0.12초 동안 판정 없이 한 번 더 그려도 되는 창
	struct Entry
	{
		const char* id;
		bool open;
		std::function<void()> draw;
		bool closeAnim = true;
	};

	// ── 프레임 ──
	// logical = 논리 화면 크기(화면 ÷ UI 배율). modal = 메뉴 · 조작 설정 · 확인 창이 떠 있음. edit = UI 편집 모드
	void BeginFrame(Vector2 logical, double now, bool modal, bool edit);
	void DrawWindows(std::vector<Entry>& entries);

	// ── 창 ── (DrawWindows가 부르는 그리기 함수 안에서, 창 그리기 맨 앞에)
	Rectangle Window(const char* id, Rectangle def, uint32_t flags = kMove, Vector2 minSize = Vector2{ 0, 0 }, Vector2 maxSize = Vector2{ 0, 0 });
	void DragHandle(Rectangle r);              // 제목줄이 없는 창의 끌기 손잡이(Window 뒤 같은 프레임) — 채팅의 탭 줄
	[[nodiscard]] bool HandlePending();        // 손잡이를 눌렀지만 아직 끌지 않았다(떼면 클릭으로 처리)
	[[nodiscard]] bool JustDragged();          // 이번 프레임에 창 끌기 · 크기 조절을 끝냈다(같은 뗌을 클릭으로 치지 않게)
	void ModalBegin();                         // 모달 창 그리기 층
	void ModalEnd();

	// ── HUD 덩어리 ── 자연 사각형(기본 배치)을 주면 옮김 · 배율 변환을 건다. 편집 모드에서는 안의 위젯이 판정 없음
	void BeginHud(const char* id, const char* label, Rectangle natural);
	void EndHud();
	// 범위 객체(중간에 return해도 변환이 풀린다): { wm::HudScope h("hud.minimap", "미니맵", r); ... }
	struct HudScope
	{
		HudScope(const char* id, const char* label, Rectangle natural) { BeginHud(id, label, natural); }
		~HudScope() { EndHud(); }
		HudScope(const HudScope&) = delete;
		HudScope& operator=(const HudScope&) = delete;
	};
	[[nodiscard]] Rectangle HudRect(const char* id, Rectangle natural);   // 옮긴 자리(논리 좌표) — 그리기 전에 다른 코드가 쓸 때
	void EditBackdrop();                       // 편집 모드: HUD 아래를 어둡게(HUD 그리기 전에)
	// 편집 모드: 덩어리 테두리 · 이름표 · 끌기 · 휠 배율 · 오른쪽 클릭 되돌리기 + 위쪽 띠(기본 배치로 · 완료).
	// 완료를 누르면 true
	bool EditOverlay();

	// ── 조회 · 조작 ──
	[[nodiscard]] const char* TopOpen();       // Esc: 맨 위 열린 창(고정 창 제외) id, 없으면 nullptr
	void Raise(const char* id);
	[[nodiscard]] bool Busy();                 // 창을 끌거나 크기를 바꾸는 중(월드 입력을 막는다)
	[[nodiscard]] int CursorHint();            // 0 없음 · 1 ↔ · 2 ↕ · 3 ↘↖ · 4 ↗↙ · 5 옮기기
	[[nodiscard]] bool Locked(const char* id);
	void SetLocked(const char* id, bool on);
	[[nodiscard]] Rectangle LastRect(const char* id);   // 지난 프레임 사각형(논리) — 없으면 {0,0,0,0}

	// ── 저장 ──
	void ToJson(json::Value& root);
	void FromJson(const json::Value& root);
	void ResetWindows();                       // 창 배치 기본으로
	void ResetHud();                           // HUD 배치 기본으로
	[[nodiscard]] bool Dirty();
	void ClearDirty();
	void MarkDirty();
}
