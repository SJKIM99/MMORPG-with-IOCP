#include "render/Actors.h"
#include "render/Draw.h"
#include "render/Icons.h"
#include "render/Props.h"
#include "core/Util.h"

#include <cmath>

namespace actors
{
	namespace
	{
		using util::Rgb;
		using util::Shade;
		using util::WithA;
		using util::Lerp;
		using util::kPi;

		// ── 로컬 좌표계(발 기준, +x = 앞) ───────────────────────────────────────
		struct Rig
		{
			Vector2 base{};
			float s = 1.f;
			float dir = 1.f;     // +1 오른쪽, -1 왼쪽
			float rot = 0.f;     // 쓰러짐 회전
			float sy = 1.f;      // 세로 눌림
			float ox = 0.f, oy = 0.f;

			Vector2 P(float x, float y) const
			{
				x += ox;
				y = (y + oy) * sy;
				const float c = std::cos(rot), sn = std::sin(rot);
				const float rx = x * c - y * sn, ry = x * sn + y * c;
				return Vector2{ base.x + rx * s * dir, base.y + ry * s };
			}
			float R(float r) const { return r * s; }
		};

		void Circ(const Rig& g, float x, float y, float r, Color c) { gfx::Circle(g.P(x, y), g.R(r), c); }
		void Ell(const Rig& g, float x, float y, float rx, float ry, Color c) { gfx::Ellipse(g.P(x, y), g.R(rx), g.R(ry * g.sy), c); }
		void EllG(const Rig& g, float x, float y, float rx, float ry, Color in, Color out) { gfx::EllipseGrad(g.P(x, y), g.R(rx), g.R(ry * g.sy), in, out); }
		void Limb(const Rig& g, float x0, float y0, float x1, float y1, float r, Color c) { gfx::Capsule(g.P(x0, y0), g.P(x1, y1), g.R(r), c); }
		void Seg(const Rig& g, float x0, float y0, float x1, float y1, float w, Color c) { gfx::Line(g.P(x0, y0), g.P(x1, y1), g.R(w), c); }
		void SegG(const Rig& g, float x0, float y0, float x1, float y1, float w0, float w1, Color a, Color b) { gfx::LineGrad(g.P(x0, y0), g.P(x1, y1), g.R(w0), g.R(w1), a, b); }
		void Tri(const Rig& g, float x0, float y0, float x1, float y1, float x2, float y2, Color c) { gfx::Tri(g.P(x0, y0), g.P(x1, y1), g.P(x2, y2), c); }
		void Quad(const Rig& g, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Color c)
		{
			gfx::Quad(g.P(x0, y0), g.P(x1, y1), g.P(x2, y2), g.P(x3, y3), c);
		}
		void Quad4(const Rig& g, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Color a, Color b, Color c, Color d)
		{
			gfx::Quad4(g.P(x0, y0), g.P(x1, y1), g.P(x2, y2), g.P(x3, y3), a, b, c, d);
		}
		// 입체 공(어두운 바탕 + 밝은 면 + 하이라이트)
		void Ball(const Rig& g, float x, float y, float r, Color c)
		{
			Circ(g, x, y, r, Shade(c, 0.7f));
			Circ(g, x - r * 0.1f, y - r * 0.12f, r * 0.86f, c);
			Circ(g, x - r * 0.3f, y - r * 0.35f, r * 0.34f, Shade(c, 1.28f));
		}
		void Eye(const Rig& g, float x, float y, float r, Color white, Color pupil, float look = 0.35f)
		{
			Circ(g, x, y, r, white);
			Circ(g, x + r * look, y + r * 0.05f, r * 0.55f, pupil);
			Circ(g, x + r * look - r * 0.2f, y - r * 0.25f, r * 0.2f, Rgb(0xFFFFFF, 220));
		}
		void Glow(const Rig& g, float x, float y, float r, Color c)
		{
			gfx::CircleGrad(g.P(x, y), g.R(r), c, WithA(c, 0.f));
		}

		struct Walk
		{
			float sw = 0.f, lift0 = 0.f, lift1 = 0.f, bob = 0.f;
		};
		Walk WalkOf(const Pose& p)
		{
			Walk w;
			w.sw = std::sin(p.walk) * p.moving;
			w.lift0 = std::max(0.f, std::cos(p.walk)) * 3.f * p.moving;
			w.lift1 = std::max(0.f, -std::cos(p.walk)) * 3.f * p.moving;
			w.bob = -std::fabs(std::sin(p.walk)) * 1.6f * p.moving + std::sin(p.time * 2.2f + p.seed * 0.001f) * 0.6f * (1.f - p.moving);
			return w;
		}

		// 공격 동작 → (무기 각도, 손 위치, 몸 앞으로 쏠림)
		struct Swing { float angle; float hx, hy; float lean; };
		Swing SwingOf(float attack, float restAngle, float restX, float restY)
		{
			Swing s{ restAngle, restX, restY, 0.f };
			if (attack < 0.f) return s;
			const float t = util::Clamp01(attack);
			if (t < 0.28f)
			{
				const float u = util::EaseOutCubic(t / 0.28f);
				s.angle = Lerp(restAngle, -2.7f, u);
				s.hx = Lerp(restX, restX - 6.f, u);
				s.hy = Lerp(restY, restY - 16.f, u);
				s.lean = -2.f * u;
			}
			else if (t < 0.5f)
			{
				const float u = util::EaseInCubic((t - 0.28f) / 0.22f);
				s.angle = Lerp(-2.7f, 1.0f, u);
				s.hx = Lerp(restX - 6.f, restX + 7.f, u);
				s.hy = Lerp(restY - 16.f, restY + 3.f, u);
				s.lean = Lerp(-2.f, 4.f, u);
			}
			else
			{
				const float u = util::EaseInOutCubic((t - 0.5f) / 0.5f);
				s.angle = Lerp(1.0f, restAngle, u);
				s.hx = Lerp(restX + 7.f, restX, u);
				s.hy = Lerp(restY + 3.f, restY, u);
				s.lean = Lerp(4.f, 0.f, u);
			}
			return s;
		}

		Rig MakeRig(const Pose& p, float extraScale = 1.f)
		{
			Rig g;
			g.base = p.base;
			g.s = p.scale * extraScale;
			g.dir = p.faceRight ? 1.f : -1.f;
			if (p.dead > 0.f)
				g.rot = -util::EaseOutBounce(util::Clamp01(p.dead * 1.4f)) * 1.35f;
			g.ox = -p.hurt * 4.f;
			return g;
		}

		// 무기(로컬 각도 → 화면 각도 변환 포함)
		void Weapon(const Rig& g, uint16_t itemId, float hx, float hy, float localAngle, float scale, float time, int enchant)
		{
			if (itemId == 0) return;
			const Vector2 hilt = g.P(hx, hy);
			// 로컬 방향 벡터를 리그 변환으로 돌려 화면 각도를 구한다
			const Vector2 tip = g.P(hx + std::cos(localAngle) * 10.f, hy + std::sin(localAngle) * 10.f);
			const float ang = std::atan2(tip.y - hilt.y, tip.x - hilt.x);
			const Color glow = icons::EnchantGlow(enchant);
			if (glow.a > 0)
			{
				const float len = icons::WeaponLength(itemId) * scale * g.s;
				const float pulse = 0.75f + 0.25f * std::sin(time * 4.f);
				const Vector2 d{ std::cos(ang), std::sin(ang) };
				const Vector2 a{ hilt.x + d.x * len * 0.25f, hilt.y + d.y * len * 0.25f };
				const Vector2 b{ hilt.x + d.x * len * 1.05f, hilt.y + d.y * len * 1.05f };
				gfx::Additive(true);
				gfx::LineGrad(a, b, g.R(11.f) * scale, g.R(4.f) * scale, util::MulA(glow, pulse), util::MulA(glow, 0.2f));
				gfx::Additive(false);
			}
			icons::DrawWeapon(itemId, hilt, ang, scale * g.s, time);
		}

		// ── 플레이어(기사) ──────────────────────────────────────────────────────
		void Legs(const Rig& g, const Walk& w, Color pants, Color boot, float spread = 1.f)
		{
			// 뒷다리
			Limb(g, -3 * spread, -22, -3 * spread - w.sw * 7.f, -2.5f - w.lift1, 3.4f, Shade(pants, 0.78f));
			Ell(g, -3 * spread - w.sw * 7.f + 1.5f, -1.5f - w.lift1, 4.2f, 2.4f, Shade(boot, 0.8f));
			// 앞다리
			Limb(g, 4 * spread, -22, 4 * spread + w.sw * 7.f, -2.5f - w.lift0, 3.6f, pants);
			Ell(g, 4 * spread + w.sw * 7.f + 1.5f, -1.5f - w.lift0, 4.5f, 2.5f, boot);
		}

		void Cape(const Rig& g, const Pose& p, Color cloth, bool over)
		{
			const float flut = std::sin(p.time * 3.f + p.walk) * (1.5f + p.moving * 2.5f);
			const float back = p.moving * 4.f;
			if (!over)
			{
				Quad4(g, -9, -43, 7, -43, 4 - back + flut * 0.4f, -12, -14 - back + flut, -13,
					Shade(cloth, 0.9f), Shade(cloth, 0.75f), Shade(cloth, 0.55f), Shade(cloth, 0.65f));
			}
			else
			{
				Quad4(g, -10, -44, 10, -44, 12 + flut * 0.4f, -11, -12 + flut, -11,
					Shade(cloth, 1.05f), Shade(cloth, 0.9f), Shade(cloth, 0.7f), Shade(cloth, 0.8f));
				Seg(g, -10, -44, 10, -44, 3.f, Shade(cloth, 0.7f));
				Seg(g, -1 + flut * 0.3f, -40, -1 + flut * 0.6f, -14, 1.2f, Shade(cloth, 0.8f));
			}
		}

		void Torso(const Rig& g, const Walk& w, Color armor, Color trim, bool back)
		{
			const float b = w.bob;
			// 치마(튜닉)
			Quad4(g, -8, -26 + b, 8, -26 + b, 10, -16 + b, -10, -16 + b, Shade(trim, 0.9f), Shade(trim, 0.75f), Shade(trim, 0.6f), Shade(trim, 0.75f));
			Seg(g, -10, -16.5f + b, 10, -16.5f + b, 1.6f, Shade(trim, 0.5f));
			// 흉갑
			Quad4(g, -10, -45 + b, 10, -45 + b, 8, -25 + b, -8, -25 + b, Shade(armor, 1.15f), Shade(armor, 0.8f), Shade(armor, 0.7f), Shade(armor, 0.95f));
			if (!back)
			{
				Quad(g, -2, -43 + b, 6, -43 + b, 5, -31 + b, -1, -30 + b, WithA(Shade(armor, 1.45f), 0.55f));
				Seg(g, 2, -44 + b, 2, -27 + b, 1.2f, Shade(armor, 0.6f));
			}
			// 허리띠
			Seg(g, -8.5f, -26 + b, 8.5f, -26 + b, 3.f, Rgb(0x4A3020));
			Circ(g, 3, -26 + b, 1.8f, Rgb(0xE0C060));
			// 어깨 갑옷
			Ball(g, -8, -43 + b, 4.5f, Shade(armor, 0.9f));
		}

		void Head(const Rig& g, const Walk& w, const Pose& p, Color armor, Color plume, bool back)
		{
			const float b = w.bob;
			Circ(g, 0, -40 + b, 3.5f, Rgb(0xE8B890));  // 목
			// 투구
			Ball(g, 0, -53 + b, 9.f, armor);
			if (!back)
			{
				// 얼굴 가리개와 틈
				Quad(g, 1, -56 + b, 9.5f, -55 + b, 9, -47 + b, 1, -46 + b, Shade(armor, 0.85f));
				Seg(g, 3, -52.5f + b, 9, -52.5f + b, 1.8f, Rgb(0x1A1A22));
				Seg(g, 6, -51 + b, 6, -46 + b, 1.2f, Rgb(0x1A1A22));
			}
			// 깃털
			const float f = std::sin(p.time * 2.5f + p.walk * 0.5f) * 1.5f;
			SegG(g, -1, -62 + b, -10 + f, -64 + b, 4.f, 1.5f, plume, Shade(plume, 0.7f));
			SegG(g, -3, -61 + b, -12 + f, -58 + b, 3.f, 1.f, Shade(plume, 0.9f), Shade(plume, 0.6f));
			Circ(g, 0, -62.5f + b, 2.f, Rgb(0xE0C060));
		}

		void Shield(const Rig& g, float x, float y, Color face, Color rim, bool front)
		{
			Circ(g, x, y, 7.5f, rim);
			Circ(g, x, y, 6.2f, face);
			if (front)
			{
				Seg(g, x, y - 5, x, y + 5, 1.6f, rim);
				Seg(g, x - 5, y, x + 5, y, 1.6f, rim);
				Circ(g, x, y, 1.8f, Rgb(0xF2D870));
			}
		}
	}

	PlayerLook LookForPlayer(uint32_t nameHash, uint16_t weaponId, int enchant)
	{
		static const Color cloths[] = { Rgb(0x3A5AB0), Rgb(0xA83232), Rgb(0x2E7A4A), Rgb(0x6A3AA0), Rgb(0xB07A2A), Rgb(0x2A7A8A), Rgb(0x8A2A5A), Rgb(0x4A4A5A) };
		static const Color armors[] = { Rgb(0xA0AABA), Rgb(0xB8A888), Rgb(0x8A96A8), Rgb(0xC0C4CC), Rgb(0x9A8A7A) };
		PlayerLook look;
		look.weaponId = weaponId;
		look.enchant = enchant;
		look.cloth = cloths[nameHash % 8];
		look.armor = armors[(nameHash / 8) % 5];
		look.trim = Shade(look.cloth, 0.85f);
		return look;
	}

	void DrawShadow(Vector2 base, float rx, float alpha)
	{
		gfx::EllipseGrad(base, rx, rx * 0.45f, Rgb(0x000000, static_cast<uint8_t>(110 * alpha)), Rgb(0x000000, 0));
	}

	float PlayerHeight() { return 70.f; }

	void DrawPlayer(const Pose& p, const PlayerLook& look)
	{
		Rig g = MakeRig(p);
		const Walk w = WalkOf(p);
		const Swing sw = SwingOf(p.attack, -0.95f, 10.f, -31.f);
		g.ox += sw.lean * 0.6f;
		const Color armor = look.armor, cloth = look.cloth;
		const Color skin = Rgb(0xE8B890);
		const bool back = p.faceBack;

		// 시전 자세: 칼을 높이 든다
		float wAngle = sw.angle, hx = sw.hx, hy = sw.hy;
		if (p.cast > 0.f && p.attack < 0.f)
		{
			const float u = util::Pulse(p.cast);
			wAngle = Lerp(wAngle, -1.57f, u);
			hy = Lerp(hy, -52.f, u);
			hx = Lerp(hx, 4.f, u);
		}
		const float armSwing = -w.sw * 5.f;

		if (!back)
		{
			Cape(g, p, cloth, false);
			// 방패 팔(몸 뒤)
			Limb(g, -7, -41 + w.bob, -9 + armSwing, -30 + w.bob, 3.f, Shade(armor, 0.75f));
			Shield(g, -10 + armSwing, -31 + w.bob, Shade(cloth, 0.8f), Shade(armor, 0.7f), false);
			Legs(g, w, Rgb(0x4A4038), Rgb(0x3A2A1E));
			Torso(g, w, armor, look.trim, false);
			Head(g, w, p, armor, cloth, false);
			// 칼 든 팔(앞)
			Limb(g, 7, -41 + w.bob, hx, hy + w.bob, 3.2f, armor);
			Circ(g, hx, hy + w.bob, 3.f, skin);
			Weapon(g, look.weaponId, hx, hy + w.bob, wAngle, 0.78f, p.time, look.enchant);
			Circ(g, 7, -42 + w.bob, 4.5f, Shade(armor, 1.05f));
		}
		else
		{
			// 뒷모습: 무기 팔이 몸 너머로
			Limb(g, 7, -41 + w.bob, hx, hy + w.bob, 3.2f, Shade(armor, 0.75f));
			Weapon(g, look.weaponId, hx, hy + w.bob, wAngle, 0.78f, p.time, look.enchant);
			Legs(g, w, Rgb(0x4A4038), Rgb(0x3A2A1E));
			Torso(g, w, armor, look.trim, true);
			Head(g, w, p, armor, cloth, true);
			Cape(g, p, cloth, true);
			Limb(g, -7, -41 + w.bob, -9 + armSwing, -30 + w.bob, 3.f, armor);
			Shield(g, -10 + armSwing, -31 + w.bob, cloth, Shade(armor, 0.9f), true);
		}
	}

	// ── 몬스터 ──────────────────────────────────────────────────────────────────
	namespace
	{
		void Blob(const Rig& g0, const Pose& p, Color c1, Color c2, bool mud)
		{
			Rig g = g0;
			const float bounce = std::fabs(std::sin(p.walk)) * p.moving;
			const float idle = std::sin(p.time * 3.f + p.seed) * 0.06f;
			const float lunge = p.attack >= 0.f ? util::Pulse(p.attack) : 0.f;
			const float sx = 1.f + idle + lunge * 0.25f - bounce * 0.12f;
			const float sy = 1.f - idle + bounce * 0.2f - lunge * 0.15f;
			g.ox += lunge * 6.f;
			const float h = 26.f * sy, w = 18.f * sx;
			const float lift = bounce * 6.f;
			// 몸통(반투명 젤리)
			Vector2 pts[14];
			for (int i = 0; i < 14; ++i)
			{
				const float a = kPi + i * (kPi / 13.f);
				const float wob = std::sin(p.time * 5.f + i) * 0.8f;
				pts[i] = g.P(std::cos(a) * (w + wob), -lift + std::sin(a) * h);
			}
			const Color body = mud ? c1 : WithA(c1, 0.9f);
			gfx::PolyGrad(pts, 14, g.P(-3, -lift - h * 0.55f), Shade(body, 1.3f), Shade(body, 0.72f));
			Ell(g, 0, -lift, w, 4.f, Shade(c2, 0.9f));
			if (!mud)
			{
				Ell(g, -7, -lift - h * 0.72f, 5.f, 3.f, Rgb(0xFFFFFF, 150));
				Circ(g, 6, -lift - h * 0.35f, 2.2f, WithA(Shade(c1, 1.4f), 0.6f));
			}
			else
			{
				for (int i = 0; i < 4; ++i)
					Limb(g, -12.f + i * 7.f, -lift - 2, -12.f + i * 7.f, -lift + 2.f + (i % 2) * 2.f, 1.8f, Shade(c1, 0.7f));
				Ell(g, -4, -lift - h * 0.85f, 7.f, 2.5f, Rgb(0x6E8A3A));
			}
			// 눈
			if (!p.faceBack)
			{
				Eye(g, 4, -lift - h * 0.55f, 3.4f, Rgb(0xFFFFFF), Rgb(0x1A1A1A));
				Eye(g, 11, -lift - h * 0.5f, 3.f, Rgb(0xFFFFFF), Rgb(0x1A1A1A));
				Seg(g, 5, -lift - h * 0.3f, 10, -lift - h * 0.28f, 1.4f, Shade(c2, 0.5f));
			}
		}

		void Quadruped(const Rig& g0, const Pose& p, const MonsterTableRow& row, Color c1, Color c2)
		{
			Rig g = g0;
			const bool bear = row.id == 6;
			const bool hell = row.id == 27;
			const float lunge = p.attack >= 0.f ? util::Pulse(p.attack) : 0.f;
			g.ox += lunge * 7.f;
			const float gait = p.moving;
			const float ph = p.walk * 1.3f;
			const float bob = -std::fabs(std::sin(ph)) * 2.f * gait;
			const float len = bear ? 17.f : 16.f, bodyH = bear ? 11.f : 8.5f, legH = bear ? 13.f : 14.f;
			const float by = -legH - bodyH * 0.6f + bob;
			// 다리(대각 쌍)
			auto leg = [&](float x, float phase, bool nearSide)
			{
				const float s = std::sin(ph + phase) * gait;
				const float lift = std::max(0.f, std::cos(ph + phase)) * 3.f * gait;
				const Color lc = nearSide ? c1 : Shade(c1, 0.7f);
				Limb(g, x, by + 2, x + s * 6.f, -1.5f - lift, bear ? 3.8f : 2.6f, lc);
				Ell(g, x + s * 6.f + 1.f, -1.f - lift, bear ? 4.f : 3.f, 1.6f, Shade(c2, 0.8f));
			};
			leg(-len * 0.6f, kPi, false);
			leg(len * 0.55f, 0.f, false);
			// 꼬리
			const float tw = std::sin(p.time * 6.f) * 3.f;
			if (!bear)
				SegG(g, -len * 0.9f, by - 2, -len * 1.6f, by - 10 + tw, 4.f, 1.5f, c1, Shade(c1, 0.8f));
			// 몸통
			EllG(g, 0, by, len, bodyH, Shade(c1, 1.12f), Shade(c1, 0.8f));
			Ell(g, 1, by + bodyH * 0.45f, len * 0.75f, bodyH * 0.35f, Shade(c2, 1.1f));
			if (hell)
			{
				for (int i = 0; i < 6; ++i)
				{
					const float x = -len * 0.6f + i * len * 0.3f;
					const float fh = 6.f + 3.f * std::sin(p.time * 12.f + i * 1.9f);
					Tri(g, x - 3, by - bodyH * 0.8f, x + 3, by - bodyH * 0.8f, x - 1, by - bodyH * 0.8f - fh, WithA(Rgb(0xFF8A2A), 0.9f));
				}
			}
			leg(-len * 0.45f, 0.f, true);
			leg(len * 0.7f, kPi, true);
			// 머리
			const float hx = len * 1.05f, hy = by - bodyH * 0.7f;
			Limb(g, len * 0.6f, by - 2, hx, hy, bear ? 7.f : 5.f, c1);
			Ball(g, hx, hy, bear ? 8.5f : 7.f, c1);
			// 주둥이
			Ell(g, hx + (bear ? 7.f : 8.f), hy + 2.f, bear ? 5.f : 6.f, 3.5f, Shade(c2, 1.15f));
			Circ(g, hx + (bear ? 11.f : 13.5f), hy + 1.f, 1.8f, Rgb(0x1A1414));
			if (p.attack >= 0.f && lunge > 0.3f)
			{
				Tri(g, hx + 5, hy + 4, hx + 13, hy + 4.5f, hx + 9, hy + 8.f, Rgb(0x3A0A0A));
				Tri(g, hx + 7, hy + 4.5f, hx + 9, hy + 4.5f, hx + 8, hy + 6.5f, Rgb(0xFFFFFF));
			}
			// 귀
			if (bear)
			{
				Circ(g, hx - 4, hy - 7, 3.f, Shade(c1, 0.8f));
				Circ(g, hx + 2, hy - 8, 3.f, c1);
			}
			else
			{
				Tri(g, hx - 5, hy - 4, hx - 1, hy - 5, hx - 4, hy - 13, Shade(c1, 0.8f));
				Tri(g, hx, hy - 5, hx + 4, hy - 4, hx + 1, hy - 13, c1);
			}
			// 눈
			const Color eye = hell ? Rgb(0xFFD040) : Rgb(0x1A1A1A);
			Circ(g, hx + 3.5f, hy - 1.5f, hell ? 1.8f : 1.5f, eye);
			if (hell)
			{
				Glow(g, hx + 3.5f, hy - 1.5f, 6.f, Rgb(0xFF8A20, 200));
				props::FrameLights().push_back({ g.P(0, 0), 90.f * g.s, Rgb(0xFF6A20), 0.7f });
			}
		}

		void Humanoid(const Rig& g0, const Pose& p, const MonsterTableRow& row, Color c1, Color c2)
		{
			Rig g = g0;
			const Walk w = WalkOf(p);
			const bool goblin = row.id == 2, kobold = row.id == 8, orc = row.id == 12 || row.id == 30, zombie = row.variant == MonsterVariant::eZombie;
			const bool chief = row.id == 30;
			const Color skin = c1;
			const Color gear = c2;
			const Swing sw = SwingOf(p.attack, zombie ? 0.2f : -0.7f, 9.f, zombie ? -34.f : -30.f);
			g.ox += sw.lean * 0.5f;
			if (zombie) g.ox += 2.f;
			const float hunch = zombie ? 5.f : (goblin ? 2.f : 0.f);

			// 무기 종류
			uint16_t weapon = 0;
			if (goblin) weapon = 2;
			else if (orc) weapon = chief ? 18 : 6;
			else if (kobold) weapon = 3;

			// 뒤 팔
			Limb(g, -6 + hunch, -40 + w.bob, -8 - w.sw * 5.f + (zombie ? 14.f : 0.f), (zombie ? -38.f : -29.f) + w.bob, 3.f, Shade(skin, 0.75f));
			Legs(g, w, Shade(gear, 0.8f), Shade(gear, 0.55f), orc ? 1.3f : 1.f);
			// 몸
			const float wide = orc ? 1.35f : (goblin ? 0.85f : 1.f);
			Quad4(g, -9 * wide + hunch, -44 + w.bob, 9 * wide + hunch, -44 + w.bob, 8 * wide, -22 + w.bob, -8 * wide, -22 + w.bob,
				Shade(skin, 1.1f), Shade(skin, 0.8f), Shade(skin, 0.7f), Shade(skin, 0.9f));
			// 옷/갑옷
			if (row.variant == MonsterVariant::eArmored)
			{
				Quad4(g, -9.5f * wide + hunch, -43 + w.bob, 9.5f * wide + hunch, -43 + w.bob, 8.5f * wide, -28 + w.bob, -8.5f * wide, -28 + w.bob,
					Shade(gear, 1.2f), Shade(gear, 0.85f), Shade(gear, 0.75f), Shade(gear, 1.f));
				Ball(g, -8 * wide + hunch, -42 + w.bob, 5.f, Shade(gear, 0.9f));
				Ball(g, 8 * wide + hunch, -42 + w.bob, 5.f, gear);
			}
			Quad4(g, -8 * wide, -26 + w.bob, 8 * wide, -26 + w.bob, 9 * wide, -17 + w.bob, -9 * wide, -17 + w.bob,
				Shade(gear, 0.9f), Shade(gear, 0.7f), Shade(gear, 0.6f), Shade(gear, 0.8f));
			if (zombie)
			{
				Seg(g, -6, -30 + w.bob, 2, -36 + w.bob, 1.5f, Rgb(0x6A2020, 200));
				Tri(g, 5, -18 + w.bob, 9, -18 + w.bob, 7, -12 + w.bob, Shade(gear, 0.6f));
			}
			// 머리
			const float hx = 1 + hunch * 1.4f, hy = -52 + w.bob + hunch * 0.6f;
			const float hr = goblin ? 8.5f : (orc ? 9.5f : 8.f);
			if (goblin)
			{
				Tri(g, hx - 6, hy - 1, hx - 3, hy - 4, hx - 17, hy - 8, Shade(skin, 0.8f));
				Tri(g, hx + 4, hy - 4, hx + 7, hy, hx + 17, hy - 9, skin);
			}
			Ball(g, hx, hy, hr, skin);
			if (kobold)
			{
				Ell(g, hx + 8, hy + 2, 6.f, 3.5f, Shade(skin, 1.1f));
				Circ(g, hx + 13, hy + 1, 1.5f, Rgb(0x1A1414));
				Tri(g, hx - 5, hy - 5, hx - 1, hy - 7, hx - 6, hy - 15, Shade(skin, 0.8f));
			}
			if (!p.faceBack)
			{
				const Color eye = zombie ? Rgb(0xE0E890) : (orc ? Rgb(0xFFD040) : Rgb(0xFFF0A0));
				Circ(g, hx + 4, hy - 1.5f, 1.8f, eye);
				Circ(g, hx + 7.5f, hy - 1.f, 1.5f, eye);
				Circ(g, hx + 4.5f, hy - 1.5f, 0.8f, Rgb(0x1A1A1A));
				if (orc)
				{
					Tri(g, hx + 3, hy + 4, hx + 5, hy + 4, hx + 4, hy + 1, Rgb(0xF2EEDC));
					Tri(g, hx + 7, hy + 4, hx + 9, hy + 3.5f, hx + 8, hy + 0.5f, Rgb(0xF2EEDC));
				}
				else
					Seg(g, hx + 3, hy + 3.5f, hx + 8, hy + 3.f, 1.3f, Shade(skin, 0.45f));
			}
			if (row.variant == MonsterVariant::eArmored)
			{
				// 투구
				Quad(g, hx - hr, hy - 2, hx + hr, hy - 2, hx + hr * 0.8f, hy - hr - 2, hx - hr * 0.8f, hy - hr - 2, Shade(gear, 1.1f));
				if (chief)
				{
					Tri(g, hx - 7, hy - hr - 1, hx - 3, hy - hr - 1, hx - 12, hy - hr - 12, Rgb(0xF0E6C8));
					Tri(g, hx + 3, hy - hr - 1, hx + 7, hy - hr - 1, hx + 12, hy - hr - 12, Rgb(0xF0E6C8));
				}
			}
			// 앞 팔 + 무기
			const float fx = zombie ? sw.hx + 12.f : sw.hx, fy = sw.hy;
			Limb(g, 6 * wide + hunch, -41 + w.bob, fx, fy + w.bob, 3.2f, skin);
			Circ(g, fx, fy + w.bob, 3.f, Shade(skin, 1.05f));
			if (weapon != 0)
				Weapon(g, weapon, fx, fy + w.bob, sw.angle, chief ? 0.95f : 0.75f, p.time, chief ? 9 : 0);
			if (chief)
			{
				// 대족장 망토
				const float fl = std::sin(p.time * 2.f) * 2.f;
				Quad4(g, -12, -44, -4, -44, -8 + fl, -10, -18 + fl, -12, Rgb(0x8A2A20), Rgb(0x6A1A14), Rgb(0x4A1010), Rgb(0x6A1A14));
			}
		}

		void Insect(const Rig& g0, const Pose& p, const MonsterTableRow& row, Color c1, Color c2)
		{
			Rig g = g0;
			const bool scorpion = row.variant == MonsterVariant::eScorpion;
			const bool spider = row.variant == MonsterVariant::eSpider;
			const bool queen = row.id == 32;
			const float lunge = p.attack >= 0.f ? util::Pulse(p.attack) : 0.f;
			g.ox += lunge * 5.f;
			const float ph = p.walk * 1.8f;
			const float gait = p.moving;
			const float by = -9.f;
			const int legs = spider ? 8 : 6;
			// 다리
			for (int i = 0; i < legs; ++i)
			{
				const int side = i % 2;
				const float lx = -8.f + (i / 2) * (spider ? 5.f : 7.f);
				const float s = std::sin(ph + i * 1.3f) * gait * 3.f;
				const float lift = std::max(0.f, std::cos(ph + i * 1.3f)) * 2.f * gait;
				const Color lc = side ? c2 : Shade(c2, 0.7f);
				const float kneeY = by - (spider ? 12.f : 7.f);
				const float kx = lx + (side ? 4.f : -2.f);
				Seg(g, lx, by, kx + s, kneeY, 2.f, lc);
				Seg(g, kx + s, kneeY, lx + (side ? 9.f : -6.f) + s * 1.5f, -1.f - lift, 1.6f, lc);
			}
			if (spider)
			{
				EllG(g, -9, by - 5, 12.f, 10.f, Shade(c1, 1.2f), Shade(c1, 0.75f));
				Tri(g, -12, by - 10, -6, by - 10, -9, by - 2, Shade(c2, 1.2f));
				Ball(g, 5, by - 4, 6.5f, c1);
				for (int i = 0; i < 4; ++i)
					Circ(g, 8.f + (i % 2) * 2.5f, by - 6.f + (i / 2) * 2.5f, 1.2f, Rgb(0xFF3A3A));
				Seg(g, 10, by - 1, 12, by + 3, 1.4f, Shade(c2, 1.3f));
				return;
			}
			if (scorpion)
			{
				// 꼬리(말려 올라감)
				const float tw = std::sin(p.time * 3.f) * 2.f + lunge * 10.f;
				float tx = -10.f, ty = by - 2.f;
				for (int i = 0; i < 5; ++i)
				{
					const float a = kPi + 0.2f + i * 0.45f;
					const float nx = tx + std::cos(a) * 5.f + (i == 4 ? tw * 0.6f : 0.f);
					const float ny = ty + std::sin(a) * 5.f - 3.f;
					Ball(g, nx, ny, 3.6f - i * 0.3f, i % 2 ? c1 : Shade(c1, 0.9f));
					tx = nx; ty = ny;
				}
				Tri(g, tx + 1, ty - 1, tx + 7 + tw * 0.3f, ty + 3, tx + 2, ty + 3, Rgb(0x3A1A10));
				EllG(g, -2, by, 12.f, 5.5f, Shade(c1, 1.15f), Shade(c1, 0.75f));
				for (int i = 0; i < 3; ++i)
					Seg(g, -9.f + i * 6.f, by - 4.f, -8.f + i * 6.f, by + 4.f, 1.f, Shade(c1, 0.6f));
				// 집게
				const float snap = lunge * 4.f;
				Limb(g, 7, by, 14, by - 3, 2.2f, c1);
				Ell(g, 17, by - 4, 5.f, 3.2f, Shade(c1, 1.1f));
				Tri(g, 19, by - 6, 26 + snap, by - 8 - snap, 21, by - 4, Shade(c1, 0.9f));
				Tri(g, 19, by - 2, 26 + snap, by, 21, by - 4, Shade(c1, 0.8f));
				Circ(g, 9, by - 4, 1.2f, Rgb(0x1A1A1A));
				return;
			}
			// 개미(여왕 포함)
			EllG(g, -12, by - 3, queen ? 13.f : 8.f, queen ? 9.f : 6.f, Shade(c1, 1.15f), Shade(c1, 0.7f));
			if (queen)
			{
				for (int i = 0; i < 4; ++i)
					Seg(g, -22.f + i * 5.f, by - 10.f, -21.f + i * 5.f, by + 4.f, 1.2f, Shade(c2, 0.9f));
				const float flap = std::sin(p.time * 18.f) * 3.f;
				Ell(g, -4, by - 14 - flap, 12.f, 4.f, Rgb(0xE0F0FF, 120));
				Ell(g, -2, by - 12 + flap, 11.f, 3.5f, Rgb(0xE0F0FF, 100));
			}
			Ball(g, 0, by - 2, 5.f, c1);
			Ball(g, 9, by - 5, queen ? 6.f : 5.f, c1);
			const float mj = lunge * 3.f;
			Seg(g, 13, by - 3, 18 + mj, by - 1, 1.6f, Shade(c2, 1.2f));
			Seg(g, 13, by - 2, 17 + mj, by + 2, 1.6f, Shade(c2, 1.2f));
			Seg(g, 10, by - 10, 15, by - 15 + std::sin(p.time * 7.f), 1.f, c2);
			Seg(g, 8, by - 10, 10, by - 16 + std::sin(p.time * 7.f + 1.f), 1.f, Shade(c2, 0.8f));
			Circ(g, 11, by - 6, 1.3f, queen ? Rgb(0xF0C040) : Rgb(0x1A1A1A));
			if (queen)
				for (int i = 0; i < 3; ++i)
					Tri(g, 6.f + i * 3.f, by - 10, 8.f + i * 3.f, by - 10, 7.f + i * 3.f, by - 14, Rgb(0xF0C040));
		}

		void Golem(const Rig& g0, const Pose& p, const MonsterTableRow& row, Color c1, Color c2)
		{
			Rig g = g0;
			const Walk w = WalkOf(p);
			const bool ice = row.variant == MonsterVariant::eIce, lava = row.variant == MonsterVariant::eLava;
			const float lunge = p.attack >= 0.f ? util::Pulse(p.attack) : 0.f;
			g.ox += lunge * 4.f;
			const float stomp = w.bob * 1.5f;
			// 다리
			Quad(g, -9 - w.sw * 3.f, -22, -3 - w.sw * 3.f, -22, -3 - w.sw * 5.f, 0, -10 - w.sw * 5.f, 0, Shade(c1, 0.7f));
			Quad(g, 3 + w.sw * 3.f, -22, 10 + w.sw * 3.f, -22, 11 + w.sw * 5.f, 0, 3 + w.sw * 5.f, 0, Shade(c1, 0.85f));
			// 몸통(바위 덩어리)
			Vector2 torso[6] = { g.P(-15, -24 + stomp), g.P(-17, -44 + stomp), g.P(-8, -52 + stomp), g.P(10, -52 + stomp), g.P(17, -42 + stomp), g.P(14, -22 + stomp) };
			gfx::PolyGrad(torso, 6, g.P(-4, -42 + stomp), Shade(c1, 1.2f), Shade(c1, 0.72f));
			Seg(g, -8, -44 + stomp, -2, -34 + stomp, 1.5f, Shade(c1, 0.55f));
			Seg(g, 6, -48 + stomp, 10, -38 + stomp, 1.5f, Shade(c1, 0.55f));
			// 머리
			Quad(g, -4, -52 + stomp, 7, -52 + stomp, 6, -61 + stomp, -3, -61 + stomp, Shade(c1, 1.05f));
			const Color eye = lava ? Rgb(0xFFD040) : (ice ? Rgb(0x9AF0FF) : Rgb(0xFFE070));
			Circ(g, 2, -57 + stomp, 1.6f, eye);
			Circ(g, 5.5f, -57 + stomp, 1.4f, eye);
			Glow(g, 3.5f, -57 + stomp, 6.f, WithA(eye, 0.6f));
			// 팔(큰 주먹) — 공격 때 내려친다
			const float armDown = lunge * 16.f;
			Limb(g, -14, -46 + stomp, -18 - w.sw * 4.f, -28 + stomp, 5.f, Shade(c1, 0.75f));
			Ball(g, -19 - w.sw * 4.f, -24 + stomp, 7.5f, Shade(c1, 0.8f));
			Limb(g, 14, -46 + stomp, 20 + armDown * 0.5f, -30 + stomp + armDown * 0.6f, 5.5f, c1);
			Ball(g, 22 + armDown * 0.5f, -26 + stomp + armDown * 0.8f, 8.f, c1);
			if (ice)
			{
				for (int i = 0; i < 4; ++i)
				{
					const float x = -12.f + i * 7.f;
					Tri(g, x - 3, -48 + stomp, x + 3, -48 + stomp, x + (i - 1.5f), -60 - (i % 2) * 5.f + stomp, WithA(Rgb(0xE8FAFF), 0.9f));
				}
			}
			if (lava)
			{
				const float pulse = 0.7f + 0.3f * std::sin(p.time * 4.f);
				const Color hot = WithA(Rgb(0xFFA030), pulse);
				Seg(g, -10, -30 + stomp, -4, -42 + stomp, 2.f, hot);
				Seg(g, -4, -42 + stomp, 4, -38 + stomp, 2.f, hot);
				Seg(g, 4, -38 + stomp, 9, -48 + stomp, 2.f, hot);
				Seg(g, 8, -28 + stomp, 12, -36 + stomp, 1.6f, hot);
				props::FrameLights().push_back({ g.P(0, 0), 120.f * g.s, Rgb(0xFF7020), 0.9f * pulse });
			}
			if (row.id == 19)
				Ell(g, -6, -50 + stomp, 6.f, 2.5f, Rgb(0x6E8A3A));
			(void)c2;
		}

		void Ghost(const Rig& g0, const Pose& p, const MonsterTableRow& row, Color c1, Color c2)
		{
			Rig g = g0;
			const float hover = std::sin(p.time * 2.2f + p.seed) * 3.f;
			const float lunge = p.attack >= 0.f ? util::Pulse(p.attack) : 0.f;
			g.ox += lunge * 8.f;
			const float y0 = -30.f + hover;
			const bool wraith = row.id == 18;
			const float alpha = 0.72f + 0.18f * std::sin(p.time * 3.f);
			// 꼬리(물결)
			Vector2 pts[16];
			for (int i = 0; i < 8; ++i)
			{
				const float t = i / 7.f;
				const float wave = std::sin(p.time * 5.f - t * 5.f) * 3.f * t;
				pts[i] = g.P(-10.f + t * 3.f - t * 10.f + wave, y0 + t * 24.f);
				pts[15 - i] = g.P(10.f - t * 10.f - t * 6.f + wave, y0 + t * 22.f);
			}
			gfx::PolyGrad(pts, 16, g.P(0, y0 + 4), WithA(Shade(c1, 1.2f), alpha), WithA(c2, alpha * 0.2f));
			// 머리
			EllG(g, 0, y0 - 4, 11.f, 12.f, WithA(Shade(c1, 1.3f), alpha), WithA(c1, alpha * 0.8f));
			if (wraith)
			{
				// 두건
				Quad(g, -12, y0 + 2, 12, y0 + 2, 8, y0 - 16, -9, y0 - 15, WithA(Shade(c2, 0.8f), alpha));
				Ell(g, 3, y0 - 4, 7.f, 7.f, Rgb(0x0A0610, 230));
				Limb(g, 6, y0 + 4, 16 + lunge * 6.f, y0 + 6, 2.f, WithA(c2, alpha));
				for (int i = 0; i < 3; ++i)
					Seg(g, 16 + lunge * 6.f, y0 + 6, 21 + lunge * 6.f, y0 + 4.f + i * 2.5f, 1.f, WithA(Rgb(0xE8E0FF), alpha));
			}
			const Color eye = wraith ? Rgb(0xE040FF) : Rgb(0x1A4A2A);
			if (!p.faceBack)
			{
				Ell(g, 2, y0 - 5, 2.f, 3.f, eye);
				Ell(g, 7, y0 - 5, 1.8f, 2.8f, eye);
				if (!wraith) Ell(g, 5, y0 + 1, 2.5f, 1.5f, WithA(eye, 0.8f));
			}
			Glow(g, 0, y0, 26.f, WithA(c1, 0.25f));
			props::FrameLights().push_back({ g.P(0, 0), 80.f * g.s, c1, 0.6f });
		}

		void Dragon(const Rig& g0, const Pose& p, const MonsterTableRow& row, Color c1, Color c2)
		{
			Rig g = g0;
			const bool boss = row.boss;
			const float lunge = p.attack >= 0.f ? util::Pulse(p.attack) : 0.f;
			g.ox += lunge * 6.f;
			const float ph = p.walk * 1.2f, gait = p.moving;
			const float flap = std::sin(p.time * (boss ? 3.f : 5.f));
			const float by = -20.f;
			// 꼬리
			SegG(g, -12, by, -30, by + 6 + std::sin(p.time * 3.f) * 3.f, 7.f, 2.f, Shade(c1, 0.85f), Shade(c1, 0.7f));
			Tri(g, -30, by + 4, -36, by + 3 + std::sin(p.time * 3.f) * 3.f, -30, by + 10, Shade(c2, 0.9f));
			// 먼 쪽 날개
			Tri(g, -4, by - 8, -22, by - 34 - flap * 8.f, 8, by - 26 - flap * 6.f, WithA(Shade(c2, 0.6f), 0.95f));
			// 다리
			for (int i = 0; i < 4; ++i)
			{
				const float x = (i < 2) ? -8.f : 8.f;
				const float s = std::sin(ph + i * kPi * 0.5f) * gait * 4.f;
				Limb(g, x + (i % 2) * 2.f, by + 4, x + s + (i % 2) * 2.f, -1.5f, 3.2f, (i % 2) ? c1 : Shade(c1, 0.7f));
			}
			// 몸통
			EllG(g, 0, by, 15.f, 10.f, Shade(c1, 1.15f), Shade(c1, 0.75f));
			Ell(g, 2, by + 5, 11.f, 4.f, Shade(c2, 1.3f));
			for (int i = 0; i < 4; ++i)
				Tri(g, -10.f + i * 5.f, by - 9.f, -7.f + i * 5.f, by - 9.f, -9.f + i * 5.f, by - 14.f, Shade(c2, 1.1f));
			// 목/머리
			const float nx = 16.f, ny = by - 16.f - lunge * 3.f;
			SegG(g, 8, by - 4, nx, ny, 9.f, 6.f, c1, c1);
			Ball(g, nx + 3, ny - 2, 7.f, c1);
			Ell(g, nx + 11, ny, 7.f, 3.8f, Shade(c1, 0.95f));
			Tri(g, nx - 1, ny - 7, nx + 2, ny - 8, nx - 8, ny - 17, Rgb(0xE8DCC0));
			Tri(g, nx + 3, ny - 8, nx + 6, ny - 7, nx + 2, ny - 18, Rgb(0xF2E8D0));
			Circ(g, nx + 6, ny - 4, 1.7f, Rgb(0xFFD040));
			if (lunge > 0.2f)
			{
				Tri(g, nx + 10, ny + 2, nx + 18, ny + 2, nx + 14, ny + 7, Rgb(0x3A0A0A));
				Glow(g, nx + 20, ny + 1, 10.f * lunge, Rgb(0xFF8A2A, 220));
			}
			// 가까운 쪽 날개
			Vector2 wing[4] = { g.P(-2, by - 6), g.P(-14, by - 38 - flap * 10.f), g.P(4, by - 34 - flap * 8.f), g.P(12, by - 10) };
			gfx::PolyGrad(wing, 4, g.P(0, by - 20), WithA(Shade(c2, 1.1f), 0.95f), WithA(Shade(c2, 0.7f), 0.95f));
			Seg(g, -2, by - 6, -14, by - 38 - flap * 10.f, 2.f, Shade(c1, 0.8f));
			Seg(g, -14, by - 38 - flap * 10.f, 4, by - 34 - flap * 8.f, 1.5f, Shade(c1, 0.8f));
			if (boss)
			{
				props::FrameLights().push_back({ g.P(0, 0), 170.f * g.s, Rgb(0xFF7A2A), 0.8f });
				Glow(g, 2, by + 4, 14.f, Rgb(0xFFB040, 90));
			}
		}

		void Demon(const Rig& g0, const Pose& p, const MonsterTableRow& row, Color c1, Color c2)
		{
			Rig g = g0;
			const Walk w = WalkOf(p);
			const bool baphomet = row.id == 34;
			const Swing sw = SwingOf(p.attack, -0.6f, 10.f, -30.f);
			g.ox += sw.lean * 0.5f;
			const float flap = std::sin(p.time * 2.5f);
			// 날개(뒤)
			const float ww = baphomet ? 1.4f : 0.8f;
			Vector2 wl[4] = { g.P(-4, -44), g.P(-26 * ww, -66 - flap * 6.f), g.P(-30 * ww, -40), g.P(-8, -34) };
			gfx::PolyGrad(wl, 4, g.P(-12, -46), Shade(c2, 1.3f), Shade(c2, 0.8f));
			Seg(g, -4, -44, -26 * ww, -66 - flap * 6.f, 2.f, Shade(c1, 0.6f));
			// 다리(염소 다리)
			Limb(g, -4, -24, -6 - w.sw * 6.f, -12, 3.6f, Shade(c1, 0.7f));
			Limb(g, -6 - w.sw * 6.f, -12, -3 - w.sw * 6.f, -2 - w.lift1, 2.6f, Shade(c1, 0.7f));
			Limb(g, 4, -24, 6 + w.sw * 6.f, -12, 3.8f, c1);
			Limb(g, 6 + w.sw * 6.f, -12, 9 + w.sw * 6.f, -2 - w.lift0, 2.8f, c1);
			Ell(g, -3 - w.sw * 6.f, -1.5f - w.lift1, 3.f, 1.8f, Rgb(0x1A1414));
			Ell(g, 9 + w.sw * 6.f, -1.5f - w.lift0, 3.f, 1.8f, Rgb(0x1A1414));
			// 몸통
			Quad4(g, -11, -48 + w.bob, 11, -48 + w.bob, 8, -22 + w.bob, -8, -22 + w.bob, Shade(c1, 1.15f), Shade(c1, 0.8f), Shade(c1, 0.65f), Shade(c1, 0.9f));
			Seg(g, -6, -40 + w.bob, 6, -40 + w.bob, 1.2f, Shade(c1, 0.6f));
			Seg(g, -5, -34 + w.bob, 5, -34 + w.bob, 1.2f, Shade(c1, 0.6f));
			// 머리
			const float hy = -57 + w.bob;
			if (baphomet)
			{
				Ball(g, 2, hy, 9.f, Shade(c1, 1.1f));
				Ell(g, 9, hy + 4, 5.f, 4.f, Shade(c1, 0.9f));
				Tri(g, 6, hy + 7, 11, hy + 7, 8, hy + 14, Rgb(0xD8D0C0));   // 수염
				// 말린 뿔
				for (int side = 0; side < 2; ++side)
				{
					const float sx = side ? 4.f : -4.f;
					for (int i = 0; i < 6; ++i)
					{
						const float a = -kPi * 0.5f + i * 0.55f * (side ? 1.f : -1.f);
						const float r = 8.f + i * 1.5f;
						Circ(g, sx + std::cos(a) * r * 0.7f * (side ? 1.f : -1.f), hy - 6 + std::sin(a) * r * 0.6f, 3.2f - i * 0.3f, Rgb(0xD8C8A8));
					}
				}
			}
			else
			{
				Ball(g, 1, hy, 8.f, c1);
				Tri(g, -5, hy - 5, -1, hy - 7, -8, hy - 16, Rgb(0x2A1A1A));
				Tri(g, 3, hy - 7, 7, hy - 5, 8, hy - 17, Rgb(0x2A1A1A));
			}
			if (!p.faceBack)
			{
				Circ(g, 5, hy - 1, 1.8f, Rgb(0xFFE040));
				Circ(g, 9, hy - 1, 1.6f, Rgb(0xFFE040));
				Glow(g, 7, hy - 1, 7.f, Rgb(0xFF4020, 160));
			}
			// 팔 + 무기
			Limb(g, -8, -44 + w.bob, -10 - w.sw * 4.f, -30 + w.bob, 3.f, Shade(c1, 0.75f));
			Limb(g, 8, -44 + w.bob, sw.hx, sw.hy + w.bob, 3.4f, c1);
			Weapon(g, baphomet ? 17 : 5, sw.hx, sw.hy + w.bob, sw.angle, baphomet ? 1.05f : 0.8f, p.time, baphomet ? 12 : 0);
			if (baphomet)
				props::FrameLights().push_back({ g.P(0, -30), 200.f * g.s, Rgb(0xFF3A3A), 0.7f });
		}

		void Crab(const Rig& g0, const Pose& p, Color c1, Color c2)
		{
			Rig g = g0;
			const float lunge = p.attack >= 0.f ? util::Pulse(p.attack) : 0.f;
			const float ph = p.walk * 2.f, gait = p.moving;
			const float by = -10.f + std::sin(ph) * gait * 1.f;
			for (int i = 0; i < 6; ++i)
			{
				const float side = (i < 3) ? -1.f : 1.f;
				const float lx = side * (8.f + (i % 3) * 3.f);
				const float s = std::sin(ph + i) * gait * 2.f;
				Seg(g, lx * 0.6f, by, lx + side * 4.f + s, by - 6, 2.f, Shade(c1, 0.8f));
				Seg(g, lx + side * 4.f + s, by - 6, lx + side * 8.f + s, -1, 1.6f, Shade(c1, 0.8f));
			}
			EllG(g, 0, by - 3, 16.f, 9.f, Shade(c1, 1.2f), Shade(c1, 0.75f));
			for (int i = 0; i < 4; ++i)
				Circ(g, -9.f + i * 6.f, by - 7.f, 1.6f, Shade(c1, 1.35f));
			Ell(g, 0, by + 3, 12.f, 3.f, Shade(c2, 1.4f));
			// 눈자루
			Seg(g, 2, by - 10, 1, by - 17, 1.4f, c1);
			Seg(g, 7, by - 10, 8, by - 17, 1.4f, c1);
			Circ(g, 1, by - 18, 2.f, Rgb(0x1A1A1A));
			Circ(g, 8, by - 18, 2.f, Rgb(0x1A1A1A));
			// 집게(공격 때 들어 올려 꽉)
			const float lift = lunge * 10.f + std::sin(p.time * 2.f) * 1.5f;
			for (int side = 0; side < 2; ++side)
			{
				const float x = side ? 15.f : -13.f;
				Limb(g, x * 0.7f, by - 2, x, by - 8 - lift, 2.5f, c1);
				Ell(g, x + 2, by - 11 - lift, 6.f, 4.f, Shade(c1, 1.1f));
				Tri(g, x + 4, by - 14 - lift, x + 11 + lunge * 2.f, by - 17 - lift - lunge * 3.f, x + 6, by - 11 - lift, Shade(c1, 0.9f));
			}
			(void)c2;
		}

		void Mushroom(const Rig& g0, const Pose& p, Color c1, Color c2)
		{
			Rig g = g0;
			const float hop = std::fabs(std::sin(p.walk * 1.5f)) * 5.f * p.moving;
			const float lunge = p.attack >= 0.f ? util::Pulse(p.attack) : 0.f;
			g.ox += lunge * 6.f;
			const float squash = 1.f + std::sin(p.time * 4.f) * 0.04f;
			// 발
			Ell(g, -5, -1.5f - hop * 0.3f, 3.5f, 2.f, Shade(c2, 0.6f));
			Ell(g, 6, -1.5f - hop * 0.3f, 3.5f, 2.f, Shade(c2, 0.7f));
			// 줄기(몸)
			Quad4(g, -8, -2 - hop, 8, -2 - hop, 7, -20 - hop, -7, -20 - hop, Shade(c2, 1.05f), Shade(c2, 0.8f), Shade(c2, 0.85f), Shade(c2, 1.1f));
			if (!p.faceBack)
			{
				Circ(g, 2, -12 - hop, 1.7f, Rgb(0x1A1A1A));
				Circ(g, 6, -12 - hop, 1.5f, Rgb(0x1A1A1A));
				Seg(g, 3, -8 - hop, 6, -8 - hop, 1.2f, Rgb(0x6A3A2A));
			}
			// 갓
			const float cy = -21.f - hop - lunge * 3.f;
			Vector2 cap[11];
			for (int i = 0; i < 11; ++i)
			{
				const float a = kPi + i * (kPi / 10.f);
				cap[i] = g.P(std::cos(a) * 17.f * squash, cy + std::sin(a) * 14.f / squash);
			}
			gfx::PolyGrad(cap, 11, g.P(-4, cy - 8), Shade(c1, 1.25f), Shade(c1, 0.8f));
			Ell(g, 0, cy, 17.f * squash, 3.5f, Shade(c1, 0.6f));
			for (int i = 0; i < 5; ++i)
			{
				const float a = kPi * (1.15f + i * 0.17f);
				Circ(g, std::cos(a) * 10.f, cy + std::sin(a) * 8.f - 1.f, 2.2f + (i % 2), Rgb(0xFFF6E6));
			}
		}

		void Skeleton(const Rig& g0, const Pose& p, const MonsterTableRow& row, Color c1, Color c2)
		{
			Rig g = g0;
			const Walk w = WalkOf(p);
			const bool armored = row.variant == MonsterVariant::eArmored;
			const bool dk = row.id == 31;
			const Swing sw = SwingOf(p.attack, -0.8f, 10.f, -31.f);
			g.ox += sw.lean * 0.5f;
			const Color bone = armored ? Rgb(0xD8D2C0) : c1;
			const Color armor = armored ? c1 : Rgb(0x6A5A4A);
			// 망토(갑옷형)
			if (armored)
			{
				const float fl = std::sin(p.time * 2.5f + p.walk) * 2.f;
				Quad4(g, -9, -44, 7, -44, 3 + fl * 0.4f, -8, -15 + fl, -9, Shade(c2, 0.55f), Shade(c2, 0.4f), Shade(c2, 0.3f), Shade(c2, 0.4f));
			}
			// 뒤 팔
			Seg(g, -6, -41 + w.bob, -8 - w.sw * 5.f, -30 + w.bob, 2.f, Shade(bone, 0.75f));
			if (!armored)
				Shield(g, -10 - w.sw * 5.f, -30 + w.bob, Rgb(0x6A4A2A), Rgb(0x4A4A4A), false);
			// 다리(뼈)
			Seg(g, -3, -23, -3 - w.sw * 7.f, -2 - w.lift1, 2.2f, Shade(bone, 0.75f));
			Seg(g, 4, -23, 4 + w.sw * 7.f, -2 - w.lift0, 2.4f, bone);
			if (armored)
			{
				Limb(g, -3, -23, -3 - w.sw * 7.f, -3 - w.lift1, 3.4f, Shade(armor, 0.7f));
				Limb(g, 4, -23, 4 + w.sw * 7.f, -3 - w.lift0, 3.6f, armor);
			}
			// 골반/척추/갈비뼈
			Ell(g, 0, -24 + w.bob, 7.f, 3.f, bone);
			Seg(g, 0, -24 + w.bob, 1, -44 + w.bob, 2.f, bone);
			if (armored)
			{
				Quad4(g, -10, -46 + w.bob, 10, -46 + w.bob, 8, -24 + w.bob, -8, -24 + w.bob, Shade(armor, 1.2f), Shade(armor, 0.8f), Shade(armor, 0.65f), Shade(armor, 0.95f));
				Seg(g, -8, -34 + w.bob, 8, -34 + w.bob, 1.5f, Shade(c2, dk ? 1.0f : 0.8f));
				Ball(g, -8, -44 + w.bob, 5.f, Shade(armor, 0.95f));
			}
			else
			{
				for (int i = 0; i < 4; ++i)
					gfx::EllipseArc(g.P(1, -41 + i * 4.f + w.bob), g.R(7.f - i * 0.6f), g.R(2.4f), kPi * 1.05f, kPi * 1.95f, g.R(1.5f), bone);
			}
			// 두개골
			const float hy = -52 + w.bob;
			Ball(g, 1, hy, 7.5f, bone);
			Ell(g, 5, hy + 5, 4.f, 2.5f, Shade(bone, 0.9f));
			if (!p.faceBack)
			{
				const Color sock = armored ? Rgb(0xFF5A2A) : Rgb(0x1A1414);
				Circ(g, 3, hy - 1, 2.2f, Rgb(0x1A1414));
				Circ(g, 7, hy - 1, 2.f, Rgb(0x1A1414));
				if (armored)
				{
					Circ(g, 3, hy - 1, 1.f, sock);
					Circ(g, 7, hy - 1, 0.9f, sock);
					Glow(g, 5, hy - 1, 6.f, WithA(sock, 0.6f));
				}
				for (int i = 0; i < 3; ++i)
					Seg(g, 3.f + i * 1.6f, hy + 4, 3.f + i * 1.6f, hy + 6.5f, 0.8f, Rgb(0x3A3028));
			}
			if (armored)
			{
				// 투구
				Quad(g, -8, hy - 2, 9, hy - 2, 7, hy - 10, -6, hy - 10, Shade(armor, 1.1f));
				Tri(g, -2, hy - 10, 2, hy - 10, -1, hy - 17, Shade(c2, 0.9f));
			}
			// 앞 팔 + 무기
			Seg(g, 7, -42 + w.bob, sw.hx, sw.hy + w.bob, 2.2f, bone);
			if (armored)
				Limb(g, 7, -42 + w.bob, (7 + sw.hx) * 0.5f, (-42 + sw.hy) * 0.5f + w.bob, 3.2f, armor);
			Weapon(g, dk ? 17 : (armored ? 4 : 3), sw.hx, sw.hy + w.bob, sw.angle, dk ? 1.0f : 0.78f, p.time, dk ? 10 : 0);
			if (dk)
				props::FrameLights().push_back({ g.P(0, -20), 150.f * g.s, Rgb(0xFF5A1A), 0.7f });
		}
	}

	float MonsterHeight(const MonsterTableRow& row)
	{
		float h = 60.f;
		switch (row.archetype)
		{
		case MonsterArchetype::eBlob: h = 34.f; break;
		case MonsterArchetype::eQuadruped: h = 40.f; break;
		case MonsterArchetype::eInsect: h = row.variant == MonsterVariant::eScorpion ? 36.f : 30.f; break;
		case MonsterArchetype::eGolem: h = 66.f; break;
		case MonsterArchetype::eGhost: h = 54.f; break;
		case MonsterArchetype::eDragon: h = 62.f; break;
		case MonsterArchetype::eDemon: h = 72.f; break;
		case MonsterArchetype::eCrab: h = 34.f; break;
		case MonsterArchetype::eMushroom: h = 40.f; break;
		default: h = 66.f; break;
		}
		return h * row.scale / 100.f;
	}

	float MonsterShadow(const MonsterTableRow& row)
	{
		float r = 14.f;
		switch (row.archetype)
		{
		case MonsterArchetype::eQuadruped: r = 18.f; break;
		case MonsterArchetype::eGolem: r = 19.f; break;
		case MonsterArchetype::eDragon: r = 22.f; break;
		case MonsterArchetype::eCrab: r = 18.f; break;
		case MonsterArchetype::eGhost: r = 10.f; break;
		default: break;
		}
		return r * row.scale / 100.f;
	}

	void DrawMonster(const Pose& p, const MonsterTableRow& row)
	{
		Rig g = MakeRig(p, row.scale / 100.f);
		const Color c1 = Rgb(row.color1), c2 = Rgb(row.color2);
		// 몸이 쓰러지는 대신 납작해지는 종류
		if (row.archetype == MonsterArchetype::eBlob || row.archetype == MonsterArchetype::eInsect || row.archetype == MonsterArchetype::eCrab || row.archetype == MonsterArchetype::eGhost)
		{
			g.rot = 0.f;
			g.sy = 1.f - util::EaseOutCubic(p.dead) * 0.65f;
		}
		switch (row.archetype)
		{
		case MonsterArchetype::eBlob: Blob(g, p, c1, c2, row.id == 16); break;
		case MonsterArchetype::eQuadruped: Quadruped(g, p, row, c1, c2); break;
		case MonsterArchetype::eHumanoid: Humanoid(g, p, row, c1, c2); break;
		case MonsterArchetype::eInsect: Insect(g, p, row, c1, c2); break;
		case MonsterArchetype::eGolem: Golem(g, p, row, c1, c2); break;
		case MonsterArchetype::eGhost: Ghost(g, p, row, c1, c2); break;
		case MonsterArchetype::eDragon: Dragon(g, p, row, c1, c2); break;
		case MonsterArchetype::eDemon: Demon(g, p, row, c1, c2); break;
		case MonsterArchetype::eCrab: Crab(g, p, c1, c2); break;
		case MonsterArchetype::eMushroom: Mushroom(g, p, c1, c2); break;
		case MonsterArchetype::eSkeleton: Skeleton(g, p, row, c1, c2); break;
		}
	}

	// ── NPC ─────────────────────────────────────────────────────────────────────
	float NpcHeight(const WorldData::NpcRow& npc)
	{
		return npc.kind == WorldData::NpcKind::eTeleporter ? 84.f : 72.f;
	}

	void DrawNpc(const Pose& p, const WorldData::NpcRow& npc)
	{
		using WorldData::NpcKind;
		Rig g = MakeRig(p);
		const Walk w = WalkOf(p);
		const float b = w.bob;
		const Color skin = Rgb(0xEBC09A);
		switch (npc.kind)
		{
		case NpcKind::eMerchant:
		{
			const Color dress = Rgb(0x8A3A4A), apron = Rgb(0xF2EAD8), scarf = Rgb(0xD8A030);
			Quad4(g, -9, -42 + b, 9, -42 + b, 13, -2, -13, -2, Shade(dress, 1.1f), Shade(dress, 0.8f), Shade(dress, 0.6f), Shade(dress, 0.85f));
			Quad(g, -5, -34 + b, 7, -34 + b, 9, -6, -6, -6, apron);
			Seg(g, -7, -33 + b, 8, -33 + b, 2.f, Shade(apron, 0.8f));
			Limb(g, -7, -40 + b, -9, -28 + b, 3.f, Shade(dress, 0.8f));
			// 바구니(물약)
			Limb(g, 7, -40 + b, 11, -30 + b, 3.f, dress);
			Ell(g, 13, -27 + b, 7.f, 4.f, Rgb(0x9A6A3A));
			Circ(g, 11, -31 + b, 2.2f, Rgb(0xE0302A));
			Circ(g, 15, -31 + b, 2.2f, Rgb(0x3AC05A));
			Seg(g, 7, -30 + b, 19, -30 + b, 1.2f, Rgb(0x6A4A2A));
			Ball(g, 1, -50 + b, 8.f, skin);
			Quad(g, -8, -52 + b, 8, -56 + b, 6, -60 + b, -7, -58 + b, scarf);
			Tri(g, -8, -52 + b, -13, -44 + b, -5, -50 + b, Shade(scarf, 0.8f));
			if (!p.faceBack)
			{
				Circ(g, 4, -50 + b, 1.2f, Rgb(0x2A1A1A));
				Seg(g, 3, -46 + b, 6, -46.5f + b, 1.f, Rgb(0xA05050));
				Circ(g, 6, -48 + b, 1.5f, Rgb(0xF0A090, 120));
			}
			break;
		}
		case NpcKind::eWeaponSmith:
		{
			const Color shirt = Rgb(0x6A5A4A), apron = Rgb(0x5A3A22);
			Legs(g, w, Rgb(0x3A3028), Rgb(0x2A2018), 1.2f);
			Quad4(g, -12, -46 + b, 12, -46 + b, 10, -22 + b, -10, -22 + b, Shade(shirt, 1.1f), Shade(shirt, 0.8f), Shade(shirt, 0.7f), Shade(shirt, 0.9f));
			Quad(g, -8, -40 + b, 9, -40 + b, 10, -16 + b, -9, -16 + b, apron);
			Limb(g, -10, -43 + b, -12, -28 + b, 4.f, skin);
			// 망치
			const float hammer = std::sin(p.time * 1.3f) * 0.2f;
			Limb(g, 10, -43 + b, 14, -30 + b, 4.f, skin);
			const float ha = -1.2f + hammer;
			const float hx = 14 + std::cos(ha) * 18.f, hy = -30 + b + std::sin(ha) * 18.f;
			Seg(g, 14, -30 + b, hx, hy, 2.4f, Rgb(0x7A5A3A));
			Quad(g, hx - 5, hy - 3, hx + 5, hy - 3, hx + 5, hy + 4, hx - 5, hy + 4, Rgb(0x5A5A62));
			Ball(g, 1, -53 + b, 8.5f, skin);
			// 수염
			Vector2 beard[5] = { g.P(-5, -52 + b), g.P(9, -52 + b), g.P(8, -44 + b), g.P(2, -39 + b), g.P(-4, -44 + b) };
			gfx::Poly(beard, 5, Rgb(0xA0582A));
			Ell(g, 1, -60 + b, 7.f, 3.f, Rgb(0x8A4A22));
			if (!p.faceBack)
				Circ(g, 5, -54 + b, 1.2f, Rgb(0x2A1A1A));
			break;
		}
		case NpcKind::eTeleporter:
		{
			const Color robe = Rgb(0x3A4AA8), trimC = Rgb(0xE0C060);
			Quad4(g, -9, -46 + b, 9, -46 + b, 14, -1, -14, -1, Shade(robe, 1.15f), Shade(robe, 0.8f), Shade(robe, 0.55f), Shade(robe, 0.85f));
			Seg(g, -14, -2, 14, -2, 2.f, trimC);
			Seg(g, 1, -44 + b, 1, -3, 1.6f, trimC);
			Limb(g, -7, -42 + b, -10, -30 + b, 3.2f, Shade(robe, 0.8f));
			// 지팡이 + 빛나는 구슬
			Limb(g, 7, -42 + b, 12, -34 + b, 3.2f, robe);
			Seg(g, 13, 0, 13, -66, 2.4f, Rgb(0x7A5A3A));
			const float pulse = 0.7f + 0.3f * std::sin(p.time * 3.f);
			Glow(g, 13, -70, 14.f * pulse, Rgb(0x8AD8FF, 200));
			Circ(g, 13, -70, 4.f, Rgb(0xD8F4FF));
			props::FrameLights().push_back({ g.P(0, 0), 150.f, Rgb(0x8AD8FF), 0.9f * pulse });
			Ball(g, 1, -53 + b, 7.5f, skin);
			// 고깔모자
			Tri(g, -10, -56 + b, 11, -56 + b, -3, -80 + b, robe);
			Ell(g, 0, -56 + b, 11.f, 3.f, Shade(robe, 0.8f));
			Circ(g, -3, -80 + b, 2.f, trimC);
			Vector2 beard[4] = { g.P(-3, -50 + b), g.P(8, -50 + b), g.P(4, -36 + b), g.P(0, -40 + b) };
			gfx::Poly(beard, 4, Rgb(0xF2F2F2));
			if (!p.faceBack)
				Circ(g, 4, -54 + b, 1.2f, Rgb(0x2A1A1A));
			break;
		}
		case NpcKind::eGuard:
		{
			const Color armor = Rgb(0xA8B0BC), tabard = Rgb(0x2A4A8A);
			// 창
			Seg(g, 12, 2, 16, -78, 2.2f, Rgb(0x7A5A3A));
			Tri(g, 14, -78, 18, -78, 17, -90, Rgb(0xD8DCE4));
			Legs(g, w, Rgb(0x4A4A52), Rgb(0x2A2A30));
			Quad4(g, -10, -45 + b, 10, -45 + b, 8, -24 + b, -8, -24 + b, Shade(armor, 1.15f), Shade(armor, 0.8f), Shade(armor, 0.7f), Shade(armor, 0.95f));
			Quad(g, -5, -42 + b, 6, -42 + b, 7, -16 + b, -6, -16 + b, tabard);
			Shield(g, -10, -30 + b, tabard, Shade(armor, 0.8f), !p.faceBack);
			Limb(g, 7, -41 + b, 13, -30 + b, 3.f, armor);
			Ball(g, 0, -53 + b, 8.5f, armor);
			if (!p.faceBack)
				Seg(g, 2, -53 + b, 8.5f, -53 + b, 1.8f, Rgb(0x1A1A22));
			Tri(g, -2, -61 + b, 2, -61 + b, -8, -66 + b, Rgb(0xC83A3A));
			break;
		}
		}
	}
}
