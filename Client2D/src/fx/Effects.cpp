#include "fx/Effects.h"
#include "fx/Particles.h"
#include "render/Draw.h"
#include "render/Assets.h"
#include "ui/Font.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>

namespace fx
{
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

		void DrawNumber(const std::string& text, Vector2 center, float size, Color fill, Color outline, float alpha)
		{
			ui::TextStyle st;
			st.size = size;
			st.color = util::MulA(fill, alpha);
			st.outline = std::max(1.5f, size * 0.09f);
			st.outlineColor = util::MulA(outline, alpha);
			st.shadow = size * 0.08f;
			st.shadowColor = Color{ 0, 0, 0, static_cast<unsigned char>(120 * alpha) };
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
		Age(m_floats, dt);
		Age(m_rings, sdt);
		Age(m_beams, sdt);
		Age(m_teles, dt);
		Age(m_slashes, sdt);
		for (auto& c : m_clicks) c.t += dt;
		m_clicks.erase(std::remove_if(m_clicks.begin(), m_clicks.end(), [](const ClickFx& c) { return c.t > 0.45f; }), m_clicks.end());
		if (m_flash > 0.f) m_flash = std::max(0.f, m_flash - dt);
		Particles().Update(sdt);
	}

	// ── 기본 요소 ─────────────────────────────────────────────────────────────
	void Effects::Damage(Vector2 iso, float height, int amount, DamageStyle style)
	{
		std::string text = std::to_string(amount);
		if (style == DamageStyle::Heal) text = "+" + text;
		if (style == DamageStyle::Exp) text = "+" + text + " EXP";
		const float life = (style == DamageStyle::Crit || style == DamageStyle::TakenCrit) ? 1.25f : 0.95f;
		m_damage.push_back({ iso, height, text, style, 0.f, life, RandF(-18.f, 18.f) });
	}

	void Effects::FloatText(Vector2 iso, float height, const std::string& text, Color color, float size, float life)
	{
		m_floats.push_back({ iso, height, text, color, size, 0.f, life });
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
	}

	void Effects::Flash(Color color, float strength, float duration)
	{
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
			Flash(Rgb(0xFFF0D0), 0.6f, 0.35f);
		}
	}

	void Effects::LevelUp(Vector2 iso)
	{
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
	}

	void Effects::TeleportOut(Vector2 iso)
	{
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
			Beam(iso, 60.f, 400.f, 1.0f, Rgb(0x9AD8FF, 230));
			Ring(iso, 10.f, 130.f, 0.6f, Rgb(0xBFE8FF, 240), 6.f);
			for (int i = 0; i < 36; ++i)
				Spark(iso, 40.f, RandF(120.f, 380.f), RandF(0.2f, 2.94f), Rgb(0xFFFFFF), Rgb(0x4A9AFF), RandF(0.4f, 0.9f), RandF(6.f, 10.f));
		}
		else
		{
			for (int i = 0; i < 14; ++i)
				Smoke(iso, RandF(20.f, 50.f), Rgb(0x5A3A6A), 16.f, 50.f, RandF(0.7f, 1.2f), Vector2{ RandF(-40.f, 40.f), RandF(-20.f, 20.f) }, RandF(20.f, 60.f));
			for (int i = 0; i < 16; ++i)
				Chunk(iso, 40.f, Rgb(0xB8C0CC), RandF(3.f, 6.f), RandF(60.f, 160.f));
			Ring(iso, 10.f, 90.f, 0.5f, Rgb(0xA05AFF, 200), 4.f);
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
		for (int i = 0; i < n; ++i)
			Smoke(Vector2{ iso.x + RandF(-6.f, 6.f), iso.y + RandF(-2.f, 2.f) }, 2.f, Rgb(0xC8B89A, 120), 6.f, 18.f, RandF(0.35f, 0.6f),
				Vector2{ RandF(-20.f, 20.f), RandF(-6.f, 6.f) }, RandF(8.f, 22.f));
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
	}

	void Effects::LootSparkle(Vector2 iso, Color color)
	{
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
				p.c0 = RandF() < 0.5f ? Rgb(0x9AF8FF) : Rgb(0xFFA8E8); p.c1 = WithA(p.c0, 0.f);
				p.wobble = 25.f;
				p.shape = Shape::Star;
				p.additive = true;
			}
			else if ((biome == Biome::eForest || biome == Biome::eMeadow || inTown) && RandF() < (inTown ? 0.05f : 0.12f) && night < 0.6f)
			{
				Particle& p = Particles().Emit();
				p.pos = randPos(150.f); p.z = RandF(120.f, 300.f);
				p.vel = { RandF(10.f, 30.f), 0.f }; p.vz = RandF(-35.f, -20.f);
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
		// 클릭 표시
		for (const auto& c : m_clicks)
		{
			const float u = c.t / 0.45f;
			gfx::SetDepthRaw(-0.97f);
			const float r = util::Lerp(22.f, 6.f, util::EaseOutCubic(u));
			gfx::EllipseArc(c.iso, r, r * 0.5f, 0.f, util::kPi * 2.f, 3.f, util::MulA(c.color, 1.f - u));
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
			const Vector2 s = GetWorldToScreen2D(Vector2{ f.iso.x, f.iso.y - f.h - rise }, cam);
			DrawNumber(f.text, s, f.size * pop, f.color, Color{ 20, 14, 8, 255 }, a);
		}
		for (const auto& d : m_damage)
		{
			const float u = d.t / d.life;
			float size = 22.f;
			Color fill = WHITE, line = Color{ 30, 20, 10, 255 };
			switch (d.style)
			{
			case DamageStyle::Normal: size = 24.f; fill = Rgb(0xFFF4E0); break;
			case DamageStyle::Crit: size = 36.f; fill = Rgb(0xFFC23A); line = Rgb(0x5A1A00); break;
			case DamageStyle::Skill: size = 28.f; fill = Rgb(0x9AE0FF); line = Rgb(0x0A2A4A); break;
			case DamageStyle::Taken: size = 24.f; fill = Rgb(0xFF5A4A); line = Rgb(0x3A0000); break;
			case DamageStyle::TakenCrit: size = 32.f; fill = Rgb(0xFF3A2A); line = Rgb(0x2A0000); break;
			case DamageStyle::Heal: size = 22.f; fill = Rgb(0x7AFF8A); line = Rgb(0x0A3A10); break;
			case DamageStyle::Other: size = 18.f; fill = Rgb(0xD8D8D8); break;
			case DamageStyle::Exp: size = 18.f; fill = Rgb(0xC8A0FF); line = Rgb(0x2A1040); break;
			}
			// 튀어나옴(오버슈트) → 위로 떠오르며 감속 → 사라짐
			const float pop = u < 0.12f ? util::EaseOutBack(u / 0.12f, 3.f) : 1.f + 0.15f * (1.f - util::Clamp01((u - 0.12f) * 5.f));
			const float rise = util::EaseOutCubic(u) * (d.style == DamageStyle::Crit ? 70.f : 55.f);
			const float a = u > 0.65f ? 1.f - (u - 0.65f) / 0.35f : 1.f;
			const float x = d.iso.x + d.vx * util::EaseOutCubic(u);
			const Vector2 s = GetWorldToScreen2D(Vector2{ x, d.iso.y - d.h - rise }, cam);
			const float shake = (d.style == DamageStyle::Crit && u < 0.2f) ? std::sin(d.t * 90.f) * 3.f : 0.f;
			DrawNumber(d.text, Vector2{ s.x + shake, s.y }, size * pop, fill, line, a);
			if (d.style == DamageStyle::Crit || d.style == DamageStyle::TakenCrit)
				DrawNumber("치명타!", Vector2{ s.x, s.y - size * 0.9f }, 15.f * pop, Rgb(0xFFF0C0), line, a);
		}
	}

	void Effects::DrawScreen(int w, int h)
	{
		if (m_flash > 0.f && m_flashLife > 0.f)
		{
			const float a = (m_flash / m_flashLife);
			Color c = m_flashColor;
			c.a = static_cast<unsigned char>(c.a * a * a);
			BeginBlendMode(BLEND_ADDITIVE);
			DrawRectangle(0, 0, w, h, c);
			EndBlendMode();
		}
	}
}
