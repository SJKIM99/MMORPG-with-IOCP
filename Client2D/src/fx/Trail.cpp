#include "fx/Trail.h"
#include "fx/FxScale.h"
#include "render/Draw.h"
#include "render/Sprites.h"
#include "core/Util.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_map>
#include <vector>

namespace trail
{
	namespace
	{
		struct ArcFx { ArcSpec s; float t = 0.f; float scroll = 0.f; };
		struct Pt { Vector2 p; float z; float age; };
		struct FollowFx { FollowSpec s; std::deque<Pt> pts; bool ended = false; };

		std::vector<ArcFx> g_arcs;
		std::unordered_map<int, FollowFx> g_follow;
		int g_nextHandle = 1;
		Texture2D g_tex{};
		bool g_texTried = false;

		// 띠 결 텍스처: 가로(u) = 띠 방향(반복), 세로(v) = 0 칼끝 테 → 1 안쪽
		void EnsureTexture()
		{
			if (g_texTried) return;
			g_texTried = true;
			constexpr int W = 128, H = 32;
			Image img = GenImageColor(W, H, BLANK);
			auto* px = static_cast<Color*>(img.data);
			float phase[6];
			for (float& f : phase) f = util::RandF(0.f, 6.28f);
			for (int y = 0; y < H; ++y)
			{
				const float v = static_cast<float>(y) / (H - 1);
				// 세로로 몇 줄의 결 — 칼끝 쪽이 가장 밝다
				float streak = 0.f;
				for (int k = 0; k < 3; ++k)
					streak += 0.5f + 0.5f * std::sin(v * (17.f + k * 13.f) + phase[k]);
				streak /= 3.f;
				for (int x = 0; x < W; ++x)
				{
					const float u = static_cast<float>(x) / W;
					const float along = 0.5f + 0.5f * std::sin(u * 6.2831853f * 2.f + v * 9.f + phase[3])
						* std::sin(u * 6.2831853f * 5.f + phase[4] + v * 3.f);
					const float edge = std::pow(1.f - v, 1.25f);
					const float val = util::Clamp01(edge * (0.6f + 0.4f * streak) * (0.75f + 0.25f * along) + 0.12f * (1.f - v) * streak);
					px[y * W + x] = Color{ 255, 255, 255, static_cast<unsigned char>(val * 255.f) };
				}
			}
			g_tex = LoadTextureFromImage(img);
			UnloadImage(img);
			if (g_tex.id != 0)
			{
				SetTextureFilter(g_tex, TEXTURE_FILTER_BILINEAR);
				SetTextureWrap(g_tex, TEXTURE_WRAP_REPEAT);
			}
		}

		float EaseOut(float u) { u = util::Clamp01(u); return 1.f - (1.f - u) * (1.f - u) * (1.f - u); }

		float ArcLife(const ArcSpec& s) { return s.sweep + s.lag + 0.05f; }

		// 바닥 평면의 각 · 반지름 → 화면(아이소 px)
		Vector2 ArcPoint(const ArcSpec& s, float ang, float rr)
		{
			const float c = std::cos(ang), sn = std::sin(ang);
			const float ky = 0.5f * std::cos(s.tilt) + std::sin(s.tilt);
			return Vector2{ s.iso.x + c * rr, s.iso.y - s.z + sn * rr * ky };
		}

		Color WithAlpha(Color c, float a) { c.a = static_cast<unsigned char>(util::Clamp01(a) * c.a); return c; }

		void V(Vector2 p, float z, float u, float v, Color c)
		{
			rlColor4ub(c.r, c.g, c.b, c.a);
			rlTexCoord2f(u, v);
			rlVertex3f(p.x, p.y, z);
		}

		void DrawArc(const ArcFx& a)
		{
			const ArcSpec& s = a.s;
			const float t = a.t - s.delay;
			if (t < 0.f) return;
			const float hp = EaseOut(t / s.sweep);
			const float tp = EaseOut((t - s.lag) / s.sweep);
			if (hp - tp < 0.004f) return;
			const float th = s.a0 + (s.a1 - s.a0) * hp;
			const float tt = s.a0 + (s.a1 - s.a0) * tp;
			const float env = s.alpha * (1.f - util::Clamp01((t - s.sweep) / (s.lag + 0.05f)) * 0.6f);
			const float z = gfx::DepthOf(s.iso.y / gfx::kHalfH + s.depthBias);
			constexpr int N = 22;
			// ① 띠 몸통(칼날 길이만큼, 결 텍스처)
			rlSetTexture(g_tex.id);
			rlBegin(RL_QUADS);
			rlNormal3f(0.f, 0.f, 1.f);
			for (int i = 0; i < N; ++i)
			{
				const float s0 = static_cast<float>(i) / N, s1 = static_cast<float>(i + 1) / N;
				const float g0 = std::pow(s0, 0.75f), g1 = std::pow(s1, 0.75f);
				const float an0 = tt + (th - tt) * s0, an1 = tt + (th - tt) * s1;
				const Vector2 o0 = ArcPoint(s, an0, s.radius), o1 = ArcPoint(s, an1, s.radius);
				const Vector2 i0 = ArcPoint(s, an0, s.radius - s.blade * g0), i1 = ArcPoint(s, an1, s.radius - s.blade * g1);
				const float al0 = env * std::pow(s0, 1.3f), al1 = env * std::pow(s1, 1.3f);
				const float u0 = s0 * 1.6f - a.scroll, u1 = s1 * 1.6f - a.scroll;
				V(o0, z, u0, 0.f, WithAlpha(s.edge, al0));
				V(i0, z, u0, 1.f, WithAlpha(s.body, al0 * 0.55f));
				V(i1, z, u1, 1.f, WithAlpha(s.body, al1 * 0.55f));
				V(o1, z, u1, 0.f, WithAlpha(s.edge, al1));
			}
			rlEnd();
			rlSetTexture(0);
			// ② 칼끝 테(가는 흰 선) — 또렷함
			rlBegin(RL_QUADS);
			for (int i = 0; i < N; ++i)
			{
				const float s0 = static_cast<float>(i) / N, s1 = static_cast<float>(i + 1) / N;
				const float an0 = tt + (th - tt) * s0, an1 = tt + (th - tt) * s1;
				const float w0 = 1.2f + 2.6f * s0, w1 = 1.2f + 2.6f * s1;
				const Vector2 o0 = ArcPoint(s, an0, s.radius + w0 * 0.3f), o1 = ArcPoint(s, an1, s.radius + w1 * 0.3f);
				const Vector2 i0 = ArcPoint(s, an0, s.radius - w0), i1 = ArcPoint(s, an1, s.radius - w1);
				const Color c0 = WithAlpha(s.edge, env * std::pow(s0, 2.f)), c1 = WithAlpha(s.edge, env * std::pow(s1, 2.f));
				V(o0, z, 0.f, 0.f, c0);
				V(i0, z, 0.f, 0.f, c0);
				V(i1, z, 0.f, 0.f, c1);
				V(o1, z, 0.f, 0.f, c1);
			}
			rlEnd();
		}

		void DrawFollow(const FollowFx& f)
		{
			const auto& pts = f.pts;
			if (pts.size() < 2) return;
			const FollowSpec& s = f.s;
			rlSetTexture(g_tex.id);
			rlBegin(RL_QUADS);
			rlNormal3f(0.f, 0.f, 1.f);
			auto normalAt = [&](size_t i)
			{
				const Vector2 a = pts[i > 0 ? i - 1 : i].p, b = pts[i + 1 < pts.size() ? i + 1 : i].p;
				float dx = b.x - a.x, dy = b.y - a.y;
				const float l = std::sqrt(dx * dx + dy * dy);
				if (l < 1e-3f) return Vector2{ 0.f, -1.f };
				return Vector2{ -dy / l, dx / l };
			};
			float acc = 0.f;
			for (size_t i = 0; i + 1 < pts.size(); ++i)
			{
				const Pt& p0 = pts[i];
				const Pt& p1 = pts[i + 1];
				const float k0 = util::Clamp01(1.f - p0.age / s.fade), k1 = util::Clamp01(1.f - p1.age / s.fade);
				if (k0 <= 0.f && k1 <= 0.f) continue;
				const Vector2 n0 = normalAt(i), n1 = normalAt(i + 1);
				const float w0 = s.width * 0.5f * k0, w1 = s.width * 0.5f * k1;
				const float dx = p1.p.x - p0.p.x, dy = p1.p.y - p0.p.y;
				const float seg = std::sqrt(dx * dx + dy * dy);
				const float u0 = acc / 64.f, u1 = (acc + seg) / 64.f;
				acc += seg;
				const Color e0 = WithAlpha(s.edge, s.alpha * k0), e1 = WithAlpha(s.edge, s.alpha * k1);
				const Color b0 = WithAlpha(s.body, s.alpha * k0 * 0.6f), b1 = WithAlpha(s.body, s.alpha * k1 * 0.6f);
				// 가운데가 밝고 양옆이 흐린 띠: 위 반쪽 + 아래 반쪽
				const Vector2 c0 = p0.p, c1 = p1.p;
				const Vector2 t0{ c0.x + n0.x * w0, c0.y + n0.y * w0 }, t1{ c1.x + n1.x * w1, c1.y + n1.y * w1 };
				const Vector2 d0{ c0.x - n0.x * w0, c0.y - n0.y * w0 }, d1{ c1.x - n1.x * w1, c1.y - n1.y * w1 };
				V(c0, p0.z, u0, 0.f, e0); V(t0, p0.z, u0, 1.f, WithAlpha(b0, 0.f)); V(t1, p1.z, u1, 1.f, WithAlpha(b1, 0.f)); V(c1, p1.z, u1, 0.f, e1);
				V(c0, p0.z, u0, 0.f, e0); V(d0, p0.z, u0, 1.f, WithAlpha(b0, 0.f)); V(d1, p1.z, u1, 1.f, WithAlpha(b1, 0.f)); V(c1, p1.z, u1, 0.f, e1);
			}
			rlEnd();
			rlSetTexture(0);
		}
	}

	void Arc(const ArcSpec& s)
	{
		if (fxscale::Alpha() <= 0.02f) return;
		if (g_arcs.size() >= 64) g_arcs.erase(g_arcs.begin());
		ArcFx a;
		a.s = s;
		a.s.alpha *= fxscale::Alpha();
		a.s.sweep = std::max(0.02f, s.sweep);
		a.s.lag = std::max(0.02f, s.lag);
		g_arcs.push_back(a);
	}

	int Begin(const FollowSpec& s)
	{
		const int h = g_nextHandle++;
		if (g_nextHandle > 1000000) g_nextHandle = 1;
		FollowFx f;
		f.s = s;
		f.s.fade = std::max(0.03f, s.fade);
		f.s.alpha *= fxscale::Alpha();
		g_follow[h] = std::move(f);
		return h;
	}

	void Push(int handle, Vector2 iso, float z)
	{
		auto it = g_follow.find(handle);
		if (it == g_follow.end() || it->second.ended) return;
		auto& pts = it->second.pts;
		const Vector2 p{ iso.x, iso.y - z };
		if (!pts.empty())
		{
			const float dx = p.x - pts.back().p.x, dy = p.y - pts.back().p.y;
			if (dx * dx + dy * dy < 4.f) { pts.back().age = 0.f; return; }
		}
		pts.push_back(Pt{ p, gfx::DepthOf(iso.y / gfx::kHalfH + 0.4f), 0.f });
		if (pts.size() > 96) pts.pop_front();
	}

	void End(int handle)
	{
		auto it = g_follow.find(handle);
		if (it != g_follow.end()) it->second.ended = true;
	}

	void Streak(Vector2 aIso, float az, Vector2 bIso, float bz, const FollowSpec& s, float age0)
	{
		const int h = Begin(s);
		constexpr int kSteps = 12;
		for (int i = 0; i <= kSteps; ++i)
		{
			const float u = static_cast<float>(i) / kSteps;
			Push(h, Vector2{ util::Lerp(aIso.x, bIso.x, u), util::Lerp(aIso.y, bIso.y, u) }, util::Lerp(az, bz, u));
		}
		// 오래된(시작) 쪽이 먼저 사라지도록 나이를 조금씩 준다
		auto& pts = g_follow[h].pts;
		for (size_t i = 0; i < pts.size(); ++i)
			pts[i].age = age0 + s.fade * 0.35f * (1.f - static_cast<float>(i) / std::max<size_t>(1, pts.size() - 1));
		End(h);
	}

	void Update(float dt)
	{
		for (auto& a : g_arcs) { a.t += dt; a.scroll += dt * 2.4f; }
		g_arcs.erase(std::remove_if(g_arcs.begin(), g_arcs.end(), [](const ArcFx& a) { return a.t >= a.s.delay + ArcLife(a.s); }), g_arcs.end());
		for (auto it = g_follow.begin(); it != g_follow.end();)
		{
			auto& f = it->second;
			for (auto& p : f.pts) p.age += dt;
			while (!f.pts.empty() && f.pts.front().age >= f.s.fade) f.pts.pop_front();
			if (f.ended && f.pts.empty()) it = g_follow.erase(it);
			else ++it;
		}
	}

	void Draw()
	{
		if (g_arcs.empty() && g_follow.empty()) return;
		EnsureTexture();
		if (g_tex.id == 0) return;
		const bool emis = gfx::EmissiveOnlyOn();
		const bool wasAdditive = gfx::AdditiveOn();
		const bool spritesOn = sprites::Suspend();   // 월드 셰이더는 텍스처 좌표 정수부를 번쩍임으로 읽는다 — 흐르는 결을 망친다
		rlDrawRenderBatchActive();
		for (int pass = 0; pass < 2; ++pass)
		{
			const bool additive = pass == 1;
			if (!additive && emis) continue;
			rlDrawRenderBatchActive();
			rlSetBlendMode(additive || emis ? RL_BLEND_ADDITIVE : RL_BLEND_ALPHA);
			for (const auto& a : g_arcs)
				if (a.s.additive == additive) DrawArc(a);
			for (const auto& [h, f] : g_follow)
				if (f.s.additive == additive) DrawFollow(f);
		}
		rlDrawRenderBatchActive();
		sprites::Resume(spritesOn);   // 월드 셰이더를 먼저(밝은 바닥 섞기는 월드 셰이더가 켜져 있어야 고른다)
		gfx::Additive(wasAdditive);
	}

	void Clear() { g_arcs.clear(); g_follow.clear(); }
	int Count() { return static_cast<int>(g_arcs.size() + g_follow.size()); }

	void Shutdown()
	{
		Clear();
		if (g_tex.id != 0) UnloadTexture(g_tex);
		g_tex = Texture2D{};
		g_texTried = false;
	}
}
