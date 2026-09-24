#include "fx/SkillFx.h"
#include "fx/Effects.h"
#include "fx/Particles.h"
#include "game/Entity.h"
#include "render/Draw.h"
#include "render/Assets.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace skillfx
{
	namespace
	{
		using util::Rgb;
		using util::RandF;
		using util::kPi;
		using fx::Particle;
		using fx::Shape;

		// ── 투사체(룬 탄환) ────────────────────────────────────────────
		struct Projectile
		{
			Vector2 a{}, b{};      // 바닥 평면
			float za = 30.f, zb = 30.f;
			float t = 0.f, dur = 0.3f;
			Color color{};
			uint16_t skill = 0;
		};
		// ── 번개(연쇄) ────────────────────────────────────────────────
		struct BoltFx
		{
			Vector2 a{}, b{};
			float za = 0.f, zb = 0.f;
			float t = 0.f, life = 0.2f;
			Color color{};
			uint32_t seed = 0;
		};
		// ── 바닥 문양(인장·룬·불 고리·균열) ───────────────────────────
		enum class Decal : uint8_t { FrostSeal, Rune, FireRing, Crack, CometMark, Whirl, Shadow };
		struct DecalFx
		{
			Decal kind;
			Vector2 iso{};
			float radius = 40.f;   // px(가로 반지름)
			float t = 0.f, life = 1.f;
			Color color{};
			float spin = 0.f;
		};
		// ── 낙하(혜성) ────────────────────────────────────────────────
		struct Fall
		{
			Vector2 iso{};
			float t = 0.f, dur = 0.8f;
		};

		std::vector<Projectile> g_proj;
		std::vector<BoltFx> g_bolts;
		std::vector<DecalFx> g_decals;
		std::vector<Fall> g_falls;
		float g_time = 0.f;

		Particle& P()
		{
			return fx::Particles().Emit();
		}

		void Spark(Vector2 iso, float z, float speed, float ang, Color c0, Color c1, float life, float size)
		{
			Particle& p = P();
			p.pos = iso; p.z = z;
			p.vel = { std::cos(ang) * speed, std::sin(ang) * speed * 0.5f };
			p.vz = std::sin(ang) * speed * 0.3f + RandF(20.f, 90.f);
			p.gravity = 380.f;
			p.drag = 2.4f;
			p.life = p.maxLife = life;
			p.size0 = size; p.size1 = 0.f;
			p.c0 = c0; p.c1 = c1;
			p.shape = Shape::Spark;
			p.stretch = true;
			p.additive = true;
		}

		void Glow(Vector2 iso, float z, float s0, float s1, Color c, float life)
		{
			Particle& p = P();
			p.pos = iso; p.z = z;
			p.life = p.maxLife = life;
			p.size0 = s0; p.size1 = s1;
			p.c0 = c; p.c1 = util::WithA(c, 0.f);
			p.shape = Shape::Glow;
			p.additive = true;
		}

		void Smoke(Vector2 iso, float z, Color c, float size, float life, float rise)
		{
			Particle& p = P();
			p.pos = { iso.x + RandF(-8.f, 8.f), iso.y + RandF(-4.f, 4.f) };
			p.z = z;
			p.vel = { RandF(-20.f, 20.f), RandF(-10.f, 10.f) };
			p.vz = rise;
			p.drag = 1.5f;
			p.life = p.maxLife = life;
			p.size0 = size * 0.6f; p.size1 = size * 1.6f;
			p.c0 = c; p.c1 = util::WithA(c, 0.f);
			p.rot = RandF(0.f, 6.f); p.vrot = RandF(-1.f, 1.f);
			p.shape = Shape::Smoke;
		}

		void Shard(Vector2 iso, float z, Color c, float speed, float size, float life)
		{
			Particle& p = P();
			const float a = RandF(0.f, kPi * 2.f);
			p.pos = iso; p.z = z;
			p.vel = { std::cos(a) * speed, std::sin(a) * speed * 0.5f };
			p.vz = RandF(80.f, 260.f);
			p.gravity = 700.f;
			p.bounce = true;
			p.drag = 0.8f;
			p.life = p.maxLife = life;
			p.size0 = size; p.size1 = size * 0.4f;
			p.c0 = c; p.c1 = util::WithA(c, 0.f);
			p.rot = RandF(0.f, 6.f); p.vrot = RandF(-8.f, 8.f);
			p.shape = Shape::Flake;
		}

		void Ember(Vector2 iso, float z, Color c0, Color c1, float spread, float life)
		{
			Particle& p = P();
			p.pos = { iso.x + RandF(-spread, spread), iso.y + RandF(-spread * 0.5f, spread * 0.5f) };
			p.z = z;
			p.vel = { RandF(-15.f, 15.f), RandF(-8.f, 8.f) };
			p.vz = RandF(40.f, 110.f);
			p.drag = 1.2f;
			p.life = p.maxLife = life;
			p.size0 = RandF(6.f, 11.f); p.size1 = 1.f;
			p.c0 = c0; p.c1 = c1;
			p.shape = Shape::Soft;
			p.additive = true;
			p.wobble = 12.f;
		}

		void AddDecal(Decal kind, Vector2 iso, float radius, float life, Color color)
		{
			g_decals.push_back(DecalFx{ kind, iso, radius, 0.f, life, color, RandF(0.f, 6.f) });
			if (g_decals.size() > 24) g_decals.erase(g_decals.begin());
		}

		// 아이소 바닥 위 원(가로 반지름 r)
		void GroundRing(Vector2 c, float r, float th, Color col)
		{
			gfx::EllipseArc(c, r, r * 0.5f, 0.f, kPi * 2.f, th, col);
		}

		// 룬 문양(바닥): 겹원 + 회전하는 획
		void DrawRuneCircle(Vector2 c, float r, float spin, Color col, float a)
		{
			GroundRing(c, r, 2.5f, util::MulA(col, a));
			GroundRing(c, r * 0.72f, 1.5f, util::MulA(col, a * 0.8f));
			for (int i = 0; i < 6; ++i)
			{
				const float ang = spin + i * kPi / 3.f;
				const Vector2 p0{ c.x + std::cos(ang) * r * 0.72f, c.y + std::sin(ang) * r * 0.36f };
				const Vector2 p1{ c.x + std::cos(ang + 0.6f) * r * 0.98f, c.y + std::sin(ang + 0.6f) * r * 0.49f };
				gfx::Line(p0, p1, 2.f, util::MulA(col, a));
				gfx::Circle(p1, 2.2f, util::MulA(col, a));
			}
			// 가운데 삼각 룬
			for (int i = 0; i < 3; ++i)
			{
				const float a0 = -spin * 1.5f + i * kPi * 2.f / 3.f, a1 = a0 + kPi * 2.f / 3.f;
				gfx::Line(Vector2{ c.x + std::cos(a0) * r * 0.45f, c.y + std::sin(a0) * r * 0.225f },
					Vector2{ c.x + std::cos(a1) * r * 0.45f, c.y + std::sin(a1) * r * 0.225f }, 1.6f, util::MulA(col, a * 0.9f));
			}
		}

		Color SkillColor(uint16_t id)
		{
			const SkillRow* r = SkillTable::Find(id);
			return r ? Rgb(r->color) : Rgb(0xFFFFFF);
		}

		float TileDist(Vector2 a, Vector2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
	}

	void Clear()
	{
		g_proj.clear();
		g_bolts.clear();
		g_decals.clear();
		g_falls.clear();
	}

	void Update(float dt)
	{
		g_time += dt;
		for (auto& p : g_proj)
		{
			p.t += dt;
			// 꼬리
			const float u = util::Clamp01(p.t / p.dur);
			const Vector2 pos = util::LerpV(p.a, p.b, u);
			const float z = util::Lerp(p.za, p.zb, u) + std::sin(u * kPi) * 18.f;
			Particle& tr = P();
			tr.pos = pos; tr.z = z;
			tr.life = tr.maxLife = 0.22f;
			tr.size0 = 13.f; tr.size1 = 2.f;
			tr.c0 = p.color; tr.c1 = util::WithA(p.color, 0.f);
			tr.shape = Shape::Glow;
			tr.additive = true;
		}
		g_proj.erase(std::remove_if(g_proj.begin(), g_proj.end(), [](const Projectile& p) { return p.t >= p.dur; }), g_proj.end());
		for (auto& b : g_bolts) b.t += dt;
		g_bolts.erase(std::remove_if(g_bolts.begin(), g_bolts.end(), [](const BoltFx& b) { return b.t >= b.life; }), g_bolts.end());
		for (auto& d : g_decals) { d.t += dt; d.spin += dt * 1.2f; }
		g_decals.erase(std::remove_if(g_decals.begin(), g_decals.end(), [](const DecalFx& d) { return d.t >= d.life; }), g_decals.end());
		for (auto& f : g_falls)
		{
			f.t += dt;
			// 떨어지는 혜성의 꼬리 불똥
			const float u = util::Clamp01(f.t / f.dur);
			const float z = (1.f - u) * 520.f;
			const Vector2 pos{ f.iso.x - (1.f - u) * 160.f, f.iso.y };
			for (int i = 0; i < 3; ++i)
				Ember(pos, z + RandF(-6.f, 6.f), Rgb(0xFFE08A), Rgb(0xFF3A10, 0), 6.f, 0.45f);
		}
		g_falls.erase(std::remove_if(g_falls.begin(), g_falls.end(), [](const Fall& f) { return f.t >= f.dur; }), g_falls.end());
	}

	void DrawGround()
	{
		for (const auto& d : g_decals)
		{
			const float u = util::Clamp01(d.t / d.life);
			const float appear = util::EaseOutBack(util::Clamp01(d.t * 6.f));
			const float fade = u > 0.7f ? 1.f - (u - 0.7f) / 0.3f : 1.f;
			gfx::SetDepthRaw(-0.965f);
			switch (d.kind)
			{
			case Decal::FrostSeal:
				gfx::EllipseGrad(d.iso, d.radius * appear, d.radius * 0.5f * appear, util::MulA(d.color, 0.35f * fade), util::MulA(d.color, 0.f));
				DrawRuneCircle(d.iso, d.radius * appear, d.spin, d.color, fade);
				break;
			case Decal::Rune:
				DrawRuneCircle(d.iso, d.radius * appear, d.spin * 2.f, d.color, fade * (0.6f + 0.4f * std::sin(g_time * 14.f)));
				break;
			case Decal::FireRing:
			{
				const float r = d.radius * util::EaseOutCubic(util::Clamp01(d.t * 3.f));
				gfx::EllipseArc(d.iso, r, r * 0.5f, 0.f, kPi * 2.f, 10.f * fade, util::MulA(d.color, 0.55f * fade));
				gfx::EllipseArc(d.iso, r * 0.9f, r * 0.45f, 0.f, kPi * 2.f, 4.f, util::MulA(Rgb(0xFFE08A), 0.7f * fade));
				break;
			}
			case Decal::Crack:
			{
				const Color c = util::MulA(Rgb(0x1A120C), 0.65f * fade);
				for (int i = 0; i < 7; ++i)
				{
					const float a = d.spin + i * kPi * 2.f / 7.f;
					const Vector2 p1{ d.iso.x + std::cos(a) * d.radius * 0.55f, d.iso.y + std::sin(a) * d.radius * 0.27f };
					const Vector2 p2{ d.iso.x + std::cos(a + 0.25f) * d.radius, d.iso.y + std::sin(a + 0.25f) * d.radius * 0.5f };
					gfx::Line(d.iso, p1, 3.f, c);
					gfx::Line(p1, p2, 2.f, c);
				}
				gfx::EllipseGrad(d.iso, d.radius * 0.4f, d.radius * 0.2f, util::MulA(d.color, 0.5f * fade), util::MulA(d.color, 0.f));
				break;
			}
			case Decal::CometMark:
			{
				// 혜성 낙하 예고: 빨간 원이 가운데로 조여 든다
				const float pulse = 0.5f + 0.5f * std::sin(g_time * 16.f);
				gfx::EllipseGrad(d.iso, d.radius, d.radius * 0.5f, util::MulA(d.color, 0.12f + 0.1f * pulse), util::MulA(d.color, 0.25f));
				GroundRing(d.iso, d.radius, 3.f, util::MulA(d.color, 0.9f));
				GroundRing(d.iso, d.radius * (1.f - u), 2.f, util::MulA(Rgb(0xFFE0A0), 0.9f));
				break;
			}
			case Decal::Whirl:
			{
				const float r = d.radius * util::EaseOutCubic(u);
				gfx::EllipseArc(d.iso, r, r * 0.5f, d.spin * 6.f, d.spin * 6.f + kPi * 1.4f, 7.f * fade, util::MulA(d.color, 0.8f * fade));
				break;
			}
			case Decal::Shadow:
				gfx::EllipseGrad(d.iso, d.radius, d.radius * 0.5f, util::MulA(d.color, 0.55f * fade), util::MulA(d.color, 0.f));
				break;
			}
		}
		// 혜성 그림자(떨어질수록 진해진다)
		for (const auto& f : g_falls)
		{
			const float u = util::Clamp01(f.t / f.dur);
			gfx::SetDepthRaw(-0.965f);
			gfx::EllipseGrad(f.iso, 20.f + 40.f * u, (20.f + 40.f * u) * 0.5f, Rgb(0x000000, static_cast<uint8_t>(40 + 120 * u)), Rgb(0x000000, 0));
		}
	}

	void DrawWorld()
	{
		const auto& tex = assets::Get();
		gfx::Additive(true);
		for (const auto& p : g_proj)
		{
			const float u = util::Clamp01(p.t / p.dur);
			const Vector2 pos = util::LerpV(p.a, p.b, u);
			const float z = util::Lerp(p.za, p.zb, u) + std::sin(u * kPi) * 18.f;
			gfx::SetDepth(pos.y / gfx::kHalfH + 0.6f);
			const Vector2 c{ pos.x, pos.y - z };
			gfx::TexQuad(tex.glow, c, Vector2{ 44.f, 44.f }, 0.f, util::MulA(p.color, 0.9f));
			gfx::Star(c, 3.f, 10.f, 4, g_time * 9.f, Rgb(0xFFFFFF, 230));
			gfx::Circle(c, 4.5f, Rgb(0xFFFFFF, 240));
		}
		for (const auto& b : g_bolts)
		{
			const float a = 1.f - util::Clamp01(b.t / b.life);
			const Vector2 p0{ b.a.x, b.a.y - b.za }, p1{ b.b.x, b.b.y - b.zb };
			gfx::SetDepth(std::max(b.a.y, b.b.y) / gfx::kHalfH + 0.6f);
			// 지그재그(매 프레임 흔들리게)
			const int seg = 7;
			Vector2 prev = p0;
			const uint32_t flick = static_cast<uint32_t>(g_time * 30.f) + b.seed;
			for (int i = 1; i <= seg; ++i)
			{
				const float t = static_cast<float>(i) / seg;
				Vector2 q = util::LerpV(p0, p1, t);
				if (i < seg)
				{
					const Vector2 d = util::Norm(util::Sub(p1, p0));
					const float off = (util::Hash01(static_cast<int>(flick), i) - 0.5f) * 26.f;
					q.x += -d.y * off;
					q.y += d.x * off;
				}
				gfx::Line(prev, q, 7.f * a, util::MulA(b.color, 0.55f * a));
				gfx::Line(prev, q, 2.5f * a, Rgb(0xFFFFFF, static_cast<uint8_t>(240 * a)));
				prev = q;
			}
		}
		gfx::Additive(false);
	}

	// ── 시전 ─────────────────────────────────────────────────────────────────
	float TravelTime(uint16_t skillId, Vector2 fromTile, Vector2 toTile)
	{
		const SkillRow* row = SkillTable::Find(skillId);
		if (!row || row->projectile == 0)
			return 0.f;
		const float speed = row->projectile / 10.f;   // 칸/초
		return std::clamp(TileDist(fromTile, toTile) / speed, 0.06f, 0.6f);
	}

	float OnCast(uint16_t id, const CastInfo& c)
	{
		const Color col = SkillColor(id);
		auto& fxs = fx::Fx();
		const float dir = c.faceRight ? 1.f : -1.f;
		switch (id)
		{
		case 1:   // 검격 — 큰 초승달
			fxs.Slash(c.casterIso, 34.f, dir, false, Rgb(0xFFF2D0, 230), 1.25f);
			return 0.f;
		case 2:   // 방패 강타 — 앞쪽 충격파
			fxs.Slash(c.casterIso, 28.f, dir, false, Rgb(0xFFD070, 200), 0.9f);
			return 0.f;
		case 3:   // 회전 베기 — 한 바퀴
			AddDecal(Decal::Whirl, c.casterIso, 80.f, 0.35f, Rgb(0xFFE8B0));
			AddDecal(Decal::Whirl, c.casterIso, 64.f, 0.3f, Rgb(0xFFC860));
			for (int i = 0; i < 16; ++i)
			{
				const float a = i * kPi * 2.f / 16.f;
				Spark(util::Add(c.casterIso, Vector2{ std::cos(a) * 40.f, std::sin(a) * 20.f }), 24.f, 160.f, a + kPi * 0.5f, Rgb(0xFFF6D0), Rgb(0xFFB040, 0), 0.25f, 7.f);
			}
			return 0.f;
		case 4:   // 돌진 — 흙먼지 궤적
		case 16:  // 잔상 회피 — 보랏빛 잔상
		{
			const int n = 10;
			for (int i = 0; i < n; ++i)
			{
				const Vector2 p = util::LerpV(c.casterIso, c.landIso, i / static_cast<float>(n));
				if (id == 4) Smoke(p, 6.f, Rgb(0xB8A078, 150), 18.f, 0.6f, 12.f);
				else { Glow(p, 30.f, 26.f, 6.f, Rgb(0xB0A0FF, 160), 0.4f); Smoke(p, 20.f, Rgb(0x6A5A9A, 110), 16.f, 0.5f, 20.f); }
			}
			return 0.f;
		}
		case 5:   // 철벽 태세
			fxs.Ring(c.casterIso, 10.f, 70.f, 0.45f, Rgb(0xFFD070, 230), 5.f);
			Glow(c.casterIso, 34.f, 40.f, 110.f, Rgb(0xFFC860, 170), 0.35f);
			return 0.f;
		case 6:   // 황혼의 처형 — 힘을 모으는 빛
			for (int i = 0; i < 18; ++i)
			{
				Particle& p = P();
				const float a = RandF(0.f, kPi * 2.f), r = RandF(40.f, 70.f);
				p.pos = { c.casterIso.x + std::cos(a) * r, c.casterIso.y + std::sin(a) * r * 0.5f };
				p.z = RandF(20.f, 70.f);
				p.vel = { -std::cos(a) * r * 2.4f, -std::sin(a) * r * 1.2f };
				p.life = p.maxLife = std::max(0.2f, c.castSec);
				p.size0 = 8.f; p.size1 = 2.f;
				p.c0 = Rgb(0xFFB060); p.c1 = Rgb(0xFF6A20, 0);
				p.shape = Shape::Glow;
				p.additive = true;
			}
			return 0.f;
		case 7:   // 룬 탄환 — 투사체
		case 11:  // 번개 폭발 — 손에서 번쩍(연쇄는 적중에서)
		{
			if (id == 11)
			{
				Glow(c.casterIso, 50.f, 20.f, 60.f, Rgb(0xE8F0FF, 220), 0.2f);
				return 0.08f;
			}
			const float dist = TileDist(gfx::IsoToTile(c.casterIso), gfx::IsoToTile(c.targetIso));
			const float dur = std::clamp(dist / 16.f, 0.07f, 0.5f);
			g_proj.push_back(Projectile{ c.casterIso, c.targetIso, 48.f, c.targetHeight * 0.55f, 0.f, dur, col, id });
			Glow(c.casterIso, 48.f, 16.f, 40.f, util::MulA(col, 0.9f), 0.15f);
			return dur;
		}
		case 8:   // 빙결 인장 — 지점에 인장
			AddDecal(Decal::FrostSeal, c.targetIso, 72.f, 1.2f, Rgb(0x9AE8FF));
			return 0.f;
		case 9:   // 화염 고리
			AddDecal(Decal::FireRing, c.casterIso, 120.f, 0.55f, Rgb(0xFF7A2A));
			for (int i = 0; i < 26; ++i)
			{
				const float a = RandF(0.f, kPi * 2.f);
				Ember(util::Add(c.casterIso, Vector2{ std::cos(a) * RandF(40.f, 110.f), std::sin(a) * RandF(20.f, 55.f) }), RandF(0.f, 10.f), Rgb(0xFFD060), Rgb(0xFF3A10, 0), 6.f, RandF(0.4f, 0.8f));
			}
			return 0.f;
		case 10:  // 공간 도약 — 사라짐/나타남 + 떠난 자리 룬
			AddDecal(Decal::Rune, c.casterIso, 60.f, 1.05f, Rgb(0xC0A0FF));
			for (int i = 0; i < 14; ++i) { Glow(c.casterIso, RandF(10.f, 60.f), 18.f, 2.f, Rgb(0xB08AFF, 200), RandF(0.2f, 0.5f)); }
			for (int i = 0; i < 14; ++i) { Glow(c.landIso, RandF(10.f, 60.f), 2.f, 22.f, Rgb(0xD8C8FF, 220), RandF(0.2f, 0.4f)); }
			fxs.Ring(c.landIso, 6.f, 60.f, 0.3f, Rgb(0xD8C8FF, 220), 3.f);
			return 0.f;
		case 12:  // 혜성 낙하 — 예고 + 낙하
			AddDecal(Decal::CometMark, c.targetIso, 150.f, std::max(0.3f, c.castSec), Rgb(0xFF5A3A));
			g_falls.push_back(Fall{ c.targetIso, 0.f, std::max(0.3f, c.castSec) });
			return 0.f;
		case 13:  // 연속 찌르기
			fxs.Slash(c.casterIso, 30.f, dir, false, Rgb(0xE8D8FF, 200), 0.7f);
			return 0.f;
		case 14:  // 그림자 돌진 — 연기 + 도착 지점 그림자
			for (int i = 0; i < 10; ++i) Smoke(c.casterIso, RandF(5.f, 40.f), Rgb(0x3A2A5A, 170), 22.f, 0.6f, 30.f);
			AddDecal(Decal::Shadow, c.landIso, 48.f, 0.6f, Rgb(0x2A1A4A));
			for (int i = 0; i < 8; ++i) Smoke(c.landIso, RandF(5.f, 40.f), Rgb(0x4A3A7A, 150), 18.f, 0.5f, 20.f);
			return 0.f;
		case 15:  // 독 칼날
			fxs.Slash(c.casterIso, 30.f, dir, false, Rgb(0x9AF06A, 220), 1.0f);
			return 0.f;
		case 17:  // 후방 습격 — 붉은 보라 X
			fxs.Slash(c.casterIso, 34.f, dir, false, Rgb(0xE06AFF, 230), 1.1f);
			fxs.Slash(c.casterIso, 26.f, -dir, true, Rgb(0xFF5A8A, 200), 0.9f);
			return 0.f;
		case 18:  // 월영 난무 — 달빛 원
			AddDecal(Decal::Whirl, c.casterIso, 84.f, 0.9f, Rgb(0xD8D0FF));
			AddDecal(Decal::Shadow, c.casterIso, 90.f, 1.0f, Rgb(0x1A1030));
			Glow(c.casterIso, 60.f, 60.f, 140.f, Rgb(0xC8C0FF, 120), 0.8f);
			return 0.f;
		default:
			return 0.f;
		}
	}

	void OnHit(uint16_t id, Vector2 iso, float height, bool crit, uint8_t comboId, uint8_t flags2, bool dot)
	{
		auto& fxs = fx::Fx();
		const float z = height * 0.5f;
		if (dot)
		{
			const bool burn = id == 9;
			for (int i = 0; i < 4; ++i)
				Ember(iso, z, burn ? Rgb(0xFFB040) : Rgb(0x9AF06A), burn ? Rgb(0xFF3A10, 0) : Rgb(0x2A8A20, 0), 10.f, 0.5f);
			return;
		}
		switch (id)
		{
		case 2:   // 방패 강타: 둔탁한 충격 + 별
			fxs.Ring(iso, 6.f, 58.f, 0.3f, Rgb(0xFFE08A, 230), 5.f);
			for (int i = 0; i < 6; ++i) Spark(iso, z, RandF(120.f, 240.f), RandF(0.f, 6.28f), Rgb(0xFFF6C0), Rgb(0xFFB030, 0), 0.3f, 8.f);
			break;
		case 3:
			fxs.HitSpark(iso, height, Rgb(0xFFD070), crit, 1.f);
			break;
		case 4:
			fxs.Ring(iso, 8.f, 70.f, 0.35f, Rgb(0xFFB050, 220), 6.f);
			for (int i = 0; i < 8; ++i) Smoke(iso, 6.f, Rgb(0xB8A078, 150), 20.f, 0.6f, 14.f);
			break;
		case 6:   // 처형: 땅이 갈라진다
			AddDecal(Decal::Crack, iso, 70.f, 1.6f, Rgb(0xFF8A3A));
			fxs.Ring(iso, 10.f, 110.f, 0.45f, Rgb(0xFF8A3A, 240), 8.f);
			fxs.Beam(iso, 50.f, 220.f, 0.4f, Rgb(0xFFB060, 220));
			for (int i = 0; i < 16; ++i) Spark(iso, z, RandF(160.f, 380.f), RandF(0.f, 6.28f), Rgb(0xFFF0C0), Rgb(0xFF6A20, 0), 0.4f, 9.f);
			break;
		case 7:   // 룬 탄환
			Glow(iso, z, 20.f, 60.f, Rgb(0x9AE8FF, 220), 0.18f);
			for (int i = 0; i < 7; ++i) Spark(iso, z, RandF(90.f, 200.f), RandF(0.f, 6.28f), Rgb(0xE0F8FF), Rgb(0x4AB8FF, 0), 0.25f, 6.f);
			break;
		case 8:   // 빙결: 얼음 조각
			for (int i = 0; i < 8; ++i) Shard(iso, z, Rgb(0xC8F0FF, 230), RandF(40.f, 120.f), RandF(4.f, 7.f), RandF(0.5f, 0.9f));
			Glow(iso, z, 30.f, 70.f, Rgb(0x9AE8FF, 200), 0.3f);
			break;
		case 9:   // 화염
			for (int i = 0; i < 8; ++i) Ember(iso, z, Rgb(0xFFD060), Rgb(0xFF3A10, 0), 12.f, RandF(0.4f, 0.7f));
			break;
		case 10:  // 룬 폭발
			Glow(iso, z, 30.f, 90.f, Rgb(0xC0A0FF, 220), 0.3f);
			break;
		case 11:  // 번개
			Glow(iso, z, 26.f, 80.f, Rgb(0xE8F0FF, 240), 0.2f);
			for (int i = 0; i < 9; ++i) Spark(iso, z, RandF(140.f, 320.f), RandF(0.f, 6.28f), Rgb(0xFFFFFF), Rgb(0x6AB8FF, 0), 0.3f, 7.f);
			break;
		case 12:  // 혜성 충돌
			AddDecal(Decal::Crack, iso, 110.f, 2.0f, Rgb(0xFF5A2A));
			fxs.Ring(iso, 20.f, 200.f, 0.55f, Rgb(0xFF7A3A, 240), 10.f);
			fxs.Ring(iso, 10.f, 140.f, 0.4f, Rgb(0xFFF0C0, 220), 5.f);
			fxs.Beam(iso, 90.f, 300.f, 0.5f, Rgb(0xFF9A4A, 230));
			for (int i = 0; i < 26; ++i) Spark(iso, 10.f, RandF(200.f, 480.f), RandF(0.f, 6.28f), Rgb(0xFFE08A), Rgb(0xFF3A10, 0), 0.6f, 10.f);
			for (int i = 0; i < 16; ++i) Smoke(iso, 10.f, Rgb(0x5A4A3A, 180), 40.f, 1.4f, 40.f);
			break;
		case 13: case 17:
			for (int i = 0; i < (crit ? 10 : 5); ++i) Spark(iso, z, RandF(140.f, 280.f), RandF(-0.8f, 0.8f), Rgb(0xF0E0FF), Rgb(0xA06AFF, 0), 0.25f, 7.f);
			break;
		case 14:
			for (int i = 0; i < 6; ++i) Smoke(iso, z, Rgb(0x3A2A5A, 160), 18.f, 0.5f, 25.f);
			break;
		case 15:  // 독
			for (int i = 0; i < 6; ++i) Ember(iso, z, Rgb(0xB8FF7A), Rgb(0x2A8A20, 0), 10.f, 0.5f);
			fxs.HitSpark(iso, height, Rgb(0x8AE85A), crit, 1.f);
			break;
		case 18:
			for (int i = 0; i < 5; ++i) Spark(iso, z, RandF(120.f, 260.f), RandF(0.f, 6.28f), Rgb(0xFFFFFF), Rgb(0xB0A0FF, 0), 0.25f, 7.f);
			break;
		default:
			fxs.HitSpark(iso, height, Rgb(0xFFE0B0), crit, 1.f);
			break;
		}
		if (flags2 & DAMAGE2_WALL)
		{
			fxs.Ring(iso, 8.f, 80.f, 0.35f, Rgb(0xD8C8A0, 220), 7.f);
			for (int i = 0; i < 10; ++i) Smoke(iso, 8.f, Rgb(0x9A8A70, 170), 22.f, 0.8f, 20.f);
		}
		if (comboId != 0)
		{
			// 연계: 금빛 폭발 + 별
			Glow(iso, z, 40.f, 140.f, Rgb(0xFFE08A, 230), 0.3f);
			Particle& s = P();
			s.pos = iso; s.z = z;
			s.life = s.maxLife = 0.35f;
			s.size0 = 40.f; s.size1 = 150.f;
			s.c0 = Rgb(0xFFFFFF); s.c1 = Rgb(0xFFB040, 0);
			s.rot = RandF(0.f, 1.f);
			s.shape = Shape::Star;
			s.additive = true;
		}
	}

	Feel CastFeel(uint16_t id)
	{
		switch (id)
		{
		case 3: case 9: case 18: return { 0.12f, 0.02f, 0.f };
		case 4: return { 0.15f, 0.03f, 0.f };
		case 5: return { 0.06f, 0.015f, 0.f };
		case 10: case 14: case 16: return { 0.04f, 0.02f, 0.f };
		default: return {};
		}
	}

	Feel HitFeel(uint16_t id, bool crit, bool combo, bool killed)
	{
		Feel f;
		switch (id)
		{
		case 1: case 13: case 7: f = { 0.05f, 0.012f, 0.045f }; break;
		case 2: f = { 0.2f, 0.03f, 0.08f }; break;
		case 3: case 18: f = { 0.1f, 0.015f, 0.04f }; break;
		case 4: f = { 0.25f, 0.035f, 0.08f }; break;
		case 6: f = { 0.45f, 0.06f, 0.14f }; break;
		case 12: f = { 0.55f, 0.06f, 0.12f }; break;
		case 17: f = { 0.2f, 0.03f, 0.08f }; break;
		case 11: f = { 0.12f, 0.02f, 0.03f }; break;
		default: f = { 0.08f, 0.015f, 0.05f }; break;
		}
		if (crit) { f.trauma += 0.08f; f.hitstop += 0.03f; f.kick += 0.01f; }
		if (combo) { f.trauma += 0.12f; f.hitstop += 0.05f; f.kick += 0.02f; }
		if (killed) { f.hitstop += 0.03f; }
		f.hitstop = std::min(f.hitstop, 0.18f);
		return f;
	}

	void Bolt(Vector2 a, float za, Vector2 b, float zb, Color color, float life)
	{
		g_bolts.push_back(BoltFx{ a, b, za, zb, 0.f, life, color, static_cast<uint32_t>(RandF(0.f, 1000.f)) });
	}

	void OnServerEffect(EffectId id, Vector2 iso, int param)
	{
		auto& fxs = fx::Fx();
		switch (id)
		{
		case EffectId::RuneExplosion:
			fxs.Ring(iso, 10.f, 90.f, 0.35f, Rgb(0xC0A0FF, 230), 6.f);
			Glow(iso, 20.f, 40.f, 120.f, Rgb(0xB08AFF, 200), 0.35f);
			for (int i = 0; i < 16; ++i) Spark(iso, 16.f, RandF(120.f, 300.f), RandF(0.f, 6.28f), Rgb(0xF0E8FF), Rgb(0x8A5AFF, 0), 0.4f, 7.f);
			break;
		case EffectId::SteamBurst:
			fxs.Ring(iso, 10.f, 100.f, 0.4f, Rgb(0xE8F8FF, 230), 7.f);
			for (int i = 0; i < 18; ++i) Smoke(iso, RandF(10.f, 30.f), Rgb(0xE8F4FF, 200), 30.f, 1.0f, 70.f);
			for (int i = 0; i < 10; ++i) Shard(iso, 20.f, Rgb(0xC8F0FF, 230), RandF(80.f, 180.f), 6.f, 0.7f);
			break;
		case EffectId::Counter:
			fxs.Ring(iso, 6.f, 60.f, 0.3f, Rgb(0xFFD070, 240), 5.f);
			break;
		case EffectId::WallSlam:
			for (int i = 0; i < 12; ++i) Smoke(iso, 10.f, Rgb(0xA89878, 180), 26.f, 0.9f, 25.f);
			break;
		case EffectId::PoisonBurst:
			fxs.Ring(iso, 6.f, 50.f + param * 12.f, 0.4f, Rgb(0x9AF06A, 230), 6.f);
			for (int i = 0; i < 8 + param * 3; ++i) Ember(iso, 20.f, Rgb(0xC8FF8A), Rgb(0x2A8A20, 0), 18.f, RandF(0.4f, 0.8f));
			break;
		// ── 장비 특수 효과 ──
		case EffectId::ProcThunder:
			// 하늘에서 떨어지는 벼락 + 바닥 번쩍임
			Bolt(Vector2{ iso.x + RandF(-18.f, 18.f), iso.y }, 230.f, iso, 18.f, Rgb(0xBFE8FF), 0.28f);
			Bolt(Vector2{ iso.x + RandF(-30.f, 30.f), iso.y }, 200.f, iso, 24.f, Rgb(0x8AC8FF), 0.22f);
			fxs.Ring(iso, 4.f, 58.f, 0.3f, Rgb(0xD8F0FF, 240), 5.f);
			Glow(iso, 20.f, 30.f, 90.f, Rgb(0x9AD8FF, 220), 0.28f);
			for (int i = 0; i < 10; ++i) Spark(iso, 18.f, RandF(120.f, 260.f), RandF(0.f, 6.28f), Rgb(0xFFFFFF), Rgb(0x6AB8FF, 0), 0.3f, 5.f);
			break;
		case EffectId::ProcLifesteal:
			// 붉은 기운이 주인에게 스며든다
			for (int i = 0; i < 8; ++i)
			{
				Particle& p = P();
				const float a = RandF(0.f, 6.28f), r = RandF(14.f, 26.f);
				p.pos = { iso.x + std::cos(a) * r, iso.y + std::sin(a) * r * 0.5f };
				p.z = RandF(10.f, 50.f);
				p.vel = { -std::cos(a) * r * 1.6f, -std::sin(a) * r * 0.8f };
				p.vz = RandF(10.f, 30.f);
				p.life = p.maxLife = RandF(0.4f, 0.6f);
				p.size0 = RandF(3.f, 5.f); p.size1 = 0.5f;
				p.c0 = Rgb(0xFF6A7A, 230); p.c1 = Rgb(0xC02040, 0);
			}
			break;
		case EffectId::ProcFrost:
			fxs.Ring(iso, 4.f, 44.f, 0.35f, Rgb(0xC8F0FF, 220), 4.f);
			for (int i = 0; i < 8; ++i) Shard(iso, RandF(10.f, 40.f), Rgb(0xE0F8FF, 230), RandF(60.f, 140.f), 4.f, 0.5f);
			break;
		case EffectId::ProcExecute:
			fxs.Ring(iso, 4.f, 52.f, 0.28f, Rgb(0xFF7A3A, 240), 6.f);
			for (int i = 0; i < 10; ++i) Spark(iso, 30.f, RandF(140.f, 280.f), RandF(0.f, 6.28f), Rgb(0xFFE0A0), Rgb(0xFF3A10, 0), 0.32f, 6.f);
			break;
		case EffectId::ProcEcho:
			fxs.Ring(iso, 4.f, 40.f, 0.3f, Rgb(0xD8C0FF, 220), 3.f);
			fxs.Ring(iso, 4.f, 64.f, 0.45f, Rgb(0xB090FF, 160), 2.f);
			Glow(iso, 30.f, 16.f, 60.f, Rgb(0xC8A8FF, 180), 0.3f);
			break;
		case EffectId::ProcGuardian:
			fxs.Ring(iso, 10.f, 46.f, 0.5f, Rgb(0xFFE08A, 230), 5.f);
			Glow(iso, 34.f, 40.f, 70.f, Rgb(0xFFD870, 160), 0.5f);
			for (int i = 0; i < 10; ++i) Ember(iso, RandF(10.f, 60.f), Rgb(0xFFF0B0), Rgb(0xFFB030, 0), 22.f, RandF(0.5f, 0.8f));
			break;
		case EffectId::ProcEmber:
			for (int i = 0; i < 10; ++i) Ember(iso, RandF(10.f, 40.f), Rgb(0xFFC050), Rgb(0xFF3A10, 0), 16.f, RandF(0.35f, 0.6f));
			fxs.Ring(iso, 4.f, 34.f, 0.3f, Rgb(0xFF8A3A, 200), 3.f);
			break;
		case EffectId::ProcVenom:
			fxs.Ring(iso, 4.f, 34.f, 0.3f, Rgb(0x9AF06A, 200), 3.f);
			for (int i = 0; i < 8; ++i) Ember(iso, RandF(10.f, 40.f), Rgb(0xC8FF8A), Rgb(0x2A8A20, 0), 14.f, RandF(0.4f, 0.7f));
			break;
		default:
			break;
		}
	}

	// ── 상태이상 ─────────────────────────────────────────────────────────────
	void EmitStatus(const Entity& e, Vector2 iso, float height, float dt, float now)
	{
		if (e.Status(static_cast<uint8_t>(StatusId::Burn), now) && RandF() < dt * 14.f)
			Ember(iso, RandF(5.f, height * 0.8f), Rgb(0xFFC050), Rgb(0xFF3A10, 0), 10.f, RandF(0.35f, 0.6f));
		if (const auto* ps = e.Status(static_cast<uint8_t>(StatusId::Poison), now); ps && RandF() < dt * (4.f + ps->stacks * 2.f))
		{
			Particle& p = P();
			p.pos = { iso.x + RandF(-10.f, 10.f), iso.y + RandF(-4.f, 4.f) };
			p.z = RandF(10.f, height * 0.7f);
			p.vz = RandF(15.f, 40.f);
			p.life = p.maxLife = RandF(0.5f, 0.9f);
			p.size0 = RandF(3.f, 6.f); p.size1 = 1.f;
			p.c0 = Rgb(0xA8F070, 230); p.c1 = Rgb(0x4AA030, 0);
			p.shape = Shape::Ring;
			p.wobble = 6.f;
		}
		if (e.Status(static_cast<uint8_t>(StatusId::Freeze), now) && RandF() < dt * 3.f)
			Shard(iso, RandF(10.f, height * 0.6f), Rgb(0xE0F8FF, 200), 20.f, 3.f, 0.5f);
		if (e.Status(static_cast<uint8_t>(StatusId::Ambush), now) && RandF() < dt * 10.f)
			Glow(iso, RandF(20.f, height * 0.8f), 10.f, 1.f, Rgb(0xC08AFF, 200), 0.4f);
	}

	void DrawStatus(const Entity& e, Vector2 iso, float height, float now)
	{
		// 기절: 머리 위를 도는 별
		if (e.Status(static_cast<uint8_t>(StatusId::Stun), now))
		{
			gfx::SetDepth(iso.y / gfx::kHalfH + 0.7f);
			for (int i = 0; i < 3; ++i)
			{
				const float a = now * 5.f + i * kPi * 2.f / 3.f;
				const Vector2 c{ iso.x + std::cos(a) * 14.f, iso.y - height - 6.f + std::sin(a) * 5.f };
				gfx::Star(c, 2.f, 6.f, 5, a, Rgb(0xFFE070, 240));
			}
		}
		// 빙결: 반투명 얼음 덩어리
		if (e.Status(static_cast<uint8_t>(StatusId::Freeze), now))
		{
			gfx::SetDepth(iso.y / gfx::kHalfH + 0.65f);
			const float w = 20.f, h = height * 0.9f;
			const Vector2 pts[6] = {
				{ iso.x - w, iso.y - 2.f }, { iso.x - w * 1.1f, iso.y - h * 0.55f }, { iso.x - w * 0.4f, iso.y - h },
				{ iso.x + w * 0.5f, iso.y - h * 0.95f }, { iso.x + w * 1.1f, iso.y - h * 0.5f }, { iso.x + w, iso.y - 2.f } };
			gfx::PolyGrad(pts, 6, Vector2{ iso.x, iso.y - h * 0.5f }, Rgb(0xE8F8FF, 70), Rgb(0x8AD0FF, 120));
			for (int i = 0; i < 6; ++i)
				gfx::Line(pts[i], pts[(i + 1) % 6], 1.5f, Rgb(0xF0FAFF, 180));
			gfx::Line(Vector2{ iso.x - w * 0.5f, iso.y - h * 0.7f }, Vector2{ iso.x - w * 0.1f, iso.y - h * 0.9f }, 3.f, Rgb(0xFFFFFF, 150));
		}
		// 철벽 태세: 금빛 방패 막
		if (e.Status(static_cast<uint8_t>(StatusId::Guard), now))
		{
			const float pulse = 0.5f + 0.5f * std::sin(now * 6.f);
			gfx::SetDepth(iso.y / gfx::kHalfH + 0.66f);
			gfx::Additive(true);
			gfx::EllipseGrad(Vector2{ iso.x, iso.y - height * 0.45f }, 34.f, height * 0.62f, Rgb(0xFFC860, 20), Rgb(0xFFD070, static_cast<uint8_t>(90 + 50 * pulse)));
			gfx::EllipseArc(Vector2{ iso.x, iso.y - height * 0.45f }, 34.f, height * 0.62f, 0.f, kPi * 2.f, 2.f, Rgb(0xFFE8A0, 160));
			gfx::Additive(false);
		}
		// 룬 표식: 머리 위 룬
		if (e.Status(static_cast<uint8_t>(StatusId::Marked), now))
		{
			gfx::SetDepth(iso.y / gfx::kHalfH + 0.7f);
			const Vector2 c{ iso.x, iso.y - height - 14.f };
			gfx::Additive(true);
			gfx::CircleGrad(c, 12.f, Rgb(0x6AE8FF, 160), Rgb(0x6AE8FF, 0));
			gfx::Additive(false);
			gfx::Line(Vector2{ c.x - 5, c.y - 6 }, Vector2{ c.x + 5, c.y + 6 }, 2.f, Rgb(0xC8F8FF, 230));
			gfx::Line(Vector2{ c.x + 5, c.y - 6 }, Vector2{ c.x - 5, c.y + 6 }, 2.f, Rgb(0xC8F8FF, 230));
			gfx::Line(Vector2{ c.x, c.y - 8 }, Vector2{ c.x, c.y + 8 }, 2.f, Rgb(0xC8F8FF, 230));
		}
		// 중독 중첩: 발밑 초록 점
		if (const auto* ps = e.Status(static_cast<uint8_t>(StatusId::Poison), now))
		{
			gfx::SetDepth(iso.y / gfx::kHalfH + 0.7f);
			for (int i = 0; i < ps->stacks; ++i)
				gfx::Circle(Vector2{ iso.x - (ps->stacks - 1) * 5.f + i * 10.f, iso.y - height - 6.f }, 3.5f, Rgb(0x8AE85A, 230));
		}
	}

	void DrawTelegraph(const Entity& e, float now)
	{
		(void)now;
		const auto& t = e.tele;
		if (!t.active)
			return;
		const float u = util::Clamp01(t.t / std::max(0.05f, t.dur));
		const float pulse = 0.5f + 0.5f * std::sin(t.t * 22.f);
		const Color red = Rgb(0xFF3A2A);
		gfx::SetDepthRaw(-0.968f);
		if (t.shape == static_cast<uint8_t>(TelegraphShape::Line))
		{
			// 시작 칸 → 끝 칸 띠(칸 폭)
			const Vector2 a = gfx::IsoPx(t.from.x, t.from.y), b = gfx::IsoPx(t.c.x, t.c.y);
			const Vector2 d = util::Norm(util::Sub(b, a));
			const Vector2 n{ -d.y * 22.f, d.x * 11.f * 2.f * 0.5f };
			const Vector2 mid = util::LerpV(a, b, u);
			gfx::Quad(util::Add(a, n), util::Add(b, n), util::Sub(b, n), util::Sub(a, n), util::MulA(red, 0.16f + 0.08f * pulse));
			gfx::Quad(util::Add(a, n), util::Add(mid, n), util::Sub(mid, n), util::Sub(a, n), util::MulA(red, 0.34f));
			gfx::Line(util::Add(a, n), util::Add(b, n), 2.f, util::MulA(red, 0.9f));
			gfx::Line(util::Sub(a, n), util::Sub(b, n), 2.f, util::MulA(red, 0.9f));
			// 화살촉
			gfx::Tri(util::Add(b, util::Mul(n, 1.4f)), util::Sub(b, util::Mul(n, 1.4f)), util::Add(b, util::Mul(d, 20.f)), util::MulA(red, 0.7f));
			return;
		}
		// 원: 체비셰프 반경 → 칸 마름모 영역(서버 판정 그대로)
		const float R = t.radius + 0.5f;
		auto corner = [&](float dx, float dy, float k) { return gfx::IsoPx(t.c.x + dx * R * k, t.c.y + dy * R * k); };
		gfx::Quad(corner(-1, -1, 1), corner(1, -1, 1), corner(1, 1, 1), corner(-1, 1, 1), util::MulA(red, 0.15f + 0.08f * pulse));
		gfx::Quad(corner(-1, -1, u), corner(1, -1, u), corner(1, 1, u), corner(-1, 1, u), util::MulA(red, 0.34f));
		const Vector2 c4[4] = { corner(-1, -1, 1), corner(1, -1, 1), corner(1, 1, 1), corner(-1, 1, 1) };
		for (int i = 0; i < 4; ++i)
			gfx::Line(c4[i], c4[(i + 1) % 4], 2.5f + pulse * 1.5f, util::MulA(util::Shade(red, 1.3f), 0.9f));
	}

	void MonsterImpact(uint8_t pattern, Vector2 fromIso, Vector2 toIso, int radius, Color bodyColor)
	{
		auto& fxs = fx::Fx();
		switch (static_cast<MonsterPattern>(pattern))
		{
		case MonsterPattern::Slam:
		{
			const float r = 44.f + radius * 44.f;
			AddDecal(Decal::Crack, toIso, r, 1.4f, util::Shade(bodyColor, 0.8f));
			fxs.Ring(toIso, 10.f, r * 1.1f, 0.35f, Rgb(0xE8D8B8, 220), 6.f);
			for (int i = 0; i < 14; ++i) Smoke(toIso, 6.f, Rgb(0xA89878, 170), 26.f, 0.9f, 22.f);
			for (int i = 0; i < 10; ++i) Shard(toIso, 8.f, Rgb(0x8A7A60), RandF(80.f, 200.f), RandF(3.f, 6.f), RandF(0.5f, 0.9f));
			break;
		}
		case MonsterPattern::Leap:
			fxs.Ring(toIso, 8.f, 90.f, 0.3f, Rgb(0xE8D8B8, 200), 5.f);
			for (int i = 0; i < 10; ++i) Smoke(toIso, 6.f, Rgb(0xA89878, 160), 22.f, 0.7f, 18.f);
			break;
		case MonsterPattern::Charge:
		{
			const int n = 10;
			for (int i = 0; i <= n; ++i)
				Smoke(util::LerpV(fromIso, toIso, i / static_cast<float>(n)), 4.f, Rgb(0xB8A078, 150), 20.f, 0.6f, 14.f);
			break;
		}
		case MonsterPattern::Ranged:
			Glow(toIso, 20.f, 20.f, 70.f, util::MulA(bodyColor, 0.8f), 0.25f);
			for (int i = 0; i < 9; ++i) Spark(toIso, 16.f, RandF(120.f, 260.f), RandF(0.f, 6.28f), Rgb(0xFFF0D0), util::WithA(bodyColor, 0.f), 0.3f, 6.f);
			fxs.Ring(toIso, 6.f, 46.f, 0.25f, util::MulA(bodyColor, 0.9f), 3.f);
			break;
		default:
			break;
		}
	}

	void Shot(Vector2 fromIso, float fromZ, Vector2 toIso, float toZ, float dur, Color color)
	{
		g_proj.push_back(Projectile{ fromIso, toIso, fromZ, toZ, 0.f, std::max(0.05f, dur), color, 0 });
	}

	float AnimDuration(uint16_t id)
	{
		switch (id)
		{
		case 1: return 0.42f;
		case 2: return 0.38f;
		case 3: return 0.45f;
		case 4: return 0.35f;
		case 5: return 0.5f;
		case 6: return 0.75f;
		case 7: return 0.36f;
		case 8: case 11: return 0.45f;
		case 9: return 0.5f;
		case 10: return 0.3f;
		case 12: return 0.9f;
		case 13: return 0.34f;
		case 14: return 0.3f;
		case 15: return 0.36f;
		case 16: return 0.3f;
		case 17: return 0.4f;
		case 18: return 0.9f;
		default: return 0.4f;
		}
	}

	// ── 스킬 아이콘(스킬바) ─────────────────────────────────────────────────
	void DrawSkillIcon(uint16_t id, Vector2 c, float s, float time, bool locked)
	{
		const SkillRow* row = SkillTable::Find(id);
		const Color col = row ? Rgb(row->color) : Rgb(0xFFFFFF);
		const float h = s * 0.5f;
		// 바탕: 직업 색 그라데이션
		DrawRectangleGradientV(static_cast<int>(c.x - h), static_cast<int>(c.y - h), static_cast<int>(s), static_cast<int>(s),
			util::Shade(col, 0.32f), util::Shade(col, 0.12f));
		auto L = [&](float x0, float y0, float x1, float y1, float w, Color cc)
		{
			DrawLineEx(Vector2{ c.x + x0 * h, c.y + y0 * h }, Vector2{ c.x + x1 * h, c.y + y1 * h }, w * s / 44.f, cc);
		};
		auto C = [&](float x, float y, float r, Color cc) { DrawCircleV(Vector2{ c.x + x * h, c.y + y * h }, r * h, cc); };
		auto Ring = [&](float x, float y, float r, float w, Color cc) { DrawRing(Vector2{ c.x + x * h, c.y + y * h }, r * h - w * 0.5f, r * h + w * 0.5f, 0, 360, 32, cc); };
		const Color hi = util::Shade(col, 1.35f), wh = Rgb(0xFFFFFF, 230);
		switch (id)
		{
		case 1: L(-0.6f, 0.6f, 0.55f, -0.55f, 5, hi); L(-0.35f, 0.15f, -0.1f, 0.4f, 4, wh); break;                     // 대검
		case 2: C(0, 0, 0.55f, util::Shade(col, 0.7f)); Ring(0, 0, 0.55f, 3, hi); L(0, -0.4f, 0, 0.4f, 3, hi); L(-0.4f, 0, 0.4f, 0, 3, hi); break;  // 방패
		case 3: DrawRing(c, h * 0.45f, h * 0.62f, 30, 330, 24, hi); DrawTriangle(Vector2{ c.x + h * 0.55f, c.y - h * 0.5f }, Vector2{ c.x + h * 0.2f, c.y - h * 0.7f }, Vector2{ c.x + h * 0.25f, c.y - h * 0.2f }, hi); break;  // 회전
		case 4: L(-0.6f, 0, 0.5f, 0, 5, hi); DrawTriangle(Vector2{ c.x + h * 0.7f, c.y }, Vector2{ c.x + h * 0.35f, c.y - h * 0.35f }, Vector2{ c.x + h * 0.35f, c.y + h * 0.35f }, hi); L(-0.6f, -0.35f, 0.1f, -0.35f, 2, wh); L(-0.6f, 0.35f, 0.1f, 0.35f, 2, wh); break;  // 돌진
		case 5: DrawPoly(c, 6, h * 0.62f, 0, util::Shade(col, 0.6f)); DrawPolyLinesEx(c, 6, h * 0.62f, 0, 3 * s / 44.f, hi); C(0, 0, 0.18f, hi); break;  // 철벽
		case 6: L(0, -0.7f, 0, 0.45f, 6, hi); L(-0.35f, -0.25f, 0.35f, -0.25f, 4, hi); C(0, 0.55f, 0.18f, Rgb(0xFF6A2A)); break;  // 처형
		case 7: C(0, 0, 0.3f, hi); Ring(0, 0, 0.5f, 2, util::WithA(hi, 0.6f)); L(-0.7f, 0.3f, -0.25f, 0.1f, 2, wh); break;  // 탄환
		case 8: for (int i = 0; i < 6; ++i) { const float a = i * kPi / 3.f + time * 0.5f; L(0, 0, std::cos(a) * 0.62f, std::sin(a) * 0.62f, 3, hi); } Ring(0, 0, 0.35f, 2, wh); break;  // 빙결
		case 9: Ring(0, 0.1f, 0.5f, 5, Rgb(0xFF7A2A)); C(0, 0.1f, 0.2f, Rgb(0xFFE08A)); for (int i = 0; i < 5; ++i) { const float a = -kPi * 0.5f + (i - 2) * 0.45f; L(std::cos(a) * 0.45f, 0.1f + std::sin(a) * 0.45f, std::cos(a) * 0.72f, 0.1f + std::sin(a) * 0.72f, 3, Rgb(0xFFB040)); } break;  // 화염 고리
		case 10: Ring(-0.3f, 0.2f, 0.28f, 2, util::WithA(hi, 0.6f)); Ring(0.3f, -0.2f, 0.28f, 3, hi); L(-0.3f, 0.2f, 0.3f, -0.2f, 2, wh); break;  // 도약
		case 11: L(0.2f, -0.7f, -0.2f, -0.05f, 5, hi); L(-0.2f, -0.05f, 0.25f, 0.05f, 5, hi); L(0.25f, 0.05f, -0.15f, 0.7f, 5, hi); break;  // 번개
		case 12: C(0.25f, 0.25f, 0.3f, Rgb(0xFF7A3A)); L(-0.7f, -0.7f, 0.1f, 0.1f, 6, util::WithA(Rgb(0xFFD070), 0.8f)); break;  // 혜성
		case 13: L(-0.6f, 0.2f, 0.6f, -0.1f, 3, hi); L(-0.6f, 0.5f, 0.6f, 0.2f, 3, hi); break;  // 찌르기
		case 14: C(0.15f, 0, 0.4f, Rgb(0x2A1A4A)); L(-0.7f, 0, 0.2f, 0, 4, hi); DrawTriangle(Vector2{ c.x + h * 0.55f, c.y }, Vector2{ c.x + h * 0.2f, c.y - h * 0.3f }, Vector2{ c.x + h * 0.2f, c.y + h * 0.3f }, hi); break;  // 그림자 돌진
		case 15: L(-0.55f, 0.55f, 0.55f, -0.55f, 5, hi); C(0.2f, 0.45f, 0.14f, Rgb(0x8AE85A)); C(-0.1f, 0.62f, 0.09f, Rgb(0x8AE85A)); break;  // 독 칼날
		case 16: for (int i = 0; i < 3; ++i) C(-0.4f + i * 0.35f, 0, 0.25f, util::WithA(hi, 0.3f + i * 0.3f)); break;  // 잔상
		case 17: L(-0.55f, -0.55f, 0.55f, 0.55f, 5, hi); L(0.55f, -0.55f, -0.55f, 0.55f, 5, Rgb(0xFF6A8A)); break;  // 후방 습격
		case 18: DrawRing(c, h * 0.35f, h * 0.55f, 0, 360, 24, util::WithA(hi, 0.5f)); for (int i = 0; i < 5; ++i) { const float a = i * kPi * 0.4f + time; L(std::cos(a) * 0.2f, std::sin(a) * 0.2f, std::cos(a + 0.6f) * 0.72f, std::sin(a + 0.6f) * 0.72f, 3, hi); } break;  // 월영
		default: C(0, 0, 0.4f, hi); break;
		}
		if (locked)
			DrawRectangle(static_cast<int>(c.x - h), static_cast<int>(c.y - h), static_cast<int>(s), static_cast<int>(s), Rgb(0x000000, 150));
	}
}
