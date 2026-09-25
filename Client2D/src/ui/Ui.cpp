#include "ui/Ui.h"
#include "ui/UiArt.h"
#include "platform/Ime.h"
#include "render/Icons.h"
#include "audio/Sfx.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace ui
{
	namespace
	{
		std::vector<Rectangle> g_blockPrev, g_blockNow;
		Vector2 g_mouse{};
		Vector2 g_pressPos{};
		bool g_consumed = false;
		float g_time = 0.f;
		double g_lastClickTime = -10.0;
		Vector2 g_lastClickPos{};
		bool g_doubleClick = false;

		std::vector<TooltipLine> g_tip;
		uint16_t g_tipIcon = 0;
		Color g_tipAccent{ 0, 0, 0, 0 };
		bool g_hasTip = false;
		std::vector<TooltipLine> g_cmp;
		uint16_t g_cmpIcon = 0;
		Color g_cmpAccent{ 0, 0, 0, 0 };
		std::string g_cmpHeader;
		bool g_hasCmp = false;

		Vector2 MeasureTip(const std::vector<TooltipLine>& lines, uint16_t icon)
		{
			float w = 0.f, h = 14.f;
			for (const auto& l : lines)
			{
				if (l.text == "---") { h += 11.f; continue; }
				const Vector2 m = Fonts().Measure(l.text, l.size, 0.f, l.face);
				w = std::max(w, m.x);
				h += l.size + 5.f;
			}
			const float iconW = icon ? 54.f : 0.f;
			w += 28.f + iconW;
			h = std::max(h + 4.f, icon ? 66.f : 0.f);
			return Vector2{ std::max(w, 130.f), h };
		}

		void DrawTip(const std::vector<TooltipLine>& lines, uint16_t icon, Color accent, Rectangle r, std::string_view header)
		{
			r.x = std::round(r.x); r.y = std::round(r.y);
			Plate(Rectangle{ r.x + 2, r.y + 5, r.width, r.height }, Color{ 0, 0, 0, 100 }, BLANK, 5.f, 0.f);
			const bool rare = accent.a > 0;
			Plate(r, Color{ 15, 18, 22, 250 }, rare ? util::MulA(accent, 0.75f) : theme::kLineStrong, 4.f, 1.f);
			if (rare)
			{
				// 위쪽 희귀도 띠: 2px 선 + 아주 옅은 번짐(광택이 아니라 색 표시)
				DrawRectangleRec(Rectangle{ r.x + 4, r.y + 1, r.width - 8, 2 }, util::MulA(accent, 0.95f));
				DrawRectangleGradientV(static_cast<int>(r.x + 1), static_cast<int>(r.y + 3), static_cast<int>(r.width - 2), 22, util::MulA(accent, 0.10f), util::MulA(accent, 0.f));
			}
			if (!header.empty())
			{
				TextStyle hs; hs.size = type::kMicro + 0.5f; hs.color = theme::kTextDim; hs.face = Face::Strong;
				const Vector2 hm = Fonts().Measure(header, hs.size, 0.f, Face::Strong);
				const Rectangle hr{ r.x + 10, r.y - 17, std::round(hm.x + 16), 18 };
				Plate(hr, theme::kSurface2, theme::kLineStrong, 3.f, 1.f);
				Fonts().Draw(header, Vector2{ hr.x + 8, hr.y + 3 }, hs);
			}
			const float iconW = icon ? 54.f : 0.f;
			if (icon)
			{
				const Rectangle ir{ r.x + 11, r.y + 11, 44, 44 };
				SlotFrame(ir, false, false, rare ? accent : theme::kBorder);
				icons::Draw(icon, Rectangle{ ir.x + 2, ir.y + 2, 40, 40 });
			}
			float ty = r.y + 11.f;
			for (const auto& l : lines)
			{
				if (l.text == "---")
				{
					Rule(r.x + 12.f + iconW, r.x + r.width - 12.f, ty + 4.f, theme::kLine);
					ty += 11.f;
					continue;
				}
				TextStyle st;
				st.size = l.size;
				st.color = l.color;
				st.face = l.face;
				Fonts().Draw(l.text, Vector2{ r.x + 14.f + iconW, ty }, st);
				ty += l.size + 5.f;
			}
		}

		// 삼각형(정점 순서와 무관하게 앞면으로)
		void Tri(Vector2 a, Vector2 b, Vector2 c, Color col)
		{
			const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
			if (cross > 0.f) std::swap(b, c);
			DrawTriangle(a, b, c, col);
		}

		bool SameColor(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

		DragPayload g_drag{};
		bool g_dragging = false;
		bool g_dragReleased = false;
		std::function<void(const DragPayload&, Vector2)> g_dragDrawer;

		TextField* g_focus = nullptr;
		bool g_focusTouched = false;
		std::vector<int> g_chars;

		Rectangle g_lastHoverButton{};
		bool g_mouseOverride = false;
		Vector2 g_mouseOverridePos{};

		bool Contains(Rectangle r, Vector2 p)
		{
			return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
		}
		bool SameRect(Rectangle a, Rectangle b)
		{
			return std::fabs(a.x - b.x) < 0.5f && std::fabs(a.y - b.y) < 0.5f && std::fabs(a.width - b.width) < 0.5f && std::fabs(a.height - b.height) < 0.5f;
		}
	}

	void BeginFrame(float dt)
	{
		g_time += dt;
		g_mouse = g_mouseOverride ? g_mouseOverridePos : GetMousePosition();
		g_blockPrev.swap(g_blockNow);
		g_blockNow.clear();
		g_consumed = false;
		g_hasTip = false;
		g_dragReleased = false;
		g_focusTouched = false;

		if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
		{
			g_pressPos = g_mouse;
			const double now = GetTime();
			g_doubleClick = (now - g_lastClickTime) < 0.33 && util::Len(util::Sub(g_mouse, g_lastClickPos)) < 6.f;
			g_lastClickTime = g_doubleClick ? -10.0 : now;
			g_lastClickPos = g_mouse;
		}
		else
			g_doubleClick = false;

		if (g_dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
			g_dragReleased = true;

		// 문자 입력(raylib) + IME 완성 글자
		g_chars.clear();
		for (int c = GetCharPressed(); c != 0; c = GetCharPressed())
			g_chars.push_back(c);
		ime::TakeCommitted(g_chars);
	}

	void EndFrame()
	{
		// 입력칸 밖을 클릭하면 포커스 해제
		if (g_focus && !g_focusTouched && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
			g_focus = nullptr;
		ime::SetEnabled(g_focus != nullptr && !g_focus->asciiOnly && !g_focus->password);

		// 툴팁(+ 비교 툴팁)
		if (g_hasTip && !g_dragging)
		{
			const float SW = static_cast<float>(GetScreenWidth()), SH = static_cast<float>(GetScreenHeight());
			const Vector2 sz = MeasureTip(g_tip, g_tipIcon);
			const Vector2 csz = g_hasCmp ? MeasureTip(g_cmp, g_cmpIcon) : Vector2{ 0, 0 };
			float x = g_mouse.x + 18.f, y = g_mouse.y + 14.f;
			if (x + sz.x > SW - 4) x = g_mouse.x - sz.x - 12.f;
			if (y + sz.y > SH - 4) y = SH - sz.y - 4.f;
			y = std::max(4.f, y);
			float cx = 0.f;
			if (g_hasCmp)
			{
				// 비교 툴팁은 주 툴팁 옆(마우스 반대쪽)에
				cx = x - csz.x - 8.f;
				if (cx < 4.f)
				{
					cx = x + sz.x + 8.f;
					if (cx + csz.x > SW - 4.f) { x = std::max(4.f, SW - 4.f - sz.x - csz.x - 8.f); cx = x + sz.x + 8.f; }
				}
			}
			const Rectangle r{ x, y, sz.x, sz.y };
			DrawTip(g_tip, g_tipIcon, g_tipAccent, r, {});
			if (g_hasCmp)
			{
				float cy = std::max(22.f, y + 18.f);
				if (cy + csz.y > SH - 4) cy = SH - csz.y - 4.f;
				DrawTip(g_cmp, g_cmpIcon, g_cmpAccent, Rectangle{ cx, cy, csz.x, csz.y }, g_cmpHeader);
			}
		}
		g_hasCmp = false;

		// 드래그 아이콘
		if (g_dragging && g_dragDrawer)
			g_dragDrawer(g_drag, g_mouse);
		if (g_dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !g_dragReleased)
			g_dragging = false;
		if (g_dragReleased)
			g_dragging = false;
	}

	Vector2 Mouse() { return g_mouse; }
	void SetMouseOverride(bool on, Vector2 pos) { g_mouseOverride = on; g_mouseOverridePos = pos; }

	bool MouseOverUi()
	{
		for (const auto& r : g_blockPrev)
			if (Contains(r, g_mouse)) return true;
		for (const auto& r : g_blockNow)
			if (Contains(r, g_mouse)) return true;
		return false;
	}

	void Block(Rectangle r) { g_blockNow.push_back(r); }
	bool Hovered(Rectangle r) { return Contains(r, g_mouse); }
	bool Pressed(Rectangle r, int button) { return IsMouseButtonPressed(button) && Contains(r, g_mouse); }
	bool Clicked(Rectangle r, int button)
	{
		return IsMouseButtonReleased(button) && Contains(r, g_mouse) && Contains(r, g_pressPos);
	}
	bool DoubleClicked(Rectangle r) { return g_doubleClick && Contains(r, g_mouse); }
	void ConsumeMouse() { g_consumed = true; }
	bool MouseConsumed() { return g_consumed; }
	float Time() { return g_time; }

	// ── 그리기 ─────────────────────────────────────────────────────────────────
	void Gradient(Rectangle r, Color top, Color bottom)
	{
		DrawRectangleGradientV(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width), static_cast<int>(r.height), top, bottom);
	}

	void Plate(Rectangle r, Color fill, Color border, float c, float bw)
	{
		r.x = std::round(r.x); r.y = std::round(r.y); r.width = std::round(r.width); r.height = std::round(r.height);
		if (r.width <= 0.f || r.height <= 0.f) return;
		c = std::max(0.f, std::min(std::round(c), std::floor(std::min(r.width, r.height) * 0.5f)));
		if (fill.a > 0)
		{
			// 겹치지 않게 나눠 칠한다(반투명이어도 이음매가 생기지 않도록)
			DrawRectangleRec(Rectangle{ r.x, r.y + c, r.width, r.height - 2.f * c }, fill);
			if (c > 0.f)
			{
				DrawRectangleRec(Rectangle{ r.x + c, r.y, r.width - 2.f * c, c }, fill);
				DrawRectangleRec(Rectangle{ r.x + c, r.y + r.height - c, r.width - 2.f * c, c }, fill);
				const float x0 = r.x, y0 = r.y, x1 = r.x + r.width, y1 = r.y + r.height;
				Tri(Vector2{ x0 + c, y0 }, Vector2{ x0, y0 + c }, Vector2{ x0 + c, y0 + c }, fill);
				Tri(Vector2{ x1 - c, y0 }, Vector2{ x1 - c, y0 + c }, Vector2{ x1, y0 + c }, fill);
				Tri(Vector2{ x0, y1 - c }, Vector2{ x0 + c, y1 }, Vector2{ x0 + c, y1 - c }, fill);
				Tri(Vector2{ x1, y1 - c }, Vector2{ x1 - c, y1 - c }, Vector2{ x1 - c, y1 }, fill);
			}
		}
		if (bw > 0.f && border.a > 0)
		{
			const float x0 = r.x, y0 = r.y, x1 = r.x + r.width, y1 = r.y + r.height;
			DrawRectangleRec(Rectangle{ x0 + c, y0, r.width - 2.f * c, bw }, border);
			DrawRectangleRec(Rectangle{ x0 + c, y1 - bw, r.width - 2.f * c, bw }, border);
			DrawRectangleRec(Rectangle{ x0, y0 + c, bw, r.height - 2.f * c }, border);
			DrawRectangleRec(Rectangle{ x1 - bw, y0 + c, bw, r.height - 2.f * c }, border);
			if (c > 0.f)
			{
				const float h = bw * 0.5f;
				DrawLineEx(Vector2{ x0 + c, y0 + h }, Vector2{ x0 + h, y0 + c }, bw, border);
				DrawLineEx(Vector2{ x1 - c, y0 + h }, Vector2{ x1 - h, y0 + c }, bw, border);
				DrawLineEx(Vector2{ x0 + h, y1 - c }, Vector2{ x0 + c, y1 - h }, bw, border);
				DrawLineEx(Vector2{ x1 - h, y1 - c }, Vector2{ x1 - c, y1 - h }, bw, border);
			}
		}
	}

	void Rule(float x0, float x1, float y, Color c)
	{
		if (x1 <= x0) return;
		DrawRectangleRec(Rectangle{ std::round(x0), std::round(y), std::round(x1 - x0), 1.f }, c);
	}

	void SectionHeader(Vector2 pos, float width, std::string_view title)
	{
		TextStyle st;
		st.size = type::kCaption + 0.5f;
		st.color = theme::kTextDim;
		st.face = Face::Strong;
		st.spacing = 0.4f;
		const float w = Fonts().Draw(title, pos, st);
		Rule(pos.x + w + 10.f, pos.x + width, pos.y + st.size * 0.5f + 1.f, theme::kLine);
	}

	void Keycap(Rectangle r, std::string_view key, bool active)
	{
		Plate(r, active ? Color{ 52, 40, 24, 235 } : Color{ 10, 12, 15, 225 }, active ? theme::kDawn : theme::kLineStrong, 2.f, 1.f);
		TextStyle st;
		st.size = std::min(type::kMicro, r.height - 3.f);
		st.color = active ? theme::kDawnHi : theme::kTextDim;
		st.face = Face::Strong;
		const Vector2 m = Fonts().Measure(key, st.size, 0.f, Face::Strong);
		Fonts().Draw(key, Vector2{ std::round(r.x + (r.width - m.x) * 0.5f), std::round(r.y + (r.height - m.y) * 0.5f) }, st);
	}

	void Panel(Rectangle r, float alpha, bool fancy)
	{
		// 그림자(판 아래로 조금) → 판 → 안쪽 윗선(빛은 위에서)
		Plate(Rectangle{ r.x + 2, r.y + 6, r.width, r.height }, util::MulA(Color{ 0, 0, 0, 110 }, alpha), BLANK, 6.f, 0.f);
		Plate(r, util::MulA(theme::kSurface0, alpha), util::MulA(theme::kLineStrong, alpha), 5.f, 1.f);
		// 그린 돌결(있으면): 먹빛 판 위에 아주 옅게 — 글자가 읽히는 대비는 그대로
		if (const Texture2D* tex = uiart::Get(uiart::PanelTex))
		{
			const Rectangle in{ r.x + 2.f, r.y + 2.f, r.width - 4.f, r.height - 4.f };
			const float k = 1.4f;   // 돌결 크기(텍스처 1px = 화면 1/1.4px)
			DrawTexturePro(*tex, Rectangle{ r.x * k, r.y * k, in.width * k, in.height * k }, in, Vector2{ 0, 0 }, 0.f,
				util::MulA(Color{ 255, 255, 255, 255 }, 0.30f * alpha));
		}
		Rule(r.x + 6.f, r.x + r.width - 6.f, r.y + 1.f, util::MulA(Color{ 255, 255, 255, 16 }, alpha));
		if (fancy)
		{
			// 새김: 3px 안쪽의 두 번째 선 — 판에 판 홈처럼 아주 옅게
			Plate(Rectangle{ r.x + 3, r.y + 3, r.width - 6, r.height - 6 }, BLANK, util::MulA(Color{ 255, 255, 255, 12 }, alpha), 3.f, 1.f);
			// 청동 모서리 장식(그린 그림) — 네 모서리를 뒤집어 맞춘다
			if (const Texture2D* c = uiart::Get(uiart::Corner); c && r.width > 120.f && r.height > 90.f)
			{
				const float s = 38.f;
				const float w = static_cast<float>(c->width), h = static_cast<float>(c->height);
				const Color tint = util::MulA(Color{ 255, 255, 255, 255 }, 0.9f * alpha);
				const float o = 5.f;   // 판 바깥으로 살짝 걸친다(모서리를 감싸는 장식)
				DrawTexturePro(*c, Rectangle{ 0, 0, w, h }, Rectangle{ r.x - o, r.y - o, s, s }, Vector2{ 0, 0 }, 0.f, tint);
				DrawTexturePro(*c, Rectangle{ 0, 0, -w, h }, Rectangle{ r.x + r.width - s + o, r.y - o, s, s }, Vector2{ 0, 0 }, 0.f, tint);
				DrawTexturePro(*c, Rectangle{ 0, 0, w, -h }, Rectangle{ r.x - o, r.y + r.height - s + o, s, s }, Vector2{ 0, 0 }, 0.f, tint);
				DrawTexturePro(*c, Rectangle{ 0, 0, -w, -h }, Rectangle{ r.x + r.width - s + o, r.y + r.height - s + o, s, s }, Vector2{ 0, 0 }, 0.f, tint);
			}
		}
		Block(r);
	}

	void Logo(Vector2 c, float k, float alpha, bool subtitle)
	{
		// 글자: 고운바탕 굵게, 넓은 자간. 그림자는 아래로 부드럽게 한 번만.
		TextStyle w;
		w.size = 62.f * k;
		w.color = util::MulA(Color{ 238, 229, 208, 255 }, alpha);
		w.spacing = 16.f * k;
		w.face = Face::Display;
		w.shadow = 3.f * k;
		w.shadowColor = util::MulA(Color{ 0, 0, 0, 170 }, alpha);
		Fonts().DrawCentered("하르벤", c, w);

		// 지평선과 샛별: 새벽 직전에 뜨는 별(데니차) — 이 게임의 표식
		const float hy = std::round(c.y + 40.f * k);
		const float half = 170.f * k;
		DrawRectangleGradientH(static_cast<int>(c.x - half), static_cast<int>(hy), static_cast<int>(half - 12.f * k), 1, util::MulA(theme::kDawn, 0.f), util::MulA(theme::kDawn, 0.85f * alpha));
		DrawRectangleGradientH(static_cast<int>(c.x + 12.f * k), static_cast<int>(hy), static_cast<int>(half - 12.f * k), 1, util::MulA(theme::kDawn, 0.85f * alpha), util::MulA(theme::kDawn, 0.f));
		const float d = 5.f * k;
		const Color star = util::MulA(theme::kDawnHi, alpha);
		DrawTriangle(Vector2{ c.x, hy - d * 1.4f }, Vector2{ c.x - d, hy + 0.5f }, Vector2{ c.x + d, hy + 0.5f }, star);
		DrawTriangle(Vector2{ c.x - d, hy + 0.5f }, Vector2{ c.x, hy + d * 1.4f + 1.f }, Vector2{ c.x + d, hy + 0.5f }, star);

		TextStyle en;
		en.size = 15.f * k;
		en.color = util::MulA(theme::kTextDim, alpha);
		en.spacing = 11.f * k;
		en.face = Face::Latin;
		Fonts().DrawCentered("HARVEN", Vector2{ c.x + en.spacing * 0.5f, hy + 20.f * k }, en);
		if (subtitle)
		{
			TextStyle sub;
			sub.size = 17.f * k;
			sub.color = util::MulA(theme::kDawn, alpha);
			sub.spacing = 5.f * k;
			sub.face = Face::Serif;
			sub.shadow = 1.f;
			sub.shadowColor = util::MulA(Color{ 0, 0, 0, 160 }, alpha);
			Fonts().DrawCentered("멈춘 심장", Vector2{ c.x + sub.spacing * 0.5f, hy + 48.f * k }, sub);
		}
	}

	namespace
	{
		bool CloseButton(Rectangle r)
		{
			const bool hover = Hovered(r);
			if (hover)
				Plate(r, theme::kSurface2, theme::kLine, 2.f, 1.f);
			const Color c = hover ? theme::kTextHi : theme::kTextDim;
			const Vector2 m{ std::round(r.x + r.width * 0.5f) + 0.5f, std::round(r.y + r.height * 0.5f) + 0.5f };
			const float k = 4.5f;
			DrawLineEx(Vector2{ m.x - k, m.y - k }, Vector2{ m.x + k, m.y + k }, 1.6f, c);
			DrawLineEx(Vector2{ m.x - k, m.y + k }, Vector2{ m.x + k, m.y - k }, 1.6f, c);
			Block(r);
			if (Clicked(r))
			{
				ConsumeMouse();
				return true;
			}
			return false;
		}
	}

	void Window(Rectangle r, std::string_view title, bool* open)
	{
		Panel(r);
		// 제목: 고운바탕(Display), 왼쪽 정렬
		TextStyle st;
		st.size = type::kTitle;
		st.color = theme::kTextHi;
		st.face = Face::Display;
		st.spacing = 0.6f;
		Fonts().Draw(title, Vector2{ std::round(r.x + 18.f), std::round(r.y + 11.f) }, st);
		// 제목 아래: 가는 선 + 새벽빛 짧은 획(이 게임의 서명 같은 표식)
		const float ly = std::round(r.y + 37.f);
		Rule(r.x + 12.f, r.x + r.width - 12.f, ly, theme::kLine);
		DrawRectangleRec(Rectangle{ std::round(r.x + 12.f), ly - 1.f, 22.f, 2.f }, theme::kDawn);
		if (open)
		{
			const Rectangle close{ r.x + r.width - 34, r.y + 8, 24, 24 };
			if (CloseButton(close))
			{
				*open = false;
				sfx::Play(sfx::Id::Close, 0.6f);
			}
		}
	}

	bool Button(Rectangle r, std::string_view label, ButtonKind kind, bool enabled, float fontSize)
	{
		const bool hover = enabled && Hovered(r);
		const bool down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && Contains(r, g_pressPos);
		Color fill = theme::kSurface2, border = theme::kLineStrong, text = theme::kTextHi;
		switch (kind)
		{
		case ButtonKind::Primary: fill = Color{ 54, 41, 24, 255 }; border = theme::kDawn; text = theme::kDawnHi; break;
		case ButtonKind::Danger:  fill = Color{ 58, 27, 23, 255 }; border = Color{ 178, 76, 58, 255 }; text = Color{ 242, 198, 186, 255 }; break;
		case ButtonKind::Ghost:   fill = Color{ 0, 0, 0, 0 }; border = theme::kLine; text = theme::kText; break;
		default: break;
		}
		if (!enabled) { fill = theme::kSurface1; border = theme::kLine; text = theme::kTextMute; }
		if (hover)
		{
			fill = kind == ButtonKind::Ghost ? Color{ 255, 255, 255, 12 } : util::Shade(fill, 1.22f);
			border = util::Shade(border, 1.2f);
		}
		Rectangle b = r;
		if (down) { b.y += 1.f; fill = util::Shade(fill, 0.88f); }
		Plate(b, fill, border, 3.f, 1.f);
		if (enabled && kind != ButtonKind::Ghost)
			Rule(b.x + 4.f, b.x + b.width - 4.f, b.y + 1.f, Color{ 255, 255, 255, static_cast<unsigned char>(hover ? 26 : 12) });
		TextStyle st;
		st.size = std::clamp(fontSize - 2.f, 12.f, 16.f);
		st.color = text;
		st.face = Face::Strong;
		const Vector2 m = Fonts().Measure(label, st.size, 0.f, Face::Strong);
		Fonts().Draw(label, Vector2{ std::round(b.x + (b.width - m.x) * 0.5f), std::round(b.y + (b.height - m.y) * 0.5f) }, st);
		Block(r);
		if (hover && !SameRect(g_lastHoverButton, r))
		{
			g_lastHoverButton = r;
			sfx::Play(sfx::Id::Hover, 0.35f, 0.02f);
		}
		if (!hover && SameRect(g_lastHoverButton, r))
			g_lastHoverButton = Rectangle{};
		if (enabled && Clicked(r))
		{
			ConsumeMouse();
			sfx::Play(sfx::Id::Click, 0.7f);
			return true;
		}
		return false;
	}

	bool IconButton(Rectangle r, std::string_view glyph, bool enabled)
	{
		if (glyph == "×")
			return enabled ? CloseButton(r) : false;
		const bool hover = enabled && Hovered(r);
		Plate(r, hover ? theme::kSurface3 : theme::kSurface2, hover ? theme::kTextDim : theme::kLineStrong, 2.f, 1.f);
		TextStyle st;
		st.size = std::min(16.f, r.height - 6.f);
		st.color = hover ? theme::kTextHi : theme::kText;
		st.face = Face::Strong;
		const Vector2 m = Fonts().Measure(glyph, st.size, 0.f, Face::Strong);
		Fonts().Draw(glyph, Vector2{ std::round(r.x + (r.width - m.x) * 0.5f), std::round(r.y + (r.height - m.y) * 0.5f) }, st);
		Block(r);
		if (enabled && Clicked(r))
		{
			ConsumeMouse();
			return true;
		}
		return false;
	}

	void Text(std::string_view s, Vector2 pos, float size, Color color, Align align, bool outline)
	{
		TextStyle st;
		st.size = size;
		st.color = color;
		if (outline) { st.outline = 1.5f; st.outlineColor = Color{ 0, 0, 0, static_cast<unsigned char>(color.a * 0.8f) }; }
		if (align != Align::Left)
		{
			const float w = Fonts().Measure(s, size).x;
			pos.x -= (align == Align::Center) ? w * 0.5f : w;
		}
		Fonts().Draw(s, pos, st);
	}

	void TextShadow(std::string_view s, Vector2 pos, float size, Color color, Align align)
	{
		TextStyle st;
		st.size = size;
		st.color = color;
		st.shadow = size >= 24.f ? 2.f : 1.f;
		st.shadowColor = Color{ 0, 0, 0, static_cast<unsigned char>(color.a * 0.65f) };
		if (align != Align::Left)
		{
			const float w = Fonts().Measure(s, size).x;
			pos.x -= (align == Align::Center) ? w * 0.5f : w;
		}
		Fonts().Draw(s, pos, st);
	}

	float TextWrapped(std::string_view s, Rectangle area, float size, Color color, float lineGap)
	{
		// 글자 단위 줄바꿈(한글은 띄어쓰기가 없어도 자연스럽다). 공백에서 우선 끊는다.
		std::vector<int> cps;
		DecodeUtf8(s, cps);
		std::string line, word;
		float y = area.y;
		auto flush = [&](bool force)
		{
			if (line.empty() && !force) return;
			TextShadow(line, Vector2{ area.x, y }, size, color);
			y += size + lineGap;
			line.clear();
		};
		for (size_t i = 0; i < cps.size(); ++i)
		{
			const int cp = cps[i];
			if (cp == '\n') { flush(true); continue; }
			std::string candidate = line;
			AppendUtf8(candidate, cp);
			if (Fonts().Measure(candidate, size).x > area.width && !line.empty())
			{
				// 마지막 공백에서 끊기
				const size_t sp = line.rfind(' ');
				if (sp != std::string::npos && sp > line.size() / 2)
				{
					const std::string rest = line.substr(sp + 1);
					line.erase(sp);
					flush(false);
					line = rest;
				}
				else
					flush(false);
				if (cp == ' ') continue;
				AppendUtf8(line, cp);
			}
			else
				line.swap(candidate);
		}
		flush(false);
		return y - area.y;
	}

	void Bar(Rectangle r, float value, float lag, Color fill, Color back, std::string_view label, float labelSize)
	{
		value = util::Clamp01(value);
		lag = util::Clamp01(lag);
		r.x = std::round(r.x); r.y = std::round(r.y); r.width = std::round(r.width); r.height = std::round(r.height);
		DrawRectangleRec(r, back);
		// 안쪽 윗그늘(홈에 담긴 느낌)
		DrawRectangleRec(Rectangle{ r.x, r.y, r.width, 1.f }, Color{ 0, 0, 0, 90 });
		if (lag > value)
			DrawRectangleRec(Rectangle{ r.x, r.y, std::round(r.width * lag), r.height }, Color{ 236, 222, 200, 150 });
		if (value * r.width >= 1.f)
		{
			const Rectangle f{ r.x, r.y, std::round(r.width * value), r.height };
			DrawRectangleRec(f, fill);
			// 윗면 1px 밝게 · 아랫면 1px 어둡게 — 광택 대신 두께감만
			DrawRectangleRec(Rectangle{ f.x, f.y, f.width, 1.f }, util::WithA(util::Shade(fill, 1.45f), 0.85f));
			DrawRectangleRec(Rectangle{ f.x, f.y + f.height - 1.f, f.width, 1.f }, util::WithA(util::Shade(fill, 0.62f), 0.9f));
			// 끝 눈금(현재 값 위치를 또렷하게)
			if (value < 0.999f)
				DrawRectangleRec(Rectangle{ f.x + f.width - 1.f, f.y, 1.f, f.height }, util::WithA(util::Shade(fill, 1.6f), 0.9f));
		}
		DrawRectangleLinesEx(Rectangle{ r.x - 1, r.y - 1, r.width + 2, r.height + 2 }, 1.f, Color{ 0, 0, 0, 170 });
		if (!label.empty())
		{
			TextStyle st;
			st.size = labelSize;
			st.color = Color{ 244, 240, 232, 255 };
			st.outline = 1.2f;
			st.outlineColor = Color{ 0, 0, 0, 190 };
			st.face = Face::Number;
			Fonts().DrawCentered(label, Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f }, st);
		}
	}

	void SlotFrame(Rectangle r, bool hover, bool selected, Color accent)
	{
		Plate(r, theme::kSlot, BLANK, 2.f, 0.f);
		DrawRectangleGradientV(static_cast<int>(std::round(r.x + 1)), static_cast<int>(std::round(r.y + 1)), static_cast<int>(std::round(r.width - 2)), static_cast<int>(r.height * 0.35f), Color{ 0, 0, 0, 80 }, Color{ 0, 0, 0, 0 });
		const bool tinted = !SameColor(accent, theme::kBorder);
		if (hover)
			Plate(r, Color{ 255, 255, 255, 14 }, BLANK, 2.f, 0.f);
		const Color border = selected ? theme::kDawn : (hover ? theme::kTextDim : (tinted ? util::MulA(accent, 0.7f) : theme::kLine));
		Plate(r, BLANK, border, 2.f, selected ? 2.f : 1.f);
		// 희귀도는 테두리 전체를 빛내지 않고 바닥 2px 띠로만 알린다
		if (tinted && !selected)
			DrawRectangleRec(Rectangle{ std::round(r.x + 3), std::round(r.y + r.height - 4), std::round(r.width - 6), 2.f }, util::MulA(accent, 0.95f));
	}

	void Divider(float x0, float x1, float y)
	{
		Rule(x0, x1, y, theme::kLine);
	}

	void Tooltip(std::vector<TooltipLine> lines, uint16_t iconItemId, Color accent)
	{
		g_tip = std::move(lines);
		g_tipIcon = iconItemId;
		g_tipAccent = accent;
		g_hasTip = true;
		g_hasCmp = false;
	}

	void TooltipCompare(std::vector<TooltipLine> lines, uint16_t iconItemId, Color accent, std::string_view header)
	{
		g_cmp = std::move(lines);
		g_cmpIcon = iconItemId;
		g_cmpAccent = accent;
		g_cmpHeader = std::string(header);
		g_hasCmp = true;
	}

	// ── 드래그 ──
	void BeginDrag(const DragPayload& p)
	{
		g_drag = p;
		g_dragging = true;
		g_dragReleased = false;
	}
	bool Dragging() { return g_dragging; }
	const DragPayload& Drag() { return g_drag; }
	bool DragReleased() { return g_dragReleased; }
	void EndDrag() { g_dragging = false; g_dragReleased = false; g_drag = DragPayload{}; }
	void SetDragIconDrawer(std::function<void(const DragPayload&, Vector2)> drawer) { g_dragDrawer = std::move(drawer); }

	// ── 입력칸 ──
	void Focus(TextField* f) { g_focus = f; if (f) f->blink = 0.f; }
	TextField* Focused() { return g_focus; }
	bool TextFocused() { return g_focus != nullptr; }

	void CommitComposition(TextField& f)
	{
		const std::string comp = ime::Composition();
		if (!comp.empty())
		{
			if (f.text.size() + comp.size() <= f.maxBytes)
				f.text += comp;
			ime::Cancel();
		}
	}

	bool TypedThisFrame(int codepoint)
	{
		for (int cp : g_chars)
			if (cp == codepoint) return true;
		return false;
	}

	bool InputText(Rectangle r, TextField& f, std::string_view placeholder, bool autofocus)
	{
		const bool hover = Hovered(r);
		if (Pressed(r))
		{
			Focus(&f);
			ConsumeMouse();
		}
		if (autofocus && g_focus == nullptr)
			Focus(&f);
		const bool focused = g_focus == &f;
		if (focused)
			g_focusTouched = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ? Contains(r, g_mouse) : true;

		bool submitted = false;
		if (focused)
		{
			f.blink += GetFrameTime();
			for (int cp : g_chars)
			{
				if (cp < 32) continue;
				if (f.asciiOnly && (cp > 126 || !(std::isalnum(cp) || cp == '_' || cp == '-' || cp == '.' || cp == ':'))) continue;
				std::string add;
				AppendUtf8(add, cp);
				if (f.text.size() + add.size() <= f.maxBytes)
				{
					f.text += add;
					f.blink = 0.f;
				}
			}
			const std::string comp = ime::Composition();
			if (comp.empty() && (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)))
			{
				PopUtf8(f.text);
				f.blink = 0.f;
			}
			if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V))
			{
				const char* clip = GetClipboardText();
				if (clip)
				{
					std::vector<int> cps;
					DecodeUtf8(clip, cps);
					for (int cp : cps)
					{
						if (cp < 32) continue;
						std::string add;
						AppendUtf8(add, cp);
						if (f.text.size() + add.size() > f.maxBytes) break;
						f.text += add;
					}
				}
			}
			if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
			{
				CommitComposition(f);
				submitted = true;
			}
		}

		// 그리기
		Plate(r, Color{ 12, 15, 18, 235 }, focused ? theme::kDawn : (hover ? theme::kTextDim : theme::kLineStrong), 3.f, 1.f);
		const float size = std::min(18.f, r.height - 10.f);
		const float ty = r.y + (r.height - size) * 0.5f - 1.f;
		std::string shown = f.password ? std::string(f.text.size(), '*') : f.text;
		const std::string comp = focused ? ime::Composition() : std::string();
		BeginScissorMode(static_cast<int>(r.x + 4), static_cast<int>(r.y), static_cast<int>(r.width - 8), static_cast<int>(r.height));
		const float textW = Fonts().Measure(shown, size).x;
		const float compW = comp.empty() ? 0.f : Fonts().Measure(comp, size).x;
		const float avail = r.width - 20.f;
		const float scroll = std::max(0.f, textW + compW - avail);
		const float x0 = r.x + 10.f - scroll;
		if (shown.empty() && comp.empty() && !focused)
			Text(placeholder, Vector2{ r.x + 10.f, ty }, size, theme::kTextDim);
		else
		{
			Text(shown, Vector2{ x0, ty }, size, theme::kText);
			if (!comp.empty())
			{
				// 조합 중인 글자: 밑줄 + 강조색
				Text(comp, Vector2{ x0 + textW, ty }, size, theme::kDawnHi);
				DrawLineEx(Vector2{ x0 + textW, ty + size + 1 }, Vector2{ x0 + textW + compW, ty + size + 1 }, 1.5f, theme::kDawnHi);
			}
			if (focused && std::fmod(f.blink, 1.f) < 0.55f)
			{
				const float cx = x0 + textW + compW + 1.f;
				DrawLineEx(Vector2{ cx, ty + 1 }, Vector2{ cx, ty + size + 1 }, 1.5f, theme::kBorderHi);
			}
		}
		EndScissorMode();
		if (focused)
			ime::SetCaret(static_cast<int>(x0 + textW + compW), static_cast<int>(ty));
		Block(r);
		return submitted;
	}
}
