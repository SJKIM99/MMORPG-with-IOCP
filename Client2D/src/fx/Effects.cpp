#include "fx/Effects.h"
#include "render/Wind.h"
#include "fx/FxScale.h"
#include "fx/Particles.h"
#include "render/Draw.h"
#include "render/Assets.h"
#include "render/FxArt.h"
#include "ui/Font.h"
#include "core/Util.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace fx
{
	namespace { constexpr float kClickLife = 0.3f; }   // 클릭 표시 수명(초)

	using util::Rgb;
	using util::RandF;
	using util::WithA;

	Effects& Fx()
	{
		static Effects instance;
		return instance;
	}

	namespace
	{
		template <typename T>
		void Age(std::vector<T>& v, float dt)
		{
			for (auto& e : v) e.t += dt;
			v.erase(std::remove_if(v.begin(), v.end(), [](const T& e) { return e.t >= e.life; }), v.end());
		}

		Particle& Spark(Vector2 iso, float z, float speed, float angle, Color c0, Color c1, float life, float size)
		{
			Particle& p = Particles().Emit();
			p.pos = iso;
			p.z = z;
			p.vel = { std::cos(angle) * speed, 0.f };
			p.vz = -std::sin(angle) * speed;
			p.gravity = 380.f;
			p.drag = 3.5f;
			p.life = p.maxLife = life;
			p.size0 = size; p.size1 = size * 0.3f;
			p.c0 = c0; p.c1 = c1;
			p.shape = Shape::Spark;
			p.additive = true;
			p.stretch = true;
			return p;
		}

		Particle& GlowP(Vector2 iso, float z, float size0, float size1, Color c, float life)
		{
			Particle& p = Particles().Emit();
			p.pos = iso; p.z = z;
			p.life = p.maxLife = life;
			p.size0 = size0; p.size1 = size1;
			p.c0 = c; p.c1 = WithA(c, 0.f);
			p.shape = Shape::Glow;
			p.additive = true;
			return p;
		}

		Particle& Chunk(Vector2 iso, float z, Color c, float size, float speed)
		{
			Particle& p = Particles().Emit();
			const float a = RandF(0.f, util::kPi * 2.f);
			p.pos = iso; p.z = z;
			p.vel = { std::cos(a) * speed, std::sin(a) * speed * 0.5f };
			p.vz = RandF(90.f, 230.f);
			p.gravity = 620.f;
			p.drag = 0.6f;
			p.bounce = true;
			p.life = p.maxLife = RandF(0.8f, 1.4f);
			p.size0 = size; p.size1 = size * 0.7f;
			p.c0 = c; p.c1 = c;
			p.rot = RandF(0.f, 6.28f); p.vrot = RandF(-12.f, 12.f);
			p.shape = Shape::Chunk;
			return p;
		}

		Particle& Smoke(Vector2 iso, float z, Color c, float size0, float size1, float life, Vector2 vel, float vz)
		{
			Particle& p = Particles().Emit();
			p.pos = iso; p.z = z;
			p.vel = vel; p.vz = vz;
			p.drag = 1.8f;
			p.life = p.maxLife = life;
			p.size0 = size0; p.size1 = size1;
			p.c0 = c; p.c1 = WithA(c, 0.f);
			p.rot = RandF(0.f, 6.28f); p.vrot = RandF(-1.f, 1.f);
			p.shape = Shape::Smoke;
			return p;
		}

		// ── 그린 효과(fxart) 한 장 — 없으면 아무 일도 없다(절차적 효과가 이미 있다) ──
		Particle* Tx(fxart::Id id, Vector2 iso, float z, float s0, float s1, float life, Color c0, Color c1, bool additive,
			float rot = 0.f, float vrot = 0.f, float pop = 0.f)
		{
			Particle* p = EmitTex(id, iso, z, s0, s1, life, c0, c1, additive);
			if (p) { p->rot = rot; p->vrot = vrot; p->pop = pop; }
			return p;
		}
		// 바닥에 눕힌 효과(인장 · 충격파): 캐릭터 발밑에
		Particle* TxGround(fxart::Id id, Vector2 iso, float s0, float s1, float life, Color c0, Color c1, bool additive, float rot = 0.f, float vrot = 0.f)
		{
			Particle* p = Tx(id, iso, 0.f, s0, s1, life, c0, c1, additive, rot, vrot);
			if (p)
			{
				p->ground = true;
				// 위에서 본 원(각인 · 후광)만 눕힌다 — 불 고리 · 충격파는 그림이 이미 비스듬하다
				p->squash = (id == fxart::FireRing || id == fxart::HitRing || id == fxart::Halo) ? 1.f : 0.5f;
			}
			return p;
		}

		// 피해 숫자는 Barlow Condensed ExtraBold(좁고 단단한 숫자) — 수치가 한눈에 읽히게.
		void DrawNumber(const std::string& text, Vector2 center, float size, Color fill, Color outline, float alpha, ui::Face face = ui::Face::NumberBold)
		{
			ui::TextStyle st;
			st.size = size;
			st.color = util::MulA(fill, alpha);
			st.outline = std::max(1.4f, size * 0.07f);
			st.outlineColor = util::MulA(outline, alpha);
			st.shadow = std::max(1.f, size * 0.06f);
			st.shadowColor = Color{ 0, 0, 0, static_cast<unsigned char>(110 * alpha) };
			st.face = face;
			ui::Fonts().DrawCentered(text, center, st);
		}
	}

	void Effects::Clear()
	{
		m_damage.clear(); m_floats.clear(); m_rings.clear(); m_beams.clear(); m_teles.clear(); m_slashes.clear(); m_clicks.clear();
		m_flash = 0.f; m_hitStop = 0.f;
	}

	void Effects::Update(float dt)
	{
		m_time += dt;
		m_hitStop = std::max(0.f, m_hitStop - dt);
		const float sdt = dt * TimeScale();
		Age(m_damage, dt);
		for (auto& d : m_damage)
		{
			d.punch = std::max(0.f, d.punch - dt * 6.f);
			d.stackShown = util::Damp(d.stackShown, d.stack, 18.f, dt);
		}
		Age(m_floats, dt);
		Age(m_rings, sdt);
		Age(m_beams, sdt);
		Age(m_teles, dt);
		Age(m_slashes, sdt);
		for (auto& c : m_clicks) c.t += dt;
		m_clicks.erase(std::remove_if(m_clicks.begin(), m_clicks.end(), [](const ClickFx& c) { return c.t > kClickLife; }), m_clicks.end());
		if (m_flash > 0.f) m_flash = std::max(0.f, m_flash - dt);
		Particles().Update(sdt);
	}

	// ── 기본 요소 ─────────────────────────────────────────────────────────────
	void Effects::Damage(Vector2 iso, float height, int amount, DamageStyle style, uint64_t stackKey, uint64_t victimKey, uint8_t tags)
	{
		const bool critLike = style == DamageStyle::Crit || style == DamageStyle::TakenCrit;
		if (stackKey != 0)
		{
			for (auto& d : m_damage)
			{
				if (d.key != stackKey || d.t > 0.55f)
					continue;
				// 같은 대상·같은 기술: 합계로 키운다. 치명타가 섞이면 치명타 모양으로 올린다.
				d.total += amount;
				d.hits += 1;
				if (critLike && d.style != style) d.style = style;
				d.text = std::to_string(d.total);
				d.t = std::min(d.t, 0.06f);
				d.life = std::max(d.life, critLike ? 1.25f : 1.05f);
				d.punch = 1.f;
				d.iso = iso;
				return;
			}
		}
		std::string text = std::to_string(amount);
		if (style == DamageStyle::Heal) text = "+" + text;
		if (style == DamageStyle::Exp) text = "+" + text + " EXP";
		const float life = critLike ? 1.25f : (style == DamageStyle::DotBurn || style == DamageStyle::DotPoison ? 0.8f : 0.95f);
		DamageNum n{ iso, height, text, style, 0.f, life, RandF(-18.f, 18.f) };
		n.key = stackKey;
		n.total = amount;
		n.tags = tags;
		if (victimKey != 0 && stackKey == 0)
		{
			// 같은 대상의 최근 숫자는 한 줄씩 위로 — 새 숫자는 좌우를 번갈아(겹침 금지)
			int recent = 0;
			for (auto& d : m_damage)
				if (d.victim == victimKey && d.t < 0.75f) { d.stack += 1.f; ++recent; }
			const float side = (recent % 2 == 0) ? -1.f : 1.f;
			n.vx = side * RandF(16.f, 28.f);
			n.victim = victimKey;
			if (recent > 0) n.life += 0.1f;
		}
		m_damage.push_back(n);
	}

	void Effects::TotalDamage(Vector2 iso, float height, int total)
	{
		char buf[32];
		// 세 자리마다 쉼표(큰 숫자를 한눈에)
		std::string digits = std::to_string(total), out;
		const int n = static_cast<int>(digits.size());
		for (int i = 0; i < n; ++i)
		{
			out += digits[static_cast<size_t>(i)];
			if ((n - 1 - i) % 3 == 0 && i != n - 1) out += ',';
		}
		std::snprintf(buf, sizeof(buf), "%s", out.c_str());
		DamageNum d{ iso, height, buf, DamageStyle::Total, 0.f, 2.1f, 0.f };
		d.total = total;
		d.punch = 1.f;
		m_damage.push_back(d);
	}

	void Effects::FloatText(Vector2 iso, float height, const std::string& text, Color color, float size, float life, ui::Face face)
	{
		m_floats.push_back({ iso, height, text, color, size, 0.f, life, face });
	}

	void Effects::Ring(Vector2 iso, float r0, float r1, float life, Color color, float thickness)
	{
		m_rings.push_back({ iso, r0, r1, 0.f, life, thickness, color });
	}

	void Effects::Beam(Vector2 iso, float width, float height, float life, Color color)
	{
		m_beams.push_back({ iso, width, height, 0.f, life, color });
	}

	void Effects::Telegraph(Vector2 iso, int radiusTiles, float duration, Color color)
	{
		m_teles.push_back({ iso, radiusTiles, 0.f, duration, color });
	}

	void Effects::Slash(Vector2 iso, float height, float dirSign, bool back, Color color, float scale)
	{
		m_slashes.push_back({ iso, height, dirSign, back, 0.f, 0.22f, scale, color });
		// 그린 칼 궤적: 휘두르는 쪽으로 튀어나왔다가 늘어나며 사라진다
		const Vector2 at{ iso.x + dirSign * 16.f * scale, iso.y + 2.f };
		const Color c0 = util::Mix(Rgb(0xFFFFFF, 245), color, 0.35f);
		if (Particle* s = Tx(scale >= 1.15f ? fxart::SlashHeavy : fxart::SlashArc, at, height, 64.f * scale, 112.f * scale, 0.2f,
			c0, WithA(color, 0.f), true, dirSign * (back ? -0.45f : 0.25f), dirSign * 1.6f, 0.05f))
			s->flipX = dirSign > 0.f;   // 그림의 초승달은 왼쪽으로 볼록 — 오른쪽을 볼 때 뒤집어 앞으로 볼록하게
	}

	void Effects::ClawMarks(Vector2 iso, float height, float dirSign, Color color)
	{
		// 짐승 · 용 · 벌레의 할퀴기: 세 줄 자국이 번쩍
		if (Particle* s = Tx(fxart::Claw, iso, height * 0.55f, 54.f, 86.f, 0.24f, util::Mix(Rgb(0xFFF0E0, 250), color, 0.3f), WithA(color, 0.f), true, dirSign * 0.35f, 0.f, 0.05f))
			s->flipX = dirSign < 0.f;
	}

	void Effects::Flash(Color color, float strength, float duration)
	{
		strength *= fxscale::Flash();
		if (strength <= 0.005f) return;
		m_flashColor = color;
		m_flashColor.a = static_cast<unsigned char>(util::Clamp01(strength) * 255.f);
		m_flash = duration;
		m_flashLife = duration;
	}

	void Effects::HitStop(float seconds) { m_hitStop = std::max(m_hitStop, seconds); }

	void Effects::ClickMarker(Vector2 iso, Color color) { m_clicks.push_back({ iso, 0.f, color }); }

	// ── 연출 묶음 ─────────────────────────────────────────────────────────────
	void Effects::HitSpark(Vector2 iso, float height, Color color, bool crit, float dirSign)
	{
		const float z = height * 0.55f;
		// 그린 타격 섬광: 맞는 순간 크게 튀어나온다 — 히트스톱 동안 이 한 장이 멈춰 보이는 것이 "손맛"의 핵심
		if (crit)
		{
			Tx(fxart::HitCrit, iso, z, 80.f, 170.f, 0.28f, Rgb(0xFFFFFF, 255), Rgb(0xFFB040, 0), true, RandF(0.f, 6.28f), RandF(-2.f, 2.f), 0.06f);
			Tx(fxart::HitRing, iso, z, 20.f, 100.f, 0.34f, Rgb(0xFFE6B0, 230), Rgb(0xFF9030, 0), true);
		}
		else
			Tx(fxart::HitStar, iso, z, 48.f, 96.f, 0.17f, util::Mix(Rgb(0xFFFFFF, 245), Rgb(0xFFE0A0), 0.3f), Rgb(0xFFC060, 0), true, RandF(0.f, 6.28f), RandF(-3.f, 3.f), 0.04f);
		const int n = crit ? 18 : 9;
		for (int i = 0; i < n; ++i)
		{
			float a = RandF(-1.1f, 1.1f) + (dirSign > 0 ? 0.f : util::kPi);
			if (RandF() < 0.3f) a = RandF(0.f, 6.28f);
			Spark(iso, z, RandF(120.f, crit ? 420.f : 280.f), a, Rgb(0xFFF6D0), crit ? Rgb(0xFF8A20) : Rgb(0xFFC060), RandF(0.18f, 0.38f), RandF(5.f, 9.f));
		}
		GlowP(iso, z, crit ? 34.f : 20.f, crit ? 60.f : 36.f, crit ? Rgb(0xFFD070, 230) : Rgb(0xFFF0C0, 200), 0.16f);
		for (int i = 0; i < (crit ? 7 : 3); ++i)
			Chunk(iso, z, util::Shade(color, RandF(0.7f, 1.1f)), RandF(3.f, 5.5f), RandF(30.f, 110.f));
		if (crit)
		{
			Particle& s = Particles().Emit();
			s.pos = iso; s.z = z;
			s.life = s.maxLife = 0.28f;
			s.size0 = 30.f; s.size1 = 110.f;
			s.c0 = Rgb(0xFFFFFF, 255); s.c1 = Rgb(0xFFC040, 0);
			s.rot = RandF(0.f, 1.f);
			s.shape = Shape::Star;
			s.additive = true;
			Ring(iso, 8.f, 70.f, 0.3f, Rgb(0xFFD070, 220), 5.f);
		}
	}

	void Effects::DeathBurst(Vector2 iso, float height, Color c1, Color c2, float scale, bool boss)
	{
		const float z = height * 0.45f;
		// 그린 연기 뭉치 + 바닥 충격파
		for (int i = 0; i < (boss ? 7 : 3); ++i)
		{
			const float a = RandF(0.f, 6.28f), sp = RandF(15.f, 45.f) * scale;
			if (Particle* s = Tx(fxart::Smoke, Vector2{ iso.x + std::cos(a) * 10.f, iso.y + std::sin(a) * 5.f }, z * RandF(0.4f, 1.1f),
				44.f * scale, 120.f * scale, RandF(0.8f, 1.2f), util::Mix(Rgb(0xD8D0C8, 190), c1, 0.25f), Rgb(0x8A8278, 0), false, RandF(0.f, 6.28f), RandF(-0.8f, 0.8f)))
			{
				s->vel = Vector2{ std::cos(a) * sp, std::sin(a) * sp * 0.5f };
				s->vz = RandF(10.f, 40.f);
				s->drag = 1.4f;
			}
		}
		TxGround(fxart::HitRing, iso, 20.f * scale, 95.f * scale, 0.4f, WithA(util::Shade(c1, 1.5f), 0.8f), WithA(c1, 0.f), true);
		const int puffs = boss ? 28 : 12;
		for (int i = 0; i < puffs; ++i)
		{
			const float a = RandF(0.f, 6.28f), sp = RandF(20.f, 90.f) * scale;
			Smoke(iso, z * RandF(0.3f, 1.2f), util::Mix(util::Mix(c1, c2, RandF()), Rgb(0xD8D8D8), 0.45f), 18.f * scale, RandF(40.f, 70.f) * scale, RandF(0.5f, 0.9f),
				Vector2{ std::cos(a) * sp, std::sin(a) * sp * 0.5f }, RandF(10.f, 60.f));
		}
		for (int i = 0; i < (boss ? 26 : 10); ++i)
			Chunk(iso, z, util::Shade(i % 2 ? c1 : c2, RandF(0.8f, 1.2f)), RandF(3.f, 7.f) * std::min(scale, 1.6f), RandF(40.f, 160.f) * scale);
		GlowP(iso, z, 40.f * scale, 90.f * scale, Rgb(0xFFF4E0, 220), 0.22f);
		// 떠오르는 넋
		Particle& soul = GlowP(iso, z, 14.f, 4.f, WithA(util::Shade(c1, 1.6f), 0.8f), 1.3f);
		soul.vz = 55.f;
		soul.vel = { RandF(-8.f, 8.f), 0.f };
		Ring(iso, 10.f, 60.f * scale, 0.35f, WithA(util::Shade(c1, 1.4f), 0.7f), 4.f);
		if (boss)
		{
			for (int i = 0; i < 40; ++i)
				Spark(iso, z, RandF(150.f, 520.f), RandF(0.f, 6.28f), Rgb(0xFFF2C0), Rgb(0xFF6020), RandF(0.4f, 0.9f), RandF(6.f, 12.f));
			Beam(iso, 90.f, 520.f, 1.2f, Rgb(0xFFD070, 200));
			Ring(iso, 20.f, 260.f, 0.8f, Rgb(0xFFD070, 220), 8.f);
			Flash(Rgb(0xFFF0D0), 0.5f, 0.35f);   // 화면 번쩍임은 스크린 혼합(DrawScreen) — 밝은 땅에서도 쓰러지는 보스가 보인다
		}
	}

	void Effects::LevelUp(Vector2 iso)
	{
		Tx(fxart::PillarGold, iso, 150.f, 260.f, 330.f, 1.4f, Rgb(0xFFF0C0, 235), Rgb(0xFFC040, 0), true, 0.f, 0.f, 0.12f);
		TxGround(fxart::Halo, iso, 24.f, 90.f, 0.9f, Rgb(0xFFE8A0, 230), Rgb(0xFFB030, 0), true);
		for (int i = 0; i < 6; ++i)
			if (Particle* g = Tx(fxart::Glint, Vector2{ iso.x + RandF(-40.f, 40.f), iso.y + RandF(-10.f, 10.f) }, RandF(20.f, 140.f), 10.f, 34.f, RandF(0.5f, 0.9f), Rgb(0xFFFFFF, 255), Rgb(0xFFD070, 0), true, RandF(0.f, 1.f), RandF(-3.f, 3.f)))
				g->vz = RandF(20.f, 60.f);
		Beam(iso, 70.f, 420.f, 1.4f, Rgb(0xFFD66A, 230));
		Ring(iso, 10.f, 110.f, 0.7f, Rgb(0xFFE08A, 240), 6.f);
		Ring(iso, 10.f, 170.f, 1.1f, Rgb(0xFFFFFF, 160), 3.f);
		for (int i = 0; i < 46; ++i)
		{
			Particle& p = Particles().Emit();
			const float a = RandF(0.f, 6.28f), r = RandF(4.f, 26.f);
			p.pos = { iso.x + std::cos(a) * r, iso.y + std::sin(a) * r * 0.5f };
			p.z = RandF(0.f, 30.f);
			p.vel = { std::cos(a) * RandF(10.f, 40.f), std::sin(a) * RandF(5.f, 20.f) };
			p.vz = RandF(70.f, 230.f);
			p.drag = 1.2f;
			p.life = p.maxLife = RandF(0.8f, 1.6f);
			p.size0 = RandF(8.f, 16.f); p.size1 = 2.f;
			p.c0 = i % 3 ? Rgb(0xFFE070) : Rgb(0xFFFFFF);
			p.c1 = Rgb(0xFF9A20, 0);
			p.rot = RandF(0.f, 6.f); p.vrot = RandF(-4.f, 4.f);
			p.shape = Shape::Star;
			p.additive = true;
		}
		Flash(Rgb(0xFFF2C0), 0.35f, 0.4f);
	}

	void Effects::Heal(Vector2 iso, float height)
	{
		for (int i = 0; i < 16; ++i)
		{
			Particle& p = Particles().Emit();
			const float a = RandF(0.f, 6.28f), r = RandF(4.f, 18.f);
			p.pos = { iso.x + std::cos(a) * r, iso.y + std::sin(a) * r * 0.5f };
			p.z = RandF(5.f, height * 0.8f);
			p.vz = RandF(30.f, 80.f);
			p.life = p.maxLife = RandF(0.6f, 1.1f);
			p.size0 = RandF(6.f, 11.f); p.size1 = 1.f;
			p.c0 = Rgb(0x8AFF9A); p.c1 = Rgb(0x2AE06A, 0);
			p.shape = i % 2 ? Shape::Star : Shape::Glow;
			p.additive = true;
		}
		GlowP(iso, height * 0.5f, 30.f, 50.f, Rgb(0x6AFF8A, 120), 0.4f);
		if (Particle* m = Tx(fxart::HealMotes, iso, height * 0.55f, 60.f, 96.f, 1.0f, Rgb(0xC8FFD0, 230), Rgb(0x6AFF8A, 0), true))
			m->vz = 28.f;
	}

	void Effects::TeleportOut(Vector2 iso)
	{
		Tx(fxart::PillarTeal, iso, 120.f, 220.f, 260.f, 0.7f, Rgb(0xE0FAFF, 235), Rgb(0x4AB8FF, 0), true, 0.f, 0.f, 0.08f);
		TxGround(fxart::RuneCircle, iso, 120.f, 60.f, 0.7f, Rgb(0xBFF0FF, 235), Rgb(0x4AB8FF, 0), true, 0.f, 3.f);
		Beam(iso, 46.f, 360.f, 0.6f, Rgb(0x9AD8FF, 230));
		Ring(iso, 40.f, 6.f, 0.4f, Rgb(0xBFE8FF, 220), 4.f);
		for (int i = 0; i < 30; ++i)
		{
			Particle& p = Particles().Emit();
			const float a = RandF(0.f, 6.28f), r = RandF(10.f, 40.f);
			p.pos = { iso.x + std::cos(a) * r, iso.y + std::sin(a) * r * 0.5f };
			p.z = RandF(0.f, 50.f);
			p.vel = { -std::cos(a) * r * 2.f, -std::sin(a) * r };
			p.vz = RandF(120.f, 320.f);
			p.life = p.maxLife = RandF(0.4f, 0.8f);
			p.size0 = RandF(6.f, 12.f); p.size1 = 1.f;
			p.c0 = Rgb(0xE0F6FF); p.c1 = Rgb(0x4A9AFF, 0);
			p.shape = Shape::Star;
			p.additive = true;
		}
	}

	void Effects::TeleportIn(Vector2 iso)
	{
		Tx(fxart::PillarTeal, iso, 120.f, 260.f, 220.f, 0.8f, Rgb(0xE0FAFF, 235), Rgb(0x4AB8FF, 0), true, 0.f, 0.f, 0.1f);
		TxGround(fxart::RuneCircle, iso, 50.f, 130.f, 0.9f, Rgb(0xBFF0FF, 235), Rgb(0x4AB8FF, 0), true, 0.f, -2.f);
		Beam(iso, 52.f, 380.f, 0.7f, Rgb(0xBFE8FF, 230));
		Ring(iso, 6.f, 90.f, 0.5f, Rgb(0xBFE8FF, 220), 5.f);
		for (int i = 0; i < 26; ++i)
		{
			Particle& p = Particles().Emit();
			const float a = RandF(0.f, 6.28f);
			p.pos = iso;
			p.z = RandF(10.f, 40.f);
			p.vel = { std::cos(a) * RandF(60.f, 150.f), std::sin(a) * RandF(30.f, 75.f) };
			p.vz = RandF(-20.f, 60.f);
			p.drag = 3.f;
			p.life = p.maxLife = RandF(0.4f, 0.8f);
			p.size0 = RandF(6.f, 11.f); p.size1 = 1.f;
			p.c0 = Rgb(0xFFFFFF); p.c1 = Rgb(0x6AB8FF, 0);
			p.shape = Shape::Glow;
			p.additive = true;
		}
		Flash(Rgb(0xD8F0FF), 0.25f, 0.25f);
	}

	void Effects::Haste(Vector2 iso)
	{
		Ring(iso, 10.f, 80.f, 0.5f, Rgb(0x7AFF9A, 220), 4.f);
		for (int i = 0; i < 24; ++i)
		{
			const float a = RandF(0.f, 6.28f);
			Particle& p = Particles().Emit();
			p.pos = { iso.x + std::cos(a) * 20.f, iso.y + std::sin(a) * 10.f };
			p.z = RandF(0.f, 40.f);
			p.vel = { -std::sin(a) * 120.f, std::cos(a) * 60.f };
			p.vz = RandF(40.f, 110.f);
			p.drag = 2.f;
			p.life = p.maxLife = RandF(0.4f, 0.7f);
			p.size0 = RandF(7.f, 11.f); p.size1 = 2.f;
			p.c0 = Rgb(0xC0FFD0); p.c1 = Rgb(0x2AE06A, 0);
			p.shape = Shape::Spark;
			p.stretch = true;
			p.additive = true;
		}
	}

	void Effects::HasteTrail(Vector2 iso)
	{
		Particle& p = Particles().Emit();
		p.pos = { iso.x + RandF(-8.f, 8.f), iso.y + RandF(-3.f, 3.f) };
		p.z = RandF(8.f, 40.f);
		p.life = p.maxLife = 0.35f;
		p.size0 = RandF(8.f, 12.f); p.size1 = 1.f;
		p.c0 = Rgb(0x9AFFB0, 180); p.c1 = Rgb(0x2AE06A, 0);
		p.shape = Shape::Glow;
		p.additive = true;
	}

	void Effects::EnchantResult(Vector2 iso, bool success)
	{
		if (success)
		{
			Tx(fxart::PillarGold, iso, 150.f, 250.f, 320.f, 1.2f, Rgb(0xFFFFFF, 240), Rgb(0x9AD8FF, 0), true, 0.f, 0.f, 0.1f);
			for (int i = 0; i < 5; ++i)
				Tx(fxart::Glint, Vector2{ iso.x + RandF(-30.f, 30.f), iso.y }, RandF(30.f, 120.f), 8.f, 40.f, RandF(0.4f, 0.8f), Rgb(0xFFFFFF, 255), Rgb(0x9AD8FF, 0), true, RandF(0.f, 1.f), RandF(-4.f, 4.f));
			Beam(iso, 60.f, 400.f, 1.0f, Rgb(0x9AD8FF, 230));
			Ring(iso, 10.f, 130.f, 0.6f, Rgb(0xBFE8FF, 240), 6.f);
			for (int i = 0; i < 36; ++i)
				Spark(iso, 40.f, RandF(120.f, 380.f), RandF(0.2f, 2.94f), Rgb(0xFFFFFF), Rgb(0x4A9AFF), RandF(0.4f, 0.9f), RandF(6.f, 10.f));
		}
		else
		{
			for (int i = 0; i < 3; ++i)
				if (Particle* s = Tx(fxart::ShadowSmoke, iso, RandF(20.f, 60.f), 50.f, 130.f, RandF(0.8f, 1.2f), Rgb(0xC8B8A8, 200), Rgb(0x2A2226, 0), false, RandF(0.f, 6.f), RandF(-1.f, 1.f)))
					s->vz = RandF(20.f, 50.f);
			for (int i = 0; i < 14; ++i)
				Smoke(iso, RandF(20.f, 50.f), Rgb(0x4A4040), 16.f, 50.f, RandF(0.7f, 1.2f), Vector2{ RandF(-40.f, 40.f), RandF(-20.f, 20.f) }, RandF(20.f, 60.f));
			for (int i = 0; i < 16; ++i)
				Chunk(iso, 40.f, Rgb(0xB8C0CC), RandF(3.f, 6.f), RandF(60.f, 160.f));
			Ring(iso, 10.f, 90.f, 0.5f, Rgb(0xC9573F, 200), 4.f);   // 적대의 불씨색(보라 · 네온 금지)
		}
	}

	void Effects::SkillCross(Vector2 iso)
	{
		// 동서남북 인접 칸(맨해튼 거리 1 — 서버 판정과 같은 칸)으로 뻗는 네 줄기 참격
		const Vector2 dirs[4] = { gfx::IsoPx(1, 0), gfx::IsoPx(-1, 0), gfx::IsoPx(0, 1), gfx::IsoPx(0, -1) };
		for (const Vector2& d : dirs)
		{
			for (int i = 0; i < 7; ++i)
			{
				const float t = i / 6.f;
				Particle& p = Particles().Emit();
				p.pos = { iso.x + d.x * t * 1.2f, iso.y + d.y * t * 1.2f };
				p.z = 24.f;
				p.vel = { d.x * 2.5f, d.y * 2.5f };
				p.life = p.maxLife = 0.3f + t * 0.1f;
				p.size0 = 22.f - t * 8.f; p.size1 = 2.f;
				p.c0 = Rgb(0xE0F4FF); p.c1 = Rgb(0x4AB8FF, 0);
				p.shape = Shape::Glow;
				p.additive = true;
			}
			Slash(Vector2{ iso.x + d.x, iso.y + d.y }, 28.f, d.x >= 0 ? 1.f : -1.f, d.y < 0, Rgb(0xBFE8FF, 255), 1.1f);
		}
		GlowP(iso, 24.f, 40.f, 110.f, Rgb(0xBFE8FF, 220), 0.25f);
		Ring(iso, 8.f, 80.f, 0.3f, Rgb(0xBFE8FF, 220), 5.f);
	}

	void Effects::BossSlamImpact(Vector2 iso, int radiusTiles)
	{
		const float r = (radiusTiles + 0.5f) * 45.f;
		TxGround(fxart::HitRing, iso, 20.f, r * 1.6f, 0.55f, Rgb(0xFFD8A0, 240), Rgb(0xFF8030, 0), true);
		for (int i = 0; i < 4; ++i)
		{
			const float a = RandF(0.f, 6.28f);
			if (Particle* d = Tx(fxart::Dirt, Vector2{ iso.x + std::cos(a) * r * 0.5f, iso.y + std::sin(a) * r * 0.25f }, 6.f, 60.f, 150.f, RandF(0.6f, 0.9f), Rgb(0xE0D0C0, 230), Rgb(0x8A7A6A, 0), false, RandF(-0.4f, 0.4f)))
				d->vz = RandF(30.f, 80.f);
		}
		Ring(iso, 10.f, r * 1.6f, 0.5f, Rgb(0xFFB060, 240), 10.f);
		Ring(iso, 10.f, r * 2.2f, 0.8f, Rgb(0xFFFFFF, 150), 5.f);
		for (int i = 0; i < 36; ++i)
		{
			const float a = RandF(0.f, 6.28f), d = RandF(0.f, r);
			const Vector2 p{ iso.x + std::cos(a) * d * 1.4f, iso.y + std::sin(a) * d * 0.7f };
			Smoke(p, 5.f, Rgb(0x9A8A7A), 20.f, RandF(50.f, 90.f), RandF(0.6f, 1.2f), Vector2{ std::cos(a) * 80.f, std::sin(a) * 40.f }, RandF(20.f, 80.f));
		}
		for (int i = 0; i < 24; ++i)
			Chunk(iso, 10.f, Rgb(0x7A6A5A), RandF(4.f, 8.f), RandF(80.f, 260.f));
		GlowP(iso, 20.f, 80.f, 220.f, Rgb(0xFFC080, 200), 0.3f);
		Flash(Rgb(0xFFE0C0), 0.45f, 0.25f);
	}

	void Effects::BossSpawn(Vector2 iso)
	{
		for (int i = 0; i < 30; ++i)
			Smoke(iso, RandF(0.f, 40.f), Rgb(0x2A1A2A), 30.f, 90.f, RandF(0.8f, 1.6f), Vector2{ RandF(-60.f, 60.f), RandF(-30.f, 30.f) }, RandF(30.f, 110.f));
		Ring(iso, 20.f, 200.f, 1.0f, Rgb(0xFF3A3A, 220), 8.f);
		Beam(iso, 110.f, 500.f, 1.3f, Rgb(0xFF4A3A, 180));
	}

	void Effects::PlayerKill(Vector2 iso)
	{
		for (int i = 0; i < 20; ++i)
			Spark(iso, 40.f, RandF(100.f, 300.f), RandF(0.f, 6.28f), Rgb(0xFF8A8A), Rgb(0x8A0A0A), RandF(0.4f, 0.8f), RandF(6.f, 10.f));
		Ring(iso, 10.f, 110.f, 0.6f, Rgb(0xFF2A2A, 220), 6.f);
		Beam(iso, 50.f, 300.f, 0.8f, Rgb(0xFF2A2A, 180));
	}

	void Effects::Restart(Vector2 iso)
	{
		Beam(iso, 64.f, 460.f, 1.2f, Rgb(0xFFF2C8, 230));
		Ring(iso, 8.f, 120.f, 0.8f, Rgb(0xFFF2C8, 220), 5.f);
		Heal(iso, 60.f);
	}

	void Effects::Dust(Vector2 iso, float amount)
	{
		const int n = std::max(1, static_cast<int>(amount * 3.f));
		// 그린 먼지(발걸음 · 착지): 한두 장이면 충분하다
		if (amount >= 1.f || RandF() < 0.35f)
			if (Particle* d = Tx(fxart::Dust, Vector2{ iso.x + RandF(-5.f, 5.f), iso.y + 2.f }, 3.f, 14.f * std::min(2.5f, 0.6f + amount * 0.4f), 34.f * std::min(2.5f, 0.6f + amount * 0.4f),
				RandF(0.45f, 0.7f), Rgb(0xD8C8A8, 150), Rgb(0xB8A888, 0), false, RandF(0.f, 6.f), RandF(-0.5f, 0.5f)))
			{
				d->vel = Vector2{ RandF(-14.f, 14.f), RandF(-4.f, 4.f) };
				d->vz = RandF(6.f, 16.f);
				d->drag = 2.f;
			}
		for (int i = 0; i < n; ++i)
			Smoke(Vector2{ iso.x + RandF(-6.f, 6.f), iso.y + RandF(-2.f, 2.f) }, 2.f, Rgb(0xC8B89A, 120), 6.f, 18.f, RandF(0.35f, 0.6f),
				Vector2{ RandF(-20.f, 20.f), RandF(-6.f, 6.f) }, RandF(8.f, 22.f));
	}

	void Effects::Footfall(Vector2 iso, int ground, float amount)
	{
		// 달리는 동안 발마다 — 적고 짧게(화면을 흐리지 않게). 눈 · 모래는 조금 더, 돌 · 나무는 거의 없이.
		struct G { Color c; float n; float size; float up; };
		static constexpr G kG[5] = {
			{ Color{ 150, 170, 110, 110 }, 0.6f, 5.f, 10.f },    // 풀: 풀잎 먼지 약간
			{ Color{ 190, 184, 170, 90 },  0.35f, 5.f, 6.f },    // 돌: 옅은 흙먼지
			{ Color{ 170, 140, 100, 70 },  0.2f, 4.f, 5.f },     // 나무
			{ Color{ 236, 242, 250, 150 }, 1.3f, 7.f, 14.f },    // 눈: 흰 가루
			{ Color{ 214, 188, 140, 130 }, 1.1f, 7.f, 12.f },    // 모래
		};
		const G& g = kG[ground >= 0 && ground < 5 ? ground : 0];
		const float want = g.n * amount;
		int n = static_cast<int>(want);
		if (RandF() < want - n) ++n;
		for (int i = 0; i < n; ++i)
			Smoke(Vector2{ iso.x + RandF(-5.f, 5.f), iso.y + RandF(-1.5f, 1.5f) }, 1.f, g.c, g.size * 0.6f, g.size * 2.4f, RandF(0.3f, 0.5f),
				Vector2{ RandF(-16.f, 16.f), RandF(-5.f, 5.f) }, RandF(g.up * 0.5f, g.up));
	}

	void Effects::CoinBurst(Vector2 iso, int count)
	{
		for (int i = 0; i < count; ++i)
		{
			Particle& p = Particles().Emit();
			const float a = RandF(0.f, 6.28f);
			p.pos = iso; p.z = 10.f;
			p.vel = { std::cos(a) * RandF(20.f, 70.f), std::sin(a) * RandF(10.f, 35.f) };
			p.vz = RandF(160.f, 280.f);
			p.gravity = 700.f;
			p.bounce = true;
			p.life = p.maxLife = RandF(0.7f, 1.0f);
			p.size0 = 9.f; p.size1 = 8.f;
			p.c0 = WHITE; p.c1 = WHITE;
			p.rot = RandF(0.f, 6.f); p.vrot = RandF(10.f, 20.f);
			p.shape = Shape::Coin;
		}
		GlowP(iso, 12.f, 20.f, 40.f, Rgb(0xFFD84A, 180), 0.25f);
		Tx(fxart::CoinSpray, iso, 22.f, 30.f, 70.f, 0.45f, Rgb(0xFFF4D0, 230), Rgb(0xFFC040, 0), true, 0.f, 0.f, 0.05f);
	}

	void Effects::LootSparkle(Vector2 iso, Color color)
	{
		if (RandF() < 0.5f)
			if (Particle* g = Tx(fxart::Glint, Vector2{ iso.x + RandF(-10.f, 10.f), iso.y + RandF(-5.f, 5.f) }, RandF(6.f, 26.f), 6.f, 24.f, RandF(0.4f, 0.7f),
				util::Shade(color, 1.6f), WithA(color, 0.f), true, RandF(0.f, 1.f), RandF(-2.f, 2.f)))
				g->vz = RandF(10.f, 30.f);
		Particle& p = Particles().Emit();
		p.pos = { iso.x + RandF(-10.f, 10.f), iso.y + RandF(-5.f, 5.f) };
		p.z = RandF(4.f, 20.f);
		p.vz = RandF(15.f, 40.f);
		p.life = p.maxLife = RandF(0.6f, 1.0f);
		p.size0 = RandF(7.f, 12.f); p.size1 = 1.f;
		p.c0 = util::Shade(color, 1.4f); p.c1 = WithA(color, 0.f);
		p.rot = RandF(0.f, 1.f);
		p.shape = Shape::Star;
		p.additive = true;
	}

	void Effects::Ambient(WorldData::Biome biome, Vector2 c, float viewW, float viewH, float dt, float night, bool inTown)
	{
		using WorldData::Biome;
		m_ambientAcc += dt;
		const float step = 1.f / 60.f;
		while (m_ambientAcc >= step)
		{
			m_ambientAcc -= step;
			auto randPos = [&](float extraBelow = 0.f) { return Vector2{ c.x + RandF(-viewW * 0.6f, viewW * 0.6f), c.y + RandF(-viewH * 0.6f, viewH * 0.6f + extraBelow) }; };
			const bool fireflies = night > 0.5f && (biome == Biome::eMeadow || biome == Biome::eForest || biome == Biome::eFairy || biome == Biome::eSwamp || inTown);

			if (biome == Biome::eSnow && !inTown)
			{
				for (int i = 0; i < 2; ++i)
				{
					Particle& p = Particles().Emit();
					p.pos = randPos(200.f); p.z = RandF(150.f, 420.f);
					p.vel = { RandF(-15.f, -5.f), 0.f }; p.vz = RandF(-60.f, -35.f);
					p.life = p.maxLife = RandF(4.f, 7.f);
					p.size0 = p.size1 = RandF(2.f, 4.5f);
					p.c0 = p.c1 = Rgb(0xFFFFFF, 220);
					p.wobble = 18.f;
					p.shape = Shape::Flake;
				}
			}
			else if ((biome == Biome::eVolcanic || biome == Biome::eHell) && RandF() < 0.8f)
			{
				Particle& p = Particles().Emit();
				p.pos = randPos(120.f); p.z = RandF(0.f, 20.f);
				p.vel = { RandF(-10.f, 10.f), 0.f }; p.vz = RandF(30.f, 80.f);
				p.life = p.maxLife = RandF(1.5f, 3.f);
				p.size0 = RandF(3.f, 6.f); p.size1 = 1.f;
				p.c0 = Rgb(0xFFB040); p.c1 = Rgb(0xFF3A10, 0);
				p.wobble = 20.f;
				p.shape = Shape::Glow;
				p.additive = true;
				if (RandF() < 0.3f)
				{
					Particle& a = Particles().Emit();
					a.pos = randPos(200.f); a.z = RandF(100.f, 300.f);
					a.vz = RandF(-25.f, -10.f);
					a.life = a.maxLife = RandF(4.f, 6.f);
					a.size0 = a.size1 = RandF(2.f, 3.5f);
					a.c0 = a.c1 = Rgb(0x6A6060, 180);
					a.wobble = 12.f;
					a.shape = Shape::Flake;
				}
			}
			else if (biome == Biome::eDesert && RandF() < 0.35f)
			{
				Particle& p = Particles().Emit();
				p.pos = randPos(); p.z = RandF(0.f, 30.f);
				p.vel = { RandF(90.f, 160.f), RandF(-10.f, 10.f) };
				p.life = p.maxLife = RandF(1.2f, 2.2f);
				p.size0 = RandF(30.f, 60.f); p.size1 = RandF(60.f, 90.f);
				p.c0 = Rgb(0xE8D0A0, 50); p.c1 = Rgb(0xE8D0A0, 0);
				p.shape = Shape::Smoke;
			}
			else if ((biome == Biome::eSwamp || biome == Biome::eGraveyard) && RandF() < 0.25f)
			{
				Particle& p = Particles().Emit();
				p.pos = randPos(100.f); p.z = RandF(0.f, 20.f);
				p.vel = { RandF(-12.f, 12.f), RandF(-5.f, 5.f) };
				p.life = p.maxLife = RandF(3.f, 5.f);
				p.size0 = RandF(40.f, 70.f); p.size1 = RandF(80.f, 130.f);
				p.c0 = biome == Biome::eSwamp ? Rgb(0xA8B8A0, 45) : Rgb(0xB8C0D8, 45);
				p.c1 = WithA(p.c0, 0.f);
				p.shape = Shape::Smoke;
			}
			else if (biome == Biome::eFairy && RandF() < 0.5f)
			{
				Particle& p = Particles().Emit();
				p.pos = randPos(100.f); p.z = RandF(0.f, 60.f);
				p.vz = RandF(10.f, 30.f);
				p.life = p.maxLife = RandF(2.f, 4.f);
				p.size0 = RandF(5.f, 9.f); p.size1 = 1.f;
				p.c0 = RandF() < 0.5f ? Rgb(0x9AF8FF) : Rgb(0xF6DCA0); p.c1 = WithA(p.c0, 0.f);
				p.wobble = 25.f;
				p.shape = Shape::Star;
				p.additive = true;
			}
			else if ((biome == Biome::eForest || biome == Biome::eMeadow || inTown) && RandF() < (inTown ? 0.05f : 0.12f) && night < 0.6f)
			{
				Particle& p = Particles().Emit();
				p.pos = randPos(150.f); p.z = RandF(120.f, 300.f);
				// 꽃잎 · 잎은 바람(render/Wind)을 타고 날린다 — 셀수록 멀리
				const Vector2 wd = wind::Dir();
				const float ws = RandF(12.f, 22.f) + 48.f * wind::At(p.pos);
				p.vel = { wd.x * ws, wd.y * ws * 0.6f }; p.vz = RandF(-35.f, -20.f);
				p.life = p.maxLife = RandF(5.f, 8.f);
				p.size0 = p.size1 = RandF(6.f, 9.f);
				p.c0 = p.c1 = inTown ? Rgb(0xFFC8DA, 230) : (RandF() < 0.5f ? Rgb(0x8AB84A, 230) : Rgb(0xC8A040, 230));
				p.rot = RandF(0.f, 6.f); p.vrot = RandF(1.f, 3.f);
				p.wobble = 30.f;
				p.shape = Shape::Leaf;
			}
			else if ((biome == Biome::eRuins || biome == Biome::eCanyon || biome == Biome::eWasteland || biome == Biome::eAntHill) && RandF() < 0.15f)
			{
				Particle& p = Particles().Emit();
				p.pos = randPos(); p.z = RandF(10.f, 80.f);
				p.vel = { RandF(-8.f, 8.f), RandF(-4.f, 4.f) }; p.vz = RandF(-5.f, 5.f);
				p.life = p.maxLife = RandF(3.f, 5.f);
				p.size0 = p.size1 = RandF(2.f, 3.f);
				p.c0 = p.c1 = biome == Biome::eRuins ? Rgb(0xD0B8FF, 150) : Rgb(0xE8D8B8, 140);
				p.wobble = 10.f;
				p.shape = Shape::Flake;
			}

			if (fireflies && RandF() < 0.12f)
			{
				Particle& p = Particles().Emit();
				p.pos = randPos(100.f); p.z = RandF(10.f, 70.f);
				p.vel = { RandF(-15.f, 15.f), RandF(-8.f, 8.f) }; p.vz = RandF(-8.f, 8.f);
				p.life = p.maxLife = RandF(2.f, 4.f);
				p.size0 = RandF(8.f, 12.f); p.size1 = 3.f;
				p.c0 = Rgb(0xD8FF70); p.c1 = Rgb(0x8AFF40, 0);
				p.wobble = 30.f;
				p.shape = Shape::Glow;
				p.additive = true;
			}
		}
	}

	// ── 그리기 ────────────────────────────────────────────────────────────────
	void Effects::DrawGround()
	{
		// 충격파 고리
		for (const auto& r : m_rings)
		{
			const float u = r.t / r.life;
			const float rad = util::Lerp(r.r0, r.r1, util::EaseOutCubic(u));
			const float a = 1.f - util::EaseInCubic(u);
			gfx::SetDepthRaw(-0.97f);
			const float th = r.thick * (1.f - u * 0.5f);
			gfx::EllipseArc(r.iso, rad, rad * 0.5f, 0.f, util::kPi * 2.f, th, util::MulA(r.color, a));
		}
		// 보스 경고 장판(체비셰프 반경 = 서버 판정 영역 그대로)
		for (const auto& t : m_teles)
		{
			const Vector2 tile = gfx::IsoToTile(t.iso);
			const float R = t.radius + 0.5f;
			const float u = util::Clamp01(t.t / t.life);
			const float pulse = 0.5f + 0.5f * std::sin(m_time * 18.f);
			auto corner = [&](float dx, float dy, float k) { return gfx::IsoPx(tile.x + dx * R * k, tile.y + dy * R * k); };
			gfx::SetDepthRaw(-0.97f);
			gfx::Quad(corner(-1, -1, 1), corner(1, -1, 1), corner(1, 1, 1), corner(-1, 1, 1), util::MulA(t.color, 0.18f + 0.08f * pulse));
			gfx::Quad(corner(-1, -1, u), corner(1, -1, u), corner(1, 1, u), corner(-1, 1, u), util::MulA(t.color, 0.35f));
			const Vector2 c[4] = { corner(-1, -1, 1), corner(1, -1, 1), corner(1, 1, 1), corner(-1, 1, 1) };
			for (int i = 0; i < 4; ++i)
				gfx::Line(c[i], c[(i + 1) % 4], 3.f + pulse * 2.f, util::MulA(util::Shade(t.color, 1.3f), 0.9f));
		}
		// 클릭 표시(Phase 1-A): 0.3초 — 바깥 고리가 조여 들고 가운데 점이 한 번 빛난다
		for (const auto& c : m_clicks)
		{
			const float u = util::Clamp01(c.t / kClickLife);
			gfx::SetDepthRaw(-0.97f);
			const float r = util::Lerp(20.f, 7.f, util::EaseOutCubic(u));
			const float a = 1.f - util::EaseInCubic(u);
			gfx::EllipseArc(c.iso, r, r * 0.5f, 0.f, util::kPi * 2.f, 2.5f, util::MulA(c.color, a));
			gfx::Ellipse(c.iso, 3.f * (1.f - u) + 1.f, 1.5f * (1.f - u) + 0.5f, util::MulA(c.color, a * 0.9f));
		}
	}

	void Effects::DrawWorld()
	{
		const auto& tex = assets::Get();
		gfx::Additive(true);
		for (const auto& b : m_beams)
		{
			const float u = b.t / b.life;
			const float a = (u < 0.15f ? u / 0.15f : 1.f - util::EaseInCubic((u - 0.15f) / 0.85f));
			const float w = b.w * (0.6f + 0.4f * util::EaseOutBack(util::Clamp01(u * 3.f)));
			gfx::SetDepth(b.iso.y / gfx::kHalfH + 0.4f);
			gfx::TexQuad(tex.beam, Vector2{ b.iso.x, b.iso.y - b.h * 0.5f }, Vector2{ w, b.h }, 0.f, util::MulA(b.color, a));
			gfx::TexQuad(tex.beam, Vector2{ b.iso.x, b.iso.y - b.h * 0.4f }, Vector2{ w * 0.35f, b.h * 0.8f }, 0.f, util::MulA(WHITE, a * 0.8f));
			gfx::TexQuad(tex.glow, b.iso, Vector2{ w * 2.2f, w * 1.1f }, 0.f, util::MulA(b.color, a * 0.8f));
		}
		// 칼 궤적(초승달)
		for (const auto& s : m_slashes)
		{
			const float u = s.t / s.life;
			const float a = 1.f - util::EaseInCubic(u);
			gfx::SetDepth(s.iso.y / gfx::kHalfH + 0.5f);
			const Vector2 c{ s.iso.x + s.dir * 10.f * s.scale, s.iso.y - s.h };
			const float r = 30.f * s.scale;
			const float start = s.back ? -2.2f : -2.6f;
			const float sweep = util::EaseOutCubic(util::Clamp01(u * 2.5f)) * 2.6f;
			const int seg = 14;
			for (int i = 0; i < seg; ++i)
			{
				const float t0 = static_cast<float>(i) / seg, t1 = static_cast<float>(i + 1) / seg;
				const float a0 = start + sweep * t0, a1 = start + sweep * t1;
				const float w0 = std::sin(t0 * util::kPi) * 9.f * s.scale, w1 = std::sin(t1 * util::kPi) * 9.f * s.scale;
				auto P = [&](float ang, float rr) { return Vector2{ c.x + std::cos(ang) * rr * s.dir, c.y + std::sin(ang) * rr * 0.75f }; };
				gfx::Quad4(P(a0, r - w0), P(a1, r - w1), P(a1, r + w1 * 0.4f), P(a0, r + w0 * 0.4f),
					util::MulA(s.color, a * 0.1f), util::MulA(s.color, a * 0.1f), util::MulA(s.color, a * t1), util::MulA(s.color, a * t0));
			}
		}
		gfx::Additive(false);
		Particles().Draw();
	}

	void Effects::DrawOverlay(const Camera2D& cam)
	{
		for (const auto& f : m_floats)
		{
			const float u = f.t / f.life;
			const float rise = util::EaseOutCubic(u) * 50.f;
			const float pop = u < 0.15f ? util::EaseOutBack(u / 0.15f) : 1.f;
			const float a = u > 0.7f ? 1.f - (u - 0.7f) / 0.3f : 1.f;
			const Vector2 s = GetWorldToScreen2D(Vector2{ f.iso.x, f.iso.y - f.h - rise - gfx::LiftAt(f.iso) }, cam);
			DrawNumber(f.text, s, f.size * pop, f.color, Color{ 12, 10, 8, 255 }, a, f.face);
		}
		for (const auto& d : m_damage)
		{
			const float u = d.t / d.life;
			float size = 22.f;
			Color fill = WHITE, line = Color{ 30, 20, 10, 255 };
			switch (d.style)
			{
			// 색 체계: 평타 = 흰 종이빛, 치명 = 새벽빛, 기술 = 옛길 청록, 받은 피해 = 불씨 붉은빛
			case DamageStyle::Normal: size = 26.f; fill = Rgb(0xF4EEE2); line = Rgb(0x16120E); break;
			case DamageStyle::Crit: size = 38.f; fill = Rgb(0xF2C47C); line = Rgb(0x361E06); break;
			case DamageStyle::Skill: size = 30.f; fill = Rgb(0x92D6CC); line = Rgb(0x082420); break;
			case DamageStyle::Taken: size = 25.f; fill = Rgb(0xE26050); line = Rgb(0x2A0804); break;
			case DamageStyle::TakenCrit: size = 33.f; fill = Rgb(0xEC4638); line = Rgb(0x220402); break;
			case DamageStyle::Heal: size = 23.f; fill = Rgb(0x8DCE78); line = Rgb(0x0A2A0C); break;
			case DamageStyle::Other: size = 19.f; fill = Rgb(0xC8C8C4); break;
			case DamageStyle::Exp: size = 19.f; fill = Rgb(0xD4AA60); line = Rgb(0x2A1C06); break;
			case DamageStyle::DotBurn: size = 18.f; fill = Rgb(0xE8965A); line = Rgb(0x2E1204); break;
			case DamageStyle::DotPoison: size = 18.f; fill = Rgb(0xA0D66E); line = Rgb(0x0E2408); break;
			case DamageStyle::Guarded: size = 21.f; fill = Rgb(0xD6C496); line = Rgb(0x2A2006); break;
			case DamageStyle::Total: size = 50.f; fill = Rgb(0xF2C47C); line = Rgb(0x2A1604); break;
			}
			// 합쳐진 숫자는 타수만큼 조금씩 커진다(최대 +45%) + 합쳐지는 순간 튄다
			if (d.hits > 1) size *= 1.f + std::min(0.45f, (d.hits - 1) * 0.09f);
			size *= 1.f + d.punch * 0.35f;
			// 튀어나옴(오버슈트) → 위로 떠오르며 감속 → 사라짐
			const float pop = u < 0.12f ? util::EaseOutBack(u / 0.12f, 3.f) : 1.f + 0.15f * (1.f - util::Clamp01((u - 0.12f) * 5.f));
			const bool total = d.style == DamageStyle::Total;
			const float rise = total ? util::EaseOutCubic(util::Clamp01(u * 3.f)) * 36.f : util::EaseOutCubic(u) * (d.style == DamageStyle::Crit ? 70.f : 55.f);
			const float a = u > 0.65f ? 1.f - (u - 0.65f) / 0.35f : 1.f;
			const float x = d.iso.x + d.vx * util::EaseOutCubic(u);
			// 쌓인 줄: 한 줄 = 글자 크기의 0.9배(화면 px → 월드 px)
			const float lineH = size * 0.9f / std::max(0.3f, cam.zoom);
			const Vector2 s = GetWorldToScreen2D(Vector2{ x, d.iso.y - d.h - rise - d.stackShown * lineH - gfx::LiftAt(d.iso) }, cam);
			if (total)
			{
				DrawNumber("총 피해", Vector2{ s.x, s.y - size * 0.95f }, 17.f * pop, Rgb(0xF6DCA8), line, a, ui::Face::Strong);
				DrawNumber(d.text, s, size * pop, fill, line, a);
				continue;
			}
			const float shake = (d.style == DamageStyle::Crit && u < 0.2f) ? std::sin(d.t * 90.f) * 3.f : 0.f;
			DrawNumber(d.text, Vector2{ s.x + shake, s.y }, size * pop, fill, line, a);
			{
				// 표지: 치명 · 급소 · 후방 · 반격 · 메아리 — 모양(색 · 크기)이 다르다
				std::string tag;
				Color tagCol = Rgb(0xF6DCA8);
				auto add = [&](const char* t, Color c) { if (!tag.empty()) tag += " · "; else tagCol = c; tag += t; };
				if (d.tags & TagWeak) add("급소", Rgb(0xFFE2A8));
				if (d.style == DamageStyle::Crit || d.style == DamageStyle::TakenCrit) add("치명", d.style == DamageStyle::Crit ? Rgb(0xF6DCA8) : Rgb(0xF0B0A4));
				if (d.tags & TagBack) add("후방", Rgb(0xD8E2EC));
				if (d.tags & TagCounter) add("반격", Rgb(0x9FE0D6));
				if (d.tags & TagEcho) add("잔향", Rgb(0xF2D8A0));
				if (d.tags & TagClone) add("분신", Rgb(0xC8D2E0));
				if (!tag.empty())
				{
					const float ts = (d.tags & TagWeak) ? 16.f : 14.f;
					DrawNumber(tag, Vector2{ s.x, s.y - size * 0.82f }, ts * pop, tagCol, line, a, ui::Face::Strong);
					if (d.tags & TagWeak)
					{
						// 급소: 숫자 양옆에 짧은 사선 두 줄(베인 자국)
						const float w = size * 0.9f + static_cast<float>(d.text.size()) * size * 0.28f;
						for (int k = -1; k <= 1; k += 2)
						{
							const float cx = s.x + k * w * 0.5f;
							DrawLineEx(Vector2{ cx - 5.f, s.y + 6.f }, Vector2{ cx + 5.f, s.y - 8.f }, 2.5f, util::MulA(Rgb(0xFFE2A8), a));
						}
					}
				}
			}
			if (d.hits > 1)
				DrawNumber("×" + std::to_string(d.hits), Vector2{ s.x + size * 0.9f + 14.f, s.y + size * 0.3f }, 16.f * pop, Rgb(0xE8DCC4), line, a * 0.9f, ui::Face::Number);
		}
	}

	void Effects::DrawScreen(int w, int h)
	{
		if (m_flash > 0.f && m_flashLife > 0.f)
		{
			const float a = (m_flash / m_flashLife);
			const float k = (m_flashColor.a / 255.f) * a * a;
			// 스크린 혼합: 결과 = 빛 + 바탕 × (1 − 빛). 어두운 바닥에서는 더하기와 같고, 밝은 바닥(눈밭 · 모래)은 하얗게 타지 않는다
			// (Phase 3 자기 검토 — 더하기 혼합이던 때는 설원 보스 처치 순간 월드 전체가 0.3초 동안 흰 판이 됐다)
			const Color c{ static_cast<unsigned char>(m_flashColor.r * k), static_cast<unsigned char>(m_flashColor.g * k),
				static_cast<unsigned char>(m_flashColor.b * k), 255 };
			rlDrawRenderBatchActive();
			rlSetBlendFactors(1 /*GL_ONE*/, 0x0301 /*GL_ONE_MINUS_SRC_COLOR*/, 0x8006 /*GL_FUNC_ADD*/);
			rlSetBlendMode(RL_BLEND_CUSTOM);
			DrawRectangle(0, 0, w, h, c);
			rlDrawRenderBatchActive();
			rlSetBlendMode(RL_BLEND_ALPHA);
		}
	}
}
