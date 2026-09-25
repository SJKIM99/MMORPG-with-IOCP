#include "fx/Particles.h"
#include "render/Draw.h"
#include "render/Assets.h"
#include "render/FxArt.h"
#include "core/Util.h"

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
		if (m_parts.size() >= kMax)
			m_parts.erase(m_parts.begin(), m_parts.begin() + 256);  // 가장 오래된 것부터 버린다
		m_parts.emplace_back();
		Particle& p = m_parts.back();
		p.seed = util::RandF(0.f, 100.f);
		return p;
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
		}
		// 수명이 다한 것 제거(순서 유지 불필요 — swap & pop)
		for (size_t i = 0; i < m_parts.size();)
		{
			if (m_parts[i].life <= 0.f)
			{
				m_parts[i] = m_parts.back();
				m_parts.pop_back();
			}
			else
				++i;
		}
	}

	void ParticleSystem::Draw()
	{
		const auto& tex = assets::Get();
		for (int pass = 0; pass < 2; ++pass)
		{
			const bool additive = pass == 1;
			gfx::Additive(additive);
			for (const auto& p : m_parts)
			{
				if (p.additive != additive)
					continue;
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
				c.a = static_cast<unsigned char>(c.a * fade);
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
