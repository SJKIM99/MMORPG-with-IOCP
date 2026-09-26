#include "render/Draw.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace gfx
{
	namespace
	{
		float g_camXY = 0.f;
		float g_zoom = 1.f;
		float g_z = -0.5f;
		Color g_tint{ 255, 255, 255, 255 };
		float g_flash = 0.f;
		Color g_flashColor{ 255, 255, 255, 255 };
		float g_alpha = 1.f;
		bool g_modActive = false;
		bool g_depthWrite = true;
		bool g_additive = false;
		bool g_emissiveOnly = false;
		// Phase 4-A 지형 높이
		GroundLiftFn g_liftFn = nullptr;
		float g_lift = 0.f;
		bool g_drape = false;
		constexpr float kDrapeBias = 0.08f;   // 데칼 깊이를 카메라 쪽으로 조금(같은 면의 지형과 겨루지 않게)

		// 보이는 X+Y 범위(카메라 기준 ±80칸 정도)를 깊이 [0.05, 0.95]로 편다.
		constexpr float kDepthSpan = 220.f;

		inline void V(Vector2 p, float z)
		{
			if (g_drape && g_liftFn)
			{
				rlVertex3f(p.x, p.y - g_liftFn(p), DepthOf(p.y / kHalfH + kDrapeBias));
				return;
			}
			rlVertex3f(p.x, p.y - g_lift, z);
		}
		inline void C(Color c) { rlColor4ub(c.r, c.g, c.b, c.a); }

		int Segments(float radius)
		{
			const float px = radius * g_zoom;
			int seg = static_cast<int>(px * 0.6f);
			return seg < 10 ? 10 : (seg > 56 ? 56 : seg);
		}

		Vector2 Iso(const P3& p) { return IsoPx(p.x, p.y, p.z); }
		float Zof(const P3& p) { return DepthOf(p.x + p.y); }
	}

	void BeginWorld(float cameraDepthXY, float zoom)
	{
		g_camXY = cameraDepthXY;
		g_zoom = zoom;
		g_lift = 0.f;
		g_drape = false;
		ResetMod();
	}

	float DepthOf(float xPlusY)
	{
		float d = 0.5f - (xPlusY - g_camXY) / kDepthSpan;
		d = d < 0.02f ? 0.02f : (d > 0.98f ? 0.98f : d);
		return -d;
	}

	float Zoom() { return g_zoom; }

	void SetGroundLiftFn(GroundLiftFn fn) { g_liftFn = fn; }
	float LiftAt(Vector2 groundIso) { return g_liftFn ? g_liftFn(groundIso) : 0.f; }
	void SetLift(float px) { g_lift = px; }
	float Lift() { return g_lift; }
	void SetDrape(bool on) { g_drape = on; }
	bool DrapeOn() { return g_drape; }

	void SetDepth(float xPlusY) { g_z = DepthOf(xPlusY); }
	void SetDepthRaw(float z) { g_z = z; }
	float CurrentZ() { return g_z; }

	void SetTint(Color tint) { g_tint = tint; g_modActive = true; }
	void SetFlash(float amount) { g_flash = util::Clamp01(amount); g_modActive = true; }
	void SetFlashColor(Color c) { g_flashColor = c; }
	void SetAlpha(float alpha) { g_alpha = util::Clamp01(alpha); g_modActive = true; }
	void ResetMod()
	{
		g_tint = Color{ 255, 255, 255, 255 };
		g_flash = 0.f;
		g_flashColor = Color{ 255, 255, 255, 255 };
		g_alpha = 1.f;
		g_modActive = false;
	}

	Color Mod(Color c)
	{
		if (g_emissiveOnly && !g_additive)
			return Color{ 0, 0, 0, 0 };
		if (!g_modActive)
			return c;
		Color out{
			static_cast<unsigned char>(c.r * g_tint.r / 255),
			static_cast<unsigned char>(c.g * g_tint.g / 255),
			static_cast<unsigned char>(c.b * g_tint.b / 255),
			static_cast<unsigned char>(c.a * g_tint.a / 255 * g_alpha) };
		if (g_flash > 0.f)
		{
			out.r = static_cast<unsigned char>(util::Lerp(out.r, g_flashColor.r, g_flash));
			out.g = static_cast<unsigned char>(util::Lerp(out.g, g_flashColor.g, g_flash));
			out.b = static_cast<unsigned char>(util::Lerp(out.b, g_flashColor.b, g_flash));
		}
		return out;
	}

	void Flush() { rlDrawRenderBatchActive(); }
	void DepthTest(bool on) { rlDrawRenderBatchActive(); on ? rlEnableDepthTest() : rlDisableDepthTest(); }
	void DepthWrite(bool on) { rlDrawRenderBatchActive(); on ? rlEnableDepthMask() : rlDisableDepthMask(); g_depthWrite = on; }
	bool DepthWriteOn() { return g_depthWrite; }
	float Alpha() { return g_alpha; }
	Color Tint() { return g_tint; }
	float Flash() { return g_flash; }
	Color FlashColor() { return g_flashColor; }
	void Additive(bool on)
	{
		rlDrawRenderBatchActive();
		g_additive = on;
		if (on || g_emissiveOnly) rlSetBlendMode(RL_BLEND_ADDITIVE);
		else rlSetBlendMode(RL_BLEND_ALPHA);
	}
	bool AdditiveOn() { return g_additive; }
	void EmissiveOnly(bool on)
	{
		rlDrawRenderBatchActive();
		g_emissiveOnly = on;
		g_additive = false;
		rlSetBlendMode(on ? RL_BLEND_ADDITIVE : RL_BLEND_ALPHA);
	}
	bool EmissiveOnlyOn() { return g_emissiveOnly; }

	// ─────────────────────────────────────────────────────────────────────────
	void Tri(Vector2 a, Vector2 b, Vector2 c, Color col)
	{
		col = Mod(col);
		if (col.a == 0) return;
		rlBegin(RL_TRIANGLES);
		C(col);
		V(a, g_z); V(b, g_z); V(c, g_z);
		rlEnd();
	}

	void Tri3(Vector2 a, Vector2 b, Vector2 c, Color ca, Color cb, Color cc)
	{
		rlBegin(RL_TRIANGLES);
		C(Mod(ca)); V(a, g_z);
		C(Mod(cb)); V(b, g_z);
		C(Mod(cc)); V(c, g_z);
		rlEnd();
	}

	void Quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col)
	{
		col = Mod(col);
		if (col.a == 0) return;
		rlBegin(RL_TRIANGLES);
		C(col);
		V(a, g_z); V(b, g_z); V(c, g_z);
		V(a, g_z); V(c, g_z); V(d, g_z);
		rlEnd();
	}

	void Quad4(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color ca, Color cb, Color cc, Color cd)
	{
		ca = Mod(ca); cb = Mod(cb); cc = Mod(cc); cd = Mod(cd);
		rlBegin(RL_TRIANGLES);
		C(ca); V(a, g_z); C(cb); V(b, g_z); C(cc); V(c, g_z);
		C(ca); V(a, g_z); C(cc); V(c, g_z); C(cd); V(d, g_z);
		rlEnd();
	}

	void Circle(Vector2 c, float r, Color col)
	{
		Ellipse(c, r, r, col);
	}

	void CircleGrad(Vector2 c, float r, Color inner, Color outer)
	{
		EllipseGrad(c, r, r, inner, outer);
	}

	void Ellipse(Vector2 c, float rx, float ry, Color col)
	{
		col = Mod(col);
		if (col.a == 0 || rx <= 0.f || ry <= 0.f) return;
		const int seg = Segments(std::max(rx, ry));
		const float step = util::kPi * 2.f / seg;
		rlBegin(RL_TRIANGLES);
		C(col);
		for (int i = 0; i < seg; ++i)
		{
			const float a0 = i * step, a1 = (i + 1) * step;
			V(c, g_z);
			V(Vector2{ c.x + std::cos(a1) * rx, c.y + std::sin(a1) * ry }, g_z);
			V(Vector2{ c.x + std::cos(a0) * rx, c.y + std::sin(a0) * ry }, g_z);
		}
		rlEnd();
	}

	void EllipseGrad(Vector2 c, float rx, float ry, Color inner, Color outer)
	{
		inner = Mod(inner); outer = Mod(outer);
		if ((inner.a == 0 && outer.a == 0) || rx <= 0.f || ry <= 0.f) return;
		const int seg = Segments(std::max(rx, ry));
		const float step = util::kPi * 2.f / seg;
		rlBegin(RL_TRIANGLES);
		for (int i = 0; i < seg; ++i)
		{
			const float a0 = i * step, a1 = (i + 1) * step;
			C(inner); V(c, g_z);
			C(outer);
			V(Vector2{ c.x + std::cos(a1) * rx, c.y + std::sin(a1) * ry }, g_z);
			V(Vector2{ c.x + std::cos(a0) * rx, c.y + std::sin(a0) * ry }, g_z);
		}
		rlEnd();
	}

	void EllipseArc(Vector2 c, float rx, float ry, float a0, float a1, float thickness, Color col)
	{
		col = Mod(col);
		if (col.a == 0) return;
		const int seg = std::max(4, static_cast<int>(Segments(std::max(rx, ry)) * std::fabs(a1 - a0) / (util::kPi * 2.f)) + 2);
		const float step = (a1 - a0) / seg;
		const float h = thickness * 0.5f;
		rlBegin(RL_TRIANGLES);
		C(col);
		for (int i = 0; i < seg; ++i)
		{
			const float t0 = a0 + i * step, t1 = a0 + (i + 1) * step;
			const Vector2 o0{ c.x + std::cos(t0) * (rx + h), c.y + std::sin(t0) * (ry + h * ry / std::max(rx, 1.f)) };
			const Vector2 i0{ c.x + std::cos(t0) * (rx - h), c.y + std::sin(t0) * (ry - h * ry / std::max(rx, 1.f)) };
			const Vector2 o1{ c.x + std::cos(t1) * (rx + h), c.y + std::sin(t1) * (ry + h * ry / std::max(rx, 1.f)) };
			const Vector2 i1{ c.x + std::cos(t1) * (rx - h), c.y + std::sin(t1) * (ry - h * ry / std::max(rx, 1.f)) };
			V(i0, g_z); V(o0, g_z); V(o1, g_z);
			V(i0, g_z); V(o1, g_z); V(i1, g_z);
		}
		rlEnd();
	}

	void Ring(Vector2 c, float r0, float r1, Color col, float a0, float a1)
	{
		col = Mod(col);
		if (col.a == 0 || r1 <= r0) return;
		const int seg = std::max(6, static_cast<int>(Segments(r1) * std::fabs(a1 - a0) / (util::kPi * 2.f)) + 2);
		const float step = (a1 - a0) / seg;
		rlBegin(RL_TRIANGLES);
		C(col);
		for (int i = 0; i < seg; ++i)
		{
			const float t0 = a0 + i * step, t1 = a0 + (i + 1) * step;
			const Vector2 i0{ c.x + std::cos(t0) * r0, c.y + std::sin(t0) * r0 };
			const Vector2 o0{ c.x + std::cos(t0) * r1, c.y + std::sin(t0) * r1 };
			const Vector2 i1{ c.x + std::cos(t1) * r0, c.y + std::sin(t1) * r0 };
			const Vector2 o1{ c.x + std::cos(t1) * r1, c.y + std::sin(t1) * r1 };
			V(i0, g_z); V(o0, g_z); V(o1, g_z);
			V(i0, g_z); V(o1, g_z); V(i1, g_z);
		}
		rlEnd();
	}

	void RingGrad(Vector2 c, float r0, float r1, Color inner, Color outer)
	{
		inner = Mod(inner); outer = Mod(outer);
		if (r1 <= r0) return;
		const int seg = Segments(r1);
		const float step = util::kPi * 2.f / seg;
		rlBegin(RL_TRIANGLES);
		for (int i = 0; i < seg; ++i)
		{
			const float t0 = i * step, t1 = (i + 1) * step;
			const Vector2 i0{ c.x + std::cos(t0) * r0, c.y + std::sin(t0) * r0 };
			const Vector2 o0{ c.x + std::cos(t0) * r1, c.y + std::sin(t0) * r1 };
			const Vector2 i1{ c.x + std::cos(t1) * r0, c.y + std::sin(t1) * r0 };
			const Vector2 o1{ c.x + std::cos(t1) * r1, c.y + std::sin(t1) * r1 };
			C(inner); V(i0, g_z); C(outer); V(o0, g_z); V(o1, g_z);
			C(inner); V(i0, g_z); C(outer); V(o1, g_z); C(inner); V(i1, g_z);
		}
		rlEnd();
	}

	void Line(Vector2 a, Vector2 b, float w, Color col)
	{
		const Vector2 d = util::Sub(b, a);
		const float len = util::Len(d);
		if (len < 1e-4f) return;
		const Vector2 n{ -d.y / len * w * 0.5f, d.x / len * w * 0.5f };
		Quad(util::Add(a, n), util::Add(b, n), util::Sub(b, n), util::Sub(a, n), col);
	}

	void LineGrad(Vector2 a, Vector2 b, float w0, float w1, Color ca, Color cb)
	{
		const Vector2 d = util::Sub(b, a);
		const float len = util::Len(d);
		if (len < 1e-4f) return;
		const Vector2 n{ -d.y / len, d.x / len };
		const Vector2 n0 = util::Mul(n, w0 * 0.5f), n1 = util::Mul(n, w1 * 0.5f);
		Quad4(util::Add(a, n0), util::Add(b, n1), util::Sub(b, n1), util::Sub(a, n0), ca, cb, cb, ca);
	}

	void Capsule(Vector2 a, Vector2 b, float r, Color col)
	{
		Line(a, b, r * 2.f, col);
		Circle(a, r, col);
		Circle(b, r, col);
	}

	void Poly(const Vector2* pts, int n, Color col)
	{
		col = Mod(col);
		if (n < 3 || col.a == 0) return;
		rlBegin(RL_TRIANGLES);
		C(col);
		for (int i = 1; i + 1 < n; ++i)
		{
			V(pts[0], g_z); V(pts[i], g_z); V(pts[i + 1], g_z);
		}
		rlEnd();
	}

	void PolyGrad(const Vector2* pts, int n, Vector2 center, Color centerCol, Color edgeCol)
	{
		centerCol = Mod(centerCol); edgeCol = Mod(edgeCol);
		if (n < 3) return;
		rlBegin(RL_TRIANGLES);
		for (int i = 0; i < n; ++i)
		{
			const Vector2& p0 = pts[i];
			const Vector2& p1 = pts[(i + 1) % n];
			C(centerCol); V(center, g_z);
			C(edgeCol); V(p0, g_z); V(p1, g_z);
		}
		rlEnd();
	}

	void Star(Vector2 c, float r0, float r1, int points, float rot, Color col)
	{
		col = Mod(col);
		if (col.a == 0 || points < 2) return;
		const float step = util::kPi / points;
		rlBegin(RL_TRIANGLES);
		C(col);
		for (int i = 0; i < points * 2; ++i)
		{
			const float a0 = rot + i * step, a1 = rot + (i + 1) * step;
			const float ra = (i % 2 == 0) ? r1 : r0;
			const float rb = (i % 2 == 0) ? r0 : r1;
			V(c, g_z);
			V(Vector2{ c.x + std::cos(a0) * ra, c.y + std::sin(a0) * ra }, g_z);
			V(Vector2{ c.x + std::cos(a1) * rb, c.y + std::sin(a1) * rb }, g_z);
		}
		rlEnd();
	}

	// ─────────────────────────────────────────────────────────────────────────
	void Face(P3 a, P3 b, P3 c, P3 d, Color col)
	{
		col = Mod(col);
		if (col.a == 0) return;
		const float za = Zof(a), zb = Zof(b), zc = Zof(c), zd = Zof(d);
		rlBegin(RL_TRIANGLES);
		C(col);
		V(Iso(a), za); V(Iso(b), zb); V(Iso(c), zc);
		V(Iso(a), za); V(Iso(c), zc); V(Iso(d), zd);
		rlEnd();
	}

	void Face4(P3 a, P3 b, P3 c, P3 d, Color ca, Color cb, Color cc, Color cd)
	{
		ca = Mod(ca); cb = Mod(cb); cc = Mod(cc); cd = Mod(cd);
		const float za = Zof(a), zb = Zof(b), zc = Zof(c), zd = Zof(d);
		rlBegin(RL_TRIANGLES);
		C(ca); V(Iso(a), za); C(cb); V(Iso(b), zb); C(cc); V(Iso(c), zc);
		C(ca); V(Iso(a), za); C(cc); V(Iso(c), zc); C(cd); V(Iso(d), zd);
		rlEnd();
	}

	void FaceTri(P3 a, P3 b, P3 c, Color col)
	{
		col = Mod(col);
		if (col.a == 0) return;
		rlBegin(RL_TRIANGLES);
		C(col);
		V(Iso(a), Zof(a)); V(Iso(b), Zof(b)); V(Iso(c), Zof(c));
		rlEnd();
	}

	void Box(float x0, float y0, float x1, float y1, float z0, float z1, Color top, Color south, Color east)
	{
		// 남(+Y) 면: y = y1
		Face({ x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 }, south);
		// 동(+X) 면: x = x1
		Face({ x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x1, y0, z1 }, east);
		// 윗면
		Face({ x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 }, top);
	}

	void FaceTex(const Texture2D& tex, P3 bl, P3 br, P3 tr, P3 tl, float repU, float repV, Color col)
	{
		col = Mod(col);
		if (col.a == 0 || tex.id == 0) return;
		repU = std::max(0.01f, repU);
		repV = std::max(0.01f, repV);
		const int nu = std::max(1, static_cast<int>(std::ceil(repU - 0.001f)));
		const int nv = std::max(1, static_cast<int>(std::ceil(repV - 0.001f)));
		auto at = [&](float s, float t) -> P3
		{
			// s: 0(왼) → 1(오른), t: 0(위) → 1(아래)
			const P3 top{ tl.x + (tr.x - tl.x) * s, tl.y + (tr.y - tl.y) * s, tl.z + (tr.z - tl.z) * s };
			const P3 bot{ bl.x + (br.x - bl.x) * s, bl.y + (br.y - bl.y) * s, bl.z + (br.z - bl.z) * s };
			return P3{ top.x + (bot.x - top.x) * t, top.y + (bot.y - top.y) * t, top.z + (bot.z - top.z) * t };
		};
		rlSetTexture(tex.id);
		rlBegin(RL_QUADS);
		C(col);
		rlNormal3f(0.f, 0.f, 1.f);
		for (int j = 0; j < nv; ++j)
		{
			const float v0 = static_cast<float>(j), v1 = std::min(repV, static_cast<float>(j + 1));
			for (int i = 0; i < nu; ++i)
			{
				const float u0 = static_cast<float>(i), u1 = std::min(repU, static_cast<float>(i + 1));
				const P3 p00 = at(u0 / repU, v0 / repV), p10 = at(u1 / repU, v0 / repV);
				const P3 p11 = at(u1 / repU, v1 / repV), p01 = at(u0 / repU, v1 / repV);
				const float tu1 = u1 - u0, tv1 = v1 - v0;
				rlTexCoord2f(0.f, 0.f); V(Iso(p00), Zof(p00));
				rlTexCoord2f(0.f, tv1); V(Iso(p01), Zof(p01));
				rlTexCoord2f(tu1, tv1); V(Iso(p11), Zof(p11));
				rlTexCoord2f(tu1, 0.f); V(Iso(p10), Zof(p10));
			}
		}
		rlEnd();
		rlTexCoord2f(0.f, 0.f);
		rlSetTexture(0);
	}

	void BoxTex(const Texture2D& tex, float x0, float y0, float x1, float y1, float z0, float z1, float texUnit, Color top, Color south, Color east)
	{
		const float k = 1.f / std::max(0.05f, texUnit);
		// 남(+Y) 면
		FaceTex(tex, { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 }, (x1 - x0) * k, (z1 - z0) * k, south);
		// 동(+X) 면
		FaceTex(tex, { x1, y1, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x1, y1, z1 }, (y1 - y0) * k, (z1 - z0) * k, east);
		// 윗면
		FaceTex(tex, { x0, y1, z1 }, { x1, y1, z1 }, { x1, y0, z1 }, { x0, y0, z1 }, (x1 - x0) * k, (y1 - y0) * k, top);
	}

	void Diamond(float X, float Y, float scale, Color col)
	{
		const float h = 0.5f * scale;
		const Vector2 n = IsoPx(X - h, Y - h), e = IsoPx(X + h, Y - h), s = IsoPx(X + h, Y + h), w = IsoPx(X - h, Y + h);
		Quad(n, e, s, w, col);
	}

	void TexQuadSrc(const Texture2D& tex, Rectangle src, Vector2 center, Vector2 size, float rot, Color col)
	{
		col = Mod(col);
		if (col.a == 0 || tex.id == 0) return;
		const float hw = size.x * 0.5f, hh = size.y * 0.5f;
		const float cs = std::cos(rot), sn = std::sin(rot);
		auto corner = [&](float x, float y) { return Vector2{ center.x + x * cs - y * sn, center.y + x * sn + y * cs }; };
		const Vector2 p0 = corner(-hw, -hh), p1 = corner(-hw, hh), p2 = corner(hw, hh), p3 = corner(hw, -hh);
		const float u0 = src.x / tex.width, v0 = src.y / tex.height;
		const float u1 = (src.x + src.width) / tex.width, v1 = (src.y + src.height) / tex.height;

		rlSetTexture(tex.id);
		rlBegin(RL_QUADS);
		C(col);
		rlNormal3f(0.f, 0.f, 1.f);
		rlTexCoord2f(u0, v0); V(p0, g_z);
		rlTexCoord2f(u0, v1); V(p1, g_z);
		rlTexCoord2f(u1, v1); V(p2, g_z);
		rlTexCoord2f(u1, v0); V(p3, g_z);
		rlEnd();
		rlSetTexture(0);
	}

	void TexQuadGround(const Texture2D& tex, Vector2 center, Vector2 size, float rot, float squash, Color col)
	{
		col = Mod(col);
		if (col.a == 0 || tex.id == 0) return;
		const float hw = size.x * 0.5f, hh = size.y * 0.5f;
		const float cs = std::cos(rot), sn = std::sin(rot);
		auto corner = [&](float x, float y) { return Vector2{ center.x + x * cs - y * sn, center.y + (x * sn + y * cs) * squash }; };
		rlSetTexture(tex.id);
		rlBegin(RL_QUADS);
		C(col);
		rlNormal3f(0.f, 0.f, 1.f);
		rlTexCoord2f(0.f, 0.f); V(corner(-hw, -hh), g_z);
		rlTexCoord2f(0.f, 1.f); V(corner(-hw, hh), g_z);
		rlTexCoord2f(1.f, 1.f); V(corner(hw, hh), g_z);
		rlTexCoord2f(1.f, 0.f); V(corner(hw, -hh), g_z);
		rlEnd();
		rlSetTexture(0);
	}

	void TexQuad(const Texture2D& tex, Vector2 center, Vector2 size, float rot, Color col)
	{
		TexQuadSrc(tex, Rectangle{ 0, 0, (float)tex.width, (float)tex.height }, center, size, rot, col);
	}

	// ─────────────────────────────────────────────────────────────────────────
	namespace
	{
		// 흰색(피격) · 붉은(강공격 예고) · 새벽빛(실루엣) · 초록(잔상) · 얼음 · 검정 · 주황 · 보라
		constexpr float kFlashPal[kFlashPaletteSize * 3] = {
			1.00f, 1.00f, 1.00f,   1.00f, 0.23f, 0.10f,   1.00f, 0.88f, 0.54f,   0.42f, 1.00f, 0.54f,
			0.66f, 0.85f, 1.00f,   0.00f, 0.00f, 0.00f,   1.00f, 0.54f, 0.23f,   0.78f, 0.63f, 1.00f,
		};
		int NearestFlash(Color c)
		{
			int best = 0;
			float bd = 1e9f;
			for (int i = 0; i < kFlashPaletteSize; ++i)
			{
				const float dr = c.r / 255.f - kFlashPal[i * 3], dg = c.g / 255.f - kFlashPal[i * 3 + 1], db = c.b / 255.f - kFlashPal[i * 3 + 2];
				const float d = dr * dr + dg * dg + db * db;
				if (d < bd) { bd = d; best = i; }
			}
			return best;
		}
	}

	const float* FlashPalette() { return kFlashPal; }

	void SpriteMesh(const Texture2D& tex, Rectangle src, Vector2 anchor, Vector2 base, float scale, const SpriteXform& xf)
	{
		if (tex.id == 0 || src.width <= 0.f || src.height <= 0.f) return;
		if (g_emissiveOnly) return;   // 캐릭터 · 소품 그림은 빛 버퍼에 넣지 않는다(번쩍임 좌표가 기본 셰이더에서 깨진다)
		// 정점 색 = 틴트 × 알파(번쩍임은 셰이더가 한다)
		const Color col{ g_tint.r, g_tint.g, g_tint.b, static_cast<unsigned char>(g_tint.a * g_alpha) };
		if (col.a == 0) return;
		const float level = std::round(util::Clamp01(g_flash) * 16.f);
		const float uOff = level * 2.f;
		// v 정수부 = 번쩍임 색 번호(0~7) + 8(알파 테스트 대상). 셰이더 밖에서는 반복 감기로 같은 곳을 읽는다.
		const int pal = level > 0.f ? NearestFlash(g_flashColor) : 0;
		const float vOff = static_cast<float>((xf.cutout ? 8 : 0) + pal) * 2.f;

		const float tw = static_cast<float>(tex.width), th = static_cast<float>(tex.height);
		const float dir = xf.flip ? -1.f : 1.f;
		const float cs = std::cos(xf.rot), sn = std::sin(xf.rot);
		const float headH = std::max(1.f, anchor.y);   // 발에서 그림 꼭대기까지(px, 원본)
		auto place = [&](float px, float py) -> Vector2
		{
			// 발 기준 지역 좌표(원본 px) → 비틀기 → 화면
			float lx = (px - anchor.x) * scale * xf.stretch;
			float ly = (py - anchor.y) * scale * xf.squash;
			const float t = util::Clamp01((anchor.y - py) / headH);
			lx += xf.lean * t * std::sqrt(t);
			const float rx = lx * cs - ly * sn;
			const float ry = lx * sn + ly * cs;
			return Vector2{ base.x + xf.offset.x + rx * dir, base.y + xf.offset.y + ry };
		};
		constexpr int kRows = 6;
		rlSetTexture(tex.id);
		rlBegin(RL_QUADS);
		C(col);
		rlNormal3f(0.f, 0.f, 1.f);
		const float u0 = src.x / tw + uOff, u1 = (src.x + src.width) / tw + uOff;
		for (int r = 0; r < kRows; ++r)
		{
			const float py0 = src.height * r / kRows, py1 = src.height * (r + 1) / kRows;
			const float v0 = (src.y + py0) / th + vOff, v1 = (src.y + py1) / th + vOff;
			const Vector2 a = place(0.f, py0), b = place(0.f, py1), c = place(src.width, py1), d = place(src.width, py0);
			// 뒷면 제거가 꺼져 있어(main.cpp) 뒤집어도 감기는 방향을 신경 쓰지 않는다
			rlTexCoord2f(u0, v0); V(a, g_z);
			rlTexCoord2f(u0, v1); V(b, g_z);
			rlTexCoord2f(u1, v1); V(c, g_z);
			rlTexCoord2f(u1, v0); V(d, g_z);
		}
		rlEnd();
		// 다음 도형이 번쩍임 좌표를 물려받지 않게
		rlTexCoord2f(0.f, 0.f);
		rlSetTexture(0);
	}
}
