#include "fx/Particles.h"
#include "fx/FxScale.h"
#include "render/Draw.h"
#include "render/Assets.h"
#include "render/FxArt.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>

namespace fx
{
	ParticleSystem& Particles()
	{
		static ParticleSystem instance;
		return instance;
	}

	Particle& ParticleSystem::Emit()
	{
		constexpr size_t kMax = 6000;
		if (!fxscale::Keep())
		{
			// 버린다: 목록 밖 임시 입자를 돌려준다(값을 채워도 그려지지 않는다)
			static Particle scratch;
			scratch = Particle{};
			return scratch;
		}
		if (m_parts.size() >= kMax)
			m_parts.erase(m_parts.begin(), m_parts.begin() + 256);  // 가장 오래된 것부터 버린다
		m_parts.emplace_back();
		Particle& p = m_parts.back();
		p.seed = util::RandF(0.f, 100.f);
		p.alphaK = fxscale::Alpha();
		return p;
	}

	void ParticleSystem::SpawnSub(Sub kind, const Particle& from, bool death)
	{
		const int n = death ? (kind == Sub::Smoke || kind == Sub::Dust ? 2 : 5) : 1;
		for (int i = 0; i < n; ++i)
		{
			if (m_parts.size() + m_pending.size() >= 6000) return;
			Particle p;
			p.seed = util::RandF(0.f, 100.f);
			p.alphaK = from.alphaK;
			p.pos = from.pos;
			p.z = std::max(0.f, from.z);
			p.subColor = from.subColor;
			const float a = util::RandF(0.f, 6.2831853f);
			switch (kind)
			{
			case Sub::Sparks:
			{
				const float sp = util::RandF(60.f, 180.f);
				p.vel = { std::cos(a) * sp, std::sin(a) * sp * 0.5f };
				p.vz = util::RandF(20.f, 120.f);
				p.gravity = 420.f; p.drag = 2.f;
				p.life = p.maxLife = util::RandF(0.15f, 0.3f);
				p.size0 = util::RandF(3.f, 6.f); p.size1 = 0.f;
				p.c0 = from.subColor; p.c1 = util::WithA(from.subColor, 0.f);
				p.shape = Shape::Spark; p.stretch = true; p.additive = true;
				break;
			}
			case Sub::Smoke:
			case Sub::Dust:
			{
				const bool dust = kind == Sub::Dust;
				p.vel = { util::RandF(-12.f, 12.f), util::RandF(-6.f, 6.f) };
				p.vz = dust ? util::RandF(4.f, 14.f) : util::RandF(12.f, 34.f);
				p.drag = 1.4f;
				p.life = p.maxLife = death ? util::RandF(0.6f, 1.0f) : util::RandF(0.35f, 0.6f);
				const float s0 = death ? util::RandF(12.f, 20.f) : util::RandF(6.f, 11.f);
				p.size0 = s0 * 0.6f; p.size1 = s0 * 1.7f;
				const Color base = dust ? Color{ 180, 160, 128, 150 } : Color{ 90, 84, 78, 150 };
				p.c0 = util::MulA(base, death ? 1.f : 0.7f); p.c1 = util::WithA(base, 0.f);
				p.rot = util::RandF(0.f, 6.f); p.vrot = util::RandF(-1.f, 1.f);
				p.shape = Shape::Smoke;
				break;
			}
			case Sub::Embers:
				p.vel = { util::RandF(-20.f, 20.f), util::RandF(-10.f, 10.f) };
				p.vz = util::RandF(20.f, 70.f);
				p.drag = 1.2f; p.wobble = 10.f;
				p.life = p.maxLife = util::RandF(0.3f, 0.7f);
				p.size0 = util::RandF(3.f, 6.f); p.size1 = 0.5f;
				p.c0 = from.subColor; p.c1 = Color{ 255, 58, 16, 0 };
				p.shape = Shape::Soft; p.additive = true;
				break;
			case Sub::Frost:
				p.vel = { std::cos(a) * util::RandF(10.f, 40.f), std::sin(a) * util::RandF(5.f, 20.f) };
				p.vz = util::RandF(-10.f, 30.f);
				p.drag = 2.f;
				p.life = p.maxLife = util::RandF(0.3f, 0.6f);
				p.size0 = util::RandF(3.f, 5.f); p.size1 = 0.f;
				p.c0 = Color{ 230, 248, 255, 230 }; p.c1 = Color{ 150, 210, 255, 0 };
				p.shape = Shape::Star; p.rot = util::RandF(0.f, 6.f); p.additive = true;
				break;
			case Sub::Ink:
				p.vel = { std::cos(a) * util::RandF(30.f, 90.f), std::sin(a) * util::RandF(15.f, 45.f) };
				p.vz = util::RandF(30.f, 90.f);
				p.gravity = 380.f; p.drag = 1.f;
				p.life = p.maxLife = util::RandF(0.3f, 0.55f);
				p.size0 = util::RandF(3.f, 6.f); p.size1 = 1.f;
				p.c0 = Color{ 14, 16, 22, 230 }; p.c1 = Color{ 10, 12, 16, 0 };
				p.shape = Shape::Flake;
				break;
			case Sub::None:
				return;
			}
			m_pending.push_back(p);
		}
	}

	Particle* EmitTex(int fxId, Vector2 iso, float z, float size0, float size1, float life, Color c0, Color c1, bool additive)
	{
		const Texture2D* t = fxart::Get(static_cast<fxart::Id>(fxId));
		if (!t) return nullptr;
		Particle& p = Particles().Emit();
		p.pos = iso;
		p.z = z;
		p.life = p.maxLife = life;
		p.size0 = size0;
		p.size1 = size1;
		p.c0 = c0;
		p.c1 = c1;
		p.shape = Shape::Tex;
		p.tex = t;
		p.aspect = t->height > 0 ? static_cast<float>(t->width) / static_cast<float>(t->height) : 1.f;
		p.additive = additive;
		return &p;
	}

	void ParticleSystem::Update(float dt)
	{
		for (auto& p : m_parts)
		{
			p.life -= dt;
			const float k = std::exp(-p.drag * dt);
			p.vel.x *= k;
			p.vel.y *= k;
			p.vz = p.vz * (p.bounce ? 1.f : k) - p.gravity * dt;
			p.pos.x += p.vel.x * dt;
			p.pos.y += p.vel.y * dt;
			if (p.wobble > 0.f)
				p.pos.x += std::sin((p.maxLife - p.life) * 3.f + p.seed) * p.wobble * dt;
			p.z += p.vz * dt;
			if (p.bounce && p.z < 0.f)
			{
				p.z = 0.f;
				if (std::fabs(p.vz) > 40.f)
				{
					p.vz = -p.vz * 0.42f;
					p.vel.x *= 0.6f;
					p.vel.y *= 0.6f;
					p.vrot *= 0.5f;
				}
				else
				{
					p.vz = 0.f;
					p.vel.x *= 0.85f;
					p.vel.y *= 0.85f;
					p.vrot *= 0.8f;
				}
			}
			p.rot += p.vrot * dt;
			if (p.trail != Sub::None && p.life > 0.f)
			{
				p.trailAcc += dt;
				while (p.trailAcc >= 0.04f) { p.trailAcc -= 0.04f; SpawnSub(p.trail, p, false); }
			}
		}
		// 수명이 다한 것 제거(순서 유지 불필요 — swap & pop). 죽을 때 하위 입자
		for (size_t i = 0; i < m_parts.size();)
		{
			if (m_parts[i].life <= 0.f)
			{
				if (m_parts[i].onDeath != Sub::None) SpawnSub(m_parts[i].onDeath, m_parts[i], true);
				m_parts[i] = m_parts.back();
				m_parts.pop_back();
			}
			else
				++i;
		}
		if (!m_pending.empty())
		{
			m_parts.insert(m_parts.end(), m_pending.begin(), m_pending.end());
			m_pending.clear();
		}
	}

	void ParticleSystem::Draw()
	{
		const auto& tex = assets::Get();
		for (int pass = 0; pass < 2; ++pass)
		{
			const bool additive = pass == 1;
			gfx::Additive(additive);
			if (!additive && gfx::EmissiveOnlyOn()) continue;   // 빛 버퍼에는 더하기 입자만
			// 더하기 입자는 순서가 결과를 바꾸지 않는다 — 모양(텍스처)별로 묶어 그리기 호출을 줄인다
			m_order.clear();
			for (uint32_t i = 0; i < m_parts.size(); ++i)
				if (m_parts[i].additive == additive) m_order.push_back(i);
			if (additive)
				std::stable_sort(m_order.begin(), m_order.end(), [&](uint32_t a, uint32_t b)
				{
					const Particle& x = m_parts[a];
					const Particle& y = m_parts[b];
					if (x.shape != y.shape) return x.shape < y.shape;
					return x.tex < y.tex;
				});
			for (const uint32_t idx : m_order)
			{
				const Particle& p = m_parts[idx];
				const float t = 1.f - util::Clamp01(p.life / p.maxLife);
				float size = util::Lerp(p.size0, p.size1, t);
				if (p.pop > 0.f)
				{
					const float age = p.maxLife - p.life;
					if (age < p.pop) size *= util::EaseOutBack(age / p.pop, 2.2f);
				}
				if (size <= 0.2f)
					continue;
				Color c = util::Mix(p.c0, p.c1, t);
				// 마지막 25%에서 서서히 사라짐
				const float fade = util::Clamp01(p.life / (p.maxLife * 0.25f));
				c.a = static_cast<unsigned char>(c.a * fade * p.alphaK);
				if (c.a == 0)
					continue;
				if (p.ground) gfx::SetDepthRaw(-0.965f);
				else gfx::SetDepth(p.pos.y / gfx::kHalfH + 0.35f);
				const Vector2 s{ p.pos.x, p.pos.y - p.z };
				switch (p.shape)
				{
				case Shape::Glow:  gfx::TexQuad(tex.glow, s, Vector2{ size * 2.f, size * 2.f }, 0.f, c); break;
				case Shape::Soft:  gfx::TexQuad(tex.soft, s, Vector2{ size, size }, 0.f, c); break;
				case Shape::Smoke: gfx::TexQuad(tex.smoke, s, Vector2{ size, size }, p.rot, c); break;
				case Shape::Star:  gfx::TexQuad(tex.star, s, Vector2{ size, size }, p.rot, c); break;
				case Shape::Ring:  gfx::TexQuad(tex.ring, s, Vector2{ size, size * 0.5f }, 0.f, c); break;
				case Shape::Spark:
				{
					float ang = p.rot, len = size * 2.2f;
					if (p.stretch)
					{
						const float vx = p.vel.x, vy = p.vel.y - p.vz;
						const float sp = std::sqrt(vx * vx + vy * vy);
						ang = std::atan2(vy, vx);
						len = size * (1.2f + std::min(sp / 120.f, 3.f));
					}
					gfx::TexQuad(tex.spark, s, Vector2{ len, size * 0.55f }, ang, c);
					break;
				}
				case Shape::Chunk:
				{
					const float h = size * 0.5f;
					const float cs = std::cos(p.rot), sn = std::sin(p.rot);
					auto R = [&](float x, float y) { return Vector2{ s.x + x * cs - y * sn, s.y + x * sn + y * cs }; };
					gfx::Quad(R(-h, -h * 0.8f), R(h, -h), R(h * 0.9f, h), R(-h, h * 0.9f), c);
					gfx::Tri(R(-h, -h * 0.8f), R(h, -h), R(0, -h * 0.2f), util::Shade(c, 1.3f));
					break;
				}
				case Shape::Leaf:
				{
					const float w = size * (0.35f + 0.65f * std::fabs(std::sin(p.rot * 2.f)));
					gfx::Ellipse(s, w * 0.5f, size * 0.22f, c);
					break;
				}
				case Shape::Flake:
					gfx::Circle(s, size * 0.5f, c);
					break;
				case Shape::Tex:
					if (!p.tex) break;
					if (p.squash != 1.f)
						gfx::TexQuadGround(*p.tex, s, Vector2{ size * p.aspect * (p.flipX ? -1.f : 1.f), size }, p.rot, p.squash, c);
					else
						gfx::TexQuad(*p.tex, s, Vector2{ size * p.aspect * (p.flipX ? -1.f : 1.f), size }, p.rot, c);
					break;
				case Shape::Coin:
				{
					const float w = size * (0.3f + 0.7f * std::fabs(std::cos(p.rot)));
					gfx::Ellipse(s, w * 0.5f, size * 0.5f, util::Rgb(0xB8860B, c.a));
					gfx::Ellipse(Vector2{ s.x - w * 0.08f, s.y - size * 0.06f }, w * 0.4f, size * 0.4f, util::Rgb(0xFFD84A, c.a));
					break;
				}
				}
			}
		}
		gfx::Additive(false);
	}
}
