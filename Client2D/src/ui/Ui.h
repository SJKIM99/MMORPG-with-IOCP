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
//
// 디자인 언어 — "새김"(docs/DESIGN_SYSTEM.md):
//   새벽이 오기 전 밤의 청회색 바탕 + 모서리를 깎은 판(새김판) + 가는 선.
//   강조는 새벽빛(호박색) 한 가지만, 옛길 · 마법은 청록. 금테 · 광택 · 그라데이션 띠는 쓰지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
namespace ui
{
	namespace theme
	{
		// ── 표면(먹빛 청회색) ──
		inline constexpr Color kInk{ 11, 13, 16, 255 };          // 가장 깊은 바탕 · 어둡게 덮기
		inline constexpr Color kShadow{ 0, 0, 0, 255 };          // 그림자 · 글자 테 · 화면 어둡게 덮기(세기는 알파로 — util::A8)
		inline constexpr Color kSheen{ 255, 255, 255, 255 };     // 윗면 빛 · 호버 옅은 밝힘(알파 8~30으로만)
		inline constexpr Color kSurface0{ 17, 21, 25, 255 };     // 창 바탕(불투명 — 뒤의 이름표가 비치지 않게. HUD는 Panel의 alpha로 조금 비친다)
		inline constexpr Color kSurface1{ 23, 28, 33, 255 };     // 목록 줄 · 입력칸 · 칸
		inline constexpr Color kSurface2{ 31, 37, 43, 255 };     // 버튼 · 호버
		inline constexpr Color kSurface3{ 41, 48, 56, 255 };     // 눌림 · 선택된 줄
		inline constexpr Color kLine{ 45, 53, 61, 255 };         // 가는 선(구분선 · 칸 테두리)
		inline constexpr Color kLineStrong{ 66, 76, 86, 255 };   // 창 · 버튼 테두리
		// ── 글자 ──
		inline constexpr Color kTextHi{ 237, 231, 219, 255 };    // 제목 · 중요한 값
		inline constexpr Color kText{ 207, 200, 187, 255 };      // 본문
		inline constexpr Color kTextDim{ 142, 136, 125, 255 };   // 보조 설명 · 라벨
		inline constexpr Color kTextMute{ 96, 92, 85, 255 };     // 비활성
		// ── 브랜드 ──
		inline constexpr Color kDawn{ 217, 165, 91, 255 };       // 새벽빛: 초점 · 주 버튼 · 핵심 값 · 퀘스트
		inline constexpr Color kDawnHi{ 242, 196, 124, 255 };
		inline constexpr Color kDawnDeep{ 104, 77, 42, 255 };
		inline constexpr Color kDawnWash{ 53, 40, 24, 255 };     // 새벽빛 바탕: 주 버튼 · 고른 탭 · 누르고 있는 칸
		inline constexpr Color kOldway{ 111, 181, 172, 255 };    // 옛길: 이동 · 마법 · 연결
		inline constexpr Color kEmber{ 201, 87, 63, 255 };       // 위험 · 적대
		inline constexpr Color kEmberWash{ 54, 24, 20, 255 };    // 위험 바탕: 위험 단추 · 경고 판
		// ── 의미 ──
		inline constexpr Color kGood{ 141, 191, 110, 255 };
		inline constexpr Color kBad{ 216, 96, 79, 255 };
		inline constexpr Color kBadHi{ 240, 136, 120, 255 };     // 나쁨 밝게: 화면 가운데 · 떠오르는 경고 글자(어두운 땅 위에서 읽히게)
		inline constexpr Color kWarn{ 224, 176, 74, 255 };
		inline constexpr Color kInfo{ 127, 168, 214, 255 };
		// ── 수치 막대 ──
		inline constexpr Color kHp{ 176, 58, 50, 255 };
		inline constexpr Color kHpBack{ 32, 14, 14, 255 };       // 체력 막대 바탕(빈 칸)
		inline constexpr Color kMp{ 60, 104, 176, 255 };
		inline constexpr Color kExp{ 196, 156, 82, 255 };
		// ── 예전 이름(호환) ──
		inline constexpr Color kPanelTop = kSurface1;
		inline constexpr Color kPanelBottom = kSurface0;
		inline constexpr Color kBorder = kLineStrong;
		inline constexpr Color kBorderHi = kDawn;
		inline constexpr Color kTitle{ 233, 223, 201, 255 };     // 창 안의 소제목 · 이름(따뜻한 흰색)
		inline constexpr Color kGold{ 222, 184, 110, 255 };      // 값 강조(돈 · 보상) — 새벽빛보다 조금 밝게
		inline constexpr Color kOption{ 157, 187, 216, 255 };    // 아이템 옵션 글자(청회색) — 툴팁 · 경매 줄
		inline constexpr Color kParchment{ 185, 176, 154, 255 }; // 흐린 양피지: 남의 결사 이름 · 한발 물러선 이름표
		inline constexpr Color kSlot{ 15, 18, 22, 235 };
		// ── 채팅 채널(Phase 3-B, DESIGN_SYSTEM 3.4) — 글자 색. 이름은 한 단계 밝게 ──
		inline constexpr Color kChatNormal{ 226, 221, 210, 255 };   // 일반(같은 지역) — 본문 색에 가깝게
		inline constexpr Color kChatWorld{ 232, 170, 104, 255 };    // 전체 — 새벽빛 쪽 주황
		inline constexpr Color kChatTrade{ 202, 180, 112, 255 };    // 거래 — 누런 금빛(돈)
		inline constexpr Color kChatGuild{ 141, 196, 126, 255 };    // 결사 — 풀빛
		inline constexpr Color kChatWhisper{ 214, 150, 178, 255 };  // 귓속말 — 흐린 분홍(보라 · 네온 금지)
		inline constexpr Color kChatSystem{ 150, 176, 196, 255 };   // 시스템 — 푸른 회색
		inline constexpr Color kChatNotice{ 217, 165, 91, 255 };    // 공지 — 새벽빛
	}

	// 글자 크기(px, 1280×720 기준) — docs/DESIGN_SYSTEM.md 2장. 새 화면은 이 값만 쓴다.
	namespace type
	{
		inline constexpr float kDisplayXL = 52.f;   // 로고
		inline constexpr float kDisplayL  = 36.f;   // 장 제목 · 엔딩
		inline constexpr float kDisplayM  = 26.f;   // 보스 이름 · 지역 진입
		inline constexpr float kDisplayS  = 20.f;   // 카드 제목
		inline constexpr float kTitle     = 17.f;   // 창 제목(Display)
		inline constexpr float kHeading   = 15.f;   // 소제목 · 이름(Strong)
		inline constexpr float kBody      = 14.f;   // 본문
		inline constexpr float kBodyS     = 13.f;   // 목록 · 툴팁 본문
		inline constexpr float kCaption   = 12.f;   // 보조 설명
		inline constexpr float kMicro     = 11.f;   // 단축키 · 배지
		inline constexpr float kNumber    = 14.f;   // HUD 수치(Number)
		inline constexpr float kLeading   = 0.5f;   // 줄 간격 = 크기 × 0.5 (행간 150%)
	}

	enum class Align { Left, Center, Right };
	enum class ButtonKind { Normal, Primary, Danger, Ghost };

	void BeginFrame(float dt);
	void EndFrame();

	// ── 좌표 변환(Phase 3-A): UI 배율 · HUD 덩어리 배율 ──
	// 화면(물리 픽셀) = off + 지역 좌표 × k. 맨 아래(기본)는 {0, 0, UI 배율}.
	// 그리기 코드는 늘 지역 좌표로 쓴다 — 판정(Hovered …) · 가위(BeginClip) · 글자 선명도가 변환을 따라간다.
	struct Xform { Vector2 off{ 0, 0 }; float k = 1.f; };
	void SetUiScale(float s);                  // BeginFrame 전에(장면마다). 로그인 화면은 1
	[[nodiscard]] float UiScale();
	[[nodiscard]] Vector2 LogicalScreen();     // 화면 크기 ÷ UI 배율
	void BindXform();                          // rlgl 행렬에 지금 변환을 건다(UI 그리기 시작)
	void UnbindXform();
	void ReapplyXform();                       // Begin/EndTextureMode 뒤에 다시 건다(지금 행렬이 modelview로 돌아가 있다)
	void SuspendXform();                       // 렌더 텍스처에 자기 좌표로 그리기 전(미니맵 캐시 등) — 변환을 잠깐 뗀다
	void ResumeXform();
	void PushXform(Vector2 off, float k);      // 부모 지역 좌표 기준 p_부모 = off + p × k
	void PopXform();
	[[nodiscard]] Xform CurrentXform();        // 지역 → 물리
	[[nodiscard]] Vector2 ToScreen(Vector2 p);
	[[nodiscard]] Rectangle ToScreen(Rectangle r);
	[[nodiscard]] Vector2 FromScreen(Vector2 p);
	[[nodiscard]] Rectangle SnapRect(Rectangle r);   // 네 변이 물리 픽셀 경계에 오도록(배율이 1이 아니어도 선이 또렷하게)
	void BeginClip(Rectangle r);               // 지역 좌표 가위(BeginScissorMode 대신)
	void EndClip();

	// ── 입력 층(Phase 3-A 창 가림) ──
	// 창 관리자가 프레임마다 "마우스 아래 맨 위 창"의 층을 주인으로 정한다. 다른 층의 위젯은
	// Hovered · Pressed · Clicked가 거짓 → 겹친 창의 뒤 단추가 눌리지 않는다. 0 = HUD · 월드.
	void SetLayer(int layer);
	[[nodiscard]] int Layer();
	void SetMouseOwner(int layer);
	[[nodiscard]] int MouseOwner();
	void SetInert(bool on);                    // 닫히는 창 그리기 · UI 편집 모드: 판정이 모두 거짓, 영역 등록도 없음
	[[nodiscard]] bool Inert();
	[[nodiscard]] Vector2 MouseScreen();       // 물리 픽셀 마우스

	// ── 입력 판정 ──
	[[nodiscard]] Vector2 Mouse();             // 지금 변환의 지역 좌표
	// 테스트 자동화: 실제 마우스 대신 이 위치(물리 픽셀)를 쓴다(툴팁·호버 스크린샷)
	void SetMouseOverride(bool on, Vector2 pos = Vector2{ 0, 0 });
	// 자동 시나리오: 마우스 단추도 대신 정한다(창 끌기 · 크기 조절 · 탭 누르기 시험). off면 실제 단추
	void SetButtonOverride(bool on, bool leftDown = false, bool rightDown = false);
	// 단추 상태(시나리오 대체 포함) — UI · 창 관리자는 raylib 대신 이것을 쓴다
	[[nodiscard]] bool BtnPressed(int button);
	[[nodiscard]] bool BtnDown(int button);
	[[nodiscard]] bool BtnReleased(int button);
	[[nodiscard]] float Wheel();                // 휠(시나리오 대체 포함) — 한 프레임에 한 번 읽는다
	void SetWheelOverride(float v);            // 자동 시나리오: 다음 프레임 휠 값
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
	// 새김판: 모서리를 chamfer px 깎은 판. borderW = 0이면 테두리 없음.
	void Plate(Rectangle r, Color fill, Color border, float chamfer = 4.f, float borderW = 1.f);
	// 가는 선(정수 픽셀에 맞춰 또렷하게)
	void Rule(float x0, float x1, float y, Color c = theme::kLine);
	// 창 안 소제목: 작은 글씨 + 오른쪽으로 이어지는 가는 선
	void SectionHeader(Vector2 pos, float width, std::string_view title);
	// 단축키 표시(작은 새김판 + 글자)
	void Keycap(Rectangle r, std::string_view key, bool active = false);
	void Panel(Rectangle r, float alpha = 1.f, bool fancy = true);
	// 게임 로고(하르벤 · HARVEN · 멈춘 심장) — center = 한글 글자의 가운데
	void Logo(Vector2 center, float scale = 1.f, float alpha = 1.f, bool subtitle = true);
	// 창: 판넬 + 제목 + 닫기 버튼. open=false가 되면 닫힌 것
	void Window(Rectangle r, std::string_view title, bool* open);
	// fontSize는 예전 크기 체계 — 글꼴(Strong)에 맞춰 조금 줄여 그린다(17 → 15).
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
	struct TooltipLine { std::string text; Color color; float size = 15.f; Face face = Face::Inherit; };
	// accent: 테두리·아이콘 칸 색(희귀도). a가 0이면 기본 테두리.
	void Tooltip(std::vector<TooltipLine> lines, uint16_t iconItemId = 0, Color accent = BLANK);
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
	// 이번 프레임에 그 글자가 입력되었나(단축키로 입력칸을 연 프레임에 같은 글자가 두 번 들어가지 않게)
	[[nodiscard]] bool TypedThisFrame(int codepoint);
}
