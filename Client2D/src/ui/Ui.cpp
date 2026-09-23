#include "ui/Ui.h"
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
		bool g_hasTip = false;

		DragPayload g_drag{};
		bool g_dragging = false;
		bool g_dragReleased = false;
		std::function<void(const DragPayload&, Vector2)> g_dragDrawer;

		TextField* g_focus = nullptr;
		bool g_focusTouched = false;
		std::vector<int> g_chars;

		Rectangle g_lastHoverButton{};

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
		g_mouse = GetMousePosition();
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

		// 툴팁
		if (g_hasTip && !g_dragging)
		{
			float w = 0.f, h = 12.f;
			for (const auto& l : g_tip)
			{
				const Vector2 m = Fonts().Measure(l.text, l.size);
				w = std::max(w, m.x);
				h += l.size + 5.f;
			}
			const float iconW = g_tipIcon ? 52.f : 0.f;
			w += 24.f + iconW;
			h = std::max(h + 4.f, g_tipIcon ? 64.f : 0.f);
			float x = g_mouse.x + 18.f, y = g_mouse.y + 14.f;
			if (x + w > GetScreenWidth() - 4) x = g_mouse.x - w - 12.f;
			if (y + h > GetScreenHeight() - 4) y = GetScreenHeight() - h - 4.f;
			const Rectangle r{ x, y, w, h };
			DrawRectangleRounded(Rectangle{ r.x + 3, r.y + 4, r.width, r.height }, 0.08f, 6, Color{ 0, 0, 0, 110 });
			Panel(r, 1.f, false);
			if (g_tipIcon)
			{
				SlotFrame(Rectangle{ x + 10, y + 10, 44, 44 }, false, false);
				icons::Draw(g_tipIcon, Rectangle{ x + 12, y + 12, 40, 40 });
			}
			float ty = y + 10.f;
			for (const auto& l : g_tip)
			{
				TextShadow(l.text, Vector2{ x + 12.f + iconW, ty }, l.size, l.color);
				ty += l.size + 5.f;
			}
		}

		// 드래그 아이콘
		if (g_dragging && g_dragDrawer)
			g_dragDrawer(g_drag, g_mouse);
		if (g_dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !g_dragReleased)
			g_dragging = false;
		if (g_dragReleased)
			g_dragging = false;
	}

	Vector2 Mouse() { return g_mouse; }

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

	void Panel(Rectangle r, float alpha, bool fancy)
	{
		const float rr = 6.f / std::max(8.f, std::min(r.width, r.height));
		DrawRectangleRounded(Rectangle{ r.x + 2, r.y + 4, r.width, r.height }, rr, 6, util::MulA(Color{ 0, 0, 0, 120 }, alpha));
		DrawRectangleRounded(r, rr, 6, util::MulA(theme::kPanelBottom, alpha));
		// 윗부분이 살짝 밝은 그라데이션
		BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width), static_cast<int>(r.height));
		Gradient(Rectangle{ r.x + 2, r.y + 2, r.width - 4, std::min(r.height * 0.5f, 60.f) }, util::MulA(Color{ 70, 60, 70, 60 }, alpha), util::MulA(Color{ 70, 60, 70, 0 }, alpha));
		EndScissorMode();
		DrawRectangleRoundedLinesEx(r, rr, 6, 2.f, util::MulA(theme::kBorder, alpha));
		DrawRectangleRoundedLinesEx(Rectangle{ r.x + 3, r.y + 3, r.width - 6, r.height - 6 }, rr, 6, 1.f, util::MulA(Color{ 214, 176, 104, 50 }, alpha));
		if (fancy)
		{
			// 모서리 장식
			const Color c = util::MulA(theme::kBorderHi, alpha);
			const float k = 7.f;
			for (int i = 0; i < 4; ++i)
			{
				const float x = (i % 2) ? r.x + r.width - 3 : r.x + 3;
				const float y = (i / 2) ? r.y + r.height - 3 : r.y + 3;
				DrawPoly(Vector2{ x, y }, 4, k * 0.5f, 45.f, c);
			}
		}
		Block(r);
	}

	void Window(Rectangle r, std::string_view title, bool* open)
	{
		Panel(r);
		// 제목 띠
		Gradient(Rectangle{ r.x + 4, r.y + 4, r.width - 8, 30 }, Color{ 90, 70, 44, 200 }, Color{ 40, 32, 26, 120 });
		DrawLineEx(Vector2{ r.x + 6, r.y + 34 }, Vector2{ r.x + r.width - 6, r.y + 34 }, 1.f, Color{ 214, 176, 104, 120 });
		TextShadow(title, Vector2{ r.x + r.width * 0.5f, r.y + 9 }, 18.f, theme::kTitle, Align::Center);
		if (open)
		{
			const Rectangle close{ r.x + r.width - 32, r.y + 7, 24, 24 };
			if (IconButton(close, "×"))
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
		Color top{ 72, 60, 50, 255 }, bottom{ 40, 32, 28, 255 }, border = theme::kBorder, text = theme::kText;
		switch (kind)
		{
		case ButtonKind::Primary: top = { 150, 110, 40, 255 }; bottom = { 90, 60, 20, 255 }; border = theme::kBorderHi; text = { 255, 240, 200, 255 }; break;
		case ButtonKind::Danger: top = { 140, 50, 40, 255 }; bottom = { 80, 24, 20, 255 }; border = { 220, 110, 90, 255 }; break;
		case ButtonKind::Ghost: top = { 40, 36, 40, 160 }; bottom = { 26, 22, 26, 160 }; break;
		default: break;
		}
		if (!enabled) { top = { 50, 48, 50, 200 }; bottom = { 34, 32, 34, 200 }; border = { 90, 86, 80, 255 }; text = theme::kTextDim; }
		if (hover) { top = util::Shade(top, 1.25f); bottom = util::Shade(bottom, 1.2f); border = util::Shade(border, 1.2f); }
		Rectangle b = r;
		if (down) { b.y += 1.f; top = util::Shade(top, 0.85f); }
		const float rr = 5.f / std::max(8.f, std::min(b.width, b.height));
		DrawRectangleRounded(Rectangle{ b.x, b.y + 2, b.width, b.height }, rr, 6, Color{ 0, 0, 0, 90 });
		DrawRectangleRounded(b, rr, 6, bottom);
		BeginScissorMode(static_cast<int>(b.x), static_cast<int>(b.y), static_cast<int>(b.width), static_cast<int>(b.height * 0.55f));
		DrawRectangleRounded(b, rr, 6, top);
		EndScissorMode();
		DrawRectangleRoundedLinesEx(b, rr, 6, hover ? 2.f : 1.5f, border);
		if (hover)
			DrawRectangleRoundedLinesEx(Rectangle{ b.x - 2, b.y - 2, b.width + 4, b.height + 4 }, rr, 6, 1.f, util::WithA(border, 0.35f));
		TextShadow(label, Vector2{ b.x + b.width * 0.5f, b.y + (b.height - fontSize) * 0.5f - 1 }, fontSize, text, Align::Center);
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
		const bool hover = enabled && Hovered(r);
		DrawRectangleRounded(r, 0.3f, 6, hover ? Color{ 120, 60, 50, 220 } : Color{ 50, 40, 36, 200 });
		DrawRectangleRoundedLinesEx(r, 0.3f, 6, 1.f, theme::kBorder);
		TextShadow(glyph, Vector2{ r.x + r.width * 0.5f, r.y + (r.height - 18.f) * 0.5f - 1 }, 18.f, hover ? WHITE : theme::kText, Align::Center);
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
		st.shadow = std::max(1.f, size * 0.07f);
		st.shadowColor = Color{ 0, 0, 0, static_cast<unsigned char>(color.a * 0.75f) };
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
		DrawRectangleRounded(r, 0.35f, 6, back);
		if (lag > value)
			DrawRectangleRounded(Rectangle{ r.x, r.y, r.width * lag, r.height }, 0.35f, 6, Color{ 255, 240, 220, 200 });
		if (value * r.width >= 2.f)
		{
			const Rectangle f{ r.x, r.y, r.width * value, r.height };
			DrawRectangleRounded(f, 0.35f, 6, fill);
			// 윗면 광택
			DrawRectangleRounded(Rectangle{ f.x + 2, f.y + 1, f.width - 4, f.height * 0.38f }, 0.5f, 6, Color{ 255, 255, 255, 50 });
		}
		DrawRectangleRoundedLinesEx(r, 0.35f, 6, 1.f, Color{ 0, 0, 0, 160 });
		if (!label.empty())
		{
			TextStyle st;
			st.size = labelSize;
			st.color = WHITE;
			st.outline = 1.5f;
			st.outlineColor = Color{ 0, 0, 0, 200 };
			Fonts().DrawCentered(label, Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f + 0.5f }, st);
		}
	}

	void SlotFrame(Rectangle r, bool hover, bool selected, Color accent)
	{
		DrawRectangleRounded(r, 0.14f, 4, theme::kSlot);
		DrawRectangleGradientV(static_cast<int>(r.x + 2), static_cast<int>(r.y + 2), static_cast<int>(r.width - 4), static_cast<int>(r.height * 0.4f), Color{ 0, 0, 0, 90 }, Color{ 0, 0, 0, 0 });
		DrawRectangleRoundedLinesEx(r, 0.14f, 4, selected ? 2.f : 1.f, selected ? theme::kBorderHi : (hover ? util::Shade(accent, 1.3f) : Color{ 90, 74, 52, 255 }));
		if (hover)
			DrawRectangleRounded(r, 0.14f, 4, Color{ 255, 230, 180, 22 });
	}

	void Divider(float x0, float x1, float y)
	{
		DrawLineEx(Vector2{ x0, y }, Vector2{ x1, y }, 1.f, Color{ 214, 176, 104, 90 });
	}

	void Tooltip(std::vector<TooltipLine> lines, uint16_t iconItemId)
	{
		g_tip = std::move(lines);
		g_tipIcon = iconItemId;
		g_hasTip = true;
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
		DrawRectangleRounded(r, 0.2f, 6, Color{ 10, 9, 12, 225 });
		DrawRectangleRoundedLinesEx(r, 0.2f, 6, focused ? 2.f : 1.f, focused ? theme::kBorderHi : (hover ? util::Shade(theme::kBorder, 1.2f) : theme::kBorder));
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
				Text(comp, Vector2{ x0 + textW, ty }, size, Color{ 255, 230, 150, 255 });
				DrawLineEx(Vector2{ x0 + textW, ty + size + 1 }, Vector2{ x0 + textW + compW, ty + size + 1 }, 1.5f, Color{ 255, 230, 150, 255 });
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
