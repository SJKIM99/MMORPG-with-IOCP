#include "render/Props.h"
#include "render/Draw.h"
#include "render/Sprites.h"
#include "render/EnvArt.h"
#include "core/Util.h"

#include <cmath>

namespace props
{
	namespace
	{
		using util::Rgb;
		using util::Shade;
		using util::WithA;
		using gfx::P3;

		std::vector<Light> g_lights;

		inline Vector2 Off(Vector2 b, float dx, float dy) { return Vector2{ b.x + dx, b.y + dy }; }
		inline float H01(int x, int y, uint32_t salt) { return util::Hash01(x, y, salt); }

		// ── 그린 소품(Higgsfield) ─────────────────────────────────────────────────
		// 발을 base에, 키 worldH(px)로. 바람(lean)에 윗부분만 흔들리고, 칸마다 좌우를 뒤집어 같은 그림이 줄지어 보이지 않게 한다.
		// widthPx > 0이면 키 대신 너비로 맞춘다(여러 칸짜리 구조물). 그림이 없으면 false — 예전 도형으로 그린다.
		bool PropSprite(const char* name, Vector2 base, float worldH, float lean, bool flip, Color mul = WHITE, float widthPx = 0.f)
		{
			const sprites::Sprite* s = sprites::Find(name);
			if (!s) return false;
			const Color t0 = gfx::Tint();
			const bool tinted = mul.r != 255 || mul.g != 255 || mul.b != 255 || mul.a != 255;
			if (tinted)
				gfx::SetTint(Color{ static_cast<unsigned char>(t0.r * mul.r / 255), static_cast<unsigned char>(t0.g * mul.g / 255),
					static_cast<unsigned char>(t0.b * mul.b / 255), static_cast<unsigned char>(t0.a * mul.a / 255) });
			gfx::SpriteXform xf;
			xf.lean = lean;
			xf.flip = flip;
			const float h = widthPx > 0.f ? widthPx * s->ref / std::max(1.f, s->src.width) : worldH;
			sprites::Draw(*s, base, h, xf);
			if (tinted) gfx::SetTint(t0);
			return true;
		}

		// ── 기본 조각 ────────────────────────────────────────────────────────────
		void Trunk(Vector2 base, float w, float h, Color c, float lean = 0.f)
		{
			gfx::Quad4(Off(base, -w * 0.5f, 0), Off(base, w * 0.5f, 0), Off(base, w * 0.3f + lean, -h), Off(base, -w * 0.3f + lean, -h),
				Shade(c, 1.05f), Shade(c, 0.7f), Shade(c, 0.78f), Shade(c, 1.1f));
			// 뿌리
			gfx::Tri(Off(base, -w * 0.5f, 0), Off(base, -w * 1.1f, 2), Off(base, -w * 0.3f, -w * 0.6f), Shade(c, 0.9f));
			gfx::Tri(Off(base, w * 0.5f, 0), Off(base, w * 1.0f, 2.5f), Off(base, w * 0.3f, -w * 0.6f), Shade(c, 0.65f));
		}

		// 입체감 있는 뭉치(어두운 바탕 → 밝은 면 → 하이라이트)
		void Puff(Vector2 c, float r, Color col)
		{
			gfx::Circle(c, r, Shade(col, 0.72f));
			gfx::Circle(Off(c, -r * 0.12f, -r * 0.14f), r * 0.86f, col);
			gfx::Circle(Off(c, -r * 0.32f, -r * 0.36f), r * 0.42f, Shade(col, 1.22f));
		}

		void RoundTree(Vector2 base, float s, Color leaf, Color bark, float sway, bool blossom, int seed)
		{
			Trunk(base, 9.f * s, 34.f * s, bark);
			const Vector2 c = Off(base, sway, -52.f * s);
			const float k[5][3] = { {-16, 6, 18}, {16, 5, 19}, {0, 12, 19}, {-5, -12, 21}, {9, -8, 17} };
			for (const auto& p : k)
				gfx::Circle(Off(c, p[0] * s, p[1] * s), p[2] * s, Shade(leaf, 0.66f));
			for (const auto& p : k)
				gfx::Circle(Off(c, (p[0] - 3) * s, (p[1] - 4) * s), p[2] * s * 0.82f, leaf);
			gfx::Circle(Off(c, -10 * s, -16 * s), 9 * s, Shade(leaf, 1.25f));
			gfx::Circle(Off(c, 6 * s, -12 * s), 6 * s, Shade(leaf, 1.18f));
			if (blossom)
			{
				for (int i = 0; i < 9; ++i)
				{
					const float a = H01(seed, i, 3) * 6.28f, r = H01(seed, i, 4) * 22.f * s;
					gfx::Circle(Off(c, std::cos(a) * r, std::sin(a) * r * 0.8f - 4 * s), 2.6f * s, Rgb(0xFFD1E3));
				}
			}
		}

		void PineTree(Vector2 base, float s, Color needle, Color bark, float sway, bool snowy)
		{
			Trunk(base, 8.f * s, 18.f * s, bark);
			for (int i = 0; i < 3; ++i)
			{
				const float w = (30.f - i * 7.f) * s;
				const float y0 = -(12.f + i * 17.f) * s;
				const float top = y0 - (30.f - i * 3.f) * s;
				const float sw = sway * (0.4f + i * 0.3f);
				const Vector2 l = Off(base, -w + sw * 0.5f, y0), r = Off(base, w + sw * 0.5f, y0), t = Off(base, sw, top), m = Off(base, sw * 0.7f, y0 + 4 * s);
				gfx::Tri(l, m, t, Shade(needle, 1.05f));
				gfx::Tri(m, r, t, Shade(needle, 0.72f));
				if (snowy)
				{
					const Vector2 tl = util::LerpV(t, l, 0.38f), tr = util::LerpV(t, r, 0.38f);
					gfx::Tri(tl, tr, t, Rgb(0xF4F8FF));
					gfx::Line(Off(l, 2, -1), Off(m, 0, -2), 3.f * s, Rgb(0xE8F0FA));
				}
			}
		}

		void DeadTree(Vector2 base, float s, Color c, int seed, bool mossy)
		{
			Trunk(base, 8.f * s, 40.f * s, c, 2.f * s);
			const Vector2 top = Off(base, 2.f * s, -40.f * s);
			for (int i = 0; i < 4; ++i)
			{
				const float y = -18.f * s - i * 7.f * s;
				const float dir = (i % 2 == 0) ? -1.f : 1.f;
				const float len = (14.f + H01(seed, i, 9) * 10.f) * s;
				const Vector2 a = Off(base, 1.f * s, y);
				const Vector2 b = Off(a, dir * len, -len * 0.7f);
				gfx::LineGrad(a, b, 4.f * s, 1.f * s, c, Shade(c, 0.8f));
				gfx::LineGrad(util::LerpV(a, b, 0.6f), Off(b, dir * 4 * s, -9 * s), 2.f * s, 0.8f * s, c, Shade(c, 0.8f));
			}
			gfx::LineGrad(top, Off(top, -3 * s, -12 * s), 4.f * s, 1.f * s, c, c);
			if (mossy)
				for (int i = 0; i < 4; ++i)
					gfx::Line(Off(base, (-6 + i * 4.f) * s, -26 * s), Off(base, (-6 + i * 4.f) * s, (-14 - H01(seed, i, 2) * 8) * s), 1.8f * s, Rgb(0x6B8E3A, 200));
		}

		void Palm(Vector2 base, float s, float t, int seed)
		{
			Vector2 p = base;
			const Color bark = Rgb(0x9C7A4A);
			Vector2 top = base;
			for (int i = 0; i < 6; ++i)
			{
				const Vector2 n = Off(p, (2.2f + i * 0.9f) * s, -9.f * s);
				gfx::Line(p, n, (8.f - i * 0.6f) * s, (i % 2) ? bark : Shade(bark, 0.85f));
				p = n;
				top = n;
			}
			for (int i = 0; i < 7; ++i)
			{
				const float a = -util::kPi * 0.5f + (i - 3) * 0.55f + std::sin(t * 1.3f + seed + i) * 0.05f;
				const float len = (26.f + (i % 2) * 6.f) * s;
				const Vector2 mid = Off(top, std::cos(a) * len * 0.6f, std::sin(a) * len * 0.35f - 6 * s);
				const Vector2 tip = Off(top, std::cos(a) * len, std::sin(a) * len * 0.3f + 8 * s);
				gfx::Tri(top, mid, tip, Rgb(0x3F8F3A));
				gfx::Tri(Off(top, 0, 2), Off(mid, 0, 3), tip, Rgb(0x2E6B2B));
			}
			gfx::Circle(Off(top, -3 * s, 3 * s), 3.2f * s, Rgb(0x6B4A2A));
			gfx::Circle(Off(top, 3 * s, 4 * s), 3.2f * s, Rgb(0x5A3D22));
		}

		void Rock(Vector2 base, float s, Color c, int seed, bool mossy, bool snowy)
		{
			Vector2 pts[7];
			Vector2 top[7];
			for (int i = 0; i < 7; ++i)
			{
				const float a = util::kPi + i * (util::kPi / 6.f);  // 위쪽 반원(왼쪽→오른쪽)
				const float r = (0.8f + H01(seed, i, 17) * 0.35f);
				pts[i] = Off(base, std::cos(a) * 20.f * s * r, std::sin(a) * 26.f * s * r - 2 * s);
				top[i] = Off(pts[i], 0, 0);
			}
			// 바닥 쪽을 닫는 다각형
			Vector2 body[9];
			body[0] = Off(base, -19.f * s, 2.f * s);
			for (int i = 0; i < 7; ++i) body[i + 1] = pts[i];
			body[8] = Off(base, 19.f * s, 2.f * s);
			gfx::PolyGrad(body, 9, Off(base, -4 * s, -12 * s), Shade(c, 1.12f), Shade(c, 0.62f));
			// 빛 받는 윗면
			Vector2 face[5] = { Off(base, -12 * s, -10 * s), top[1], top[2], top[3], Off(base, 4 * s, -16 * s) };
			gfx::Poly(face, 5, WithA(Shade(c, 1.28f), 0.7f));
			gfx::Line(top[3], Off(base, 10 * s, -6 * s), 1.5f * s, Shade(c, 0.55f));
			if (mossy)
				gfx::EllipseGrad(Off(base, -5 * s, -18 * s), 11 * s, 5 * s, Rgb(0x6E9A3E, 230), Rgb(0x4E7A2E, 0));
			if (snowy)
			{
				Vector2 cap[5] = { Off(top[1], 1, 2), top[2], top[3], top[4], Off(top[5], -1, 2) };
				gfx::Poly(cap, 5, Rgb(0xF2F7FF));
			}
		}

		void Crystal(Vector2 base, float s, Color c, int seed, float t)
		{
			for (int i = 0; i < 4; ++i)
			{
				const float lean = (i - 1.5f) * 0.28f + (H01(seed, i, 5) - 0.5f) * 0.2f;
				const float h = (26.f + H01(seed, i, 6) * 22.f) * s * (i == 1 || i == 2 ? 1.2f : 0.8f);
				const float w = (5.f + H01(seed, i, 7) * 3.f) * s;
				const Vector2 b = Off(base, (i - 1.5f) * 7.f * s, 0);
				const Vector2 dir{ std::sin(lean), -std::cos(lean) };
				const Vector2 nrm{ -dir.y, dir.x };
				const Vector2 tip = util::Add(b, util::Mul(dir, h));
				const Vector2 shoulder = util::Add(b, util::Mul(dir, h * 0.78f));
				gfx::Quad(util::Add(b, util::Mul(nrm, -w)), b, shoulder, util::Add(shoulder, util::Mul(nrm, -w)), Shade(c, 1.2f));
				gfx::Quad(b, util::Add(b, util::Mul(nrm, w)), util::Add(shoulder, util::Mul(nrm, w)), shoulder, Shade(c, 0.78f));
				gfx::Tri(util::Add(shoulder, util::Mul(nrm, -w)), shoulder, tip, Shade(c, 1.45f));
				gfx::Tri(shoulder, util::Add(shoulder, util::Mul(nrm, w)), tip, Shade(c, 0.95f));
			}
			const float pulse = 0.7f + 0.3f * std::sin(t * 2.f + seed);
			g_lights.push_back({ base, 70.f * s, c, 0.55f * pulse });
		}

		void Mushroom(Vector2 base, float s, Color cap, int seed, float t)
		{
			const float h = (22.f + H01(seed, 1, 3) * 10.f) * s;
			gfx::Quad4(Off(base, -5 * s, 0), Off(base, 5 * s, 0), Off(base, 4 * s, -h), Off(base, -4 * s, -h),
				Rgb(0xF2E6D0), Rgb(0xC9B89A), Rgb(0xD8C8AA), Rgb(0xFFF6E6));
			const Vector2 c = Off(base, 0, -h);
			gfx::Ellipse(Off(c, 0, 2 * s), 21 * s, 7 * s, Shade(cap, 0.55f));
			Vector2 dome[9];
			for (int i = 0; i < 9; ++i)
			{
				const float a = util::kPi + i * (util::kPi / 8.f);
				dome[i] = Off(c, std::cos(a) * 21.f * s, std::sin(a) * 17.f * s);
			}
			gfx::PolyGrad(dome, 9, Off(c, -5 * s, -9 * s), Shade(cap, 1.25f), Shade(cap, 0.85f));
			for (int i = 0; i < 5; ++i)
			{
				const float a = util::kPi * (1.15f + i * 0.17f);
				gfx::Circle(Off(c, std::cos(a) * 12.f * s, std::sin(a) * 9.f * s - 2 * s), (2.2f + (i % 2)) * s, Rgb(0xFFF4E0));
			}
			g_lights.push_back({ c, 60.f * s, cap, 0.35f + 0.1f * std::sin(t * 1.7f + seed) });
		}

		void Pillar(Vector2 base, float s, int seed)
		{
			const Color st = Rgb(0xB7B0A3);
			const float h = (30.f + H01(seed, 0, 1) * 26.f) * s;
			gfx::Quad4(Off(base, -11 * s, 0), Off(base, 11 * s, 0), Off(base, 11 * s, -h), Off(base, -11 * s, -h), Shade(st, 1.05f), Shade(st, 0.72f), Shade(st, 0.8f), Shade(st, 1.12f));
			for (int i = -1; i <= 1; ++i)
				gfx::Line(Off(base, i * 6.f * s, -2 * s), Off(base, i * 6.f * s, -h + 3 * s), 1.2f * s, Shade(st, 0.78f));
			// 부서진 윗면
			Vector2 top[5] = { Off(base, -11 * s, -h), Off(base, -4 * s, -h - 6 * s), Off(base, 2 * s, -h - 1 * s), Off(base, 7 * s, -h - 8 * s), Off(base, 11 * s, -h) };
			gfx::Poly(top, 5, Shade(st, 1.2f));
			gfx::Ellipse(Off(base, 0, 0), 15 * s, 5 * s, Shade(st, 0.6f));
			Rock(Off(base, 13 * s, 3 * s), 0.35f * s, Shade(st, 0.95f), seed + 7, false, false);
		}

		void Tombstone(Vector2 base, float s, int variant)
		{
			const Color st = Rgb(0x8E918F);
			if (variant == 0)
			{
				gfx::Quad(Off(base, -10 * s, 0), Off(base, 10 * s, 0), Off(base, 10 * s, -24 * s), Off(base, -10 * s, -24 * s), st);
				gfx::Ellipse(Off(base, 0, -24 * s), 10 * s, 8 * s, st);
				gfx::Quad(Off(base, 10 * s, 0), Off(base, 13 * s, -2 * s), Off(base, 13 * s, -25 * s), Off(base, 10 * s, -24 * s), Shade(st, 0.65f));
				gfx::Line(Off(base, -5 * s, -20 * s), Off(base, 5 * s, -20 * s), 1.6f * s, Shade(st, 0.6f));
				gfx::Line(Off(base, -5 * s, -15 * s), Off(base, 3 * s, -15 * s), 1.6f * s, Shade(st, 0.6f));
			}
			else
			{
				const Color w = Rgb(0x6A5A4A);
				gfx::Line(Off(base, 0, 0), Off(base, 0, -36 * s), 5 * s, w);
				gfx::Line(Off(base, -10 * s, -26 * s), Off(base, 10 * s, -26 * s), 5 * s, Shade(w, 1.1f));
			}
			gfx::Ellipse(Off(base, 0, 1 * s), 14 * s, 4 * s, Rgb(0x2A2F2A, 150));
		}

		void Cactus(Vector2 base, float s, int seed)
		{
			const Color g = Rgb(0x4E9A4A);
			gfx::Capsule(Off(base, 0, -4 * s), Off(base, 0, -40 * s), 7 * s, g);
			gfx::Capsule(Off(base, -7 * s, -22 * s), Off(base, -14 * s, -24 * s), 4 * s, g);
			gfx::Capsule(Off(base, -14 * s, -24 * s), Off(base, -14 * s, -34 * s), 4 * s, g);
			if (H01(seed, 0, 3) > 0.4f)
			{
				gfx::Capsule(Off(base, 7 * s, -16 * s), Off(base, 13 * s, -18 * s), 4 * s, Shade(g, 0.85f));
				gfx::Capsule(Off(base, 13 * s, -18 * s), Off(base, 13 * s, -28 * s), 4 * s, Shade(g, 0.85f));
			}
			gfx::Line(Off(base, 2 * s, -6 * s), Off(base, 2 * s, -40 * s), 1.5f * s, Shade(g, 0.7f));
			gfx::Line(Off(base, -3 * s, -8 * s), Off(base, -3 * s, -42 * s), 1.2f * s, Shade(g, 1.2f));
			if (H01(seed, 1, 3) > 0.6f)
				gfx::Circle(Off(base, 0, -47 * s), 3 * s, Rgb(0xF06A8A));
		}

		void Bones(Vector2 base, float s)
		{
			const Color b = Rgb(0xE6DFC8);
			gfx::Circle(Off(base, -6 * s, -8 * s), 7 * s, b);
			gfx::Circle(Off(base, -8 * s, -9 * s), 1.8f * s, Rgb(0x3A3028));
			gfx::Circle(Off(base, -3 * s, -9 * s), 1.8f * s, Rgb(0x3A3028));
			for (int i = 0; i < 3; ++i)
				gfx::EllipseArc(Off(base, 8 * s, -4 * s), (8.f - i * 2) * s, (5.f - i) * s, util::kPi, util::kPi * 2.f, 1.8f * s, b);
			gfx::Line(Off(base, 2 * s, 0), Off(base, 16 * s, -2 * s), 2.f * s, Shade(b, 0.9f));
		}

		void Palisade(Vector2 base, float s, int seed)
		{
			const Color w = Rgb(0x8A6038);
			for (int i = 0; i < 4; ++i)
			{
				const float x = (-13.f + i * 8.5f) * s;
				const float h = (30.f + H01(seed, i, 2) * 10.f) * s;
				const Color c = (i % 2) ? w : Shade(w, 0.85f);
				gfx::Quad(Off(base, x - 4 * s, 2 * s - i * s), Off(base, x + 4 * s, 2 * s - i * s), Off(base, x + 4 * s, -h), Off(base, x - 4 * s, -h), c);
				gfx::Tri(Off(base, x - 4 * s, -h), Off(base, x + 4 * s, -h), Off(base, x, -h - 9 * s), Shade(c, 1.2f));
			}
			gfx::Line(Off(base, -18 * s, -14 * s), Off(base, 18 * s, -18 * s), 2.5f * s, Rgb(0x5A4028));
			gfx::Line(Off(base, -18 * s, -22 * s), Off(base, 18 * s, -26 * s), 2.5f * s, Rgb(0x5A4028));
		}

		void Barrel(Vector2 base, float s)
		{
			const Color w = Rgb(0x9A6A3A);
			gfx::Quad4(Off(base, -11 * s, 0), Off(base, 11 * s, 0), Off(base, 12 * s, -26 * s), Off(base, -12 * s, -26 * s), Shade(w, 1.05f), Shade(w, 0.7f), Shade(w, 0.75f), Shade(w, 1.1f));
			gfx::Ellipse(Off(base, 0, -26 * s), 12 * s, 5 * s, Shade(w, 1.25f));
			gfx::Line(Off(base, -11.5f * s, -7 * s), Off(base, 11.5f * s, -7 * s), 2.2f * s, Rgb(0x4A4A50));
			gfx::Line(Off(base, -12 * s, -19 * s), Off(base, 12 * s, -19 * s), 2.2f * s, Rgb(0x4A4A50));
			// 옆 상자
			const Vector2 cb = Off(base, 16 * s, 4 * s);
			gfx::Quad(Off(cb, -8 * s, 0), Off(cb, 8 * s, 0), Off(cb, 8 * s, -14 * s), Off(cb, -8 * s, -14 * s), Rgb(0x7A5A34));
			gfx::Quad(Off(cb, -8 * s, -14 * s), Off(cb, 8 * s, -14 * s), Off(cb, 4 * s, -18 * s), Off(cb, -12 * s, -18 * s), Rgb(0x9A7A4A));
			gfx::Line(Off(cb, -8 * s, 0), Off(cb, 8 * s, -14 * s), 1.4f * s, Rgb(0x5A4028));
		}

		void Reeds(Vector2 base, float s, float t, int seed)
		{
			for (int i = 0; i < 7; ++i)
			{
				const float x = (-12.f + i * 4.f) * s;
				const float h = (22.f + H01(seed, i, 4) * 16.f) * s;
				const float sw = std::sin(t * 1.6f + i * 0.7f + seed) * 3.f * s;
				gfx::LineGrad(Off(base, x, 2 * s), Off(base, x + sw, -h), 2.2f * s, 1.f * s, Rgb(0x5A7A30), Rgb(0x8AAA50));
				if (i % 2 == 0)
					gfx::Capsule(Off(base, x + sw * 0.9f, -h + 2 * s), Off(base, x + sw, -h - 5 * s), 2.2f * s, Rgb(0x6A4A2A));
			}
		}

		void Mound(Vector2 base, float s)
		{
			const Color d = Rgb(0x7A5634);
			Vector2 dome[9];
			for (int i = 0; i < 9; ++i)
			{
				const float a = util::kPi + i * (util::kPi / 8.f);
				dome[i] = Off(base, std::cos(a) * 24.f * s, std::sin(a) * 20.f * s + 2 * s);
			}
			gfx::PolyGrad(dome, 9, Off(base, -6 * s, -12 * s), Shade(d, 1.2f), Shade(d, 0.7f));
			gfx::Ellipse(Off(base, 1 * s, -15 * s), 6 * s, 3 * s, Rgb(0x1A120C));
			for (int i = 0; i < 4; ++i)
				gfx::Circle(Off(base, -16.f * s + i * 10.f * s, 1 * s), 2.2f * s, Shade(d, 0.85f));
		}

		void Spike(Vector2 base, float s, Color c, int seed, bool lava, float t)
		{
			for (int i = 0; i < 3; ++i)
			{
				const float x = (i - 1) * 9.f * s;
				const float h = (24.f + H01(seed, i, 8) * 22.f) * s * (i == 1 ? 1.3f : 0.85f);
				const float lean = (i - 1) * 4.f * s;
				gfx::Tri(Off(base, x - 7 * s, 0), Off(base, x, 1 * s), Off(base, x + lean, -h), Shade(c, 1.3f));
				gfx::Tri(Off(base, x, 1 * s), Off(base, x + 7 * s, 0), Off(base, x + lean, -h), Shade(c, 0.8f));
			}
			if (lava)
			{
				const float pulse = 0.75f + 0.25f * std::sin(t * 3.f + seed);
				gfx::EllipseGrad(Off(base, 0, 1), 16 * s, 6 * s, Rgb(0xFFB040, static_cast<uint8_t>(220 * pulse)), Rgb(0xFF4010, 0));
				g_lights.push_back({ base, 80.f * s, Rgb(0xFF7020), 0.6f * pulse });
			}
		}

		void LavaVent(Vector2 base, float s, float t, int seed)
		{
			const Color r = Rgb(0x2A2020);
			Vector2 cone[6] = { Off(base, -22 * s, 2 * s), Off(base, -10 * s, -16 * s), Off(base, -5 * s, -18 * s), Off(base, 5 * s, -18 * s), Off(base, 10 * s, -16 * s), Off(base, 22 * s, 2 * s) };
			gfx::PolyGrad(cone, 6, Off(base, -4 * s, -10 * s), Shade(r, 1.6f), r);
			const float pulse = 0.7f + 0.3f * std::sin(t * 4.f + seed);
			gfx::Ellipse(Off(base, 0, -17 * s), 6 * s, 2.5f * s, Rgb(0xFFD060, static_cast<uint8_t>(255 * pulse)));
			gfx::Line(Off(base, -3 * s, -15 * s), Off(base, -9 * s, -2 * s), 2.f * s, Rgb(0xFF6A20, static_cast<uint8_t>(200 * pulse)));
			g_lights.push_back({ Off(base, 0, -16 * s), 110.f * s, Rgb(0xFF6A20), 0.9f * pulse });
		}

		void Bush(Vector2 base, float s, Color c, bool berries, int seed)
		{
			Puff(Off(base, -9 * s, -9 * s), 10 * s, c);
			Puff(Off(base, 9 * s, -8 * s), 10 * s, Shade(c, 0.92f));
			Puff(Off(base, 0, -15 * s), 12 * s, c);
			if (berries)
				for (int i = 0; i < 5; ++i)
					gfx::Circle(Off(base, (-10.f + H01(seed, i, 1) * 20.f) * s, (-20.f + H01(seed, i, 2) * 14.f) * s), 2.f * s, Rgb(0xD83A4A));
		}

		void Snowman(Vector2 base, float s)
		{
			Puff(Off(base, 0, -9 * s), 11 * s, Rgb(0xF4F8FF));
			Puff(Off(base, 0, -26 * s), 8 * s, Rgb(0xF4F8FF));
			gfx::Tri(Off(base, 0, -27 * s), Off(base, 0, -25 * s), Off(base, 7 * s, -26 * s), Rgb(0xF08A2A));
			gfx::Circle(Off(base, -3 * s, -29 * s), 1.2f * s, BLACK);
			gfx::Circle(Off(base, 2 * s, -29 * s), 1.2f * s, BLACK);
			gfx::Quad(Off(base, -7 * s, -33 * s), Off(base, 7 * s, -33 * s), Off(base, 5 * s, -35 * s), Off(base, -5 * s, -35 * s), Rgb(0x2A2A2A));
			gfx::Quad(Off(base, -5 * s, -35 * s), Off(base, 5 * s, -35 * s), Off(base, 5 * s, -43 * s), Off(base, -5 * s, -43 * s), Rgb(0x2A2A2A));
			gfx::Line(Off(base, -4 * s, -21 * s), Off(base, 4 * s, -21 * s), 2.f * s, Rgb(0xC83030));
		}

		void Stump(Vector2 base, float s)
		{
			const Color w = Rgb(0x7A5A3A);
			gfx::Quad4(Off(base, -10 * s, 0), Off(base, 10 * s, 0), Off(base, 9 * s, -12 * s), Off(base, -9 * s, -12 * s), Shade(w, 1.05f), Shade(w, 0.7f), Shade(w, 0.75f), Shade(w, 1.1f));
			gfx::Ellipse(Off(base, 0, -12 * s), 9 * s, 4 * s, Rgb(0xC9A676));
			gfx::EllipseArc(Off(base, 0, -12 * s), 5 * s, 2 * s, 0, util::kPi * 2.f, 1.f * s, Rgb(0x9A7A4A));
			gfx::EllipseGrad(Off(base, 4 * s, -13 * s), 6 * s, 3 * s, Rgb(0x6E9A3E, 200), Rgb(0x6E9A3E, 0));
		}

		// ── 마을 ────────────────────────────────────────────────────────────────
		float g_night = 0.f;

		Color StoneVar(int x, int y, Color base)
		{
			const float v = H01(x, y, 77) * 0.14f - 0.07f;
			return Shade(base, 1.f + v);
		}

		// 그린 성벽 텍스처가 있으면 돌 면에 입힌다(없으면 단색 상자 + 줄눈)
		const Texture2D* WallTex() { return envart::Get(envart::Tex::TownWall); }

		void StoneBox(float x0, float y0, float x1, float y1, float z0, float z1, Color st, float unit = 1.1f)
		{
			if (const Texture2D* tex = WallTex())
			{
				// 텍스처 색 위에 밝기만 곱한다(윗면 밝게, 동쪽 면 어둡게 — 빛은 왼쪽 위에서)
				const float v = std::min(1.f, (st.r + st.g + st.b) / (3.f * 150.f));
				gfx::BoxTex(*tex, x0, y0, x1, y1, z0, z1, unit, Shade(WHITE, 1.0f * v), Shade(WHITE, 0.86f * v), Shade(WHITE, 0.66f * v));
			}
			else
				gfx::Box(x0, y0, x1, y1, z0, z1, Shade(st, 1.18f), Shade(st, 0.92f), Shade(st, 0.72f));
		}

		void Wall(int x, int y, bool tall)
		{
			const float X = static_cast<float>(x), Y = static_cast<float>(y);
			const Color st = StoneVar(x, y, Rgb(0x9C958A));
			const float h = tall ? 2.35f : 1.55f;
			StoneBox(X - 0.5f, Y - 0.5f, X + 0.5f, Y + 0.5f, 0.f, h, st);
			// 돌 줄눈(텍스처가 없을 때만)
			if (!WallTex())
				for (int i = 1; i < (tall ? 5 : 3); ++i)
				{
					const float z = h * i / (tall ? 5.f : 3.f);
					gfx::Face({ X - 0.5f, Y + 0.5f, z - 0.02f }, { X + 0.5f, Y + 0.5f, z - 0.02f }, { X + 0.5f, Y + 0.5f, z + 0.02f }, { X - 0.5f, Y + 0.5f, z + 0.02f }, Shade(st, 0.7f));
					gfx::Face({ X + 0.5f, Y - 0.5f, z - 0.02f }, { X + 0.5f, Y + 0.5f, z - 0.02f }, { X + 0.5f, Y + 0.5f, z + 0.02f }, { X + 0.5f, Y - 0.5f, z + 0.02f }, Shade(st, 0.55f));
				}
			// 흉벽(총안)
			if (!tall && (x + y) % 2 == 0)
				StoneBox(X - 0.3f, Y - 0.3f, X + 0.3f, Y + 0.3f, h, h + 0.35f, st);
			if (tall)
			{
				// 작은 지붕
				const Color roof = Rgb(0x4A5A7A);
				const P3 apex{ X, Y, h + 0.9f };
				gfx::FaceTri({ X - 0.6f, Y - 0.6f, h }, { X + 0.6f, Y - 0.6f, h }, apex, Shade(roof, 0.8f));
				gfx::FaceTri({ X - 0.6f, Y - 0.6f, h }, { X - 0.6f, Y + 0.6f, h }, apex, Shade(roof, 0.9f));
				gfx::FaceTri({ X - 0.6f, Y + 0.6f, h }, { X + 0.6f, Y + 0.6f, h }, apex, Shade(roof, 1.1f));
				gfx::FaceTri({ X + 0.6f, Y - 0.6f, h }, { X + 0.6f, Y + 0.6f, h }, apex, Shade(roof, 0.75f));
				// 횃불
				const Vector2 torch = gfx::IsoPx(X + 0.5f, Y + 0.5f, 1.6f);
				gfx::Circle(torch, 3.f, Rgb(0xFFC050));
				g_lights.push_back({ gfx::IsoPx(X + 0.6f, Y + 0.6f, 0.f), 120.f, Rgb(0xFF9A40), 0.9f });
			}
		}

		void Tower(float X0, float Y0)   // 2x2 칸, (X0, Y0) = 왼쪽 위 칸 중심
		{
			const float x0 = X0 - 0.5f, y0 = Y0 - 0.5f, x1 = X0 + 1.5f, y1 = Y0 + 1.5f;
			const Color st = Rgb(0x8E887E);
			const float h = 3.0f;
			StoneBox(x0, y0, x1, y1, 0.f, h, st);
			// 창문(화살 구멍)
			for (float z : { 1.2f, 2.2f })
			{
				gfx::Face({ X0 + 0.35f, y1 + 0.01f, z }, { X0 + 0.65f, y1 + 0.01f, z }, { X0 + 0.65f, y1 + 0.01f, z + 0.45f }, { X0 + 0.35f, y1 + 0.01f, z + 0.45f }, Rgb(0x2A2622));
				gfx::Face({ x1 + 0.01f, Y0 + 0.35f, z }, { x1 + 0.01f, Y0 + 0.65f, z }, { x1 + 0.01f, Y0 + 0.65f, z + 0.45f }, { x1 + 0.01f, Y0 + 0.35f, z + 0.45f }, Rgb(0x2A2622));
			}
			const Color roof = Rgb(0x3E5A8A);
			const P3 apex{ X0 + 0.5f, Y0 + 0.5f, h + 1.9f };
			const float o = 0.15f;
			gfx::FaceTri({ x0 - o, y0 - o, h }, { x1 + o, y0 - o, h }, apex, Shade(roof, 0.8f));
			gfx::FaceTri({ x0 - o, y0 - o, h }, { x0 - o, y1 + o, h }, apex, Shade(roof, 0.9f));
			gfx::FaceTri({ x0 - o, y1 + o, h }, { x1 + o, y1 + o, h }, apex, Shade(roof, 1.15f));
			gfx::FaceTri({ x1 + o, y0 - o, h }, { x1 + o, y1 + o, h }, apex, Shade(roof, 0.72f));
			// 깃발
			const Vector2 pole = gfx::IsoPx(apex.x, apex.y, apex.z);
			gfx::SetDepth(apex.x + apex.y + 0.8f);
			gfx::Line(pole, Off(pole, 0, -26), 2.f, Rgb(0x5A4A3A));
			const float wave = std::sin(static_cast<float>(GetTime()) * 4.f + X0) * 3.f;
			gfx::Quad(Off(pole, 1, -26), Off(pole, 20, -23 + wave), Off(pole, 20, -14 + wave), Off(pole, 1, -16), Rgb(0xC83A3A));
			gfx::Circle(Off(pole, 10, -20 + wave * 0.5f), 2.5f, Rgb(0xF2D060));
		}

		void GateArch(float X0, float Y0, bool horizontal)   // 4칸 폭 문 위의 들보
		{
			const Color wood = Rgb(0x6A4A2E);
			const Texture2D* planks = envart::Get(envart::Tex::Wood);
			auto beam = [&](float ax, float ay, float bx, float by)
			{
				if (planks) gfx::BoxTex(*planks, ax, ay, bx, by, 2.0f, 2.45f, 0.9f, Shade(WHITE, 1.05f), Shade(WHITE, 0.85f), Shade(WHITE, 0.62f));
				else gfx::Box(ax, ay, bx, by, 2.0f, 2.45f, Shade(wood, 1.2f), wood, Shade(wood, 0.75f));
			};
			if (horizontal)
			{
				beam(X0 - 0.5f, Y0 - 0.45f, X0 + 3.5f, Y0 + 0.45f);
				StoneBox(X0 - 0.5f, Y0 - 0.5f, X0 + 3.5f, Y0 + 0.5f, 2.45f, 2.6f, Rgb(0x8E887E));
			}
			else
			{
				beam(X0 - 0.45f, Y0 - 0.5f, X0 + 0.45f, Y0 + 3.5f);
				StoneBox(X0 - 0.5f, Y0 - 0.5f, X0 + 0.5f, Y0 + 3.5f, 2.45f, 2.6f, Rgb(0x8E887E));
			}
		}

		void Lamp(float X, float Y)
		{
			const Vector2 base = gfx::IsoPx(X, Y);
			gfx::SetDepth(X + Y);
			const Color iron = Rgb(0x2E3036);
			gfx::Ellipse(base, 7, 3, Rgb(0x3A3A40));
			gfx::Line(base, Off(base, 0, -70), 3.5f, iron);
			gfx::Line(Off(base, 0, -70), Off(base, 8, -74), 2.5f, iron);
			const Vector2 l = Off(base, 8, -66);
			gfx::Quad(Off(l, -5, -9), Off(l, 5, -9), Off(l, 4, 3), Off(l, -4, 3), iron);
			const float flick = 0.85f + 0.15f * util::Noise1(static_cast<float>(GetTime()) * 6.f + X);
			gfx::Quad(Off(l, -3.5f, -7), Off(l, 3.5f, -7), Off(l, 3, 1), Off(l, -3, 1),
				util::Mix(Rgb(0xE8D8A0), Rgb(0xFFE070), g_night * flick));
			gfx::Tri(Off(l, -6, -9), Off(l, 6, -9), Off(l, 0, -15), iron);
			g_lights.push_back({ gfx::IsoPx(X + 0.3f, Y + 0.3f), 150.f, Rgb(0xFFC870), 1.1f * flick });
		}

		void Stall(float X0, float Y0, int variant)   // 2x2
		{
			const float x0 = X0 - 0.45f, y0 = Y0 - 0.45f, x1 = X0 + 1.45f, y1 = Y0 + 1.45f;
			const Color wood = Rgb(0x8A6038);
			// 기둥
			for (const auto& p : { P3{ x0, y0, 0 }, P3{ x1, y0, 0 }, P3{ x0, y1, 0 }, P3{ x1, y1, 0 } })
				gfx::Box(p.x - 0.05f, p.y - 0.05f, p.x + 0.05f, p.y + 0.05f, 0.f, 1.9f, Shade(wood, 1.1f), wood, Shade(wood, 0.75f));
			// 판매대
			gfx::Box(x0 + 0.1f, y0 + 0.1f, x1 - 0.1f, y1 - 0.1f, 0.f, 0.75f, Rgb(0xB08A5A), Rgb(0x8A6A42), Rgb(0x6A5032));
			// 물건
			const Color goods[] = { Rgb(0xE05A3A), Rgb(0xF0C040), Rgb(0x6AB04A), Rgb(0xC06AE0), Rgb(0x4A9AE0) };
			for (int i = 0; i < 6; ++i)
			{
				const float gx = x0 + 0.35f + (i % 3) * 0.45f, gy = y0 + 0.4f + (i / 3) * 0.6f;
				const Vector2 p = gfx::IsoPx(gx, gy, 0.8f);
				gfx::SetDepth(gx + gy);
				gfx::Circle(p, 4.5f, goods[(i + variant) % 5]);
				gfx::Circle(Off(p, -1.5f, -1.5f), 1.6f, Rgb(0xFFFFFF, 140));
			}
			// 줄무늬 차양
			const Color a = variant ? Rgb(0x3A7AC8) : Rgb(0xC83A3A);
			const Color b = Rgb(0xF2EEE6);
			const int stripes = 6;
			for (int i = 0; i < stripes; ++i)
			{
				const float t0 = static_cast<float>(i) / stripes, t1 = static_cast<float>(i + 1) / stripes;
				const float xa = util::Lerp(x0 - 0.15f, x1 + 0.15f, t0), xb = util::Lerp(x0 - 0.15f, x1 + 0.15f, t1);
				const Color c = (i % 2) ? a : b;
				gfx::Face({ xa, y0 - 0.15f, 2.2f }, { xb, y0 - 0.15f, 2.2f }, { xb, y1 + 0.25f, 1.75f }, { xa, y1 + 0.25f, 1.75f }, c);
				// 앞쪽 늘어진 천(물결)
				gfx::Face({ xa, y1 + 0.25f, 1.75f }, { xb, y1 + 0.25f, 1.75f }, { xb, y1 + 0.26f, 1.5f + (i % 2) * 0.08f }, { xa, y1 + 0.26f, 1.5f + (i % 2) * 0.08f }, Shade(c, 0.85f));
			}
		}

		void Fountain(float X0, float Y0, float t)   // 4x4, (X0,Y0) = 왼쪽 위 칸 중심
		{
			const float cx = X0 + 1.5f, cy = Y0 + 1.5f;
			const Color st = Rgb(0xC9C2B4);
			const int seg = 16;
			const float r = 1.9f, rin = 1.6f, h = 0.55f;
			// 수조 벽(앞쪽 반만 보이지만 깊이 버퍼가 처리)
			for (int i = 0; i < seg; ++i)
			{
				const float a0 = i * util::kPi * 2.f / seg, a1 = (i + 1) * util::kPi * 2.f / seg;
				const P3 p0{ cx + std::cos(a0) * r, cy + std::sin(a0) * r, 0 }, p1{ cx + std::cos(a1) * r, cy + std::sin(a1) * r, 0 };
				const float light = 0.75f + 0.3f * std::cos((a0 + a1) * 0.5f - util::kPi * 0.75f);
				gfx::Face(p0, p1, { p1.x, p1.y, h }, { p0.x, p0.y, h }, Shade(st, light));
				const P3 q0{ cx + std::cos(a0) * rin, cy + std::sin(a0) * rin, h }, q1{ cx + std::cos(a1) * rin, cy + std::sin(a1) * rin, h };
				gfx::Face({ p0.x, p0.y, h }, { p1.x, p1.y, h }, q1, q0, Shade(st, 1.15f));
			}
			// 수면
			const P3 c3{ cx, cy, h - 0.12f };
			for (int i = 0; i < seg; ++i)
			{
				const float a0 = i * util::kPi * 2.f / seg, a1 = (i + 1) * util::kPi * 2.f / seg;
				const float w0 = 0.5f + 0.5f * std::sin(t * 2.f + i), w1 = 0.5f + 0.5f * std::sin(t * 2.f + i + 1);
				gfx::Face4(c3, c3, { cx + std::cos(a1) * rin, cy + std::sin(a1) * rin, h - 0.12f }, { cx + std::cos(a0) * rin, cy + std::sin(a0) * rin, h - 0.12f },
					Rgb(0x7AC8F0), Rgb(0x7AC8F0), util::Mix(Rgb(0x3A8AC8), Rgb(0x6AB8E8), w1), util::Mix(Rgb(0x3A8AC8), Rgb(0x6AB8E8), w0));
			}
			// 가운데 기둥 + 그릇
			gfx::Box(cx - 0.18f, cy - 0.18f, cx + 0.18f, cy + 0.18f, 0.f, 1.35f, Shade(st, 1.2f), st, Shade(st, 0.75f));
			for (int i = 0; i < 12; ++i)
			{
				const float a0 = i * util::kPi * 2.f / 12, a1 = (i + 1) * util::kPi * 2.f / 12;
				const float light = 0.75f + 0.3f * std::cos((a0 + a1) * 0.5f - util::kPi * 0.75f);
				gfx::Face({ cx + std::cos(a0) * 0.3f, cy + std::sin(a0) * 0.3f, 1.3f }, { cx + std::cos(a1) * 0.3f, cy + std::sin(a1) * 0.3f, 1.3f },
					{ cx + std::cos(a1) * 0.7f, cy + std::sin(a1) * 0.7f, 1.6f }, { cx + std::cos(a0) * 0.7f, cy + std::sin(a0) * 0.7f, 1.6f }, Shade(st, light));
			}
			// 물줄기
			const Vector2 top = gfx::IsoPx(cx, cy, 1.95f);
			gfx::SetDepth(cx + cy + 0.2f);
			gfx::LineGrad(gfx::IsoPx(cx, cy, 1.6f), top, 6.f, 3.f, Rgb(0xBFE8FF, 220), Rgb(0xFFFFFF, 240));
			gfx::Circle(top, 4.f + std::sin(t * 7.f), Rgb(0xE8F8FF, 230));
			g_lights.push_back({ gfx::IsoPx(cx, cy), 160.f, Rgb(0x90D0FF), 0.45f });
		}

		// 창문(밤에 불이 켜진다)
		void Window(P3 a, P3 b, float z0, float z1, bool eastFace)
		{
			const Color glass = util::Mix(Rgb(0x3A4A5A), Rgb(0xFFD27A), g_night);
			const Color frame = Rgb(0x4A3624);
			const float e = 0.012f;
			if (eastFace)
			{
				gfx::Face({ a.x + e, a.y - 0.04f, z0 - 0.04f }, { a.x + e, b.y + 0.04f, z0 - 0.04f }, { a.x + e, b.y + 0.04f, z1 + 0.04f }, { a.x + e, a.y - 0.04f, z1 + 0.04f }, frame);
				gfx::Face({ a.x + 2 * e, a.y, z0 }, { a.x + 2 * e, b.y, z0 }, { a.x + 2 * e, b.y, z1 }, { a.x + 2 * e, a.y, z1 }, glass);
				gfx::Face({ a.x + 3 * e, (a.y + b.y) * 0.5f - 0.02f, z0 }, { a.x + 3 * e, (a.y + b.y) * 0.5f + 0.02f, z0 }, { a.x + 3 * e, (a.y + b.y) * 0.5f + 0.02f, z1 }, { a.x + 3 * e, (a.y + b.y) * 0.5f - 0.02f, z1 }, frame);
				if (g_night > 0.05f) g_lights.push_back({ gfx::IsoPx(a.x + 0.4f, (a.y + b.y) * 0.5f), 70.f, Rgb(0xFFC870), 0.7f * g_night });
			}
			else
			{
				gfx::Face({ a.x - 0.04f, a.y + e, z0 - 0.04f }, { b.x + 0.04f, a.y + e, z0 - 0.04f }, { b.x + 0.04f, a.y + e, z1 + 0.04f }, { a.x - 0.04f, a.y + e, z1 + 0.04f }, frame);
				gfx::Face({ a.x, a.y + 2 * e, z0 }, { b.x, a.y + 2 * e, z0 }, { b.x, a.y + 2 * e, z1 }, { a.x, a.y + 2 * e, z1 }, glass);
				gfx::Face({ (a.x + b.x) * 0.5f - 0.02f, a.y + 3 * e, z0 }, { (a.x + b.x) * 0.5f + 0.02f, a.y + 3 * e, z0 }, { (a.x + b.x) * 0.5f + 0.02f, a.y + 3 * e, z1 }, { (a.x + b.x) * 0.5f - 0.02f, a.y + 3 * e, z1 }, frame);
				if (g_night > 0.05f) g_lights.push_back({ gfx::IsoPx((a.x + b.x) * 0.5f, a.y + 0.4f), 70.f, Rgb(0xFFC870), 0.7f * g_night });
			}
		}

		void Door(float xa, float xb, float y, float h)
		{
			gfx::Face({ xa - 0.06f, y + 0.01f, 0 }, { xb + 0.06f, y + 0.01f, 0 }, { xb + 0.06f, y + 0.01f, h + 0.06f }, { xa - 0.06f, y + 0.01f, h + 0.06f }, Rgb(0x3A2A1C));
			gfx::Face({ xa, y + 0.02f, 0 }, { xb, y + 0.02f, 0 }, { xb, y + 0.02f, h }, { xa, y + 0.02f, h }, Rgb(0x7A5232));
			for (int i = 1; i < 4; ++i)
			{
				const float x = util::Lerp(xa, xb, i / 4.f);
				gfx::Face({ x - 0.01f, y + 0.03f, 0.05f }, { x + 0.01f, y + 0.03f, 0.05f }, { x + 0.01f, y + 0.03f, h - 0.05f }, { x - 0.01f, y + 0.03f, h - 0.05f }, Rgb(0x5A3A22));
			}
			const Vector2 knob = gfx::IsoPx(xb - 0.12f, y + 0.04f, h * 0.5f);
			gfx::SetDepth(xb + y);
			gfx::Circle(knob, 1.8f, Rgb(0xE0C060));
		}
	}

	std::vector<Light>& FrameLights() { return g_lights; }

	void DrawFieldObstacle(int x, int y, WorldData::Biome biome, float time)
	{
		using WorldData::Biome;
		const float X = static_cast<float>(x), Y = static_cast<float>(y);
		const float jx = (H01(x, y, 11) - 0.5f) * 0.18f, jy = (H01(x, y, 12) - 0.5f) * 0.18f;
		const Vector2 base = gfx::IsoPx(X + jx, Y + jy);
		gfx::SetDepth(X + Y);
		const float pick = H01(x, y, 13);
		const float s = 0.85f + H01(x, y, 14) * 0.35f;
		const int seed = static_cast<int>(util::Hash2(x, y, 5) & 0xFFFF);
		const float sway = std::sin(time * 1.2f + seed * 0.01f) * 1.6f;
		const bool flip = (seed & 1) != 0;

		// 그린 소품: 나무는 바람에 윗부분이 흔들린다(절차적 흔들림의 2.5배 — 그림이 더 크다)
		auto tree = [&](const char* name, float h, Color mul = WHITE) { return PropSprite(name, base, h * s, sway * 2.5f, flip, mul); };
		auto thing = [&](const char* name, float h, Color mul = WHITE) { return PropSprite(name, base, h * s, 0.f, flip, mul); };
		auto glow = [&](Color c, float r, float k) { g_lights.push_back({ Off(base, 0, -10 * s), r * s, c, k }); };
		const float pulse = 0.7f + 0.3f * std::sin(time * 2.f + seed);

		switch (biome)
		{
		case Biome::eMeadow:
			// 들녘은 탁 트여야 한다 — 그린 나무는 크고 무거우니 덤불 · 바위를 더 많이
			if (pick < 0.26f) { if (!tree("prop_tree_oak", 128.f)) RoundTree(base, s, Rgb(0x5FA040), Rgb(0x7A5634), sway, false, seed); }
			else if (pick < 0.68f) { if (!thing(pick > 0.52f ? "prop_bush_flower" : "prop_bush", 52.f)) Bush(base, s, Rgb(0x4F9A3A), pick > 0.52f, seed); }
			else if (!thing(pick > 0.9f ? "prop_rock_moss" : "prop_rock_grey", 46.f)) Rock(base, s * 0.8f, Rgb(0x9A968A), seed, pick > 0.9f, false);
			break;
		case Biome::eForest:
			if (pick < 0.4f) { if (!tree("prop_tree_forest", 140.f)) RoundTree(base, s * 1.1f, Rgb(0x3E7A30), Rgb(0x5A3E26), sway, false, seed); }
			else if (pick < 0.78f) { if (!tree("prop_tree_pine", 155.f)) PineTree(base, s * 1.1f, Rgb(0x2E6A3A), Rgb(0x5A3E26), sway, false); }
			else if (pick < 0.9f) { if (!thing("prop_stump", 30.f)) Stump(base, s); }
			else if (!thing("prop_rock_moss", 46.f)) Rock(base, s * 0.8f, Rgb(0x7E7A70), seed, true, false);
			break;
		case Biome::eSnow:
			if (pick < 0.55f) { if (!tree("prop_tree_pine_snow", 150.f)) PineTree(base, s * 1.1f, Rgb(0x2E5A4A), Rgb(0x4A3A2E), sway * 0.5f, true); }
			else if (pick < 0.85f) { if (!thing("prop_rock_snow", 48.f)) Rock(base, s * 0.85f, Rgb(0xA8C0D8), seed, false, true); }
			else if (pick < 0.856f) { if (!thing("prop_snowman", 52.f)) Snowman(base, s); }
			else if (!tree("prop_tree_dead", 125.f, Rgb(0xDCE6F0))) DeadTree(base, s, Rgb(0x6A6A70), seed, false);
			break;
		case Biome::eCanyon:
			if (pick < 0.55f) { if (!thing("prop_rock_red", 54.f)) Rock(base, s, Rgb(0xB0603A), seed, false, false); }
			else if (pick < 0.85f) { if (!thing("prop_spire_red", 112.f)) Spike(base, s * 0.9f, Rgb(0xA05A38), seed, false, time); }
			else if (!tree("prop_tree_dead", 120.f, Rgb(0xE8C8A8))) DeadTree(base, s * 0.8f, Rgb(0x6A4A34), seed, false);
			break;
		case Biome::eVolcanic:
			if (pick < 0.45f)
			{
				if (thing("prop_spike_obsidian", 100.f)) { if (pick < 0.2f) glow(Rgb(0xFF7020), 80.f, 0.6f * pulse); }
				else Spike(base, s, Rgb(0x2E2A2E), seed, pick < 0.2f, time);
			}
			else if (pick < 0.7f)
			{
				if (thing("prop_lava_vent", 42.f)) g_lights.push_back({ Off(base, 0, -16 * s), 110.f * s, Rgb(0xFF6A20), 0.9f * pulse });
				else LavaVent(base, s, time, seed);
			}
			else if (!tree("prop_tree_burnt", 110.f)) DeadTree(base, s, Rgb(0x2A2222), seed, false);
			break;
		case Biome::eFairy:
			if (pick < 0.4f)
			{
				const bool pink = pick < 0.2f;
				if (thing(pink ? "prop_mushroom_pink" : "prop_mushroom_blue", 82.f)) glow(pink ? Rgb(0xE05AB0) : Rgb(0x5AB0F0), 60.f, 0.35f + 0.1f * std::sin(time * 1.7f + seed));
				else Mushroom(base, s, pink ? Rgb(0xE05AB0) : Rgb(0x5AB0F0), seed, time);
			}
			else if (pick < 0.7f)
			{
				const bool cyan = pick < 0.55f;
				if (thing(cyan ? "prop_crystal_cyan" : "prop_crystal_violet", 78.f)) glow(cyan ? Rgb(0x9AE8F0) : Rgb(0xE0A0F8), 70.f, 0.55f * pulse);
				else Crystal(base, s, cyan ? Rgb(0x9AE8F0) : Rgb(0xE0A0F8), seed, time);
			}
			else if (!tree("prop_tree_oak", 145.f, Rgb(0x9AE0D0))) RoundTree(base, s, Rgb(0x4ABFA0), Rgb(0x6A5A7A), sway, true, seed);
			break;
		case Biome::eRuins:
			if (pick < 0.45f) { if (!thing("prop_pillar", 105.f)) Pillar(base, s, seed); }
			else if (pick < 0.8f) { if (!thing(pick > 0.7f ? "prop_rock_moss" : "prop_rock_grey", 46.f)) Rock(base, s * 0.85f, Rgb(0x8A8494), seed, pick > 0.7f, false); }
			else if (!tree("prop_tree_dead", 125.f)) DeadTree(base, s, Rgb(0x5A4E4A), seed, false);
			break;
		case Biome::eOrcFort:
			if (pick < 0.45f) { if (!thing("prop_palisade", 80.f)) Palisade(base, s, seed); }
			else if (pick < 0.7f) { if (!thing("prop_barrel", 44.f)) Barrel(base, s); }
			else if (pick < 0.85f) { if (!thing("prop_rock_sand", 44.f)) Rock(base, s * 0.8f, Rgb(0x8A7A62), seed, false, false); }
			else if (!tree("prop_tree_dead", 125.f)) DeadTree(base, s, Rgb(0x5A4432), seed, false);
			break;
		case Biome::eSwamp:
			if (pick < 0.35f) { if (!tree("prop_tree_swamp", 140.f)) DeadTree(base, s * 1.1f, Rgb(0x4A4A36), seed, true); }
			else if (pick < 0.7f) { if (!PropSprite("prop_reeds", base, 62.f * s, sway * 1.5f, flip)) Reeds(base, s, time, seed); }
			else if (!thing("prop_rock_moss", 44.f, Rgb(0xC8D0B0))) Rock(base, s * 0.8f, Rgb(0x5E6450), seed, true, false);
			break;
		case Biome::eDesert:
			if (pick < 0.45f) { if (!thing("prop_cactus", 88.f)) Cactus(base, s, seed); }
			else if (pick < 0.8f) { if (!thing("prop_rock_sand", 48.f)) Rock(base, s * 0.9f, Rgb(0xC8A06A), seed, false, false); }
			else if (!thing("prop_bones", 28.f)) Bones(base, s);
			break;
		case Biome::eWasteland:
			if (pick < 0.5f) { if (!thing("prop_rock_rust", 52.f)) Rock(base, s, Rgb(0x9A4A30), seed, false, false); }
			else if (pick < 0.8f) { if (!tree("prop_tree_burnt", 110.f)) DeadTree(base, s, Rgb(0x4A3428), seed, false); }
			else if (!thing("prop_bones", 28.f)) Bones(base, s);
			break;
		case Biome::eGraveyard:
			if (pick < 0.45f) { if (!thing("prop_grave_round", 54.f)) Tombstone(base, s, 0); }
			else if (pick < 0.65f) { if (!thing("prop_grave_cross", 64.f)) Tombstone(base, s, 1); }
			else if (!tree("prop_tree_dead", 130.f, Rgb(0xB8B8C0))) DeadTree(base, s * 1.05f, Rgb(0x3A3634), seed, false);
			break;
		case Biome::eBeach:
			if (pick < 0.5f) { if (!tree("prop_tree_palm", 148.f)) Palm(base, s, time, seed); }
			else if (pick < 0.8f) { if (!thing("prop_rock_grey", 46.f)) Rock(base, s * 0.85f, Rgb(0x9AA0A6), seed, false, false); }
			else if (!thing("prop_stump", 28.f)) Stump(base, s * 0.8f);
			break;
		case Biome::eAntHill:
			if (pick < 0.55f) { if (!thing("prop_mound", 60.f)) Mound(base, s); }
			else if (pick < 0.8f) { if (!thing("prop_rock_sand", 44.f, Rgb(0xD8C0A8))) Rock(base, s * 0.8f, Rgb(0x8A6A4A), seed, false, false); }
			else if (!thing("prop_bones", 28.f)) Bones(base, s);
			break;
		case Biome::eHell:
			if (pick < 0.35f) { if (!thing("prop_spike_bone", 96.f)) Spike(base, s, Rgb(0xE6D8C0), seed, false, time); }
			else if (pick < 0.7f)
			{
				if (thing("prop_spike_obsidian", 100.f, Rgb(0xFFD8D0))) glow(Rgb(0xFF7020), 80.f, 0.6f * pulse);
				else Spike(base, s, Rgb(0x2A1A1E), seed, true, time);
			}
			else
			{
				if (thing("prop_lava_vent", 42.f)) g_lights.push_back({ Off(base, 0, -16 * s), 110.f * s, Rgb(0xFF6A20), 0.9f * pulse });
				else LavaVent(base, s, time, seed);
			}
			break;
		}
	}

	void DrawTownCell(int x, int y, char cell, float time)
	{
		const float X = static_cast<float>(x), Y = static_cast<float>(y);
		const char left = WorldData::TownCell(x - 1, y), up = WorldData::TownCell(x, y - 1);
		switch (cell)
		{
		case '#':
		{
			const bool tall = WorldData::TownCell(x - 1, y) == 'G' || WorldData::TownCell(x + 1, y) == 'G'
				|| WorldData::TownCell(x, y - 1) == 'G' || WorldData::TownCell(x, y + 1) == 'G';
			Wall(x, y, tall);
			break;
		}
		case 'O':
			if (left != 'O' && up != 'O')
				Tower(X, Y);
			break;
		case 'G':
		{
			// 문 한 줄(4칸)의 첫 칸에서만 들보를 그린다
			const bool horizontalRun = WorldData::TownCell(x + 1, y) == 'G' || left == 'G';
			if (horizontalRun && left != 'G')
				GateArch(X, Y, true);
			else if (!horizontalRun && up != 'G')
				GateArch(X, Y, false);
			break;
		}
		case 'T':
		{
			const Vector2 base = gfx::IsoPx(X, Y);
			gfx::SetDepth(X + Y);
			const int seed = static_cast<int>(util::Hash2(x, y, 9) & 0xFFFF);
			const float sway = std::sin(time * 1.2f + seed) * 1.5f;
			const bool blossom = (seed % 3) == 0;
			if (!PropSprite(blossom ? "prop_bush_flower" : "prop_tree_oak", base, blossom ? 66.f : 132.f, sway * 2.5f, (seed & 1) != 0))
				RoundTree(base, 1.05f, Rgb(0x5AA848), Rgb(0x7A5634), sway, blossom, seed);
			break;
		}
		case 'L':
		{
			const Vector2 base = gfx::IsoPx(X, Y);
			gfx::SetDepth(X + Y);
			if (PropSprite("prop_lamp", base, 96.f, 0.f, false))
			{
				const float flick = 0.85f + 0.15f * util::Noise1(static_cast<float>(GetTime()) * 6.f + X);
				g_lights.push_back({ gfx::IsoPx(X + 0.3f, Y + 0.3f), 150.f, Rgb(0xFFC870), 1.1f * flick });
			}
			else
				Lamp(X, Y);
			break;
		}
		case 'S':
			if (left != 'S' && up != 'S')
			{
				// 2x2 칸: 가운데 깊이, 그림 너비 = 칸 마름모 너비
				const float cx = X + 0.5f, cy = Y + 0.5f;
				gfx::SetDepth(cx + cy);
				const bool blue = x > WorldData::kTownX0 + 20;
				if (!PropSprite(blue ? "prop_stall_blue" : "prop_stall_red", Off(gfx::IsoPx(cx, cy), 0.f, 22.f), 0.f, 0.f, false, WHITE, 4.f * 32.f * 1.08f))
					Stall(X, Y, blue ? 1 : 0);
			}
			break;
		case 'F':
			if (left != 'F' && up != 'F')
			{
				const float cx = X + 1.5f, cy = Y + 1.5f;
				gfx::SetDepth(cx + cy);
				if (PropSprite("prop_fountain", Off(gfx::IsoPx(cx, cy), 0.f, 40.f), 0.f, 0.f, false, WHITE, 8.f * 32.f * 0.86f))
					g_lights.push_back({ gfx::IsoPx(cx, cy), 160.f, Rgb(0x90D0FF), 0.45f });   // 물빛
				else
					Fountain(X, Y, time);
			}
			break;
		default:
			break;
		}
	}

	void DrawBuilding(const WorldData::BuildingRow& b, float time)
	{
		using WorldData::BuildingKind;
		const float x0 = b.x0 - 0.5f, y0 = b.y0 - 0.5f, x1 = b.x1 + 0.5f, y1 = b.y1 + 0.5f;
		Color wall, roof, trim;
		float h = 2.1f, roofH = 1.5f;
		switch (b.kind)
		{
		case BuildingKind::eStore:      wall = Rgb(0xE8D8B8); roof = Rgb(0xB8483A); trim = Rgb(0x6A4A2E); break;
		case BuildingKind::eWeaponShop: wall = Rgb(0x8A8278); roof = Rgb(0x3A3E48); trim = Rgb(0x4A3A2E); h = 2.0f; break;
		case BuildingKind::eTemple:     wall = Rgb(0xF2EEE6); roof = Rgb(0x3A6AB0); trim = Rgb(0xC8B070); h = 2.6f; roofH = 1.9f; break;
		case BuildingKind::eInn:        wall = Rgb(0xB08A5A); roof = Rgb(0x3E7A4A); trim = Rgb(0x5A3E26); h = 2.3f; break;
		default:
		{
			const int v = (b.x0 * 3 + b.y0) % 3;
			wall = v == 0 ? Rgb(0xE0D0B0) : (v == 1 ? Rgb(0xD8C8A8) : Rgb(0xC8B898));
			roof = v == 0 ? Rgb(0xA8583A) : (v == 1 ? Rgb(0x6A5A8A) : Rgb(0x8A6A3A));
			trim = Rgb(0x6A4A2E);
			h = 1.5f; roofH = 1.1f;
			break;
		}
		}

		// 그린 텍스처(있으면): 벽 = 회칠 목조 · 돌, 지붕 = 붉은 기와 · 청석판(건물마다 색을 곱한다)
		const Texture2D* plaster = envart::Get(envart::Tex::Plaster);
		const Texture2D* stoneTex = envart::Get(envart::Tex::Stone);
		const Texture2D* wallTex = nullptr;
		const Texture2D* roofTex = nullptr;
		Color wallMul = WHITE, roofMul = WHITE;
		switch (b.kind)
		{
		case BuildingKind::eStore:      wallTex = plaster; roofTex = envart::Get(envart::Tex::RoofRed); break;
		case BuildingKind::eWeaponShop: wallTex = stoneTex; wallMul = Rgb(0xD8D4CC); roofTex = envart::Get(envart::Tex::RoofSlate); break;
		case BuildingKind::eTemple:     wallTex = stoneTex; wallMul = Rgb(0xFFFCF4); roofTex = envart::Get(envart::Tex::RoofSlate); roofMul = Rgb(0xB0C8FF); break;
		case BuildingKind::eInn:        wallTex = plaster; wallMul = Rgb(0xF2E4CC); roofTex = envart::Get(envart::Tex::RoofSlate); roofMul = Rgb(0xB8E0B0); break;
		default:
		{
			const int v = (b.x0 * 3 + b.y0) % 3;
			wallTex = plaster;
			wallMul = v == 0 ? WHITE : (v == 1 ? Rgb(0xF4EEE4) : Rgb(0xE8DCC8));
			roofTex = envart::Get(v == 1 ? envart::Tex::RoofSlate : envart::Tex::RoofRed);
			roofMul = v == 1 ? Rgb(0xD0C4F0) : (v == 2 ? Rgb(0xF0D8B8) : WHITE);
			break;
		}
		}

		// 기초
		if (stoneTex)
			gfx::BoxTex(*stoneTex, x0 - 0.05f, y0 - 0.05f, x1 + 0.05f, y1 + 0.05f, 0.f, 0.18f, 1.0f, Rgb(0xC8C4BC), Rgb(0x9A968E), Rgb(0x7A766E));
		else
			gfx::Box(x0 - 0.05f, y0 - 0.05f, x1 + 0.05f, y1 + 0.05f, 0.f, 0.18f, Rgb(0x8A857A), Rgb(0x6E6A62), Rgb(0x5A564E));
		// 벽
		if (wallTex)
			gfx::BoxTex(*wallTex, x0, y0, x1, y1, 0.18f, h, 2.0f, wallMul, Shade(wallMul, 0.96f), Shade(wallMul, 0.72f));
		else
			gfx::Box(x0, y0, x1, y1, 0.18f, h, util::Shade(wall, 1.1f), wall, util::Shade(wall, 0.78f));

		// 목조 뼈대(잡화점/집/여관) — 회칠 목조 텍스처에는 이미 그려져 있다
		if (!wallTex && (b.kind == BuildingKind::eStore || b.kind == BuildingKind::eHouse || b.kind == BuildingKind::eInn))
		{
			const Color beam = trim;
			for (float x = x0; x <= x1 + 0.01f; x += 2.f)
				gfx::Face({ x - 0.05f, y1 + 0.01f, 0.18f }, { x + 0.05f, y1 + 0.01f, 0.18f }, { x + 0.05f, y1 + 0.01f, h }, { x - 0.05f, y1 + 0.01f, h }, beam);
			for (float y = y0; y <= y1 + 0.01f; y += 2.f)
				gfx::Face({ x1 + 0.01f, y - 0.05f, 0.18f }, { x1 + 0.01f, y + 0.05f, 0.18f }, { x1 + 0.01f, y + 0.05f, h }, { x1 + 0.01f, y - 0.05f, h }, util::Shade(beam, 0.8f));
			gfx::Face({ x0, y1 + 0.01f, h * 0.55f }, { x1, y1 + 0.01f, h * 0.55f }, { x1, y1 + 0.01f, h * 0.55f + 0.1f }, { x0, y1 + 0.01f, h * 0.55f + 0.1f }, beam);
			gfx::Face({ x1 + 0.01f, y0, h * 0.55f }, { x1 + 0.01f, y1, h * 0.55f }, { x1 + 0.01f, y1, h * 0.55f + 0.1f }, { x1 + 0.01f, y0, h * 0.55f + 0.1f }, util::Shade(beam, 0.8f));
		}
		// 예배당 기둥
		if (b.kind == BuildingKind::eTemple)
		{
			for (float x = x0 + 0.6f; x < x1 - 0.3f; x += 1.2f)
				gfx::Box(x - 0.15f, y1 + 0.05f, x + 0.15f, y1 + 0.35f, 0.18f, h, Rgb(0xFFFFFF), Rgb(0xE8E4DC), Rgb(0xC8C4BC));
			gfx::Box(x0 - 0.1f, y1 - 0.1f, x1 + 0.1f, y1 + 0.45f, h, h + 0.2f, Rgb(0xFFFFFF), Rgb(0xE6E0D6), Rgb(0xC8C2B8));
		}

		// 문과 창문(남쪽 면 = 광장 쪽이 아닐 수도 있지만 보이는 면에 둔다)
		const float midX = (x0 + x1) * 0.5f;
		const float doorH = std::min(1.25f, h - 0.4f);
		Door(midX - 0.4f, midX + 0.4f, y1, doorH + 0.18f);
		const float wz0 = h * 0.5f, wz1 = h * 0.5f + 0.5f;
		if (x1 - x0 >= 5.f)
		{
			Window({ x0 + 0.8f, y1, 0 }, { x0 + 1.6f, y1, 0 }, wz0, wz1, false);
			Window({ x1 - 1.6f, y1, 0 }, { x1 - 0.8f, y1, 0 }, wz0, wz1, false);
		}
		else
			Window({ x0 + 0.5f, y1, 0 }, { x0 + 1.1f, y1, 0 }, wz0, wz1, false);
		for (float y = y0 + 0.9f; y + 0.8f < y1; y += 2.2f)
			Window({ x1, y, 0 }, { x1, y + 0.7f, 0 }, wz0, wz1, true);

		// 박공지붕(용마루는 긴 축 방향)
		const float o = 0.28f;
		const float top = h + roofH;
		const Color rs = roof, rn = util::Shade(roof, 0.75f), re = util::Shade(roof, 0.9f);
		if (roofTex)
		{
			// 기와 줄은 용마루와 나란히(u = 용마루 방향, v = 용마루 → 처마)
			constexpr float kRoofUnit = 1.4f;
			if ((x1 - x0) >= (y1 - y0))
			{
				const float yc = (y0 + y1) * 0.5f;
				const float slope = std::sqrt((yc - y0 + o) * (yc - y0 + o) + roofH * roofH);
				const float len = x1 - x0 + 2.f * o;
				gfx::FaceTex(*roofTex, { x0 - o, y0 - o, h }, { x1 + o, y0 - o, h }, { x1 + o, yc, top }, { x0 - o, yc, top }, len / kRoofUnit, slope / kRoofUnit, Shade(roofMul, 0.7f));
				gfx::FaceTri({ x1, y0, h }, { x1, y1, h }, { x1, yc, top }, util::Shade(wall, 0.72f));
				gfx::FaceTex(*roofTex, { x0 - o, y1 + o, h }, { x1 + o, y1 + o, h }, { x1 + o, yc, top }, { x0 - o, yc, top }, len / kRoofUnit, slope / kRoofUnit, roofMul);
				gfx::Face({ x1 + o, yc, top }, { x1 + o, y1 + o, h }, { x1 + o + 0.02f, y1 + o, h - 0.08f }, { x1 + o + 0.02f, yc, top - 0.08f }, Shade(roofMul, 0.45f));
			}
			else
			{
				const float xc = (x0 + x1) * 0.5f;
				const float slope = std::sqrt((xc - x0 + o) * (xc - x0 + o) + roofH * roofH);
				const float len = y1 - y0 + 2.f * o;
				gfx::FaceTex(*roofTex, { x0 - o, y1 + o, h }, { x0 - o, y0 - o, h }, { xc, y0 - o, top }, { xc, y1 + o, top }, len / kRoofUnit, slope / kRoofUnit, Shade(roofMul, 0.78f));
				gfx::FaceTri({ x0, y1, h }, { x1, y1, h }, { xc, y1, top }, util::Shade(wall, 0.95f));
				gfx::FaceTex(*roofTex, { x1 + o, y0 - o, h }, { x1 + o, y1 + o, h }, { xc, y1 + o, top }, { xc, y0 - o, top }, len / kRoofUnit, slope / kRoofUnit, Shade(roofMul, 0.86f));
			}
		}
		else if ((x1 - x0) >= (y1 - y0))
		{
			const float yc = (y0 + y1) * 0.5f;
			gfx::Face({ x0 - o, y0 - o, h }, { x1 + o, y0 - o, h }, { x1 + o, yc, top }, { x0 - o, yc, top }, rn);        // 북사면
			gfx::FaceTri({ x1, y0, h }, { x1, y1, h }, { x1, yc, top }, util::Shade(wall, 0.72f));                       // 동쪽 박공
			gfx::Face({ x0 - o, yc, top }, { x1 + o, yc, top }, { x1 + o, y1 + o, h }, { x0 - o, y1 + o, h }, rs);        // 남사면
			// 기와 줄
			for (int i = 1; i < 5; ++i)
			{
				const float t = i / 5.f;
				const float yy = util::Lerp(yc, y1 + o, t), zz = util::Lerp(top, h, t);
				gfx::Face({ x0 - o, yy - 0.03f, zz + 0.02f }, { x1 + o, yy - 0.03f, zz + 0.02f }, { x1 + o, yy + 0.03f, zz - 0.02f }, { x0 - o, yy + 0.03f, zz - 0.02f }, util::Shade(rs, 0.8f));
			}
			gfx::Face({ x1 + o, yc, top }, { x1 + o, y1 + o, h }, { x1 + o + 0.02f, y1 + o, h - 0.08f }, { x1 + o + 0.02f, yc, top - 0.08f }, util::Shade(re, 0.7f));
		}
		else
		{
			const float xc = (x0 + x1) * 0.5f;
			gfx::Face({ x0 - o, y0 - o, h }, { x0 - o, y1 + o, h }, { xc, y1 + o, top }, { xc, y0 - o, top }, rn);
			gfx::FaceTri({ x0, y1, h }, { x1, y1, h }, { xc, y1, top }, util::Shade(wall, 0.95f));
			gfx::Face({ xc, y0 - o, top }, { xc, y1 + o, top }, { x1 + o, y1 + o, h }, { x1 + o, y0 - o, h }, util::Shade(rs, 0.85f));
			for (int i = 1; i < 5; ++i)
			{
				const float t = i / 5.f;
				const float xx = util::Lerp(xc, x1 + o, t), zz = util::Lerp(top, h, t);
				gfx::Face({ xx - 0.03f, y0 - o, zz + 0.02f }, { xx - 0.03f, y1 + o, zz + 0.02f }, { xx + 0.03f, y1 + o, zz - 0.02f }, { xx + 0.03f, y0 - o, zz - 0.02f }, util::Shade(rs, 0.7f));
			}
		}

		// 굴뚝(대장간은 연기, 집/여관은 장식)
		if (b.kind == BuildingKind::eWeaponShop || b.kind == BuildingKind::eInn || b.kind == BuildingKind::eHouse)
		{
			const float cx = x0 + (x1 - x0) * 0.75f, cy = y0 + (y1 - y0) * 0.3f;
			if (stoneTex)
				gfx::BoxTex(*stoneTex, cx - 0.25f, cy - 0.25f, cx + 0.25f, cy + 0.25f, h, top + 0.5f, 0.8f, Rgb(0xC0A090), Rgb(0xA08070), Rgb(0x786052));
			else
				gfx::Box(cx - 0.25f, cy - 0.25f, cx + 0.25f, cy + 0.25f, h, top + 0.5f, Rgb(0x8A5A4A), Rgb(0x7A4A3A), Rgb(0x5A3A2E));
			if (b.kind == BuildingKind::eWeaponShop)
				g_lights.push_back({ gfx::IsoPx(x1 + 0.5f, y1 + 0.5f), 140.f, Rgb(0xFF8A30), 0.8f });
		}
		// 예배당 종탑
		if (b.kind == BuildingKind::eTemple)
		{
			const float cx = x0 + 1.2f, cy = (y0 + y1) * 0.5f;
			gfx::Box(cx - 0.6f, cy - 0.6f, cx + 0.6f, cy + 0.6f, h, top + 1.4f, Rgb(0xFFFFFF), Rgb(0xE8E4DC), Rgb(0xC8C4BC));
			const P3 apex{ cx, cy, top + 3.0f };
			const float t2 = top + 1.4f;
			gfx::FaceTri({ cx - 0.75f, cy - 0.75f, t2 }, { cx + 0.75f, cy - 0.75f, t2 }, apex, util::Shade(roof, 0.8f));
			gfx::FaceTri({ cx - 0.75f, cy - 0.75f, t2 }, { cx - 0.75f, cy + 0.75f, t2 }, apex, util::Shade(roof, 0.9f));
			gfx::FaceTri({ cx - 0.75f, cy + 0.75f, t2 }, { cx + 0.75f, cy + 0.75f, t2 }, apex, util::Shade(roof, 1.1f));
			gfx::FaceTri({ cx + 0.75f, cy - 0.75f, t2 }, { cx + 0.75f, cy + 0.75f, t2 }, apex, util::Shade(roof, 0.75f));
			const Vector2 sym = gfx::IsoPx(apex.x, apex.y, apex.z);
			gfx::SetDepth(cx + cy + 1.f);
			gfx::Line(sym, Off(sym, 0, -16), 2.5f, Rgb(0xF2D060));
			gfx::Line(Off(sym, -6, -10), Off(sym, 6, -10), 2.5f, Rgb(0xF2D060));
			g_lights.push_back({ gfx::IsoPx(midX, y1 + 0.8f), 180.f, Rgb(0xFFF0C0), 0.7f });
		}
		(void)time;
	}

	void SetNight(float night) { g_night = util::Clamp01(night); }

	Palette PaletteFor(int regionId)
	{
		using WorldData::Biome;
		const auto* r = WorldData::FindRegion(regionId);
		const Biome biome = r ? r->biome : Biome::eMeadow;
		auto P = [](uint32_t a, uint32_t b, uint32_t c)
		{
			Palette p{};
			const Color ca = Rgb(a), cb = Rgb(b), cc = Rgb(c);
			p.a[0] = ca.r / 255.f; p.a[1] = ca.g / 255.f; p.a[2] = ca.b / 255.f;
			p.b[0] = cb.r / 255.f; p.b[1] = cb.g / 255.f; p.b[2] = cb.b / 255.f;
			p.c[0] = cc.r / 255.f; p.c[1] = cc.g / 255.f; p.c[2] = cc.b / 255.f;
			return p;
		};
		switch (biome)
		{
		case Biome::eMeadow:    return regionId == 9 ? P(0x5E9E58, 0x4A8A48, 0xF0F0FA) : P(0x6B9E47, 0x558538, 0xF2F2FA);
		case Biome::eForest:    return P(0x416B33, 0x2F5226, 0x735433);
		case Biome::eSnow:      return P(0xEBF2FC, 0xB8CCE6, 0xFFFFFF);
		case Biome::eCanyon:    return P(0xB86B47, 0x9A5238, 0x5A2E1E);
		case Biome::eVolcanic:  return P(0x33292B, 0x1F1A1A, 0xFF6A1A);
		case Biome::eFairy:     return P(0x4E9E85, 0x3A8073, 0xCCFFF2);
		case Biome::eRuins:     return P(0x85808C, 0x6B6678, 0x5A6B3E);
		case Biome::eOrcFort:   return P(0x856645, 0x6B5233, 0x66803A);
		case Biome::eSwamp:     return P(0x4D5433, 0x383F26, 0x334D47);
		case Biome::eDesert:    return P(0xE6C785, 0xCCA86B, 0xF2DB9E);
		case Biome::eWasteland: return P(0x9E4D33, 0x803D29, 0x4D1F14);
		case Biome::eGraveyard: return P(0x4D5947, 0x383F38, 0xD9D1B8);
		case Biome::eBeach:     return P(0xEDD9A8, 0xCCB88C, 0x3F99BF);
		case Biome::eAntHill:   return P(0x805938, 0x66452B, 0x261A12);
		case Biome::eHell:      return P(0x401414, 0x260D0F, 0xFF4D0D);
		}
		return P(0x6B9E47, 0x558538, 0xF2F2FA);
	}
}
