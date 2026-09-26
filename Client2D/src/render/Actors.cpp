#include "render/Actors.h"
#include "render/ActorSprites.h"
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
			float sx = 1.f;      // 가로 눌림(몸을 돌리는 회전 베기 등)
			float ox = 0.f, oy = 0.f;

			Vector2 P(float x, float y) const
			{
				x = (x + ox) * sx;
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
			{
				const float t = TurnScale(p);   // 좌우 돌기: 부호 = 방향, 크기 = 돌아서는 중의 가로 배율
				g.dir = (t < 0.f ? -1.f : 1.f) * std::max(kTurnMinWidth, std::fabs(t));
			}
			g.rot += (p.lean * 0.4f + p.bank * 0.3f) * (3.14159265f / 180.f) * (p.dead > 0.f ? 0.f : 1.f);
			if (p.dead > 0.f)
				g.rot = -util::EaseOutBounce(util::Clamp01(p.dead * 1.4f)) * 1.35f;
			g.ox = -p.hurt * 4.f - p.knock * 6.f;
			if (p.windup > 0.f)
			{
				// 강공격 예고: 웅크리고 뒤로 젖힌 채 떤다(몸이 "곧 친다"고 말하게)
				g.sy = 1.f - 0.1f * p.windup;
				g.ox -= 3.5f * p.windup;
				g.ox += std::sin(p.time * 55.f) * 1.4f * p.windup;
			}
			return g;
		}

		// 무기(로컬 각도 → 화면 각도 변환 포함)
		void Weapon(const Rig& g, uint16_t itemId, float hx, float hy, float localAngle, float scale, float time, int enchant, int rarity = 0)
		{
			if (itemId == 0) return;
			const Vector2 hilt = g.P(hx, hy);
			// 로컬 방향 벡터를 리그 변환으로 돌려 화면 각도를 구한다
			const Vector2 tip = g.P(hx + std::cos(localAngle) * 10.f, hy + std::sin(localAngle) * 10.f);
			const float ang = std::atan2(tip.y - hilt.y, tip.x - hilt.x);
			// 영웅 이상: 희귀도 빛(전설은 더 크게, 신화는 반짝임까지)
			if (rarity >= static_cast<int>(Rarity::Heroic))
			{
				const Color rc = Rgb(ItemRules::RarityColor(static_cast<Rarity>(rarity)));
				const float len = icons::WeaponLength(itemId) * scale * g.s;
				const float pulse = 0.7f + 0.3f * std::sin(time * 3.2f);
				const float k = rarity >= static_cast<int>(Rarity::Legendary) ? 1.4f : 1.f;
				const Vector2 d{ std::cos(ang), std::sin(ang) };
				gfx::Additive(true);
				gfx::LineGrad(Vector2{ hilt.x + d.x * len * 0.15f, hilt.y + d.y * len * 0.15f }, Vector2{ hilt.x + d.x * len * 1.1f, hilt.y + d.y * len * 1.1f },
					g.R(9.f * k) * scale, g.R(3.f * k) * scale, WithA(rc, 0.35f * pulse), WithA(rc, 0.05f));
				if (rarity >= static_cast<int>(Rarity::Mythic))
				{
					for (int i = 0; i < 3; ++i)
					{
						const float u = std::fmod(time * 0.9f + i * 0.33f, 1.f);
						const Vector2 sp{ hilt.x + d.x * len * (0.3f + 0.7f * u), hilt.y + d.y * len * (0.3f + 0.7f * u) - g.R(4.f) * std::sin(u * kPi) };
						gfx::Star(sp, g.R(0.6f), g.R(2.8f) * (1.f - u * 0.5f), 4, time * 2.f + i, WithA(Shade(rc, 1.4f), 0.9f * (1.f - u)));
					}
				}
				gfx::Additive(false);
			}
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

	PlayerLook LookForPlayer(uint32_t nameHash, uint16_t weaponId, int enchant, uint8_t cls)
	{
		PlayerLook look;
		look.cls = cls;
		look.weaponId = weaponId;
		look.enchant = enchant;
		switch (cls)
		{
		case 1:   // 각인술사: 짙은 로브 + 이름마다 다른 각인 빛
		{
			static const Color robes[] = { Rgb(0x2A3468), Rgb(0x3A2A5A), Rgb(0x1E3A4A), Rgb(0x2E2A3E), Rgb(0x3A3050) };
			static const Color runes[] = { Rgb(0x6AD8FF), Rgb(0xBFE6FF), Rgb(0x7AF0C8), Rgb(0xFFC870) };
			look.cloth = robes[nameHash % 5];
			look.trim = runes[(nameHash / 5) % 4];
			look.armor = Rgb(0xB8A07A);
			break;
		}
		case 2:   // 밤사냥꾼: 검은 가죽 + 스카프 색
		{
			static const Color scarves[] = { Rgb(0x9A2A3A), Rgb(0x5A3A9A), Rgb(0x2A6A5A), Rgb(0x7A6A2A), Rgb(0x3A4A8A) };
			look.cloth = scarves[nameHash % 5];
			look.armor = Rgb(0x3A3236);
			look.trim = Rgb(0xC8D2E0);
			break;
		}
		default:  // 서약기사: 강철 + 저녁노을 망토
		{
			static const Color cloths[] = { Rgb(0x8A3A2A), Rgb(0xA8582A), Rgb(0x6A2A3A), Rgb(0x3A4A8A), Rgb(0x7A5A2A), Rgb(0x4A2A5A) };
			static const Color armors[] = { Rgb(0xA0AABA), Rgb(0xB8A888), Rgb(0x8A96A8), Rgb(0xC0C4CC), Rgb(0x9A8A7A) };
			look.cloth = cloths[nameHash % 6];
			look.armor = armors[(nameHash / 6) % 5];
			look.trim = Shade(look.cloth, 0.85f);
			break;
		}
		}
		return look;
	}

	void ApplyGear(PlayerLook& look, const uint16_t* items, const uint8_t* rarity, int count)
	{
		for (int i = 0; i < count; ++i)
		{
			const uint16_t id = items[i];
			if (id == 0) continue;
			int style = 0;
			Color m{}, t{}, a{};
			const EquipSlot part = static_cast<EquipSlot>(i + 1);
			if (part == EquipSlot::Weapon)
			{
				look.weaponRarity = rarity[i];
				continue;
			}
			if (rarity[i] > look.auraRarity && part != EquipSlot::Ring && part != EquipSlot::Necklace)
				look.auraRarity = rarity[i];
			if (!icons::GearStyle(id, style, m, t, a))
				continue;
			switch (part)
			{
			case EquipSlot::Helmet:
				look.helmStyle = static_cast<int8_t>(style); look.helmMain = m; look.helmTrim = t; look.helmAccent = a;
				break;
			case EquipSlot::Armor:
				look.bodyStyle = static_cast<int8_t>(style); look.bodyMain = m; look.bodyTrim = t; look.bodyAccent = a;
				// 직업별 기본 색 자리에 갑옷 색을 앉힌다(로브는 옷자락, 나머지는 갑옷/조끼)
				if (look.cls == 1 && (style == 3 || style == 6)) look.cloth = m;
				else if (look.cls == 1) look.armor = m;
				else { look.armor = m; if (style == 3 || style == 6) look.trim = Shade(m, 0.8f); }
				break;
			case EquipSlot::Cloak:
				look.hasCloak = true; look.cloakMain = m; look.cloakTrim = t;
				if (look.cls == 0) look.cloth = m;   // 기사는 원래 망토를 두른다 — 색만 바뀐다
				break;
			case EquipSlot::Gloves:
				look.hasGloves = true; look.gloveMain = m;
				break;
			case EquipSlot::Boots:
				look.hasBoots = true; look.bootMain = m;
				break;
			default:
				break;
			}
		}
	}

	void DrawShadow(Vector2 base, float rx, float alpha)
	{
		// 부드러운 발밑 그림자(Phase 1-C): 넓고 옅은 번짐 + 발 밑의 짙은 핵 — 가장자리까지 매끈하게 줄어든다
		gfx::EllipseGrad(base, rx * 1.15f, rx * 0.5f, Rgb(0x000000, static_cast<uint8_t>(54 * alpha)), Rgb(0x000000, 0));
		gfx::EllipseGrad(base, rx * 0.62f, rx * 0.27f, Rgb(0x000000, static_cast<uint8_t>(78 * alpha)), Rgb(0x000000, 0));
	}

	float PlayerHeight() { return 70.f; }

	namespace
	{
		// 기술 동작 공통: 팔·무기·몸 자세
		struct Arms
		{
			float hx = 10.f, hy = -31.f, ang = -0.95f;   // 주무기 손(앞)
			float fx = -9.f, fy = -30.f, ang2 = 0.f;     // 보조 손(뒤): 방패/빈손/두 번째 단검
			float lean = 0.f, crouch = 0.f;
			float spin = 0.f;                              // 회전(라디안) — 좌우가 뒤집히며 가로로 눌린다
			bool  offFront = false;                        // 보조 손이 몸 앞으로 나왔나(방패 강타·철벽)
			float glow = 0.f;                              // 손끝/지팡이 빛
		};

		// 기술 진행값: 스킬 모션이 없으면 레거시 공격 동작(attack)을 기본 공격으로 본다
		void Progress(const Pose& p, uint16_t basicId, uint16_t& sk, float& t)
		{
			if (p.skillT >= 0.f && p.skill != 0) { sk = p.skill; t = p.skillT; return; }
			if (p.attack >= 0.f) { sk = basicId; t = p.attack; return; }
			sk = 0; t = -1.f;
		}

		void ApplySpin(Rig& g, float spin)
		{
			if (spin == 0.f) return;
			const float c = std::cos(spin);
			if (c < 0.f) g.dir = -g.dir;
			g.sx = std::max(0.28f, std::fabs(c));
		}

		// ── V2 장비 외형 ────────────────────────────────────────────────────────
		// 투구: 직업 기본 머리 위에 덮는다(얼굴은 그늘 속 — 두건은 눈빛만 보인다).
		void HelmOverlay(const Rig& g, float cx, float cy, float r, const PlayerLook& look, const Pose& p, bool back)
		{
			if (look.helmStyle < 0) return;
			const Color m = look.helmMain, t = look.helmTrim, a = look.helmAccent;
			switch (look.helmStyle)
			{
			case 0: case 4:   // 두건
			{
				Ball(g, cx, cy, r + 1.2f, m);
				Tri(g, cx - r + 1.f, cy - 5.f, cx - 1.f, cy - r - 2.f, cx - r - 6.f, cy + 4.f, Shade(m, 0.78f));
				if (!back)
				{
					Ell(g, cx + 4.f, cy + 2.f, r * 0.58f, r * 0.62f, Rgb(0x16121C));
					gfx::Additive(true);
					Circ(g, cx + 3.f, cy + 1.f, 1.1f, look.helmStyle == 4 ? Shade(t, 1.3f) : Rgb(0xE8D8B0, 200));
					Circ(g, cx + 6.5f, cy + 1.f, 1.f, look.helmStyle == 4 ? Shade(t, 1.3f) : Rgb(0xE8D8B0, 200));
					gfx::Additive(false);
				}
				if (look.helmStyle == 4)
				{
					gfx::Additive(true);
					for (int i = 0; i < 3; ++i)
						gfx::Star(g.P(cx - 5.f + i * 4.f, cy - r * 0.55f + (i % 2) * 3.f), g.R(0.4f), g.R(1.9f), 4, p.time + i, WithA(a, 0.85f));
					gfx::Additive(false);
				}
				break;
			}
			case 1:   // 철모: 챙 + 코가리개
				Ball(g, cx, cy - 1.f, r + 0.6f, m);
				Ell(g, cx, cy + 3.f, r + 3.f, 2.2f, Shade(m, 0.75f));
				if (!back) Seg(g, cx + 6.f, cy - 3.f, cx + 6.5f, cy + 5.f, 1.6f, t);
				break;
			case 2: case 3:   // 닫힌 투구(면갑) + 깃털 / 날개
			{
				Ball(g, cx, cy, r + 0.8f, m);
				if (!back)
				{
					Quad(g, cx + 1.f, cy - 3.f, cx + r + 1.f, cy - 2.f, cx + r, cy + 6.f, cx + 1.f, cy + 7.f, Shade(m, 0.85f));
					Seg(g, cx + 3.f, cy + 0.5f, cx + r, cy + 0.5f, 1.6f, Rgb(0x14121A));
				}
				Seg(g, cx - r * 0.2f, cy - r - 0.5f, cx - r * 0.2f, cy + r * 0.3f, 1.2f, t);
				const float f = std::sin(p.time * 2.5f + p.walk * 0.5f) * 1.5f;
				if (look.helmStyle == 2)
				{
					SegG(g, cx - 1.f, cy - r - 1.f, cx - 10.f + f, cy - r - 4.f, 4.f, 1.5f, a, Shade(a, 0.7f));
					SegG(g, cx - 3.f, cy - r, cx - 12.f + f, cy - r + 3.f, 3.f, 1.f, Shade(a, 0.9f), Shade(a, 0.6f));
				}
				else
				{
					for (int s = -1; s <= 1; s += 2)
						Tri(g, cx + s * (r - 1.f), cy - 3.f, cx + s * (r - 1.f), cy + 2.f, cx + s * (r + 7.f), cy - 9.f + f * 0.3f, a);
					gfx::Additive(true);
					Glow(g, cx, cy - 2.f, r + 6.f, WithA(a, 0.22f));
					gfx::Additive(false);
				}
				break;
			}
			default: break;
			}
		}

		// 가슴 갑옷(로브를 입는 각인술사가 판금·사슬·가죽을 입었을 때 로브 위에)
		void ChestOverlay(const Rig& g, float b, const PlayerLook& look, bool back)
		{
			if (look.bodyStyle < 0 || look.bodyStyle == 3 || look.bodyStyle == 6) return;
			const Color m = look.bodyMain, t = look.bodyTrim;
			Quad4(g, -9, -44 + b, 9, -44 + b, 8, -28 + b, -8, -28 + b, Shade(m, 1.12f), Shade(m, 0.85f), Shade(m, 0.7f), Shade(m, 0.95f));
			if (!back) Seg(g, 1, -43 + b, 1, -30 + b, 1.1f, Shade(t, 0.9f));
			Ball(g, -8, -43 + b, 3.8f, Shade(m, 0.9f));
		}

		// 방어구 희귀도(영웅 이상) — 발밑에 은은한 빛
		void GearAura(const Rig& g, const Pose& p, const PlayerLook& look)
		{
			if (look.auraRarity < static_cast<uint8_t>(Rarity::Heroic) || p.dead > 0.f) return;
			const Color rc = Rgb(ItemRules::RarityColor(static_cast<Rarity>(look.auraRarity)));
			const float pulse = 0.6f + 0.4f * std::sin(p.time * 2.2f);
			gfx::Additive(true);
			gfx::EllipseGrad(g.P(0, -1), g.R(19.f), g.R(6.5f), WithA(rc, 0.24f * pulse), WithA(rc, 0.f));
			gfx::Additive(false);
		}

		Color HandColor(const PlayerLook& look, Color skin) { return look.hasGloves ? look.gloveMain : skin; }

		// ── 서약기사 ─────────────────────────────────────────────────────────
		Arms KnightArms(const Pose& p)
		{
			Arms a;
			uint16_t sk; float t;
			Progress(p, 1, sk, t);
			switch (sk)
			{
			case 1:   // 검격 — 크게 휘두른다
			{
				const Swing s = SwingOf(t, -0.95f, 10.f, -31.f);
				a.ang = s.angle; a.hx = s.hx; a.hy = s.hy; a.lean = s.lean * 1.3f;
				break;
			}
			case 2:   // 방패 강타 — 당겼다가 앞으로 내지른다
			{
				float k;
				if (t < 0.3f) k = -util::EaseOutCubic(t / 0.3f) * 0.5f;
				else if (t < 0.45f) k = Lerp(-0.5f, 1.f, util::EaseInCubic((t - 0.3f) / 0.15f));
				else k = 1.f - util::EaseInOutCubic((t - 0.45f) / 0.55f);
				a.fx = k > 0.f ? Lerp(-9.f, 15.f, k) : -9.f + k * 8.f;
				a.fy = -34.f;
				a.offFront = k > 0.25f;
				a.lean = k * 6.f;
				a.ang = 0.9f; a.hx = 4.f; a.hy = -28.f;
				break;
			}
			case 3:   // 회전 베기 — 칼을 수평으로 들고 한 바퀴
				a.spin = util::EaseOutCubic(t) * kPi * 2.f;
				a.ang = -0.12f; a.hx = 14.f; a.hy = -34.f;
				a.crouch = util::Pulse(t) * 3.f;
				break;
			case 4:   // 돌진 — 몸을 숙이고 칼끝을 앞으로
			{
				const float k = util::Pulse(std::min(1.f, t * 1.4f));
				a.lean = 8.f * k; a.ang = Lerp(-0.95f, -0.1f, k); a.hx = Lerp(10.f, 16.f, k); a.hy = Lerp(-31.f, -33.f, k);
				a.fx = Lerp(-9.f, 8.f, k); a.offFront = k > 0.3f; a.crouch = 3.f * k;
				break;
			}
			case 5:   // 철벽 태세 — 방패를 들고 버틴다
			{
				const float k = util::EaseOutBack(std::min(1.f, t * 3.f));
				a.fx = Lerp(-9.f, 10.f, k); a.fy = Lerp(-30.f, -38.f, k); a.offFront = k > 0.3f;
				a.ang = 1.1f; a.hx = 2.f; a.hy = -26.f; a.crouch = 3.f * std::min(1.f, k);
				break;
			}
			case 6:   // 단죄 — 들어 올려 힘을 모았다가 내려찍는다
			{
				const float hold = std::max(0.2f, p.skillHold);
				if (t < hold)
				{
					const float u = util::EaseOutCubic(t / hold);
					a.ang = Lerp(-0.95f, -2.55f, u); a.hx = Lerp(10.f, 2.f, u); a.hy = Lerp(-31.f, -60.f, u);
					a.lean = -3.f * u; a.crouch = 2.f * u; a.glow = u;
				}
				else
				{
					const float u = (t - hold) / std::max(0.01f, 1.f - hold);
					const float s = util::EaseInCubic(std::min(1.f, u * 3.f));
					const float back = u > 0.45f ? util::EaseInOutCubic((u - 0.45f) / 0.55f) : 0.f;
					a.ang = Lerp(Lerp(-2.55f, 1.35f, s), -0.95f, back);
					a.hx = Lerp(Lerp(2.f, 14.f, s), 10.f, back);
					a.hy = Lerp(Lerp(-60.f, -18.f, s), -31.f, back);
					a.lean = Lerp(Lerp(-3.f, 8.f, s), 0.f, back);
					a.crouch = Lerp(Lerp(2.f, 5.f, s), 0.f, back);
					a.glow = 1.f - back;
				}
				break;
			}
			default:
				break;
			}
			return a;
		}

		void DrawKnight(const Pose& p, const PlayerLook& look)
		{
			Rig g = MakeRig(p);
			const Walk w = WalkOf(p);
			Arms a = KnightArms(p);
			// 레거시 시전 자세(십자베기): 칼을 높이 든다
			if (p.cast > 0.f && p.attack < 0.f && p.skillT < 0.f)
			{
				const float u = util::Pulse(p.cast);
				a.ang = Lerp(a.ang, -1.57f, u);
				a.hy = Lerp(a.hy, -52.f, u);
				a.hx = Lerp(a.hx, 4.f, u);
			}
			ApplySpin(g, a.spin);
			g.ox += a.lean * 0.6f;
			g.oy += a.crouch;
			const Color armor = look.armor, cloth = look.cloth;
			const Color skin = HandColor(look, Rgb(0xE8B890));
			const Color bootCol = look.hasBoots ? look.bootMain : Rgb(0x3A2A1E);
			GearAura(g, p, look);
			const bool back = p.faceBack;
			const float armSwing = (a.offFront ? 0.f : -w.sw * 5.f);
			const float sx = a.fx + armSwing, sy = a.fy + w.bob;
			if (a.glow > 0.f)
			{
				gfx::Additive(true);
				Glow(g, a.hx, a.hy + w.bob, 26.f * a.glow, Rgb(0xFF9A40, static_cast<uint8_t>(150 * a.glow)));
				gfx::Additive(false);
			}

			if (!back)
			{
				Cape(g, p, cloth, false);
				if (!a.offFront)
				{
					Limb(g, -7, -41 + w.bob, sx, sy, 3.f, Shade(armor, 0.75f));
					Shield(g, sx, sy, Shade(cloth, 0.8f), Shade(armor, 0.7f), false);
				}
				Legs(g, w, Rgb(0x4A4038), bootCol);
				Torso(g, w, armor, look.trim, false);
				Head(g, w, p, armor, cloth, false);
				HelmOverlay(g, 0.f, -53.f + w.bob, 9.f, look, p, false);
				Limb(g, 7, -41 + w.bob, a.hx, a.hy + w.bob, 3.2f, armor);
				Circ(g, a.hx, a.hy + w.bob, 3.f, skin);
				Weapon(g, look.weaponId, a.hx, a.hy + w.bob, a.ang, 0.78f, p.time, look.enchant, look.weaponRarity);
				Circ(g, 7, -42 + w.bob, 4.5f, Shade(armor, 1.05f));
				if (a.offFront)
				{
					// 방패를 몸 앞으로 내민 자세
					Limb(g, -5, -41 + w.bob, sx, sy, 3.f, armor);
					Shield(g, sx, sy, cloth, Shade(armor, 0.9f), true);
				}
			}
			else
			{
				Limb(g, 7, -41 + w.bob, a.hx, a.hy + w.bob, 3.2f, Shade(armor, 0.75f));
				Weapon(g, look.weaponId, a.hx, a.hy + w.bob, a.ang, 0.78f, p.time, look.enchant, look.weaponRarity);
				Legs(g, w, Rgb(0x4A4038), bootCol);
				Torso(g, w, armor, look.trim, true);
				Head(g, w, p, armor, cloth, true);
				HelmOverlay(g, 0.f, -53.f + w.bob, 9.f, look, p, true);
				Cape(g, p, cloth, true);
				Limb(g, -7, -41 + w.bob, sx, sy, 3.f, armor);
				Shield(g, sx, sy, cloth, Shade(armor, 0.9f), true);
			}
		}

		// ── 각인술사 ───────────────────────────────────────────────────────────────
		Arms CasterArms(const Pose& p)
		{
			Arms a;
			a.hx = 9.f; a.hy = -30.f; a.ang = -1.35f;   // 지팡이를 세워 든다
			a.fx = -8.f; a.fy = -30.f;
			uint16_t sk; float t;
			Progress(p, 7, sk, t);
			switch (sk)
			{
			case 7:   // 각인탄: 지팡이를 앞으로 내민다
			{
				const float k = util::Pulse(std::min(1.f, t * 1.5f));
				a.ang = Lerp(-1.35f, -0.35f, k); a.hx = Lerp(9.f, 15.f, k); a.hy = Lerp(-30.f, -36.f, k);
				a.fx = Lerp(-8.f, 3.f, k); a.fy = Lerp(-30.f, -37.f, k); a.lean = 3.f * k; a.glow = k;
				break;
			}
			case 8: case 11:   // 지팡이를 높이, 빈손은 대상 쪽으로
			{
				const float k = util::Pulse(t);
				a.ang = Lerp(-1.35f, -1.75f, k); a.hx = Lerp(9.f, 6.f, k); a.hy = Lerp(-30.f, -54.f, k);
				a.fx = Lerp(-8.f, 14.f, k); a.fy = Lerp(-30.f, -41.f, k); a.offFront = k > 0.3f; a.glow = k; a.lean = 2.f * k;
				break;
			}
			case 12:  // 낙성: 양손을 하늘로(예고 동안 떨린다)
			{
				const float hold = std::max(0.2f, p.skillHold);
				const float k = t < hold ? util::EaseOutCubic(t / hold) : 1.f - util::EaseInOutCubic((t - hold) / std::max(0.01f, 1.f - hold));
				a.ang = Lerp(-1.35f, -1.6f, k); a.hx = Lerp(9.f, 4.f, k); a.hy = Lerp(-30.f, -62.f, k);
				a.fx = Lerp(-8.f, -3.f, k); a.fy = Lerp(-30.f, -60.f, k); a.glow = k; a.crouch = -2.f * k;
				break;
			}
			case 9:   // 화염 고리: 웅크렸다가 두 팔을 활짝
			{
				const float k = t < 0.3f ? -util::EaseOutCubic(t / 0.3f) : util::Pulse((t - 0.3f) / 0.7f);
				a.crouch = k < 0.f ? -k * 5.f : 0.f;
				const float o = std::max(0.f, k);
				a.fx = Lerp(-8.f, -18.f, o); a.fy = Lerp(-30.f, -41.f, o);
				a.hx = Lerp(9.f, 16.f, o); a.hy = Lerp(-30.f, -42.f, o); a.ang = Lerp(-1.35f, -0.8f, o); a.glow = o;
				break;
			}
			case 10:  // 옛길 도약: 몸을 웅크린다
			{
				const float k = util::Pulse(t);
				a.crouch = 4.f * k; a.fx = Lerp(-8.f, -2.f, k); a.hx = Lerp(9.f, 4.f, k); a.glow = k;
				break;
			}
			default:
				break;
			}
			return a;
		}

		void Staff(const Rig& g, const PlayerLook& look, float hx, float hy, float ang, float time, float glow)
		{
			if (look.weaponId != 0 && !ItemTableData::IsStaff(look.weaponId))
			{
				Weapon(g, look.weaponId, hx, hy, ang, 0.72f, time, look.enchant, look.weaponRarity);   // 지팡이가 아니면 그 무기를 든다
				return;
			}
			// 초보자 지팡이는 주인의 각인 색, 다른 지팡이는 그 지팡이의 수정 색
			const Color crystal = (look.weaponId != 0 && look.weaponId != ItemTableData::Id::ApprenticeStaff) ? icons::WeaponAccent(look.weaponId) : look.trim;
			// 지팡이: 손 아래로 조금, 위로 길게. 끝에 각인 수정.
			const float c = std::cos(ang), sn = std::sin(ang);
			const float x0 = hx - c * 13.f, y0 = hy - sn * 13.f;
			const float x1 = hx + c * 40.f, y1 = hy + sn * 40.f;
			Seg(g, x0, y0, x1, y1, 2.6f, Rgb(0x6A4A2E));
			Seg(g, x0, y0, x1, y1, 1.1f, Rgb(0x9A7448));
			Seg(g, hx + c * 30.f - sn * 3.f, hy + sn * 30.f + c * 3.f, hx + c * 34.f + sn * 3.f, hy + sn * 34.f - c * 3.f, 1.5f, Rgb(0xC8A060));
			const float pulse = 0.75f + 0.25f * std::sin(time * 4.f);
			const float gk = std::max(glow, 0.25f + look.enchant * 0.05f);
			gfx::Additive(true);
			Glow(g, x1 + c * 4.f, y1 + sn * 4.f, (12.f + 14.f * gk) * pulse, WithA(crystal, 0.55f + 0.35f * gk));
			gfx::Additive(false);
			const float cx = x1 + c * 4.f, cy = y1 + sn * 4.f;
			Tri(g, cx - 3.f, cy, cx + 3.f, cy, cx, cy - 8.f, Shade(crystal, 1.3f));
			Tri(g, cx - 3.f, cy, cx + 3.f, cy, cx, cy + 5.f, Shade(crystal, 0.8f));
			Circ(g, cx, cy - 1.f, 1.3f, Rgb(0xFFFFFF, 230));
			if (look.weaponRarity >= static_cast<uint8_t>(Rarity::Heroic))
			{
				const Color rc = Rgb(ItemRules::RarityColor(static_cast<Rarity>(look.weaponRarity)));
				gfx::Additive(true);
				Glow(g, cx, cy, 16.f + 6.f * pulse, WithA(rc, 0.35f));
				if (look.weaponRarity >= static_cast<uint8_t>(Rarity::Mythic))
					for (int i = 0; i < 3; ++i)
					{
						const float ang2 = time * 2.4f + i * kPi * 2.f / 3.f;
						gfx::Star(g.P(cx + std::cos(ang2) * 8.f, cy + std::sin(ang2) * 5.f), g.R(0.5f), g.R(2.4f), 4, time * 3.f, WithA(Shade(rc, 1.4f), 0.9f));
					}
				gfx::Additive(false);
			}
		}

		void DrawRuneCaster(const Pose& p, const PlayerLook& look)
		{
			Rig g = MakeRig(p);
			const Walk w = WalkOf(p);
			const Arms a = CasterArms(p);
			g.ox += a.lean * 0.6f;
			g.oy += a.crouch;
			const float b = w.bob;
			const Color robe = look.cloth, rune = look.trim, leather = look.armor;
			const Color skin = HandColor(look, Rgb(0xE8C0A0));
			const bool back = p.faceBack;
			const float sway = std::sin(p.walk) * 2.2f * p.moving + std::sin(p.time * 1.7f) * 0.6f;
			GearAura(g, p, look);

			auto offHand = [&](bool front)
			{
				Limb(g, -6, -41 + b, a.fx, a.fy + b, 2.8f, front ? robe : Shade(robe, 0.75f));
				Circ(g, a.fx, a.fy + b, 2.6f, skin);
				if (a.glow > 0.05f)
				{
					gfx::Additive(true);
					Glow(g, a.fx, a.fy + b, 10.f + 10.f * a.glow, WithA(rune, 0.7f * a.glow));
					gfx::Additive(false);
					gfx::Star(g.P(a.fx, a.fy + b), g.R(1.5f), g.R(5.f + 3.f * a.glow), 4, p.time * 3.f, WithA(Shade(rune, 1.4f), a.glow));
				}
			};
			auto boots = [&]()
			{
				const Color bc = look.hasBoots ? look.bootMain : Rgb(0x3A2A1E);
				Ell(g, -3 - w.sw * 5.f, -1.5f - w.lift1, 4.f, 2.2f, Shade(bc, 0.8f));
				Ell(g, 4 + w.sw * 5.f, -1.5f - w.lift0, 4.2f, 2.3f, bc);
			};
			auto body = [&]()
			{
				// 로브(치맛자락이 걸음에 흔들린다)
				Quad4(g, -9, -44 + b, 9, -44 + b, 13 + sway, -2, -13 + sway, -2,
					Shade(robe, 1.15f), Shade(robe, 0.85f), Shade(robe, 0.55f), Shade(robe, 0.8f));
				if (!back)
				{
					Quad(g, 0, -40 + b, 4, -40 + b, 6 + sway, -2, 1 + sway, -2, Shade(robe, 0.55f));
					Seg(g, 2, -40 + b, 3.5f + sway, -3, 1.2f, WithA(rune, 0.8f));
				}
				Seg(g, -13 + sway, -2.5f, 13 + sway, -2.5f, 1.6f, WithA(rune, 0.7f));
				// 허리띠 + 각인 주머니
				Seg(g, -8.5f, -25 + b, 8.5f, -25 + b, 2.6f, Rgb(0x3A2A1E));
				Ell(g, -6, -22 + b, 2.6f, 3.2f, leather);
				// 어깨 망토
				Quad4(g, -10, -45 + b, 10, -45 + b, 11, -36 + b, -11, -36 + b, Shade(leather, 1.1f), Shade(leather, 0.9f), Shade(leather, 0.7f), Shade(leather, 0.8f));
				if (!back)
				{
					// 가슴의 각인(맥동)
					const float pulse = 0.6f + 0.4f * std::sin(p.time * 3.f);
					gfx::Additive(true);
					Glow(g, 3, -33 + b, 7.f, WithA(rune, 0.5f * pulse));
					gfx::Additive(false);
					Tri(g, 1, -33 + b, 5, -33 + b, 3, -37 + b, Shade(rune, 1.2f));
					Tri(g, 1, -33 + b, 5, -33 + b, 3, -30 + b, rune);
				}
			};
			auto head = [&]()
			{
				// 두건: 얼굴은 그늘 속, 눈만 각인빛
				Ball(g, 0, -52 + b, 9.5f, Shade(robe, 0.9f));
				Tri(g, -8, -58 + b, -2, -61 + b, -14 + sway * 0.5f, -50 + b, Shade(robe, 0.75f));
				if (!back)
				{
					Ell(g, 4, -50 + b, 5.5f, 6.f, Rgb(0x1A1624));
					Circ(g, 3.5f, -48 + b, 3.f, WithA(skin, 0.35f));
					gfx::Additive(true);
					Circ(g, 3.f, -51 + b, 1.3f, Shade(rune, 1.3f));
					Circ(g, 7.f, -51 + b, 1.1f, Shade(rune, 1.3f));
					gfx::Additive(false);
				}
			};

			// 떠다니는 각인 두 점(직업 표식)
			gfx::Additive(true);
			for (int i = 0; i < 2; ++i)
			{
				const float ang = p.time * 1.6f + i * kPi;
				const float rx = std::cos(ang) * 16.f, ry = -38.f + std::sin(ang * 1.3f) * 6.f + b;
				Glow(g, rx, ry, 6.f, WithA(rune, 0.55f));
			}
			gfx::Additive(false);

			if (!back)
			{
				if (look.hasCloak) Cape(g, p, look.cloakMain, false);
				if (!a.offFront) offHand(false);
				boots();
				body();
				ChestOverlay(g, b, look, false);
				head();
				HelmOverlay(g, 0.f, -52.f + b, 9.5f, look, p, false);
				Limb(g, 6, -41 + b, a.hx, a.hy + b, 2.9f, robe);
				Circ(g, a.hx, a.hy + b, 2.8f, skin);
				Staff(g, look, a.hx, a.hy + b, a.ang, p.time, a.glow);
				if (a.offFront) offHand(true);
			}
			else
			{
				Staff(g, look, a.hx, a.hy + b, a.ang, p.time, a.glow);
				Limb(g, 6, -41 + b, a.hx, a.hy + b, 2.9f, Shade(robe, 0.8f));
				boots();
				body();
				ChestOverlay(g, b, look, true);
				head();
				HelmOverlay(g, 0.f, -52.f + b, 9.5f, look, p, true);
				if (look.hasCloak) Cape(g, p, look.cloakMain, true);
				offHand(true);
			}
		}

		// ── 밤사냥꾼 ───────────────────────────────────────────────────────
		Arms HunterArms(const Pose& p)
		{
			Arms a;
			a.hx = 9.f; a.hy = -28.f; a.ang = 0.35f;      // 단검을 낮게 앞으로
			a.fx = -7.f; a.fy = -30.f; a.ang2 = 2.5f;     // 보조 단검은 역수로
			a.crouch = 2.f;
			uint16_t sk; float t;
			Progress(p, 13, sk, t);
			switch (sk)
			{
			case 13:  // 연속 찌르기: 오른손 → 왼손
				if (t < 0.5f)
				{
					const float k = util::Pulse(t / 0.5f);
					a.hx = Lerp(9.f, 19.f, k); a.hy = Lerp(-28.f, -34.f, k); a.ang = Lerp(0.35f, -0.05f, k); a.lean = 4.f * k;
				}
				else
				{
					const float k = util::Pulse((t - 0.5f) / 0.5f);
					a.fx = Lerp(-7.f, 17.f, k); a.fy = Lerp(-30.f, -35.f, k); a.ang2 = Lerp(2.5f, -0.05f, k); a.lean = 4.f * k; a.offFront = k > 0.2f;
				}
				break;
			case 14:  // 그늘 걸음: 낮게 웅크려 뛰어든다
			{
				const float k = util::Pulse(t);
				a.crouch = 2.f + 5.f * k; a.lean = 7.f * k; a.hx = Lerp(9.f, 2.f, k); a.fx = Lerp(-7.f, -13.f, k);
				break;
			}
			case 15:  // 독 칼날: 크게 가로베기
			{
				const Swing s = SwingOf(t, 0.35f, 9.f, -28.f);
				a.ang = s.angle; a.hx = s.hx + 3.f; a.hy = s.hy; a.lean = s.lean; a.glow = util::Pulse(t);
				break;
			}
			case 16:  // 잔상 회피: 뒤로 젖히며 튄다
			{
				const float k = util::Pulse(t);
				a.lean = -7.f * k; a.crouch = 2.f - 4.f * k; a.hx = Lerp(9.f, 4.f, k); a.hy = Lerp(-28.f, -40.f, k); a.fx = Lerp(-7.f, -14.f, k); a.fy = Lerp(-30.f, -40.f, k);
				break;
			}
			case 17:  // 후방 습격: 두 단검을 머리 위로 → 내리꽂는다
			{
				if (t < 0.4f)
				{
					const float u = util::EaseOutCubic(t / 0.4f);
					a.hx = Lerp(9.f, 5.f, u); a.hy = Lerp(-28.f, -56.f, u); a.ang = Lerp(0.35f, -1.9f, u);
					a.fx = Lerp(-7.f, 1.f, u); a.fy = Lerp(-30.f, -55.f, u); a.ang2 = Lerp(2.5f, -1.8f, u); a.offFront = true;
					a.lean = -2.f * u;
				}
				else
				{
					const float u = util::EaseInCubic(std::min(1.f, (t - 0.4f) / 0.2f));
					const float back = t > 0.7f ? util::EaseInOutCubic((t - 0.7f) / 0.3f) : 0.f;
					a.hx = Lerp(Lerp(5.f, 15.f, u), 9.f, back); a.hy = Lerp(Lerp(-56.f, -22.f, u), -28.f, back); a.ang = Lerp(Lerp(-1.9f, 1.3f, u), 0.35f, back);
					a.fx = Lerp(Lerp(1.f, 12.f, u), -7.f, back); a.fy = Lerp(Lerp(-55.f, -24.f, u), -30.f, back); a.ang2 = Lerp(Lerp(-1.8f, 1.4f, u), 2.5f, back);
					a.offFront = back < 0.5f; a.lean = Lerp(7.f * u, 0.f, back); a.crouch = 2.f + 3.f * u * (1.f - back);
				}
				break;
			}
			case 18:  // 그믐 베기: 두 바퀴 돌며 벤다
				a.spin = t * kPi * 4.f;
				a.hx = 15.f; a.hy = -33.f; a.ang = -0.1f;
				a.fx = 13.f; a.fy = -30.f; a.ang2 = 0.2f; a.offFront = true;
				a.crouch = 3.f; a.glow = 1.f - t;
				break;
			default:
				break;
			}
			return a;
		}

		void DrawShadowHunter(const Pose& p, const PlayerLook& look)
		{
			Rig g = MakeRig(p);
			const Walk w = WalkOf(p);
			Arms a = HunterArms(p);
			ApplySpin(g, a.spin);
			g.ox += a.lean * 0.6f;
			g.oy += a.crouch;
			const float b = w.bob;
			const Color leather = look.armor, scarf = look.cloth, eye = look.trim;
			const Color skin = HandColor(look, Rgb(0xD8A888));
			const bool back = p.faceBack;
			const float flut = std::sin(p.time * 5.f + p.walk) * (1.5f + p.moving * 3.f);
			const float trail = p.moving * 7.f + std::fabs(a.lean) * 0.8f;
			GearAura(g, p, look);

			auto scarfTail = [&]()
			{
				Quad4(g, -3, -44 + b, 2, -45 + b, -12 - trail + flut * 0.3f, -36 + b + flut * 0.4f, -16 - trail + flut, -39 + b + flut,
					Shade(scarf, 1.1f), Shade(scarf, 0.95f), Shade(scarf, 0.6f), Shade(scarf, 0.7f));
			};
			auto dagger = [&](float x, float y, float ang, bool front)
			{
				Circ(g, x, y, 2.6f, front ? skin : Shade(skin, 0.8f));
				Weapon(g, look.weaponId != 0 ? look.weaponId : static_cast<uint16_t>(ItemTableData::Id::HunterDagger), x, y, ang, 0.62f, p.time, look.enchant, look.weaponRarity);
			};
			auto body = [&]()
			{
				// 다리: 가늘고 날렵하게
				Legs(g, w, Rgb(0x2A2628), look.hasBoots ? look.bootMain : Rgb(0x151214), 0.9f);
				// 가죽 조끼 + X자 끈
				Quad4(g, -8, -43 + b, 8, -43 + b, 7, -22 + b, -7, -22 + b, Shade(leather, 1.25f), Shade(leather, 0.9f), Shade(leather, 0.75f), Shade(leather, 1.0f));
				if (!back)
				{
					Seg(g, -7, -41 + b, 6, -25 + b, 1.6f, Rgb(0x5A4638));
					Seg(g, 7, -41 + b, -6, -25 + b, 1.6f, Rgb(0x5A4638));
					Circ(g, 0, -33 + b, 1.6f, Rgb(0xB8A070));
				}
				Seg(g, -7.5f, -23 + b, 7.5f, -23 + b, 2.4f, Rgb(0x1E1A1C));
				// 짧은 망토(허리까지)
				Quad4(g, -9, -44 + b, 9, -44 + b, 8 - trail * 0.3f, -27 + b, -11 - trail * 0.5f + flut * 0.3f, -26 + b,
					WithA(Shade(leather, 0.7f), 0.95f), WithA(Shade(leather, 0.6f), 0.95f), WithA(Shade(leather, 0.45f), 0.9f), WithA(Shade(leather, 0.5f), 0.9f));
			};
			auto head = [&]()
			{
				// 두건 + 복면, 보랏빛 눈
				Ball(g, 0, -51 + b, 8.5f, Rgb(0x2A2428));
				Tri(g, -6, -57 + b, 0, -60 + b, -10, -49 + b, Rgb(0x221E22));
				if (!back)
				{
					Ell(g, 4, -50 + b, 5.f, 4.2f, skin);
					Quad(g, -1, -48.5f + b, 10, -48.5f + b, 9, -43 + b, -1, -44 + b, Rgb(0x1A161A));
					gfx::Additive(true);
					Seg(g, 2.f, -51.5f + b, 5.f, -51 + b, 1.4f, Shade(eye, 1.2f));
					Seg(g, 6.5f, -51 + b, 9.f, -51.5f + b, 1.2f, Shade(eye, 1.2f));
					gfx::Additive(false);
				}
				// 스카프 매듭
				Ell(g, 0, -44 + b, 6.f, 2.6f, scarf);
			};
			if (a.glow > 0.05f)
			{
				gfx::Additive(true);
				Glow(g, a.hx, a.hy + b, 18.f * a.glow, WithA(p.skill == 15 ? Rgb(0x8AE85A) : Rgb(0xC8B8FF), 0.5f * a.glow));
				gfx::Additive(false);
			}

			if (!back)
			{
				if (look.hasCloak) Cape(g, p, look.cloakMain, false);
				scarfTail();
				if (!a.offFront)
				{
					Limb(g, -6, -41 + b, a.fx, a.fy + b, 2.6f, Shade(leather, 0.8f));
					dagger(a.fx, a.fy + b, a.ang2, false);
				}
				body();
				head();
				HelmOverlay(g, 0.f, -51.f + b, 8.5f, look, p, false);
				Limb(g, 6, -41 + b, a.hx, a.hy + b, 2.8f, leather);
				dagger(a.hx, a.hy + b, a.ang, true);
				if (a.offFront)
				{
					Limb(g, -5, -41 + b, a.fx, a.fy + b, 2.6f, leather);
					dagger(a.fx, a.fy + b, a.ang2, true);
				}
			}
			else
			{
				Limb(g, 6, -41 + b, a.hx, a.hy + b, 2.8f, Shade(leather, 0.8f));
				dagger(a.hx, a.hy + b, a.ang, false);
				body();
				head();
				HelmOverlay(g, 0.f, -51.f + b, 8.5f, look, p, true);
				if (look.hasCloak) Cape(g, p, look.cloakMain, true);
				scarfTail();
				Limb(g, -6, -41 + b, a.fx, a.fy + b, 2.6f, leather);
				dagger(a.fx, a.fy + b, a.ang2, true);
			}
		}
	}

	void DrawPlayer(const Pose& p, const PlayerLook& look)
	{
		if (actorspr::DrawPlayer(p, look)) return;   // 그린 동작판이 있으면 그것으로
		switch (look.cls)
		{
		case 1: DrawRuneCaster(p, look); break;
		case 2: DrawShadowHunter(p, look); break;
		default: DrawKnight(p, look); break;
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
			// 몸통(반투명 덩이)
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
				props::PushLight({ g.P(0, 0), 90.f * g.s, Rgb(0xFF6A20), 0.7f });
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
				props::PushLight({ g.P(0, 0), 120.f * g.s, Rgb(0xFF7020), 0.9f * pulse });
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
			props::PushLight({ g.P(0, 0), 80.f * g.s, c1, 0.6f });
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
				props::PushLight({ g.P(0, 0), 170.f * g.s, Rgb(0xFF7A2A), 0.8f });
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
				props::PushLight({ g.P(0, -30), 200.f * g.s, Rgb(0xFF3A3A), 0.7f });
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

		// ── 허수아비(Phase 2-E 훈련장) — 나무 기둥 · 가로대 · 짚 몸통 · 칠한 과녁. 맞으면 기둥째 흔들린다(쓰러지지 않는다)
		void Dummy(const Rig& g0, const Pose& p, Color c1, Color c2)
		{
			Rig g = g0;
			g.rot = 0.f;
			// 맞은 뒤 잦아드는 흔들림 + 늘 조금 삐걱이는 흔들림
			const float wob = (p.hurt > 0.f ? std::sin(p.time * 34.f) * p.hurt * 5.f : 0.f) + std::sin(p.time * 1.3f + static_cast<float>(p.seed % 97u) * 0.7f) * 0.6f
				+ (p.knock > 0.f ? p.knock * 4.f : 0.f);
			const Color wood = c2, woodHi = Shade(c2, 1.35f), woodLo = Shade(c2, 0.65f);
			const Color straw = c1, strawHi = Shade(c1, 1.25f), strawLo = Shade(c1, 0.75f);
			// 받침: 땅에 박은 십자 발
			Ell(g, 0, -1, 13.f, 4.5f, Shade(woodLo, 0.7f));
			Seg(g, -11, -1, 11, -1, 3.f, woodLo);
			Seg(g, -5, 2, 5, -4, 2.6f, wood);
			// 기둥(아래는 그대로, 위로 갈수록 흔들림)
			Quad4(g, -2.4f, 0, 2.4f, 0, 2.2f + wob * 0.9f, -58, -2.2f + wob * 0.9f, -58, wood, woodLo, woodLo, woodHi);
			// 가로대(팔)
			Seg(g, -19 + wob * 0.7f, -41, 19 + wob * 0.7f, -42, 3.4f, woodHi);
			Seg(g, -19 + wob * 0.7f, -39.5f, 19 + wob * 0.7f, -40.5f, 1.2f, woodLo);
			// 팔 끝 짚 뭉치
			for (int s = -1; s <= 1; s += 2)
			{
				const float ax = s * 20.f + wob * 0.7f;
				Ell(g, ax, -41, 4.f, 3.2f, straw);
				Seg(g, ax, -41, ax + s * 5.f, -44, 0.9f, strawHi);
				Seg(g, ax, -41, ax + s * 5.5f, -38, 0.9f, strawHi);
			}
			// 짚 몸통(자루) — 허리를 끈으로 묶었다
			const float bx = wob * 0.6f;
			Quad4(g, -10 + bx, -20, 10 + bx, -20, 12 + bx, -44, -12 + bx, -44, strawLo, strawLo, straw, strawHi);
			Ell(g, bx, -44, 12.f, 4.f, strawHi);
			Ell(g, bx, -20, 10.f, 3.2f, strawLo);
			Seg(g, -10 + bx, -25, 10 + bx, -25, 1.3f, woodLo);
			// 칠한 과녁(앞에서만)
			if (!p.faceBack)
			{
				Ell(g, bx + 1.f, -33, 7.5f, 7.f, Rgb(0xE8DFC8));
				Ell(g, bx + 1.f, -33, 5.4f, 5.f, Rgb(0xA8382A));
				Ell(g, bx + 1.f, -33, 3.2f, 3.f, Rgb(0xE8DFC8));
				Ell(g, bx + 1.f, -33, 1.3f, 1.2f, Rgb(0xA8382A));
			}
			// 머리: 짚 공 + 끈
			Circ(g, wob, -53, 7.f, strawHi);
			Circ(g, wob - 1.5f, -54.5f, 4.5f, Shade(strawHi, 1.1f));
			Seg(g, wob - 6, -47.5f, wob + 6, -47.5f, 1.6f, woodLo);
			// 삐져나온 짚
			for (int i = 0; i < 7; ++i)
			{
				const float a = -2.4f + i * 0.8f;
				const float hx = wob + std::cos(a) * 7.f, hy = -53.f + std::sin(a) * 7.f;
				Seg(g, hx, hy, hx + std::cos(a) * 3.5f, hy + std::sin(a) * 3.5f, 0.8f, Rgb(0xEAD08A));
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
				props::PushLight({ g.P(0, -20), 150.f * g.s, Rgb(0xFF5A1A), 0.7f });
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
		case MonsterArchetype::eDummy: h = 60.f; break;
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
		if (actorspr::DrawMonster(p, row)) return;
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
		case MonsterArchetype::eDummy: Dummy(g, p, c1, c2); break;
		}
	}

	// ── NPC ─────────────────────────────────────────────────────────────────────
	float NpcHeight(const WorldData::NpcRow& npc)
	{
		using WorldData::NpcKind;
		switch (npc.kind)
		{
		case NpcKind::eTeleporter: return 84.f;
		case NpcKind::eChild:      return 50.f;
		case NpcKind::eVillager:   return 62.f;
		case NpcKind::eDog:        return 30.f;
		case NpcKind::eCat:        return 24.f;
		case NpcKind::eChicken:    return 22.f;
		default:                   return 72.f;
		}
	}

	void DrawNpc(const Pose& p, const WorldData::NpcRow& npc)
	{
		if (actorspr::DrawNpc(p, npc)) return;
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
			// 길잡이(교회 필경사 출신): 짙은 청회색 두건 달린 옷 + 옛길 돌을 매단 지팡이(청록 빛)
			const Color robe = Rgb(0x34426A), trimC = Rgb(0xB89A5A);
			Quad4(g, -9, -46 + b, 9, -46 + b, 14, -1, -14, -1, Shade(robe, 1.15f), Shade(robe, 0.8f), Shade(robe, 0.55f), Shade(robe, 0.85f));
			Seg(g, -14, -2, 14, -2, 2.f, trimC);
			Seg(g, 1, -44 + b, 1, -3, 1.4f, Shade(trimC, 0.9f));
			// 두루마리 가방(비스듬한 끈 + 통)
			Seg(g, -8, -44 + b, 8, -22 + b, 1.6f, Rgb(0x5A4030));
			Quad(g, -12, -24 + b, -4, -26 + b, -3, -20 + b, -11, -18 + b, Rgb(0x7A5A3A));
			Limb(g, -7, -42 + b, -10, -30 + b, 3.2f, Shade(robe, 0.8f));
			// 지팡이 + 옛길 돌
			Limb(g, 7, -42 + b, 12, -34 + b, 3.2f, robe);
			Seg(g, 13, 0, 13, -64, 2.4f, Rgb(0x6A4E34));
			const float pulse = 0.7f + 0.3f * std::sin(p.time * 2.4f);
			Glow(g, 13, -68, 12.f * pulse, Rgb(0x6FB5AC, 190));
			Quad(g, 10.5f, -71, 15.5f, -71, 15, -65, 11, -65, Rgb(0x8A9A98));
			Seg(g, 12.2f, -70, 13.8f, -66, 1.f, Rgb(0xC8F0E8));
			props::PushLight({ g.P(0, 0), 140.f, Rgb(0x6FB5AC), 0.75f * pulse });
			Ball(g, 1, -53 + b, 7.5f, skin);
			// 두건(머리를 감싸고 어깨로 흘러내린다)
			Ell(g, 0, -56 + b, 10.5f, 10.f, Shade(robe, 0.9f));
			Tri(g, -10, -54 + b, -12, -42 + b, -4, -47 + b, Shade(robe, 0.8f));
			Tri(g, 10, -54 + b, 11, -42 + b, 5, -47 + b, Shade(robe, 0.8f));
			if (!p.faceBack)
			{
				// 얼굴은 두건 안에서 보인다
				Ell(g, 3, -52 + b, 6.f, 6.5f, skin);
				Vector2 beard[4] = { g.P(-1, -49 + b), g.P(8, -49 + b), g.P(5, -41 + b), g.P(1, -44 + b) };
				gfx::Poly(beard, 4, Rgb(0xB8B4AC));
				Circ(g, 5, -53 + b, 1.1f, Rgb(0x2A1A1A));
			}
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
		// ── V2: 살아 있는 마을 ──
		case NpcKind::eElder:
		{
			// 굽은 허리, 긴 수염, 지팡이
			const Color robe = Rgb(0x3A5A3A);
			g.ox += 2.f;
			Seg(g, 12, 1, 14 + w.sw * 2.f, -50 + b, 2.2f, Rgb(0x6A4A2A));
			Circ(g, 14 + w.sw * 2.f, -51 + b, 2.6f, Rgb(0x7A5A3A));
			Ell(g, -3 - w.sw * 4.f, -1.5f - w.lift1, 4.f, 2.2f, Rgb(0x2A2018));
			Ell(g, 4 + w.sw * 4.f, -1.5f - w.lift0, 4.2f, 2.3f, Rgb(0x3A2A1E));
			Quad4(g, -9, -42 + b, 9, -44 + b, 12, -2, -12, -2, Shade(robe, 1.15f), Shade(robe, 0.85f), Shade(robe, 0.6f), Shade(robe, 0.8f));
			Seg(g, -12, -3, 12, -3, 1.6f, Rgb(0xC8A860));
			Limb(g, 6, -39 + b, 13 + w.sw * 2.f, -34 + b, 3.f, robe);
			Circ(g, 13 + w.sw * 2.f, -34 + b, 2.6f, skin);
			Ball(g, 3, -50 + b, 8.f, skin);
			Ell(g, 2, -57 + b, 8.f, 3.4f, Rgb(0xE8E8E8));
			Vector2 beard[5] = { g.P(-2, -49 + b), g.P(11, -49 + b), g.P(9, -38 + b), g.P(4, -30 + b), g.P(0, -40 + b) };
			gfx::Poly(beard, 5, Rgb(0xF0F0F0));
			if (!p.faceBack)
			{
				Seg(g, 5, -53 + b, 9, -53 + b, 1.4f, Rgb(0xDADADA));
				Circ(g, 7, -51 + b, 1.1f, Rgb(0x2A1A1A));
			}
			break;
		}
		case NpcKind::ePriest:
		{
			// 흰 사제복 + 금빛 테, 손에 작은 성표 — 은은한 빛
			const Color robe = Rgb(0xE8E4DA), gold = Rgb(0xE0C060);
			const float pulse = 0.7f + 0.3f * std::sin(p.time * 2.f);
			gfx::Additive(true);
			Glow(g, 0, -36 + b, 30.f * pulse, Rgb(0xFFF0C0, 60));
			gfx::Additive(false);
			props::PushLight({ g.P(0, 0), 120.f, Rgb(0xFFE8B0), 0.55f * pulse });
			Ell(g, -3 - w.sw * 4.f, -1.5f - w.lift1, 4.f, 2.2f, Rgb(0x6A5A4A));
			Ell(g, 4 + w.sw * 4.f, -1.5f - w.lift0, 4.2f, 2.3f, Rgb(0x7A6A5A));
			Quad4(g, -9, -45 + b, 9, -45 + b, 13, -2, -13, -2, Shade(robe, 1.05f), Shade(robe, 0.88f), Shade(robe, 0.7f), Shade(robe, 0.85f));
			Seg(g, 1, -44 + b, 1, -3, 2.4f, gold);
			Seg(g, -13, -3, 13, -3, 1.8f, gold);
			Quad(g, -10, -46 + b, 10, -46 + b, 8, -38 + b, -8, -38 + b, Shade(robe, 0.92f));
			Limb(g, 6, -41 + b, 10, -32 + b, 3.f, robe);
			Circ(g, 10, -32 + b, 2.5f, skin);
			Seg(g, 11, -35 + b, 11, -27 + b, 1.6f, gold);
			Seg(g, 8.5f, -32 + b, 13.5f, -32 + b, 1.6f, gold);
			Ball(g, 1, -53 + b, 7.8f, skin);
			Ell(g, 0, -58 + b, 8.5f, 4.f, Rgb(0xC8A878));
			if (!p.faceBack)
			{
				Circ(g, 4.5f, -53 + b, 1.1f, Rgb(0x2A1A1A));
				Seg(g, 3, -49 + b, 6, -49.5f + b, 0.9f, Rgb(0xB06060));
			}
			break;
		}
		case NpcKind::eInnkeeper:
		{
			// 통통한 여관 주인: 앞치마, 걷어 올린 소매, 국자
			const Color dress = Rgb(0x8A5A3A), apron = Rgb(0xF0E8D8);
			Ell(g, -3 - w.sw * 4.f, -1.5f - w.lift1, 4.4f, 2.4f, Rgb(0x3A2A1E));
			Ell(g, 4 + w.sw * 4.f, -1.5f - w.lift0, 4.6f, 2.5f, Rgb(0x4A3A2A));
			Quad4(g, -11, -40 + b, 11, -40 + b, 14, -2, -14, -2, Shade(dress, 1.1f), Shade(dress, 0.85f), Shade(dress, 0.6f), Shade(dress, 0.85f));
			Ell(g, 0, -30 + b, 12.f, 11.f, Shade(dress, 1.0f));
			Quad(g, -6, -34 + b, 8, -34 + b, 10, -4, -7, -4, apron);
			Limb(g, -9, -38 + b, -12, -27 + b, 3.4f, skin);
			const float stir = std::sin(p.time * 3.f) * 2.f;
			Limb(g, 9, -38 + b, 13, -29 + b, 3.4f, skin);
			Seg(g, 13, -29 + b, 17 + stir, -43 + b, 1.6f, Rgb(0x9A7A4A));
			Ell(g, 17 + stir, -44 + b, 2.8f, 2.f, Rgb(0x9A7A4A));
			Ball(g, 1, -49 + b, 8.4f, skin);
			Quad(g, -8, -53 + b, 9, -56 + b, 8, -60 + b, -7, -58 + b, Rgb(0xC83A3A));
			if (!p.faceBack)
			{
				Circ(g, 4.5f, -50 + b, 1.2f, Rgb(0x2A1A1A));
				Circ(g, 6, -46.5f + b, 1.6f, Rgb(0xF0A090, 130));
				Seg(g, 3, -45 + b, 6, -45.5f + b, 1.f, Rgb(0xA05050));
			}
			break;
		}
		case NpcKind::eBard:
		{
			// 깃털 모자, 망토, 류트(줄을 튕긴다)
			const Color cloak = Rgb(0x7A2A4A), tunic = Rgb(0x2A5A7A);
			Legs(g, w, Rgb(0x3A3A48), Rgb(0x2A2020));
			Quad4(g, -10, -44 + b, 8, -44 + b, 6 - p.moving * 3.f, -14, -14 - p.moving * 3.f, -15, Shade(cloak, 0.9f), Shade(cloak, 0.75f), Shade(cloak, 0.55f), Shade(cloak, 0.65f));
			Quad4(g, -8, -44 + b, 8, -44 + b, 7, -22 + b, -7, -22 + b, Shade(tunic, 1.15f), Shade(tunic, 0.9f), Shade(tunic, 0.75f), Shade(tunic, 1.f));
			Seg(g, -7.5f, -23 + b, 7.5f, -23 + b, 2.4f, Rgb(0x5A3A22));
			// 류트: 몸통(배) + 목
			Ell(g, 6, -30 + b, 7.f, 5.5f, Rgb(0xA0703A));
			Ell(g, 6, -30 + b, 2.f, 2.f, Rgb(0x3A2410));
			Seg(g, 8, -32 + b, 20, -43 + b, 2.f, Rgb(0x7A5028));
			const float strum = std::sin(p.time * 9.f) * 2.5f;
			Limb(g, -6, -41 + b, 5 + strum * 0.5f, -28 + b, 2.8f, tunic);
			Circ(g, 5 + strum * 0.5f, -28 + b, 2.4f, skin);
			Limb(g, 6, -41 + b, 16, -39 + b, 2.8f, tunic);
			Circ(g, 16, -39 + b, 2.3f, skin);
			Ball(g, 1, -52 + b, 7.8f, skin);
			Quad(g, -9, -55 + b, 9, -57 + b, 6, -63 + b, -6, -62 + b, cloak);
			SegG(g, -6, -62 + b, -16 + std::sin(p.time * 2.f) * 1.5f, -70 + b, 2.5f, 1.f, Rgb(0xF0E8D0), Rgb(0xC8B890));
			if (!p.faceBack)
			{
				Circ(g, 4.5f, -52 + b, 1.1f, Rgb(0x2A1A1A));
				Seg(g, 3, -48 + b, 6.5f, -47.5f + b, 1.f, Rgb(0xA05050));
			}
			break;
		}
		case NpcKind::eChild:
		{
			// 아이: 작고 통통 튄다
			Rig c = MakeRig(p, 0.68f);
			const Color shirt = npc.color ? Rgb(npc.color) : Rgb(0x3A7AD8);
			const float hop = std::fabs(std::sin(p.walk)) * 3.f * p.moving;
			c.oy -= hop;
			Legs(c, w, Rgb(0x4A3A2A), Rgb(0x2A2018));
			Quad4(c, -8, -44 + b, 8, -44 + b, 9, -22 + b, -9, -22 + b, Shade(shirt, 1.15f), Shade(shirt, 0.9f), Shade(shirt, 0.75f), Shade(shirt, 1.f));
			Limb(c, -7, -40 + b, -10 - w.sw * 5.f, -28 + b, 2.8f, shirt);
			Limb(c, 7, -40 + b, 10 + w.sw * 5.f, -28 + b, 2.8f, shirt);
			Circ(c, -10 - w.sw * 5.f, -28 + b, 2.6f, skin);
			Circ(c, 10 + w.sw * 5.f, -28 + b, 2.6f, skin);
			Ball(c, 1, -55 + b, 11.f, skin);
			Ell(c, 0, -62 + b, 11.f, 6.f, npc.id % 2 ? Rgb(0x8A4A2A) : Rgb(0x3A2418));
			if (npc.id % 2 == 0)
			{
				Circ(c, -11, -56 + b, 3.2f, Rgb(0x3A2418));   // 땋은 머리
				Circ(c, 12, -56 + b, 3.2f, Rgb(0x3A2418));
			}
			if (!p.faceBack)
			{
				Circ(c, 5, -55 + b, 1.5f, Rgb(0x2A1A1A));
				Circ(c, 7, -50 + b, 2.f, Rgb(0xF08A8A, 120));
				Seg(c, 3, -49 + b, 6, -48.5f + b, 1.2f, Rgb(0xA04040));
			}
			break;
		}
		case NpcKind::eVillager:
		{
			// 할멈: 숄, 바구니, 굽은 등
			const Color shawl = npc.color ? Rgb(npc.color) : Rgb(0x6A5A7A), dress = Rgb(0x5A4A3A);
			Rig c = MakeRig(p, 0.9f);
			c.ox += 2.f;
			Ell(c, -3 - w.sw * 3.f, -1.5f - w.lift1, 4.f, 2.2f, Rgb(0x2A2018));
			Ell(c, 4 + w.sw * 3.f, -1.5f - w.lift0, 4.2f, 2.3f, Rgb(0x3A2A1E));
			Quad4(c, -9, -40 + b, 9, -42 + b, 12, -2, -12, -2, Shade(dress, 1.1f), Shade(dress, 0.85f), Shade(dress, 0.6f), Shade(dress, 0.85f));
			Quad4(c, -11, -46 + b, 10, -46 + b, 12, -30 + b, -12, -30 + b, Shade(shawl, 1.15f), Shade(shawl, 0.9f), Shade(shawl, 0.7f), Shade(shawl, 0.9f));
			Limb(c, 7, -40 + b, 12, -28 + b, 3.f, shawl);
			Ell(c, 13, -24 + b, 7.f, 4.f, Rgb(0x9A6A3A));
			Seg(c, 7, -25 + b, 19, -25 + b, 1.2f, Rgb(0x6A4A2A));
			Circ(c, 11, -28 + b, 2.f, Rgb(0xD84A3A));
			Circ(c, 15, -28 + b, 2.f, Rgb(0xE8C040));
			Ball(c, 3, -52 + b, 8.f, skin);
			Ell(c, 0, -57 + b, 9.f, 5.f, Rgb(0xD8D8D8));
			Circ(c, -7, -56 + b, 3.5f, Rgb(0xD8D8D8));
			if (!p.faceBack)
			{
				Circ(c, 7, -52 + b, 1.1f, Rgb(0x2A1A1A));
				Seg(c, 5, -47 + b, 8, -47.5f + b, 1.f, Rgb(0x9A6060));
			}
			break;
		}
		case NpcKind::eFarmer:
		{
			// 밀짚모자, 멜빵바지, 쇠스랑
			const Color overall = npc.color ? Rgb(npc.color) : Rgb(0x4A5A8A), shirt = Rgb(0xD8C8A8);
			Seg(g, 13, 2, 17, -70, 2.f, Rgb(0x7A5A3A));
			for (int i = -1; i <= 1; ++i) Seg(g, 17 + i * 3.f, -70, 17.5f + i * 3.5f, -80, 1.4f, Rgb(0x8A8A92));
			Legs(g, w, overall, Rgb(0x3A2A1E));
			Quad4(g, -9, -44 + b, 9, -44 + b, 8, -22 + b, -8, -22 + b, Shade(shirt, 1.1f), Shade(shirt, 0.9f), Shade(shirt, 0.75f), Shade(shirt, 0.95f));
			Quad(g, -7, -36 + b, 7, -36 + b, 8, -20 + b, -8, -20 + b, overall);
			Seg(g, -5, -44 + b, -5, -36 + b, 1.6f, overall);
			Seg(g, 5, -44 + b, 5, -36 + b, 1.6f, overall);
			Limb(g, 7, -41 + b, 14, -32 + b, 3.f, shirt);
			Circ(g, 14, -32 + b, 2.6f, skin);
			Limb(g, -7, -41 + b, -10 - w.sw * 4.f, -29 + b, 3.f, shirt);
			Ball(g, 1, -53 + b, 8.2f, Rgb(0xD8A07A));
			Ell(g, 0, -59 + b, 15.f, 4.f, Rgb(0xE8C870));
			Ell(g, 0, -62 + b, 8.f, 4.5f, Rgb(0xF0D888));
			Seg(g, -8, -60 + b, 8, -60 + b, 1.5f, Rgb(0xA84A2A));
			if (!p.faceBack)
			{
				Circ(g, 5, -53 + b, 1.2f, Rgb(0x2A1A1A));
				Seg(g, 3, -48 + b, 7, -48 + b, 1.f, Rgb(0x9A5A40));
			}
			break;
		}
		case NpcKind::eDog:
		{
			// 누렁이: 걸을 때 통통, 멈추면 꼬리를 흔든다
			const Color fur = Rgb(0xC8904A), dark = Rgb(0x8A5A2A);
			const float gait = std::sin(p.walk * 2.f) * p.moving;
			const float wag = std::sin(p.time * (p.moving > 0.3f ? 10.f : 16.f)) * (6.f + 3.f * (1.f - p.moving));
			const float by = -std::fabs(gait) * 1.5f;
			// 다리
			Limb(g, -9 + gait * 3.f, -9 + by, -9 + gait * 4.f, -1, 2.4f, dark);
			Limb(g, 8 - gait * 3.f, -9 + by, 8 - gait * 4.f, -1, 2.4f, dark);
			Limb(g, -6 - gait * 3.f, -9 + by, -6 - gait * 4.f, -1, 2.4f, fur);
			Limb(g, 11 + gait * 3.f, -9 + by, 11 + gait * 4.f, -1, 2.4f, fur);
			// 꼬리 + 몸
			SegG(g, -12, -15 + by, -18 + wag * 0.3f, -24 + by - std::fabs(wag) * 0.3f, 3.f, 1.5f, fur, Shade(fur, 1.2f));
			Ell(g, 0, -13 + by, 13.f, 6.5f, fur);
			Ell(g, 1, -11 + by, 10.f, 3.5f, Rgb(0xF0D0A0));
			// 머리
			Ball(g, 13, -20 + by, 6.f, fur);
			Ell(g, 18, -18 + by, 3.5f, 2.5f, Rgb(0xE8C8A0));
			Circ(g, 20.5f, -19 + by, 1.3f, Rgb(0x1A1210));
			Tri(g, 9, -24 + by, 12, -25 + by, 10, -17 + by, dark);
			if (!p.faceBack)
				Circ(g, 15, -22 + by, 1.2f, Rgb(0x1A1210));
			break;
		}
		case NpcKind::eCat:
		{
			// 나비: 식빵 굽는 자세 + 느린 꼬리
			const Color fur = Rgb(0xE89A4A), stripe = Rgb(0xB8662A);
			const float swish = std::sin(p.time * 1.6f) * 5.f;
			const float gait = std::sin(p.walk * 2.f) * p.moving;
			if (p.moving > 0.2f)
			{
				Limb(g, -6 + gait * 3.f, -6, -6 + gait * 4.f, -1, 1.8f, stripe);
				Limb(g, 7 - gait * 3.f, -6, 7 - gait * 4.f, -1, 1.8f, fur);
			}
			SegG(g, -9, -8, -15 + swish, -17, 2.4f, 1.2f, fur, stripe);
			Ell(g, 0, -8 - p.moving * 2.f, 10.f, 5.5f, fur);
			for (int i = -1; i <= 1; ++i) Seg(g, i * 4.f - 1.f, -13, i * 4.f + 1.f, -5, 1.2f, stripe);
			Ball(g, 9, -14 - p.moving * 2.f, 5.f, fur);
			Tri(g, 6, -17 - p.moving * 2.f, 8, -22 - p.moving * 2.f, 9, -17 - p.moving * 2.f, fur);
			Tri(g, 10, -17 - p.moving * 2.f, 13, -21 - p.moving * 2.f, 13, -16 - p.moving * 2.f, fur);
			if (!p.faceBack)
			{
				const float blink = std::fmod(p.time + p.seed * 0.37f, 4.f) < 0.12f ? 0.3f : 1.f;
				Ell(g, 11, -15 - p.moving * 2.f, 1.1f, 1.1f * blink, Rgb(0x2A3A1A));
			}
			break;
		}
		case NpcKind::eChicken:
		{
			// 닭: 멈추면 모이를 쪼아 먹는다
			const Color body = Rgb(0xF4F0E8);
			const float peck = p.moving < 0.2f ? std::max(0.f, std::sin(p.time * 5.f + p.seed)) : 0.f;
			const float gait = std::sin(p.walk * 2.f) * p.moving;
			Seg(g, -2 + gait * 2.f, -5, -2 + gait * 3.f, 0, 1.2f, Rgb(0xE8A030));
			Seg(g, 2 - gait * 2.f, -5, 2 - gait * 3.f, 0, 1.2f, Rgb(0xE8A030));
			Tri(g, -8, -12, -13, -17, -7, -8, Shade(body, 0.85f));
			Ell(g, 0, -9, 7.5f, 5.5f, body);
			Ell(g, -1, -9, 4.5f, 3.f, Shade(body, 0.9f));
			const float hx = 6.f + peck * 3.f, hy = -15.f + peck * 9.f;
			Ball(g, hx, hy, 3.5f, body);
			Tri(g, hx + 3.f, hy - 0.5f, hx + 6.f, hy + 0.5f, hx + 3.f, hy + 1.5f, Rgb(0xE8A030));
			Circ(g, hx, hy - 3.5f, 1.8f, Rgb(0xD83A2A));
			Circ(g, hx + 2.5f, hy + 2.5f, 1.2f, Rgb(0xD83A2A));
			if (!p.faceBack)
				Circ(g, hx + 1.2f, hy - 0.8f, 0.8f, Rgb(0x1A1210));
			break;
		}
		}
	}
}
