#include "render/Emblem.h"

#include "core/Util.h"

#include <algorithm>
#include <cmath>

using util::Rgb;

namespace emblem
{
	namespace
	{
		constexpr int kShapes = 12;
		constexpr int kColors = 6;

		// 방패 윤곽(단위: 방패 높이 32, 가운데 0,0) — 윗변 · 옆 · 아래 뾰족
		constexpr float kHalfW = 12.5f;

		struct Pen
		{
			Vector2 c;
			float   s;          // 1단위 = s px
			float   alpha;
			[[nodiscard]] Vector2 P(float x, float y) const { return Vector2{ c.x + x * s, c.y + y * s }; }
			[[nodiscard]] Color A(Color col) const { return util::MulA(col, alpha); }
			void Tri(Vector2 a, Vector2 b, Vector2 d, Color col) const
			{
				// raylib은 화면에서 반시계 순서만 채운다 — 방향을 맞춰 준다
				const float cross = (b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x);
				if (cross > 0.f) DrawTriangle(a, d, b, A(col));
				else DrawTriangle(a, b, d, A(col));
			}
			void T(float ax, float ay, float bx, float by, float dx, float dy, Color col) const { Tri(P(ax, ay), P(bx, by), P(dx, dy), col); }
			void Quad(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, Color col) const
			{
				T(ax, ay, bx, by, cx, cy, col);
				T(ax, ay, cx, cy, dx, dy, col);
			}
			void Rect(float x, float y, float w, float h, Color col) const { DrawRectangleRec(Rectangle{ c.x + x * s, c.y + y * s, w * s, h * s }, A(col)); }
			void Circle(float x, float y, float r, Color col) const { DrawCircleV(P(x, y), r * s, A(col)); }
			void Line(float ax, float ay, float bx, float by, float w, Color col) const { DrawLineEx(P(ax, ay), P(bx, by), std::max(1.f, w * s), A(col)); }
			// 볼록 다각형(부채꼴 삼각형)
			void Fan(const Vector2* pts, int n, Color col) const
			{
				for (int i = 1; i + 1 < n; ++i) Tri(P(pts[0].x, pts[0].y), P(pts[i].x, pts[i].y), P(pts[i + 1].x, pts[i + 1].y), col);
			}
		};

		// 방패 모양 점(볼록) — k = 크기 배율(테두리 · 안쪽 판)
		int ShieldPoints(Vector2* out, float k)
		{
			const float w = kHalfW * k, top = -15.f * k, side = 4.f * k, bot = 16.f * k;
			int n = 0;
			out[n++] = Vector2{ 0.f, top };
			out[n++] = Vector2{ w, top };
			out[n++] = Vector2{ w, side };
			// 아래로 둥글게 모여드는 곡선(볼록 유지)
			for (int i = 1; i <= 5; ++i)
			{
				const float t = static_cast<float>(i) / 6.f;
				const float x = w * std::cos(t * util::kPi * 0.5f);
				const float y = side + (bot - side) * std::sin(t * util::kPi * 0.5f);
				out[n++] = Vector2{ x, y };
			}
			out[n++] = Vector2{ 0.f, bot };
			for (int i = 5; i >= 1; --i)
			{
				const float t = static_cast<float>(i) / 6.f;
				const float x = -w * std::cos(t * util::kPi * 0.5f);
				const float y = side + (bot - side) * std::sin(t * util::kPi * 0.5f);
				out[n++] = Vector2{ x, y };
			}
			out[n++] = Vector2{ -w, side };
			out[n++] = Vector2{ -w, top };
			return n;
		}

		void Star(const Pen& p, float cx, float cy, float ro, float ri, Color col)
		{
			Vector2 pts[10];
			for (int i = 0; i < 10; ++i)
			{
				const float a = -util::kPi * 0.5f + static_cast<float>(i) * util::kPi / 5.f;
				const float r = (i % 2 == 0) ? ro : ri;
				pts[i] = Vector2{ cx + std::cos(a) * r, cy + std::sin(a) * r };
			}
			for (int i = 0; i < 10; ++i)
				p.T(cx, cy, pts[i].x, pts[i].y, pts[(i + 1) % 10].x, pts[(i + 1) % 10].y, col);
		}

		void Glyph(const Pen& p, int shape, Color ink, Color hole)
		{
			switch (shape)
			{
			case 0:   // 방패: 안쪽 작은 방패 테두리 + 세로 띠
			{
				Vector2 pts[20];
				const int n = ShieldPoints(pts, 0.5f);
				for (int i = 0; i < n; ++i) pts[i].y -= 1.f;
				p.Fan(pts, n, ink);
				const int m = ShieldPoints(pts, 0.34f);
				for (int i = 0; i < m; ++i) pts[i].y -= 1.2f;
				p.Fan(pts, m, hole);
				p.Rect(-1.1f, -8.f, 2.2f, 14.5f, ink);
				break;
			}
			case 1:   // 종
				p.Circle(0.f, -9.5f, 1.6f, ink);
				p.Circle(0.f, -3.5f, 5.f, ink);
				p.Quad(-5.f, -3.5f, 5.f, -3.5f, 7.8f, 5.f, -7.8f, 5.f, ink);
				p.Rect(-8.6f, 4.6f, 17.2f, 2.f, ink);
				p.Circle(0.f, 8.4f, 1.9f, ink);
				p.Line(-2.5f, -3.f, -4.f, 3.5f, 1.f, hole);
				break;
			case 2:   // 별
				Star(p, 0.f, -0.5f, 9.5f, 3.9f, ink);
				break;
			case 3:   // 늑대(머리)
			{
				p.T(-7.5f, -10.f, -2.5f, -3.5f, -8.2f, -2.f, ink);
				p.T(7.5f, -10.f, 8.2f, -2.f, 2.5f, -3.5f, ink);
				const Vector2 head[] = { { -8.5f, -3.5f }, { 8.5f, -3.5f }, { 6.f, 3.5f }, { 1.6f, 9.5f }, { -1.6f, 9.5f }, { -6.f, 3.5f } };
				p.Fan(head, 6, ink);
				p.T(-2.6f, 1.f, -4.8f, -0.8f, -1.2f, -1.4f, hole);
				p.T(2.6f, 1.f, 1.2f, -1.4f, 4.8f, -0.8f, hole);
				p.T(-1.4f, 7.f, 1.4f, 7.f, 0.f, 8.8f, hole);
				break;
			}
			case 4:   // 매(날개 편 모양)
			{
				const Vector2 body[] = { { 0.f, -6.f }, { 2.6f, 0.f }, { 1.4f, 8.f }, { 0.f, 10.f }, { -1.4f, 8.f }, { -2.6f, 0.f } };
				p.Fan(body, 6, ink);
				p.Quad(-2.f, -2.5f, -11.5f, -8.f, -9.5f, -2.f, -2.f, 3.f, ink);
				p.Quad(2.f, -2.5f, 2.f, 3.f, 9.5f, -2.f, 11.5f, -8.f, ink);
				p.T(-9.5f, -2.f, -11.f, 1.f, -6.f, 0.5f, ink);
				p.T(9.5f, -2.f, 6.f, 0.5f, 11.f, 1.f, ink);
				p.Circle(0.f, -7.f, 2.3f, ink);
				p.T(0.f, -6.f, 1.8f, -5.f, 0.f, -3.8f, ink);
				break;
			}
			case 5:   // 참나무
				p.Rect(-1.7f, 1.5f, 3.4f, 9.f, ink);
				p.T(-1.7f, 10.5f, -4.5f, 10.5f, -1.7f, 7.f, ink);
				p.T(1.7f, 10.5f, 1.7f, 7.f, 4.5f, 10.5f, ink);
				p.Circle(0.f, -5.f, 5.2f, ink);
				p.Circle(-5.f, -1.5f, 4.2f, ink);
				p.Circle(5.f, -1.5f, 4.2f, ink);
				p.Circle(0.f, 0.f, 4.2f, ink);
				p.Circle(-2.2f, -4.2f, 1.1f, hole);
				p.Circle(3.2f, -1.8f, 0.9f, hole);
				break;
			case 6:   // 탑
				p.Rect(-5.f, -4.5f, 10.f, 15.f, ink);
				p.Rect(-6.2f, -9.f, 3.f, 4.8f, ink);
				p.Rect(-1.5f, -9.f, 3.f, 4.8f, ink);
				p.Rect(3.2f, -9.f, 3.f, 4.8f, ink);
				p.Rect(-6.2f, -5.f, 12.4f, 1.6f, ink);
				p.Circle(0.f, 5.5f, 1.8f, hole);
				p.Rect(-1.8f, 5.5f, 3.6f, 5.f, hole);
				p.Rect(-0.8f, -2.f, 1.6f, 2.6f, hole);
				break;
			case 7:   // 열쇠
				p.Circle(0.f, -6.5f, 4.8f, ink);
				p.Circle(0.f, -6.5f, 2.2f, hole);
				p.Rect(-1.2f, -2.2f, 2.4f, 13.f, ink);
				p.Rect(1.f, 5.f, 3.4f, 1.9f, ink);
				p.Rect(1.f, 8.2f, 2.5f, 1.9f, ink);
				break;
			case 8:   // 칼
			{
				const Vector2 blade[] = { { 0.f, -12.5f }, { 1.9f, -9.5f }, { 1.9f, 3.8f }, { -1.9f, 3.8f }, { -1.9f, -9.5f } };
				p.Fan(blade, 5, ink);
				p.Line(0.f, -9.f, 0.f, 2.5f, 0.6f, hole);
				p.Rect(-6.5f, 3.6f, 13.f, 2.2f, ink);
				p.Rect(-1.1f, 5.8f, 2.2f, 4.8f, ink);
				p.Circle(0.f, 11.5f, 1.9f, ink);
				break;
			}
			case 9:   // 달(그믐)
				p.Circle(0.f, 0.f, 8.5f, ink);
				p.Circle(3.8f, -2.2f, 7.3f, hole);
				Star(p, 5.6f, 3.2f, 2.2f, 0.9f, ink);
				break;
			case 10:  // 불꽃
				p.Circle(0.f, 3.5f, 5.8f, ink);
				p.T(-5.6f, 2.f, 0.f, -12.f, 5.6f, 2.f, ink);
				p.T(-5.6f, 2.f, -6.5f, -5.f, -2.5f, -1.f, ink);
				p.T(5.6f, 2.f, 2.5f, -1.f, 6.5f, -5.f, ink);
				p.Circle(0.f, 5.3f, 2.6f, hole);
				p.T(-2.4f, 4.6f, 0.f, -3.f, 2.4f, 4.6f, hole);
				break;
			case 11:  // 물결
			default:
				for (int row = -1; row <= 1; ++row)
				{
					const float base = static_cast<float>(row) * 5.5f;
					Vector2 prev{ -9.f, base + 2.2f * std::sin(-9.f * 0.62f) };
					for (int i = 1; i <= 12; ++i)
					{
						const float x = -9.f + static_cast<float>(i) * 1.5f;
						const Vector2 cur{ x, base + 2.2f * std::sin(x * 0.62f) };
						p.Line(prev.x, prev.y, cur.x, cur.y, 2.f, ink);
						prev = cur;
					}
				}
				break;
			}
		}
	}

	Color Field(int color)
	{
		static const uint32_t kField[kColors] = { 0x3E5C7E, 0x8E3B2E, 0x4E6E3A, 0xA9812F, 0x2E3136, 0x2F6F6A };
		return Rgb(kField[((color % kColors) + kColors) % kColors]);
	}

	const char* ShapeName(int shape)
	{
		static const char* kNames[kShapes] = { "방패", "종", "별", "늑대", "매", "참나무", "탑", "열쇠", "칼", "달", "불꽃", "물결" };
		return kNames[((shape % kShapes) + kShapes) % kShapes];
	}

	const char* ColorName(int color)
	{
		static const char* kNames[kColors] = { "쪽빛", "적갈", "이끼", "황토", "먹", "청록" };
		return kNames[((color % kColors) + kColors) % kColors];
	}

	void DrawShape(Vector2 center, float size, int shape, Color ink, Color hole)
	{
		const Pen p{ center, size / 32.f, 1.f };
		Glyph(p, ((shape % kShapes) + kShapes) % kShapes, ink, hole);
	}

	void Draw(Vector2 center, float size, uint8_t e, float alpha, float glow)
	{
		const int shape = (e / kColors) % kShapes;
		const int color = e % kColors;
		const Color field = Field(color);
		const Pen p{ center, size / 32.f, alpha };
		Vector2 pts[20];

		if (glow > 0.f)
		{
			const int g = ShieldPoints(pts, 1.25f + 0.08f * glow);
			const Pen gp{ center, size / 32.f, alpha * 0.35f * glow };
			gp.Fan(pts, g, Rgb(0xF2C47C));
		}
		// 바깥 어두운 테두리 → 밝은 테 → 바탕 → 윗단 빛(작아도 또렷하게)
		int n = ShieldPoints(pts, 1.1f);
		p.Fan(pts, n, Color{ 8, 9, 11, 235 });
		n = ShieldPoints(pts, 1.0f);
		p.Fan(pts, n, util::Shade(field, 1.55f));
		n = ShieldPoints(pts, 0.9f);
		p.Fan(pts, n, field);
		if (size >= 20.f)
		{
			// 아래로 갈수록 조금 어둡게(바탕 명암 두 단)
			const Color low = util::Shade(field, 0.78f);
			for (int i = 0; i < n; ++i) pts[i].y = std::max(pts[i].y, 2.f);
			p.Fan(pts, n, util::WithA(low, 0.55f));
		}
		const Color ink = Rgb(0xEFE6D0);
		Glyph(Pen{ center, size / 32.f * 0.86f, alpha }, shape, ink, field);
	}
}
