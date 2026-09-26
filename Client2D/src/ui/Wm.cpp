#include "ui/Wm.h"
#include "ui/Ui.h"
#include "ui/Font.h"
#include "audio/Sfx.h"
#include "core/Util.h"

#include "rlgl.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <unordered_map>

namespace wm
{
	namespace
	{
		constexpr float kSnap = 8.f;          // 자석 거리(논리 px)
		constexpr float kEdge = 6.f;          // 크기 조절 띠(창 안쪽)
		constexpr float kTitleH = 38.f;       // 제목줄 높이(ui::Window)
		constexpr float kCloseW = 44.f;       // 제목줄 오른쪽 닫기 단추 자리(끌기에서 뺀다)
		constexpr double kOpenDur = 0.14, kCloseDur = 0.12;
		constexpr float kHudMinK = 0.8f, kHudMaxK = 1.3f;

		enum Edge { L = 1, R = 2, T = 4, B = 8 };

		struct St
		{
			std::string id;
			int layer = 0;
			uint32_t flags = 0;
			bool hud = false;
			std::string label;
			// 배치(저장)
			Vector2 delta{ 0, 0 };     // 기본 자리에서 옮긴 거리(논리 px)
			Vector2 size{ 0, 0 };      // 사용자가 바꾼 크기(0 = 기본)
			float k = 1.f;             // HUD 배율
			bool locked = false;
			// 실행 중
			int z = 0;
			uint64_t frame = 0;        // 마지막으로 그린 프레임
			Rectangle rect{};          // 그 프레임의 사각형(논리)
			Rectangle natural{};       // HUD 자연 사각형
			bool open = false, closing = false;
			double openAt = -10.0, closeAt = -10.0;
		};

		struct DragState
		{
			St* st = nullptr;
			int mode = 0;              // 1 끌기 대기(3px 움직이면 2) · 2 옮기는 중 · 3 크기 · 4 HUD 옮기기
			int edges = 0;
			bool handle = false;       // 손잡이(DragHandle)로 시작
			Vector2 pressMouse{};
			Rectangle startRect{};
			Vector2 startDelta{};
		};

		std::vector<std::unique_ptr<St>> g_all;
		std::unordered_map<std::string, St*> g_map;
		uint64_t g_frame = 1;
		double g_now = 0.0;
		Vector2 g_logical{ 1280, 720 };
		bool g_modal = false, g_edit = false, g_dirty = false;
		int g_topZ = 0;
		St* g_cur = nullptr;
		DragState g_drag;
		int g_cursor = 0;
		uint64_t g_dragEndFrame = 0;
		std::vector<bool> g_inertStack;

		struct Rt { RenderTexture2D rt{}; int w = 0, h = 0; };
		std::array<Rt, 2> g_rt;

		St& Get(const std::string& id)
		{
			if (auto it = g_map.find(id); it != g_map.end())
				return *it->second;
			auto st = std::make_unique<St>();
			st->id = id;
			st->layer = static_cast<int>(g_all.size()) + 1;
			st->z = ++g_topZ;
			St* p = st.get();
			g_all.push_back(std::move(st));
			g_map[id] = p;
			return *p;
		}
		St* Find(const char* id)
		{
			if (!id) return nullptr;
			auto it = g_map.find(id);
			return it == g_map.end() ? nullptr : it->second;
		}

		Vector2 MouseL()
		{
			const Vector2 m = ui::MouseScreen();
			const float s = ui::UiScale();
			return Vector2{ m.x / s, m.y / s };
		}
		bool Contains(Rectangle r, Vector2 p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }

		Rectangle ClampToScreen(Rectangle r)
		{
			if (r.width >= g_logical.x) r.x = 0.f;
			else r.x = std::clamp(r.x, 0.f, g_logical.x - r.width);
			if (r.height >= g_logical.y) r.y = 0.f;
			else r.y = std::clamp(r.y, 0.f, g_logical.y - r.height);
			return r;
		}

		// 자석: 움직이는 변(lo/hi)마다 가장 가까운 목표(화면 가장자리 · 다른 사각형의 변)로 8px 안이면 붙인다
		struct Targets { std::vector<float> xs, ys; };
		Targets Collect(const St* self)
		{
			Targets t;
			t.xs = { 0.f, g_logical.x };
			t.ys = { 0.f, g_logical.y };
			for (const auto& s : g_all)
			{
				if (s.get() == self || s->closing) continue;
				if (s->frame + 1 < g_frame) continue;   // 지난 프레임 이후 안 그린 것
				const Rectangle& r = s->rect;
				if (r.width <= 0.f) continue;
				t.xs.push_back(r.x); t.xs.push_back(r.x + r.width);
				t.ys.push_back(r.y); t.ys.push_back(r.y + r.height);
			}
			return t;
		}
		float BestShift(float edge, const std::vector<float>& ts, float best)
		{
			for (float t : ts)
			{
				const float d = t - edge;
				if (std::fabs(d) <= kSnap && std::fabs(d) < std::fabs(best)) best = d;
			}
			return best;
		}
		Rectangle SnapMove(Rectangle r, const St* self)
		{
			const Targets t = Collect(self);
			float sx = 1e9f, sy = 1e9f;
			sx = BestShift(r.x, t.xs, sx);
			sx = BestShift(r.x + r.width, t.xs, sx);
			sy = BestShift(r.y, t.ys, sy);
			sy = BestShift(r.y + r.height, t.ys, sy);
			if (sx < 1e8f) r.x += sx;
			if (sy < 1e8f) r.y += sy;
			return r;
		}

		int HitEdges(Rectangle r, Vector2 m)
		{
			if (!Contains(r, m)) return 0;
			int e = 0;
			if (m.x < r.x + kEdge) e |= L; else if (m.x > r.x + r.width - kEdge) e |= R;
			if (m.y < r.y + 4.f) e |= T; else if (m.y > r.y + r.height - kEdge) e |= B;
			if (e & (L | R)) { if (m.y < r.y + 12.f) e |= T; else if (m.y > r.y + r.height - 12.f) e |= B; }
			if (e & (T | B)) { if (m.x < r.x + 12.f) e |= L; else if (m.x > r.x + r.width - 12.f) e |= R; }
			return e;
		}
		int EdgeCursor(int e)
		{
			if ((e & (L | R)) && (e & (T | B))) return ((e & L) && (e & T)) || ((e & R) && (e & B)) ? 3 : 4;
			if (e & (L | R)) return 1;
			if (e & (T | B)) return 2;
			return 0;
		}

		// HUD 덩어리: 자연 사각형 → 옮기고 배율을 건 자리. 고정점 = 자연 사각형의 가운데가 있는 화면 3분할 칸의 모서리 · 변 가운데
		void HudPlace(const St& st, Vector2 delta, float k, Rectangle& placed, Vector2& off)
		{
			const Rectangle n = st.natural;
			const float cx = n.x + n.width * 0.5f, cy = n.y + n.height * 0.5f;
			const float ax = cx < g_logical.x / 3.f ? 0.f : (cx > g_logical.x * 2.f / 3.f ? 1.f : 0.5f);
			const float ay = cy < g_logical.y / 3.f ? 0.f : (cy > g_logical.y * 2.f / 3.f ? 1.f : 0.5f);
			const Vector2 an{ n.x + n.width * ax, n.y + n.height * ay };
			const Vector2 a{ an.x + delta.x, an.y + delta.y };
			placed = Rectangle{ a.x - n.width * k * ax, a.y - n.height * k * ay, n.width * k, n.height * k };
			placed = ClampToScreen(placed);
			if (k == 1.f) { placed.x = std::round(placed.x); placed.y = std::round(placed.y); }
			off = Vector2{ placed.x - n.x * k, placed.y - n.y * k };
		}

		RenderTexture2D* AcquireRt(int idx)
		{
			if (idx < 0 || idx >= static_cast<int>(g_rt.size())) return nullptr;
			Rt& r = g_rt[static_cast<size_t>(idx)];
			const int w = GetScreenWidth(), h = GetScreenHeight();
			if (r.rt.id == 0 || r.w != w || r.h != h)
			{
				if (r.rt.id != 0) UnloadRenderTexture(r.rt);
				r.rt = LoadRenderTexture(w, h);
				r.w = w; r.h = h;
				if (r.rt.id == 0) return nullptr;
				SetTextureFilter(r.rt.texture, TEXTURE_FILTER_BILINEAR);
			}
			return &r.rt;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	void BeginFrame(Vector2 logical, double now, bool modal, bool edit)
	{
		++g_frame;
		g_now = now;
		g_logical = logical;
		g_modal = modal;
		g_edit = edit;
		g_cursor = 0;
		g_cur = nullptr;
		g_inertStack.clear();

		// 끄는 중인 창이 사라졌으면(닫힘) 끌기를 끝낸다
		if (g_drag.st && g_drag.st->frame + 1 < g_frame)
			g_drag = DragState{};

		const Vector2 m = MouseL();
		int owner = 0;
		if (g_drag.st && !g_drag.st->hud && g_drag.mode >= 2)
			owner = g_drag.st->layer;
		else if (modal)
			owner = kModalLayer;
		else
		{
			const St* best = nullptr;
			for (const auto& s : g_all)
			{
				if (s->hud || s->closing || s->frame + 1 != g_frame) continue;
				if (!Contains(s->rect, m)) continue;
				if (!best || s->z > best->z) best = s.get();
			}
			if (best) owner = best->layer;
		}
		ui::SetMouseOwner(owner);
		if (owner > 0 && owner != kModalLayer && (ui::BtnPressed(MOUSE_BUTTON_LEFT) || ui::BtnPressed(MOUSE_BUTTON_RIGHT)))
			for (auto& s : g_all)
				if (s->layer == owner && s->z != g_topZ) { s->z = ++g_topZ; break; }
	}

	void DrawWindows(std::vector<Entry>& entries)
	{
		// 열림 변화: 새로 열린 창은 맨 위로 · 열기 애니메이션, 닫힌 창은 0.12초 유령
		for (auto& e : entries)
		{
			St& st = Get(e.id);
			if (e.open && !st.open)
			{
				st.open = true;
				st.closing = false;
				st.openAt = g_now;
				st.z = ++g_topZ;
			}
			else if (!e.open && st.open)
			{
				st.open = false;
				st.closing = e.closeAnim && !(st.flags & kNoAnim) && st.frame + 1 >= g_frame && st.rect.width > 0.f;
				st.closeAt = g_now;
			}
			if (st.closing && g_now - st.closeAt >= kCloseDur)
				st.closing = false;
		}
		std::vector<Entry*> order;
		order.reserve(entries.size());
		for (auto& e : entries)
		{
			St& st = Get(e.id);
			if (e.open || st.closing) order.push_back(&e);
		}
		std::stable_sort(order.begin(), order.end(), [](const Entry* a, const Entry* b) { return Get(a->id).z < Get(b->id).z; });

		int rtUsed = 0;
		for (Entry* e : order)
		{
			St& st = Get(e->id);
			const bool closing = !e->open && st.closing;
			const double tOpen = g_now - st.openAt;
			const bool opening = !closing && tOpen >= 0.0 && tOpen < kOpenDur && !(st.flags & kNoAnim);
			RenderTexture2D* rt = (opening || closing) ? AcquireRt(rtUsed) : nullptr;
			if (rt) ++rtUsed;

			if (rt)
			{
				BeginTextureMode(*rt);
				ClearBackground(BLANK);
				ui::ReapplyXform();
			}
			g_cur = &st;
			ui::SetInert(closing);
			e->draw();
			ui::SetInert(false);
			ui::SetLayer(0);
			g_cur = nullptr;
			if (!rt)
				continue;
			EndTextureMode();
			ui::ReapplyXform();

			// 합성: 창 가운데를 축으로 배율 · 투명도(그림자 · 모서리 장식까지 12px 여유)
			float u = static_cast<float>(closing ? (g_now - st.closeAt) / kCloseDur : tOpen / kOpenDur);
			u = util::Clamp01(u);
			float scale, alpha;
			if (closing) { scale = 1.f - 0.03f * u * u; alpha = 1.f - u; }
			else { const float e3 = 1.f - (1.f - u) * (1.f - u) * (1.f - u); scale = 0.96f + 0.04f * e3; alpha = std::min(1.f, u * 1.7f); }
			const float S = ui::UiScale();
			const float m = 12.f * S;
			Rectangle src{ st.rect.x * S - m, st.rect.y * S - m, st.rect.width * S + 2.f * m, st.rect.height * S + 2.f * m };
			src.x = std::max(0.f, src.x); src.y = std::max(0.f, src.y);
			src.width = std::min(src.width, static_cast<float>(rt->texture.width) - src.x);
			src.height = std::min(src.height, static_cast<float>(rt->texture.height) - src.y);
			if (src.width <= 0.f || src.height <= 0.f) continue;
			const Vector2 c{ src.x + src.width * 0.5f, src.y + src.height * 0.5f };
			const Rectangle dst{ c.x - src.width * scale * 0.5f, c.y - src.height * scale * 0.5f, src.width * scale, src.height * scale };
			const Rectangle flip{ src.x, static_cast<float>(rt->texture.height) - src.y - src.height, src.width, -src.height };
			ui::SuspendXform();
			DrawTexturePro(rt->texture, flip, dst, Vector2{ 0, 0 }, 0.f, util::WithA(WHITE, alpha));
			ui::ResumeXform();
		}
	}

	Rectangle Window(const char* id, Rectangle def, uint32_t flags, Vector2 minSize, Vector2 maxSize)
	{
		St& st = Get(id);
		st.flags = flags;
		st.hud = false;
		st.frame = g_frame;
		g_cur = &st;
		const Vector2 minS{ minSize.x > 0.f ? minSize.x : 120.f, minSize.y > 0.f ? minSize.y : 80.f };
		const Vector2 maxS{ maxSize.x > 0.f ? maxSize.x : g_logical.x, maxSize.y > 0.f ? maxSize.y : g_logical.y };
		Rectangle r = def;
		if ((flags & kResize) && st.size.x > 0.f && st.size.y > 0.f)
		{
			r.width = st.size.x;
			r.height = st.size.y;
		}
		if (flags & kResize)
		{
			r.width = std::clamp(r.width, minS.x, std::max(minS.x, maxS.x));
			r.height = std::clamp(r.height, minS.y, std::max(minS.y, maxS.y));
		}
		r.x = def.x + st.delta.x;
		r.y = def.y + st.delta.y;

		const Vector2 m = MouseL();
		if (g_drag.st == &st && g_drag.mode != 4)
		{
			if (ui::BtnDown(MOUSE_BUTTON_LEFT))
			{
				const Vector2 d{ m.x - g_drag.pressMouse.x, m.y - g_drag.pressMouse.y };
				if (g_drag.mode == 1 && std::fabs(d.x) + std::fabs(d.y) > 3.f)
					g_drag.mode = 2;
				if (g_drag.mode == 2)
				{
					Rectangle nr{ g_drag.startRect.x + d.x, g_drag.startRect.y + d.y, r.width, r.height };
					nr = ClampToScreen(SnapMove(nr, &st));
					st.delta = Vector2{ nr.x - def.x, nr.y - def.y };
					r = nr;
					g_cursor = 5;
				}
				else if (g_drag.mode == 3)
				{
					const Rectangle s = g_drag.startRect;
					float x0 = s.x, x1 = s.x + s.width, y0 = s.y, y1 = s.y + s.height;
					const Targets t = Collect(&st);
					auto snapEdge = [&](float v, const std::vector<float>& ts) { const float sh = BestShift(v, ts, 1e9f); return sh < 1e8f ? v + sh : v; };
					if (g_drag.edges & L) { x0 = snapEdge(std::max(0.f, s.x + d.x), t.xs); x0 = std::clamp(x0, x1 - maxS.x, x1 - minS.x); }
					if (g_drag.edges & R) { x1 = snapEdge(std::min(g_logical.x, x1 + d.x), t.xs); x1 = std::clamp(x1, x0 + minS.x, x0 + maxS.x); }
					if (g_drag.edges & T) { y0 = snapEdge(std::max(0.f, s.y + d.y), t.ys); y0 = std::clamp(y0, y1 - maxS.y, y1 - minS.y); }
					if (g_drag.edges & B) { y1 = snapEdge(std::min(g_logical.y, y1 + d.y), t.ys); y1 = std::clamp(y1, y0 + minS.y, y0 + maxS.y); }
					r = Rectangle{ std::round(x0), std::round(y0), std::round(x1 - x0), std::round(y1 - y0) };
					st.size = Vector2{ r.width, r.height };
					st.delta = Vector2{ r.x - def.x, r.y - def.y };
					g_cursor = EdgeCursor(g_drag.edges);
				}
			}
			else
			{
				if (g_drag.mode >= 2)
				{
					g_dirty = true;
					g_dragEndFrame = g_frame;
				}
				g_drag = DragState{};
			}
		}
		r = ClampToScreen(r);

		// 새로 누름: 가장자리 → 크기, 제목줄 → 끌기(3px 움직여야 시작 — 제목줄 클릭이 창을 흔들지 않게)
		const bool mine = !ui::Inert() && ui::MouseOwner() == st.layer && !st.closing;
		if (mine && !g_drag.st && !st.locked)
		{
			const int e = (flags & kResize) ? HitEdges(r, m) : 0;
			if (e)
			{
				if (!g_cursor) g_cursor = EdgeCursor(e);
				if (ui::BtnPressed(MOUSE_BUTTON_LEFT))
					g_drag = DragState{ &st, 3, e, false, m, r, st.delta };
			}
			else if ((flags & kMove) && ui::BtnPressed(MOUSE_BUTTON_LEFT)
				&& Contains(Rectangle{ r.x, r.y, r.width - kCloseW, kTitleH }, m))
				g_drag = DragState{ &st, 1, 0, false, m, r, st.delta };
		}
		st.rect = r;
		ui::SetLayer(st.layer);
		return r;
	}

	void DragHandle(Rectangle r)
	{
		St* st = g_cur;
		if (!st || st->locked || ui::Inert() || g_drag.st || ui::MouseOwner() != st->layer || st->closing) return;
		const Vector2 m = MouseL();
		if (ui::BtnPressed(MOUSE_BUTTON_LEFT) && Contains(r, m))
			g_drag = DragState{ st, 1, 0, true, m, st->rect, st->delta };
	}

	bool HandlePending() { return g_drag.st && g_drag.st == g_cur && g_drag.mode == 1; }

	void ModalBegin() { ui::SetLayer(kModalLayer); }
	void ModalEnd() { ui::SetLayer(0); }

	// ── HUD ────────────────────────────────────────────────────────────────────
	void BeginHud(const char* id, const char* label, Rectangle natural)
	{
		St& st = Get(id);
		st.hud = true;
		st.label = label ? label : id;
		st.natural = natural;
		st.frame = g_frame;
		Rectangle placed{};
		Vector2 off{};
		HudPlace(st, st.delta, st.k, placed, off);
		st.rect = placed;
		ui::PushXform(off, st.k);
		g_inertStack.push_back(ui::Inert());
		if (g_edit) ui::SetInert(true);
	}

	void EndHud()
	{
		ui::PopXform();
		if (!g_inertStack.empty()) { ui::SetInert(g_inertStack.back()); g_inertStack.pop_back(); }
	}

	Rectangle HudRect(const char* id, Rectangle natural)
	{
		St* st = Find(id);
		if (!st) return natural;
		St tmp = *st;
		tmp.natural = natural;
		Rectangle placed{};
		Vector2 off{};
		HudPlace(tmp, tmp.delta, tmp.k, placed, off);
		return placed;
	}

	void EditBackdrop()
	{
		DrawRectangleRec(Rectangle{ 0, 0, g_logical.x, g_logical.y }, util::WithA(ui::theme::kInk, 0.42f));
	}

	bool EditOverlay()
	{
		const Vector2 m = MouseL();
		// 지금 프레임에 놓인 덩어리
		std::vector<St*> chunks;
		for (auto& s : g_all)
			if (s->hud && s->frame == g_frame) chunks.push_back(s.get());

		// 마우스 아래 덩어리(겹치면 작은 것)
		St* hover = nullptr;
		if (!g_drag.st)
			for (St* s : chunks)
				if (Contains(s->rect, m) && (!hover || s->rect.width * s->rect.height < hover->rect.width * hover->rect.height))
					hover = s;

		// 끌기 · 배율 · 되돌리기
		const float wheel = ui::Wheel();
		if (hover && wheel != 0.f)
		{
			hover->k = std::clamp(std::round((hover->k + (wheel > 0.f ? 0.05f : -0.05f)) * 20.f) / 20.f, kHudMinK, kHudMaxK);
			g_dirty = true;
			sfx::Play(sfx::Id::Hover, 0.3f);
		}
		if (hover && ui::BtnPressed(MOUSE_BUTTON_RIGHT))
		{
			hover->delta = Vector2{ 0, 0 };
			hover->k = 1.f;
			g_dirty = true;
			sfx::Play(sfx::Id::Close, 0.4f);
		}
		const Rectangle bar{ std::round(g_logical.x * 0.5f - 300.f), 10.f, 600.f, 46.f };
		const bool overBar = Contains(bar, m);
		if (hover && !overBar && ui::BtnPressed(MOUSE_BUTTON_LEFT))
			g_drag = DragState{ hover, 4, 0, false, m, hover->rect, hover->delta };
		if (g_drag.st && g_drag.mode == 4)
		{
			St& st = *g_drag.st;
			if (ui::BtnDown(MOUSE_BUTTON_LEFT))
			{
				Vector2 nd{ g_drag.startDelta.x + (m.x - g_drag.pressMouse.x), g_drag.startDelta.y + (m.y - g_drag.pressMouse.y) };
				Rectangle placed{};
				Vector2 off{};
				HudPlace(st, nd, st.k, placed, off);
				// 자석: 놓일 사각형의 변을 화면 · 다른 덩어리 · 창에 맞추고 그만큼 거리를 고친다
				const Rectangle snapped = SnapMove(placed, &st);
				nd.x += snapped.x - placed.x;
				nd.y += snapped.y - placed.y;
				st.delta = Vector2{ std::round(nd.x), std::round(nd.y) };
				g_cursor = 5;
			}
			else
			{
				g_dirty = true;
				g_drag = DragState{};
			}
		}

		// 그리기: 테두리 · 이름표(배율)
		for (St* s : chunks)
		{
			const bool hot = s == hover || (g_drag.st == s);
			const Rectangle r = s->rect;
			DrawRectangleRec(r, util::WithA(ui::theme::kDawn, hot ? 0.10f : 0.04f));
			ui::Plate(Rectangle{ r.x - 1.f, r.y - 1.f, r.width + 2.f, r.height + 2.f }, BLANK, util::WithA(ui::theme::kDawn, hot ? 0.95f : 0.55f), 3.f, hot ? 2.f : 1.f);
			char buf[96];
			std::snprintf(buf, sizeof(buf), "%s  %d%%", s->label.c_str(), static_cast<int>(std::lround(s->k * 100.f)));
			ui::TextStyle ts; ts.size = ui::type::kCaption; ts.color = hot ? ui::theme::kDawnHi : ui::theme::kText; ts.face = ui::Face::Strong;
			const Vector2 tm = ui::Fonts().Measure(buf, ts.size, 0.f, ui::Face::Strong);
			const Rectangle lb{ r.x + 4.f, r.y + 4.f, std::round(tm.x + 12.f), 18.f };
			ui::Plate(lb, Color{ 16, 18, 22, 235 }, util::WithA(ui::theme::kDawn, 0.8f), 2.f, 1.f);
			ui::Fonts().Draw(buf, Vector2{ lb.x + 6.f, lb.y + 2.f }, ts);
		}

		// 위쪽 띠
		ui::Panel(bar, 0.97f, false);
		ui::Text("화면 배치", Vector2{ bar.x + 16.f, bar.y + 8.f }, ui::type::kHeading, ui::theme::kDawnHi);
		ui::Text("끌어서 옮기기 · 휠로 크기(80~130%) · 오른쪽 클릭 되돌리기", Vector2{ bar.x + 16.f, bar.y + 27.f }, ui::type::kCaption, ui::theme::kTextDim);
		bool done = false;
		if (ui::Button(Rectangle{ bar.x + bar.width - 204.f, bar.y + 9.f, 108.f, 28.f }, "기본 배치로", ui::ButtonKind::Normal, true, 14.f))
		{
			ResetHud();
			sfx::Play(sfx::Id::Open, 0.5f);
		}
		if (ui::Button(Rectangle{ bar.x + bar.width - 88.f, bar.y + 9.f, 76.f, 28.f }, "완료", ui::ButtonKind::Primary, true, 14.f))
			done = true;
		return done;
	}

	// ── 조회 ──────────────────────────────────────────────────────────────────
	const char* TopOpen()
	{
		const St* best = nullptr;
		for (const auto& s : g_all)
		{
			if (s->hud || !s->open || s->closing || (s->flags & kPinned)) continue;
			if (s->frame + 2 < g_frame) continue;
			if (!best || s->z > best->z) best = s.get();
		}
		return best ? best->id.c_str() : nullptr;
	}

	void Raise(const char* id)
	{
		if (St* s = Find(id)) s->z = ++g_topZ;
	}

	bool Busy() { return g_drag.st != nullptr && g_drag.mode >= 2; }
	int CursorHint() { return g_cursor; }
	bool Locked(const char* id) { const St* s = Find(id); return s && s->locked; }
	void SetLocked(const char* id, bool on)
	{
		St& s = Get(id);
		if (s.locked != on) { s.locked = on; g_dirty = true; }
	}
	Rectangle LastRect(const char* id)
	{
		const St* s = Find(id);
		return s ? s->rect : Rectangle{ 0, 0, 0, 0 };
	}
	bool JustDragged() { return g_dragEndFrame == g_frame; }

	// ── 저장 ──────────────────────────────────────────────────────────────────
	void ToJson(json::Value& root)
	{
		json::Value win = json::Value::Obj(), hud = json::Value::Obj();
		for (const auto& s : g_all)
		{
			json::Value o = json::Value::Obj();
			o.Set("dx", json::Value::Num(std::round(s->delta.x)));
			o.Set("dy", json::Value::Num(std::round(s->delta.y)));
			if (s->hud)
			{
				o.Set("k", json::Value::Num(s->k));
				if (s->delta.x != 0.f || s->delta.y != 0.f || s->k != 1.f) hud.Set(s->id, std::move(o));
			}
			else
			{
				if (s->size.x > 0.f) { o.Set("w", json::Value::Num(std::round(s->size.x))); o.Set("h", json::Value::Num(std::round(s->size.y))); }
				if (s->locked) o.Set("lock", json::Value::Bool(true));
				if (s->delta.x != 0.f || s->delta.y != 0.f || s->size.x > 0.f || s->locked) win.Set(s->id, std::move(o));
			}
		}
		root.Set("win", std::move(win));
		root.Set("hud", std::move(hud));
	}

	void FromJson(const json::Value& root)
	{
		if (const json::Value* win = root.Get("win"); win && win->IsObject())
			for (const auto& [id, o] : win->o)
			{
				St& s = Get(id);
				s.hud = false;
				s.delta = Vector2{ static_cast<float>(o.NumOr("dx", 0.0)), static_cast<float>(o.NumOr("dy", 0.0)) };
				s.size = Vector2{ static_cast<float>(o.NumOr("w", 0.0)), static_cast<float>(o.NumOr("h", 0.0)) };
				s.locked = o.BoolOr("lock", false);
			}
		if (const json::Value* hud = root.Get("hud"); hud && hud->IsObject())
			for (const auto& [id, o] : hud->o)
			{
				St& s = Get(id);
				s.hud = true;
				s.delta = Vector2{ static_cast<float>(o.NumOr("dx", 0.0)), static_cast<float>(o.NumOr("dy", 0.0)) };
				s.k = std::clamp(static_cast<float>(o.NumOr("k", 1.0)), kHudMinK, kHudMaxK);
			}
		g_dirty = false;
	}

	void ResetWindows()
	{
		for (auto& s : g_all)
			if (!s->hud) { s->delta = Vector2{ 0, 0 }; s->size = Vector2{ 0, 0 }; }
		g_dirty = true;
	}

	void ResetHud()
	{
		for (auto& s : g_all)
			if (s->hud) { s->delta = Vector2{ 0, 0 }; s->k = 1.f; }
		g_dirty = true;
	}

	bool Dirty() { return g_dirty; }
	void ClearDirty() { g_dirty = false; }
	void MarkDirty() { g_dirty = true; }
}
