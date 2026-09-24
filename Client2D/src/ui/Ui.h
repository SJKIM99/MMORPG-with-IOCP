#pragma once

#include "raylib.h"
#include "ui/Font.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 즉시 모드(immediate-mode) UI. 매 프레임 "그리면서 동시에 입력을 판정"한다.
//   · 창/버튼이 차지한 영역은 Block()으로 등록 → 월드 클릭이 UI를 뚫고 나가지 않는다
//     (판정은 직전 프레임에 등록된 영역 기준이라 그리기 순서와 무관).
//   · 툴팁/드래그 중인 아이콘은 EndFrame에서 맨 위에 그린다.
// 겉모습: 어두운 판넬 + 청동 테두리(리니지풍).
// ─────────────────────────────────────────────────────────────────────────────
namespace ui
{
	namespace theme
	{
		inline constexpr Color kPanelTop{ 34, 30, 38, 238 };
		inline constexpr Color kPanelBottom{ 18, 16, 22, 244 };
		inline constexpr Color kBorder{ 138, 106, 58, 255 };
		inline constexpr Color kBorderHi{ 214, 176, 104, 255 };
		inline constexpr Color kTitle{ 240, 214, 150, 255 };
		inline constexpr Color kText{ 232, 226, 212, 255 };
		inline constexpr Color kTextDim{ 160, 152, 138, 255 };
		inline constexpr Color kGold{ 255, 214, 90, 255 };
		inline constexpr Color kGood{ 120, 230, 130, 255 };
		inline constexpr Color kBad{ 255, 96, 86, 255 };
		inline constexpr Color kSlot{ 14, 12, 16, 220 };
	}

	enum class Align { Left, Center, Right };
	enum class ButtonKind { Normal, Primary, Danger, Ghost };

	void BeginFrame(float dt);
	void EndFrame();

	// ── 입력 판정 ──
	[[nodiscard]] Vector2 Mouse();
	// 테스트 자동화: 실제 마우스 대신 이 위치를 쓴다(툴팁·호버 스크린샷)
	void SetMouseOverride(bool on, Vector2 pos = Vector2{ 0, 0 });
	[[nodiscard]] bool MouseOverUi();         // 직전 프레임 UI 영역 위에 마우스가 있는가
	void Block(Rectangle r);                   // 이번 프레임 UI 영역 등록
	[[nodiscard]] bool Hovered(Rectangle r);
	[[nodiscard]] bool Pressed(Rectangle r, int button = MOUSE_BUTTON_LEFT);    // 이번 프레임 눌림 시작
	[[nodiscard]] bool Clicked(Rectangle r, int button = MOUSE_BUTTON_LEFT);    // 안에서 눌렀다가 안에서 뗌
	[[nodiscard]] bool DoubleClicked(Rectangle r);
	void ConsumeMouse();                       // 이번 프레임 마우스 입력을 UI가 썼다
	[[nodiscard]] bool MouseConsumed();
	[[nodiscard]] float Time();

	// ── 그리기 ──
	void Panel(Rectangle r, float alpha = 1.f, bool fancy = true);
	// 창: 판넬 + 제목 + 닫기 버튼. open=false가 되면 닫힌 것
	void Window(Rectangle r, std::string_view title, bool* open);
	bool Button(Rectangle r, std::string_view label, ButtonKind kind = ButtonKind::Normal, bool enabled = true, float fontSize = 17.f);
	bool IconButton(Rectangle r, std::string_view glyph, bool enabled = true);
	void Text(std::string_view s, Vector2 pos, float size, Color color, Align align = Align::Left, bool outline = false);
	void TextShadow(std::string_view s, Vector2 pos, float size, Color color, Align align = Align::Left);
	// 줄바꿈해서 그리고 차지한 높이를 돌려준다
	float TextWrapped(std::string_view s, Rectangle area, float size, Color color, float lineGap = 4.f);
	void Bar(Rectangle r, float value, float lag, Color fill, Color back, std::string_view label = {}, float labelSize = 14.f);
	void SlotFrame(Rectangle r, bool hover, bool selected, Color accent = theme::kBorder);
	void Divider(float x0, float x1, float y);
	void Gradient(Rectangle r, Color top, Color bottom);

	// ── 툴팁(맨 위에 그림) ──
	// text가 "---"인 줄은 구분선으로 그린다.
	struct TooltipLine { std::string text; Color color; float size = 15.f; };
	// accent: 테두리·아이콘 칸 색(희귀도). a가 0이면 기본 테두리.
	void Tooltip(std::vector<TooltipLine> lines, uint16_t iconItemId = 0, Color accent = Color{ 0, 0, 0, 0 });
	// 비교 툴팁(착용 중인 장비) — 같은 프레임의 Tooltip 옆에 붙여 그린다. header는 위에 작은 띠("착용 중").
	void TooltipCompare(std::vector<TooltipLine> lines, uint16_t iconItemId, Color accent, std::string_view header);

	// ── 드래그 앤 드롭 ──
	struct DragPayload
	{
		int kind = 0;           // 0 없음, 1 인벤토리 슬롯, 2 퀵슬롯
		int index = -1;
		uint16_t itemId = 0;
	};
	void BeginDrag(const DragPayload& p);
	[[nodiscard]] bool Dragging();
	[[nodiscard]] const DragPayload& Drag();
	// 마우스를 뗀 프레임에만 true. 받은 쪽이 처리하고 나면 EndDrag
	[[nodiscard]] bool DragReleased();
	void EndDrag();
	void SetDragIconDrawer(std::function<void(const DragPayload&, Vector2)> drawer);

	// ── 글 입력칸 ──
	struct TextField
	{
		std::string text;
		size_t maxBytes = 120;
		bool password = false;
		bool asciiOnly = false;
		float blink = 0.f;
	};
	// 반환: Enter로 제출되었는가
	bool InputText(Rectangle r, TextField& f, std::string_view placeholder, bool autofocus = false);
	void Focus(TextField* f);
	[[nodiscard]] TextField* Focused();
	[[nodiscard]] bool TextFocused();
	// 조합 중인 글자까지 포함해 확정(엔터 처리 시)
	void CommitComposition(TextField& f);
}
