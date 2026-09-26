#include "fx/Impact.h"
#include "fx/Particles.h"
#include "fx/FxScale.h"
#include "render/Post.h"
#include "render/FlashLight.h"
#include "audio/Sfx.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>

namespace impact
{
	namespace
	{
		using util::Rgb;
		using util::RandF;
		using fx::Particle;
		using fx::Shape;
		constexpr float kPi = 3.14159265f;

		Particle& P() { return fx::Particles().Emit(); }

		void Spark(Vector2 iso, float z, float speed, float ang, Color c0, Color c1, float life, float size, fx::Sub death = fx::Sub::None)
		{
			Particle& p = P();
			p.pos = iso; p.z = z;
			p.vel = { std::cos(ang) * speed, std::sin(ang) * speed * 0.5f };
			p.vz = std::sin(ang) * speed * 0.3f + RandF(20.f, 110.f);
			p.gravity = 420.f; p.drag = 2.2f;
			p.life = p.maxLife = life;
			p.size0 = size; p.size1 = 0.f;
			p.c0 = c0; p.c1 = c1;
			p.shape = Shape::Spark; p.stretch = true; p.additive = true;
			p.onDeath = death; p.subColor = c0;
		}

		void Glow(Vector2 iso, float z, float s0, float s1, Color c, float life)
		{
			Particle& p = P();
			p.pos = iso; p.z = z;
			p.life = p.maxLife = life;
			p.size0 = s0; p.size1 = s1;
			p.c0 = c; p.c1 = util::WithA(c, 0.f);
			p.shape = Shape::Glow; p.additive = true;
		}

		void Puff(Vector2 iso, float z, Color c, float size, float life, float rise, float spread)
		{
			Particle& p = P();
			const float a = RandF(0.f, 2.f * kPi);
			p.pos = { iso.x + std::cos(a) * spread * RandF(0.2f, 1.f), iso.y + std::sin(a) * spread * 0.5f * RandF(0.2f, 1.f) };
			p.z = z;
			p.vel = { std::cos(a) * RandF(10.f, 50.f), std::sin(a) * RandF(5.f, 25.f) };
			p.vz = rise;
			p.drag = 1.6f;
			p.life = p.maxLife = life;
			p.size0 = size * 0.6f; p.size1 = size * 1.7f;
			p.c0 = c; p.c1 = util::WithA(c, 0.f);
			p.rot = RandF(0.f, 6.f); p.vrot = RandF(-1.f, 1.f);
			p.shape = Shape::Smoke;
		}

		// 튀는 파편(돌 · 얼음 · 금속 조각) — 땅에서 튕기고 사라질 때 먼지/서리
		void Chunk(Vector2 iso, float z, Color c, float size, float speed, fx::Sub death, Shape shape = Shape::Chunk)
		{
			Particle& p = P();
			const float a = RandF(0.f, 2.f * kPi);
			p.pos = iso; p.z = z;
			p.vel = { std::cos(a) * speed, std::sin(a) * speed * 0.5f };
			p.vz = RandF(140.f, 320.f);
			p.gravity = 820.f; p.bounce = true; p.drag = 0.7f;
			p.life = p.maxLife = RandF(0.7f, 1.2f);
			p.size0 = size; p.size1 = size * 0.7f;
			p.c0 = c; p.c1 = util::WithA(c, 0.f);
			p.rot = RandF(0.f, 6.f); p.vrot = RandF(-10.f, 10.f);
			p.shape = shape;
			p.onDeath = death;
		}

		void Ember(Vector2 iso, float z, Color c0, float spread, float life, bool trail)
		{
			Particle& p = P();
			p.pos = { iso.x + RandF(-spread, spread), iso.y + RandF(-spread * 0.5f, spread * 0.5f) };
			p.z = z;
			p.vel = { RandF(-40.f, 40.f), RandF(-20.f, 20.f) };
			p.vz = RandF(50.f, 160.f);
			p.gravity = 40.f; p.drag = 1.1f; p.wobble = 14.f;
			p.life = p.maxLife = life;
			p.size0 = RandF(5.f, 10.f); p.size1 = 1.f;
			p.c0 = c0; p.c1 = Rgb(0xFF3A10, 0);
			p.shape = Shape::Soft; p.additive = true;
			if (trail) { p.trail = fx::Sub::Embers; p.subColor = c0; }
		}

		const fxgrade::Budget& B(fxgrade::Tier t) { return fxgrade::Get(t); }
		int Budgeted(const Ctx& c, int n) { return std::max(1, static_cast<int>(n * (c.crit ? 1.3f : 1.f) * (c.heavy ? 1.2f : 1.f))); }

		Color DebrisOf(Element e)
		{
			switch (e)
			{
			case Element::Frost: return Rgb(0xD8F2FF);
			case Element::Ink: return Rgb(0x14161C);
			case Element::Fire: case Element::Ember: return Rgb(0x4A3A30);
			default: return Rgb(0x7A6A58);
			}
		}
		fx::Sub DebrisSub(Element e)
		{
			switch (e)
			{
			case Element::Frost: return fx::Sub::Frost;
			case Element::Ink: return fx::Sub::Ink;
			case Element::Fire: case Element::Ember: return fx::Sub::Embers;
			default: return fx::Sub::Dust;
			}
		}
	}

	vfx::Ramp RampOf(Element e)
	{
		switch (e)
		{
		case Element::Gold: return vfx::GoldRamp();
		case Element::Fire: return vfx::FireRamp();
		case Element::Frost: return vfx::FrostRamp();
		case Element::Bolt: return vfx::BoltRamp();
		case Element::Teal: return vfx::TealRamp();
		case Element::Ink: return vfx::MoonRamp();     // 먹빛 기술의 빛나는 부분은 달빛
		case Element::Moon: return vfx::MoonRamp();
		case Element::Poison: return vfx::PoisonRamp();
		case Element::Earth: return vfx::DustRamp();
		case Element::Ember: return vfx::EmberRamp();
		case Element::Steel: default: return { Rgb(0xFFFFFF), Rgb(0xEDE7DB), Rgb(0x8E887D) };
		}
	}

	Color LightOf(Element e)
	{
		switch (e)
		{
		case Element::Gold: return Rgb(0xFFD28A);
		case Element::Fire: return Rgb(0xFF9A4A);
		case Element::Frost: return Rgb(0xA8DCFF);
		case Element::Bolt: return Rgb(0xC8FFF4);
		case Element::Teal: return Rgb(0x8FE0D4);
		case Element::Ink: case Element::Moon: return Rgb(0xD8E2F0);
		case Element::Poison: return Rgb(0xB0F07A);
		case Element::Earth: return Rgb(0xFFE0B0);
		case Element::Ember: return Rgb(0xFF7A4A);
		case Element::Steel: default: return Rgb(0xFFF4E0);
		}
	}

	void SlashFlash(const Ctx& c, float size)
	{
		vfx::Spec s = vfx::Make(vfx::Kind::Slash, c.iso, c.z, size * c.scale, 0.26f, RampOf(c.elem));
		s.rot = std::atan2(c.dir.y, c.dir.x);
		s.depthBias = 0.6f;
		vfx::Spawn(s);
	}

	void Strike(const Ctx& c)
	{
		const auto& b = B(c.tier);
		const vfx::Ramp r = RampOf(c.elem);
		const int t = static_cast<int>(c.tier);
		const float k = c.scale * (c.crit ? 1.25f : 1.f);
		// ① 중심 섬광
		{
			vfx::Spec f = vfx::Make(vfx::Kind::Flare, c.iso, c.z, (48.f + 34.f * t) * k, 0.16f + 0.04f * t, r);
			f.depthBias = 0.7f;
			f.intensity = c.crit ? 1.3f : 1.f;
			vfx::Spawn(f);
		}
		// ② 충격 고리(바닥) · T3부터 화면 충격파 — T1(기본 공격)도 작은 고리 하나(5겹)
		{
			vfx::Spec g = vfx::Make(vfx::Kind::GroundRing, c.iso, 0.f, (t >= 2 ? 70.f + 40.f * t : 64.f) * k, t >= 2 ? 0.3f + 0.05f * t : 0.2f, r);
			if (t < 2) g.intensity = 0.7f;
			vfx::Spawn(g);
		}
		if (b.shockwave)
			post::Shockwave(c.iso, 8.f, (70.f + 50.f * (t - 2)) * k, 18.f, 0.8f + 0.4f * (t - 3), 0.32f);
		// ③ 튀는 불꽃 · 파편
		const int sparks = Budgeted(c, std::min(b.particles / 6, 10 + 8 * t));
		const Color hot = r.hot, mid = util::WithA(r.mid, 0.f);
		for (int i = 0; i < sparks; ++i)
		{
			float a = RandF(-1.2f, 1.2f) + std::atan2(c.dir.y, c.dir.x);
			if (RandF() < 0.35f) a = RandF(0.f, 2.f * kPi);
			Spark(c.iso, c.z, RandF(140.f, 260.f + 60.f * t) * k, a, hot, mid, RandF(0.18f, 0.35f + 0.05f * t), RandF(5.f, 8.f + t),
				t >= 3 && i % 3 == 0 ? fx::Sub::Sparks : fx::Sub::None);
		}
		if (c.heavy || t >= 3)
			for (int i = 0; i < 2 + t; ++i)
				Chunk(c.iso, c.z * 0.5f, DebrisOf(c.elem), RandF(3.f, 6.f), RandF(40.f, 150.f), DebrisSub(c.elem));
		// ④ 연기 · 먼지 — T1은 한 줌
		if (t < 2)
		{
			const Color dust = c.elem == Element::Ink ? Rgb(0x14161C, 110) : (c.elem == Element::Frost ? Rgb(0xE8F6FF, 90) : Rgb(0xA8987A, 90));
			Puff(c.iso, RandF(4.f, c.z * 0.5f), dust, RandF(12.f, 18.f) * k, RandF(0.35f, 0.5f), RandF(10.f, 20.f), 10.f);
		}
		if (t >= 2)
		{
			const Color smoke = c.elem == Element::Ink ? Rgb(0x14161C, 150) : (c.elem == Element::Frost ? Rgb(0xE8F6FF, 120) : Rgb(0xA8987A, 120));
			for (int i = 0; i < t + (c.heavy ? 2 : 0); ++i)
				Puff(c.iso, RandF(4.f, c.z * 0.6f), smoke, RandF(16.f, 26.f) * k, RandF(0.5f, 0.9f), RandF(10.f, 30.f), 14.f);
		}
		// 몸 빛
		Glow(c.iso, c.z, 18.f * k, (40.f + 16.f * t) * k, util::WithA(r.mid, 0.8f), 0.14f + 0.03f * t);
		// ⑤ 순간 조명
		flashlight::Add(c.iso, b.lightRadius * (c.crit ? 1.2f : 1.f) * c.scale, LightOf(c.elem), 0.55f + 0.1f * t, b.lightSec);
		// 바닥 자국(T2부터 — 원소별)
		if (t >= 2 && b.decalSec > 0.f)
		{
			vfx::Kind dk = vfx::Kind::Scorch;
			if (c.elem == Element::Frost) dk = vfx::Kind::Frost;
			else if (c.elem == Element::Ink) dk = vfx::Kind::InkStain;
			vfx::Spec d = vfx::Make(dk, c.iso, 0.f, (54.f + 18.f * t) * k, b.decalSec, dk == vfx::Kind::Scorch ? vfx::SmokeRamp() : r);
			d.alpha = dk == vfx::Kind::Scorch ? 0.55f : 0.8f;
			vfx::Spawn(d);
		}
	}

	void Explosion(const Ctx& c, float radius)
	{
		const auto& b = B(c.tier);
		const vfx::Ramp r = RampOf(c.elem);
		const float R = radius * c.scale;
		// 불 공(가운데 뜨고) + 섬광
		vfx::Spec e = vfx::Make(vfx::Kind::Explosion, c.iso, R * 0.45f, R * 1.7f, 0.9f, r);
		e.depthBias = 0.9f;
		e.grow = 1.25f;
		vfx::Spawn(e);
		vfx::Spec f = vfx::Make(vfx::Kind::Flare, c.iso, R * 0.35f, R * 2.2f, 0.28f, r);
		f.depthBias = 1.f;
		f.intensity = 1.4f;
		vfx::Spawn(f);
		// 바닥: 충격 고리 · 균열(빛나다 식음) · 그을음
		vfx::Spawn(vfx::Make(vfx::Kind::GroundRing, c.iso, 0.f, R * 3.2f, 0.55f, r));
		vfx::Spec crack = vfx::Make(vfx::Kind::Crack, c.iso, 0.f, R * 2.2f, std::max(2.f, b.decalSec * 0.6f), r);
		vfx::Spawn(crack);
		vfx::Spec sc = vfx::Make(vfx::Kind::Scorch, c.iso, 0.f, R * 2.6f, b.decalSec, vfx::SmokeRamp());
		sc.alpha = 0.7f;
		vfx::Spawn(sc);
		// 연기 기둥(늦게 · 위로)
		for (int i = 0; i < 3; ++i)
		{
			vfx::Spec sm = vfx::Make(vfx::Kind::Smoke, Vector2{ c.iso.x + RandF(-R * 0.4f, R * 0.4f), c.iso.y + RandF(-R * 0.2f, R * 0.2f) },
				R * (0.6f + 0.3f * i), R * (1.3f + 0.3f * i), 1.6f + 0.3f * i, vfx::SmokeRamp());
			sm.delay = 0.12f + 0.1f * i;
			sm.depthBias = 0.8f;
			vfx::Spawn(sm);
		}
		// 화면: 충격파 · 열 아지랑이 · 줌 펀치는 부르는 쪽(카메라)
		post::Shockwave(c.iso, R * 0.3f, R * 3.4f, 26.f, 1.3f + 0.3f * (static_cast<int>(c.tier) - 3), 0.55f);
		post::HeatHaze(c.iso, R * 2.2f, 1.f, 1.6f);
		// 파편 · 불씨(꼬리 달린 것 몇 개)
		const int n = Budgeted(c, std::min(b.particles / 10, 60));
		for (int i = 0; i < n; ++i)
			Spark(c.iso, R * 0.3f, RandF(200.f, 520.f) * c.scale, RandF(0.f, 2.f * kPi), r.hot, util::WithA(r.mid, 0.f), RandF(0.35f, 0.7f), RandF(7.f, 12.f),
				i % 4 == 0 ? fx::Sub::Embers : fx::Sub::None);
		for (int i = 0; i < 6 + static_cast<int>(c.tier) * 2; ++i)
			Chunk(c.iso, 20.f, DebrisOf(c.elem), RandF(4.f, 8.f), RandF(80.f, 260.f) * c.scale, DebrisSub(c.elem));
		for (int i = 0; i < 10 + static_cast<int>(c.tier) * 4; ++i)
			Ember(c.iso, RandF(10.f, 40.f), r.mid, R * 0.5f, RandF(0.6f, 1.3f), i % 3 == 0);
		flashlight::Add(c.iso, std::max(b.lightRadius, R * 3.f), LightOf(c.elem), 1.f, b.lightSec + 0.2f, 0.015f);
	}

	void Slam(const Ctx& c, float radius)
	{
		const auto& b = B(c.tier);
		const vfx::Ramp r = RampOf(c.elem);
		const float R = radius * c.scale;
		vfx::Spec crack = vfx::Make(vfx::Kind::Crack, c.iso, 0.f, R * 2.4f, std::max(1.5f, b.decalSec), r);
		vfx::Spawn(crack);
		vfx::Spec d = vfx::Make(vfx::Kind::Dust, c.iso, 0.f, R * 2.6f, 1.1f, vfx::DustRamp());
		d.h = R * 1.1f;
		d.depthBias = 0.6f;
		vfx::Spawn(d);
		vfx::Spawn(vfx::Make(vfx::Kind::GroundRing, c.iso, 0.f, R * 2.8f, 0.45f, r));
		if (b.shockwave) post::Shockwave(c.iso, R * 0.2f, R * 2.8f, 20.f, 1.f, 0.4f);
		for (int i = 0; i < 6 + 3 * static_cast<int>(c.tier); ++i)
			Chunk(c.iso, 6.f, DebrisOf(Element::Earth), RandF(4.f, 9.f), RandF(60.f, 220.f) * c.scale, fx::Sub::Dust);
		for (int i = 0; i < 4 + static_cast<int>(c.tier); ++i)
			Puff(c.iso, 6.f, Rgb(0xB8A888, 140), RandF(20.f, 34.f) * c.scale, RandF(0.7f, 1.2f), RandF(8.f, 20.f), R * 0.8f);
		flashlight::Add(c.iso, b.lightRadius * c.scale, LightOf(c.elem), 0.8f, b.lightSec);
	}

	void Shatter(const Ctx& c, float radius)
	{
		const auto& b = B(c.tier);
		const vfx::Ramp r = RampOf(Element::Frost);
		const float R = radius * c.scale;
		vfx::Spec i = vfx::Make(vfx::Kind::IceBurst, c.iso, c.z, R * 2.f, 0.7f, r);
		i.depthBias = 0.8f;
		vfx::Spawn(i);
		vfx::Spec f = vfx::Make(vfx::Kind::Frost, c.iso, 0.f, R * 2.6f, std::max(2.f, b.decalSec), r);
		vfx::Spawn(f);
		vfx::Spawn(vfx::Make(vfx::Kind::GroundRing, c.iso, 0.f, R * 2.4f, 0.4f, r));
		for (int k = 0; k < 8 + 4 * static_cast<int>(c.tier); ++k)
			Chunk(c.iso, c.z, Rgb(0xD8F2FF, 235), RandF(3.f, 7.f), RandF(60.f, 240.f) * c.scale, fx::Sub::Frost, Shape::Flake);
		for (int k = 0; k < 3; ++k)
			Puff(c.iso, RandF(6.f, c.z), Rgb(0xE8F6FF, 110), RandF(18.f, 30.f) * c.scale, RandF(0.8f, 1.3f), RandF(10.f, 25.f), R * 0.5f);
		flashlight::Add(c.iso, b.lightRadius * c.scale, LightOf(Element::Frost), 0.8f, b.lightSec);
	}

	void InkBurst(const Ctx& c, float radius)
	{
		const auto& b = B(c.tier);
		const float R = radius * c.scale;
		vfx::Spec i = vfx::Make(vfx::Kind::InkBurst, c.iso, c.z, R * 2.f, 0.6f, vfx::InkRamp());
		i.depthBias = 0.8f;
		vfx::Spawn(i);
		vfx::Spec st = vfx::Make(vfx::Kind::InkStain, c.iso, 0.f, R * 2.4f, std::max(2.f, b.decalSec), vfx::InkRamp());
		vfx::Spawn(st);
		// 먹물 속에서 달빛 초승 섬광
		vfx::Spec f = vfx::Make(vfx::Kind::Flare, c.iso, c.z, R * 1.4f, 0.22f, vfx::MoonRamp());
		f.depthBias = 0.9f;
		vfx::Spawn(f);
		for (int k = 0; k < 10 + 4 * static_cast<int>(c.tier); ++k)
			Chunk(c.iso, c.z, Rgb(0x101218, 230), RandF(3.f, 6.f), RandF(60.f, 200.f) * c.scale, fx::Sub::Ink, Shape::Flake);
		for (int k = 0; k < 3; ++k)
			Puff(c.iso, RandF(6.f, c.z), Rgb(0x14161C, 150), RandF(18.f, 30.f) * c.scale, RandF(0.6f, 1.0f), RandF(10.f, 25.f), R * 0.4f);
		flashlight::Add(c.iso, b.lightRadius * 0.8f * c.scale, LightOf(Element::Moon), 0.5f, b.lightSec);
	}

	void LightningStrike(const Ctx& c, float height)
	{
		const auto& b = B(c.tier);
		const vfx::Ramp r = RampOf(Element::Bolt);
		vfx::Spec l = vfx::Make(vfx::Kind::Lightning, c.iso, 0.f, 70.f * c.scale, 0.32f, r);
		l.h = height;
		l.depthBias = 0.7f;
		vfx::Spawn(l);
		vfx::Spec f = vfx::Make(vfx::Kind::Flare, c.iso, 12.f, 130.f * c.scale, 0.22f, r);
		f.depthBias = 0.9f;
		vfx::Spawn(f);
		vfx::Spec s = vfx::Make(vfx::Kind::Scorch, c.iso, 0.f, 90.f * c.scale, std::max(2.f, b.decalSec), vfx::SmokeRamp());
		s.alpha = 0.6f;
		vfx::Spawn(s);
		vfx::Spawn(vfx::Make(vfx::Kind::Crack, c.iso, 0.f, 110.f * c.scale, 1.2f, r));
		for (int k = 0; k < 14; ++k)
			Spark(c.iso, 10.f, RandF(160.f, 360.f), RandF(0.f, 2.f * kPi), r.hot, util::WithA(r.mid, 0.f), RandF(0.2f, 0.4f), RandF(5.f, 9.f));
		flashlight::Add(c.iso, b.lightRadius * 1.3f * c.scale, LightOf(Element::Bolt), 1.f, b.lightSec, 0.005f);
	}

	void Pillar(const Ctx& c, float height)
	{
		const vfx::Ramp r = RampOf(c.elem);
		vfx::Spec p = vfx::Make(vfx::Kind::Pillar, c.iso, 0.f, 90.f * c.scale, 0.9f, r);
		p.h = height;
		p.depthBias = 0.6f;
		vfx::Spawn(p);
		vfx::Spawn(vfx::Make(vfx::Kind::GroundRing, c.iso, 0.f, 160.f * c.scale, 0.5f, r));
		for (int k = 0; k < 16; ++k)
		{
			Particle& q = P();
			q.pos = { c.iso.x + RandF(-18.f, 18.f) * c.scale, c.iso.y + RandF(-8.f, 8.f) };
			q.z = RandF(0.f, height * 0.4f);
			q.vz = RandF(120.f, 320.f);
			q.drag = 0.8f;
			q.life = q.maxLife = RandF(0.5f, 0.9f);
			q.size0 = RandF(4.f, 7.f); q.size1 = 0.f;
			q.c0 = r.hot; q.c1 = util::WithA(r.mid, 0.f);
			q.shape = Shape::Soft; q.additive = true;
		}
		flashlight::Add(c.iso, B(c.tier).lightRadius * c.scale, LightOf(c.elem), 0.9f, 0.6f);
	}

	Feel FeelOf(fxgrade::Tier t, bool crit, bool combo, bool killed, int targets)
	{
		const auto& b = B(t);
		Feel f;
		f.hitstop = b.hitstop + (crit ? 0.015f : 0.f) + (combo ? 0.025f : 0.f) + (killed ? 0.01f : 0.f);
		f.hitstop = std::min(f.hitstop, b.hitstopMax + (combo ? 0.03f : 0.f));
		f.trauma = std::min(b.traumaMax, b.trauma * (crit ? 1.2f : 1.f) * (combo ? 1.3f : 1.f)) * fxgrade::MultiTargetShake(targets);
		f.trauma = std::min(f.trauma, b.traumaMax * 1.2f);
		f.kick = b.zoomPunch + (crit ? 0.01f : 0.f) + (combo ? 0.015f : 0.f);
		return f;
	}

	Element ElementOfSkill(uint16_t id)
	{
		switch (id)
		{
		case 1: case 2: case 3: case 5: case 6: case 20: case 21: case 22: case 24: return Element::Gold;
		case 4: case 19: return Element::Earth;
		case 7: case 10: case 27: case 30: return Element::Teal;
		case 8: case 25: case 29: return Element::Frost;
		case 9: case 12: case 26: return Element::Fire;
		case 11: case 28: return Element::Bolt;
		case 14: case 32: case 36: return Element::Ink;
		case 15: case 34: return Element::Poison;
		case 13: case 16: case 17: case 18: case 31: case 33: case 35: return Element::Moon;
		default: return Element::Steel;
		}
	}

	void HitSound(Element e, fxgrade::Tier t, float volume, float pan, int targets, bool skipBody)
	{
		const int layers = std::min(6, fxgrade::Get(t).soundLayers + fxgrade::MultiTargetLayers(targets));
		sfx::Layer l[6];
		int n = 0;
		// 1 몸통 · 2 원소 · 3 저음 · 4 쇳소리/파열 · 5 여운 · (6 많이 맞힘: 한 겹 더)
		l[n++] = { static_cast<int>(t) >= 3 ? sfx::Id::HitHeavy : sfx::Id::Hit, 0.f, 1.f, 1.f };
		sfx::Id elem = sfx::Id::MetalRing;
		switch (e)
		{
		case Element::Fire: case Element::Ember: elem = sfx::Id::FireRoar; break;
		case Element::Frost: elem = sfx::Id::IceCrack; break;
		case Element::Bolt: elem = sfx::Id::ThunderClap; break;
		case Element::Ink: elem = sfx::Id::InkSplash; break;
		case Element::Teal: case Element::Moon: elem = sfx::Id::Scribe; break;
		case Element::Gold: elem = sfx::Id::BellSmall; break;
		case Element::Earth: elem = sfx::Id::Burst; break;
		default: elem = sfx::Id::MetalRing; break;
		}
		if (layers >= 2) l[n++] = { elem, 0.f, 0.55f, 1.f };
		if (layers >= 3) l[n++] = { sfx::Id::BoomLow, 0.f, 0.5f + 0.1f * static_cast<int>(t), 1.f };
		if (layers >= 4) l[n++] = { static_cast<int>(t) >= 4 ? sfx::Id::Burst : sfx::Id::MetalRing, 0.01f, 0.45f, 1.1f };
		if (layers >= 5) l[n++] = { sfx::Id::TailVerb, 0.05f, 0.6f, 1.f };
		if (layers >= 6) l[n++] = { sfx::Id::Burst, 0.03f, 0.35f, 1.3f };
		// skipBody: 몸통(첫 겹)은 부른 쪽이 기술 고유 소리로 이미 냈다 — 나머지 겹만
		if (skipBody && n > 1) sfx::Layered(l + 1, n - 1, volume, pan);
		else if (!skipBody) sfx::Layered(l, n, volume, pan);
	}
}
