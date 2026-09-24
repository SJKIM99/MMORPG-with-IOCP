#include "render/Icons.h"
#include "render/Draw.h"
#include "core/Util.h"

#include "rlgl.h"
#include <cmath>
#include <vector>

namespace icons
{
	namespace
	{
		using util::Rgb;
		using util::Shade;
		using util::WithA;

		constexpr int kCell = 128;
		constexpr int kCols = 8;
		constexpr int kRows = 8;
		constexpr int kUnknownSlot = kCols * kRows - 1;
		Texture2D g_atlas{};
		int g_slotOf[256];   // iconId → 아틀라스 칸(Build가 표를 읽어 차례로 배정)

		int SlotOf(uint8_t iconId)
		{
			const int s = g_slotOf[iconId];
			return s >= 0 ? s : kUnknownSlot;
		}

		struct BladeSpec
		{
			float len = 38.f, width = 7.f, grip = 11.f, guard = 16.f;
			Color blade{}, edge{}, guardCol{}, grip2{};
			int style = 0;   // 0 곧은 검, 1 카타나, 2 단검, 3 목검, 4 대검, 5 화염검, 6 집행검, 7 지팡이
			Color glow{ 0, 0, 0, 0 };
		};

		BladeSpec SpecOf(uint16_t itemId)
		{
			BladeSpec s;
			switch (itemId)
			{
			case 2:  s = { 30.f, 7.f, 10.f, 12.f, Rgb(0xB88A58), Rgb(0xD8B080), Rgb(0x7A5634), Rgb(0x5A3A22), 3 }; break;
			case 3:  s = { 38.f, 6.5f, 11.f, 16.f, Rgb(0xAEB4BE), Rgb(0xE6EAF0), Rgb(0x4A4E58), Rgb(0x5A3A26), 0 }; break;
			case 4:  s = { 42.f, 7.f, 11.f, 18.f, Rgb(0xD2DAE6), Rgb(0xFFFFFF), Rgb(0xC8A040), Rgb(0x3A2A4A), 0 }; break;
			case 5:  s = { 42.f, 7.5f, 11.f, 18.f, Rgb(0xE8502A), Rgb(0xFFD060), Rgb(0x2A2226), Rgb(0x6A1A12), 5, Rgb(0xFF7A2A, 160) }; break;
			case 6:  s = { 56.f, 11.f, 14.f, 24.f, Rgb(0x5A6070), Rgb(0xC8D0DC), Rgb(0x8A2A2A), Rgb(0x2A2A30), 4, Rgb(0xFF3A3A, 90) }; break;
			case 15: s = { 25.f, 5.f, 9.f, 11.f, Rgb(0xB8D878), Rgb(0xF0FFC0), Rgb(0xC8A040), Rgb(0x4A5A2A), 2, Rgb(0xC8FF80, 70) }; break;
			case 16: s = { 47.f, 5.f, 15.f, 10.f, Rgb(0xE2E8F0), Rgb(0xFFFFFF), Rgb(0x2A2A2A), Rgb(0x3A3A6A), 1 }; break;
			case 17: s = { 48.f, 8.f, 12.f, 20.f, Rgb(0x2A2226), Rgb(0xFF6A20), Rgb(0x6A6A70), Rgb(0x1A1A1E), 5, Rgb(0xFF5A1A, 180) }; break;
			case 18: s = { 52.f, 14.f, 14.f, 22.f, Rgb(0x8A929C), Rgb(0xE8ECF2), Rgb(0x3A3A40), Rgb(0x4A2020), 6, Rgb(0xB02030, 80) }; break;
			case 19: s = { 46.f, 4.f, 16.f, 8.f, Rgb(0x7A5634), Rgb(0x6AD8FF), Rgb(0xC8A060), Rgb(0x5A3A22), 7, Rgb(0x6AD8FF, 90) }; break;
			case 20: s = { 22.f, 5.5f, 9.f, 10.f, Rgb(0x9098A8), Rgb(0xE8E4F8), Rgb(0x3A3440), Rgb(0x2A2226), 2, Rgb(0xB88AFF, 40) }; break;
			// V2 직업 무기 — 지팡이(나무 자루 + 수정 색) · 단검
			case 21: s = { 44.f, 4.5f, 16.f, 8.f, Rgb(0x8A6A3A), Rgb(0x8AE87A), Rgb(0xB89060), Rgb(0x5A3A22), 7, Rgb(0x8AE87A, 70) }; break;
			case 22: s = { 48.f, 4.5f, 16.f, 9.f, Rgb(0x4A3A5A), Rgb(0x6AD8FF), Rgb(0xC8A060), Rgb(0x2A2030), 7, Rgb(0x6AD8FF, 110) }; break;
			case 23: s = { 48.f, 5.f, 16.f, 9.f, Rgb(0x2A2226), Rgb(0xFF7A2A), Rgb(0x8A6A4A), Rgb(0x1A1416), 7, Rgb(0xFF7A2A, 150) }; break;
			case 24: s = { 52.f, 5.f, 17.f, 10.f, Rgb(0xE8E0D0), Rgb(0xC88AFF), Rgb(0xE8C060), Rgb(0x6A4A8A), 7, Rgb(0xC88AFF, 180) }; break;
			case 25: s = { 24.f, 5.5f, 9.f, 10.f, Rgb(0x6A6A7A), Rgb(0xC8B8FF), Rgb(0x2A2230), Rgb(0x1A161A), 2, Rgb(0xB88AFF, 50) }; break;
			case 26: s = { 27.f, 5.5f, 9.f, 11.f, Rgb(0xC8D8F0), Rgb(0xFFFFFF), Rgb(0x8AA0C8), Rgb(0x2A3040), 2, Rgb(0xB8D8FF, 80) }; break;
			case 27: s = { 26.f, 6.f, 9.f, 11.f, Rgb(0x6A8A4A), Rgb(0xC8FF8A), Rgb(0x3A2A1E), Rgb(0x2A1E14), 2, Rgb(0x8AE85A, 110) }; break;
			case 28: s = { 30.f, 6.5f, 10.f, 12.f, Rgb(0x2A2230), Rgb(0xFF4A6A), Rgb(0x6A6A70), Rgb(0x1A1016), 2, Rgb(0xFF3A5A, 150) }; break;
			default: s = { 34.f, 6.f, 10.f, 14.f, Rgb(0xB0B6C0), Rgb(0xE0E4EA), Rgb(0x5A5A5A), Rgb(0x5A3A26), 0 }; break;
			}
			return s;
		}

		inline Vector2 P(Vector2 o, Vector2 d, Vector2 n, float along, float across)
		{
			return Vector2{ o.x + d.x * along + n.x * across, o.y + d.y * along + n.y * across };
		}

		// ── 아이콘 그림 ──────────────────────────────────────────────────────────
		void Potion(Vector2 o, Color liquid, bool sparkle)
		{
			const Vector2 c{ o.x + 64, o.y + 78 };
			gfx::Ellipse(Vector2{ c.x, c.y + 34 }, 30, 7, Rgb(0x000000, 60));
			// 병(유리)
			gfx::Circle(c, 33, Rgb(0x2A3440, 230));
			gfx::Quad(Vector2{ o.x + 52, o.y + 26 }, Vector2{ o.x + 76, o.y + 26 }, Vector2{ o.x + 76, o.y + 52 }, Vector2{ o.x + 52, o.y + 52 }, Rgb(0x2A3440, 230));
			gfx::Circle(c, 30, Rgb(0xDCE8F0, 90));
			// 내용물(윗면이 평평한 원)
			std::vector<Vector2> pts;
			const float level = c.y - 12;
			for (int i = 0; i < 28; ++i)
			{
				const float a = i * util::kPi * 2.f / 28;
				Vector2 p{ c.x + std::cos(a) * 27.f, c.y + std::sin(a) * 27.f };
				p.y = std::max(p.y, level);
				pts.push_back(p);
			}
			gfx::PolyGrad(pts.data(), static_cast<int>(pts.size()), Vector2{ c.x - 6, c.y + 2 }, Shade(liquid, 1.35f), Shade(liquid, 0.7f));
			gfx::Ellipse(Vector2{ c.x, level }, 25, 4, Shade(liquid, 1.5f));
			// 거품
			gfx::Circle(Vector2{ c.x + 8, c.y + 8 }, 3, WithA(WHITE, 0.5f));
			gfx::Circle(Vector2{ c.x - 4, c.y + 16 }, 2, WithA(WHITE, 0.4f));
			// 목/코르크
			gfx::Quad(Vector2{ o.x + 55, o.y + 30 }, Vector2{ o.x + 73, o.y + 30 }, Vector2{ o.x + 73, o.y + 50 }, Vector2{ o.x + 55, o.y + 50 }, Rgb(0xDCE8F0, 80));
			gfx::Quad(Vector2{ o.x + 53, o.y + 16 }, Vector2{ o.x + 75, o.y + 16 }, Vector2{ o.x + 73, o.y + 32 }, Vector2{ o.x + 55, o.y + 32 }, Rgb(0xA0703A));
			gfx::Quad(Vector2{ o.x + 53, o.y + 16 }, Vector2{ o.x + 75, o.y + 16 }, Vector2{ o.x + 75, o.y + 20 }, Vector2{ o.x + 53, o.y + 20 }, Rgb(0xC8904A));
			// 유리 반사
			gfx::EllipseArc(c, 22, 22, util::kPi * 1.1f, util::kPi * 1.45f, 4.f, WithA(WHITE, 0.75f));
			gfx::Circle(Vector2{ c.x - 17, c.y - 4 }, 2.5f, WithA(WHITE, 0.8f));
			if (sparkle)
			{
				gfx::Star(Vector2{ c.x + 20, c.y - 22 }, 2.f, 9.f, 4, 0.f, Rgb(0xFFFFFF));
				gfx::Star(Vector2{ c.x - 22, c.y + 18 }, 1.5f, 6.f, 4, 0.3f, Rgb(0xFFFFFF, 200));
			}
		}

		void Scroll(Vector2 o, Color seal, int runeStyle)
		{
			const Color paper = Rgb(0xF0E2BE), paperDark = Rgb(0xC8B088);
			gfx::Ellipse(Vector2{ o.x + 64, o.y + 110 }, 38, 7, Rgb(0x000000, 60));
			// 펼쳐진 몸통
			gfx::Quad4(Vector2{ o.x + 30, o.y + 34 }, Vector2{ o.x + 98, o.y + 34 }, Vector2{ o.x + 98, o.y + 94 }, Vector2{ o.x + 30, o.y + 94 }, paper, Shade(paper, 0.92f), paperDark, Shade(paper, 0.96f));
			// 위/아래 말린 부분
			for (float y : { 34.f, 94.f })
			{
				gfx::Quad(Vector2{ o.x + 24, o.y + y - 7 }, Vector2{ o.x + 104, o.y + y - 7 }, Vector2{ o.x + 104, o.y + y + 7 }, Vector2{ o.x + 24, o.y + y + 7 }, Shade(paper, 0.88f));
				gfx::Quad(Vector2{ o.x + 24, o.y + y - 7 }, Vector2{ o.x + 104, o.y + y - 7 }, Vector2{ o.x + 104, o.y + y - 3 }, Vector2{ o.x + 24, o.y + y - 3 }, Shade(paper, 1.05f));
				gfx::Ellipse(Vector2{ o.x + 24, o.y + y }, 4, 7, paperDark);
				gfx::Ellipse(Vector2{ o.x + 104, o.y + y }, 4, 7, Shade(paperDark, 0.85f));
			}
			// 글자/룬
			const Color ink = runeStyle >= 2 ? Rgb(0xB0782A) : Rgb(0x6A5A4A);
			for (int i = 0; i < 4; ++i)
			{
				const float y = o.y + 48 + i * 10.f;
				const float w = (i % 2) ? 40.f : 52.f;
				gfx::Line(Vector2{ o.x + 38, y }, Vector2{ o.x + 38 + w * 0.45f, y }, 2.2f, ink);
				gfx::Line(Vector2{ o.x + 42 + w * 0.5f, y }, Vector2{ o.x + 38 + w, y }, 2.2f, ink);
			}
			// 리본 + 봉인
			gfx::Quad(Vector2{ o.x + 58, o.y + 34 }, Vector2{ o.x + 70, o.y + 34 }, Vector2{ o.x + 70, o.y + 94 }, Vector2{ o.x + 58, o.y + 94 }, Shade(seal, 0.9f));
			gfx::Circle(Vector2{ o.x + 64, o.y + 66 }, 13, Shade(seal, 0.7f));
			gfx::Circle(Vector2{ o.x + 64, o.y + 66 }, 11, seal);
			gfx::Star(Vector2{ o.x + 64, o.y + 66 }, 3.f, 7.f, 5, -util::kPi * 0.5f, Shade(seal, 1.4f));
			if (runeStyle == 3)
			{
				gfx::Star(Vector2{ o.x + 100, o.y + 22 }, 2.f, 11.f, 4, 0.f, Rgb(0xFFF4C0));
				gfx::Star(Vector2{ o.x + 26, o.y + 104 }, 2.f, 8.f, 4, 0.4f, Rgb(0xFFF4C0));
				gfx::Star(Vector2{ o.x + 106, o.y + 80 }, 1.5f, 6.f, 4, 0.2f, Rgb(0xFFFFFF));
			}
		}

		void Coins(Vector2 o)
		{
			const Color gold = Rgb(0xF2C23A), dark = Rgb(0xA87A18), light = Rgb(0xFFF0A0);
			gfx::Ellipse(Vector2{ o.x + 64, o.y + 104 }, 44, 9, Rgb(0x000000, 60));
			const float stacks[][3] = { { 42, 96, 4 }, { 78, 100, 3 }, { 60, 106, 1 } };
			for (const auto& s : stacks)
			{
				for (int i = 0; i < static_cast<int>(s[2]); ++i)
				{
					const Vector2 c{ o.x + s[0], o.y + s[1] - i * 8.f };
					gfx::Ellipse(Vector2{ c.x, c.y + 3 }, 20, 8, dark);
					gfx::Ellipse(c, 20, 8, gold);
					gfx::Ellipse(Vector2{ c.x - 2, c.y - 1 }, 13, 4.5f, light);
					gfx::Ellipse(Vector2{ c.x - 1, c.y }, 10, 3.2f, gold);
				}
			}
			// 세워진 동전
			const Vector2 c{ o.x + 86, o.y + 58 };
			gfx::Circle(Vector2{ c.x + 3, c.y }, 20, dark);
			gfx::Circle(c, 20, gold);
			gfx::Circle(c, 14, Shade(gold, 1.12f));
			gfx::Star(c, 3.f, 8.f, 4, 0.f, dark);
			gfx::Star(Vector2{ c.x - 10, c.y - 12 }, 1.5f, 7.f, 4, 0.f, WHITE);
		}

		// ── V2 방어구·장신구 아이콘 ────────────────────────────────────────────────
		struct GearLook { uint8_t icon; uint8_t style; uint32_t main, trim, accent; };
		constexpr GearLook kGearLooks[] =
		{
			// 투구
			{ 50, 0, 0x8A6038, 0x5A3A22, 0x2A1A12 }, { 51, 1, 0x9AA2AE, 0x5A606A, 0x3A3E46 },
			{ 52, 2, 0xC8D0DA, 0xD8B060, 0xA8302A }, { 68, 3, 0xF2EAD8, 0xE8C060, 0xFFF0B0 },
			{ 72, 4, 0x2A3A78, 0x8AB8FF, 0xFFFFFF },
			// 갑옷
			{ 53, 0, 0xC8A878, 0x8A6A48, 0x5A4430 }, { 54, 1, 0xA8B0BC, 0x6A707A, 0x4A4E56 },
			{ 55, 2, 0xC8D0DA, 0x8A929E, 0xD8B060 }, { 56, 3, 0x3A4A9A, 0xD8B060, 0x6AD8FF },
			{ 57, 4, 0x3A3236, 0x8A6AC8, 0x5A4638 }, { 69, 5, 0xF2EAD8, 0xE8C060, 0xFFF0B0 },
			{ 73, 6, 0x1E2A5A, 0x8AB8FF, 0xFFFFFF },
			// 장갑
			{ 58, 0, 0x8A6038, 0x5A3A22, 0x3A2416 }, { 59, 1, 0xA8B0BC, 0x5A606A, 0x3A3E46 },
			{ 70, 1, 0xF2EAD8, 0xE8C060, 0xFFF0B0 }, { 76, 2, 0x2E2830, 0xB88AFF, 0x1A161C },
			// 신발
			{ 60, 0, 0x7A5230, 0x4A301C, 0x2A1A10 }, { 61, 1, 0xA8B0BC, 0x5A606A, 0x3A3E46 },
			{ 71, 1, 0xF2EAD8, 0xE8C060, 0xFFF0B0 }, { 77, 2, 0x2E2830, 0xB88AFF, 0x1A161C },
			// 망토
			{ 62, 0, 0x5A6A3A, 0x3A2A1E, 0xC8A060 }, { 63, 0, 0x1E2448, 0x4A2A6A, 0xB8C8E8 },
			{ 78, 1, 0x2A1E3A, 0x6A4A9A, 0xE8E0FF },
			// 목걸이
			{ 64, 0, 0x6A4A2E, 0xE8E0C8, 0x8A6A4A }, { 65, 1, 0xC8CED8, 0x4AA8E8, 0xFFFFFF },
			{ 74, 2, 0xD8B060, 0x8AB8FF, 0xFFFFFF },
			// 반지
			{ 66, 0, 0xC87A48, 0x8A4A28, 0xF0B080 }, { 67, 1, 0xC8CED8, 0x6AD8A8, 0xFFFFFF },
			{ 75, 2, 0xD8B060, 0x8AB8FF, 0xFFFFFF }, { 79, 1, 0x3A3040, 0xB88AFF, 0xE8D8FF },
		};
		const GearLook* FindGearLook(uint8_t icon)
		{
			for (const auto& g : kGearLooks) if (g.icon == icon) return &g;
			return nullptr;
		}

		void IconShadow(Vector2 o, float w = 38.f) { gfx::Ellipse(Vector2{ o.x + 64, o.y + 112 }, w, 7, Rgb(0x000000, 60)); }

		void HelmetIcon(Vector2 o, const GearLook& g)
		{
			const Color m = Rgb(g.main), t = Rgb(g.trim), a = Rgb(g.accent);
			const Vector2 c{ o.x + 64, o.y + 66 };
			IconShadow(o, 34);
			switch (g.style)
			{
			case 0:   // 두건
			case 4:   // 별빛 두건
			{
				// 뒤로 흘러내리는 천 + 둥근 머리 + 어두운 얼굴 구멍
				gfx::Quad4(Vector2{ c.x - 30, c.y - 4 }, Vector2{ c.x + 30, c.y - 4 }, Vector2{ c.x + 36, c.y + 42 }, Vector2{ c.x - 36, c.y + 42 },
					Shade(m, 0.95f), Shade(m, 0.8f), Shade(m, 0.55f), Shade(m, 0.7f));
				gfx::Circle(c, 32, Shade(m, 0.75f));
				gfx::Circle(Vector2{ c.x - 4, c.y - 5 }, 29, m);
				gfx::Circle(Vector2{ c.x - 12, c.y - 16 }, 10, Shade(m, 1.25f));
				gfx::Ellipse(Vector2{ c.x + 2, c.y + 8 }, 17, 20, Rgb(0x14101A));
				gfx::EllipseArc(Vector2{ c.x + 2, c.y + 8 }, 19, 22, util::kPi * 1.05f, util::kPi * 1.95f, 3.f, t);
				gfx::Tri(Vector2{ c.x - 8, c.y - 36 }, Vector2{ c.x + 6, c.y - 34 }, Vector2{ c.x - 20, c.y - 50 }, Shade(m, 0.85f));
				if (g.style == 4)
				{
					for (int i = 0; i < 5; ++i)
					{
						const float ax = c.x - 22 + i * 11.f, ay = c.y - 18 + ((i * 7) % 3) * 9.f - (i == 2 ? 8.f : 0.f);
						gfx::Star(Vector2{ ax, ay }, 1.2f, 4.5f + (i % 2) * 2.f, 4, 0.f, util::WithA(a, 0.9f));
					}
					gfx::CircleGrad(Vector2{ c.x + 2, c.y + 10 }, 14, util::WithA(t, 0.45f), util::WithA(t, 0.f));
				}
				break;
			}
			case 1:   // 철 투구(챙 + 코가리개)
			{
				gfx::Ellipse(Vector2{ c.x, c.y + 18 }, 44, 12, Shade(m, 0.6f));
				gfx::Ellipse(Vector2{ c.x, c.y + 15 }, 42, 10, Shade(m, 0.85f));
				gfx::Circle(Vector2{ c.x, c.y + 6 }, 32, Shade(m, 0.75f));
				gfx::Circle(Vector2{ c.x - 3, c.y + 2 }, 29, m);
				gfx::Quad(Vector2{ c.x - 36, c.y + 8 }, Vector2{ c.x + 36, c.y + 8 }, Vector2{ c.x + 36, c.y + 20 }, Vector2{ c.x - 36, c.y + 20 }, Shade(m, 0.8f));
				gfx::Line(Vector2{ c.x, c.y - 28 }, Vector2{ c.x, c.y + 8 }, 5.f, t);
				gfx::Quad(Vector2{ c.x - 4, c.y + 8 }, Vector2{ c.x + 4, c.y + 8 }, Vector2{ c.x + 3, c.y + 34 }, Vector2{ c.x - 3, c.y + 34 }, Shade(m, 0.9f));
				for (int i = -2; i <= 2; ++i) gfx::Circle(Vector2{ c.x + i * 14.f, c.y + 14 }, 2.5f, Shade(t, 1.3f));
				gfx::Circle(Vector2{ c.x - 14, c.y - 12 }, 7, Shade(m, 1.35f));
				break;
			}
			case 2:   // 기사의 투구(닫힌 면갑 + 깃털)
			case 3:   // 여명의 투구(날개)
			{
				if (g.style == 3)
				{
					for (int sgn = -1; sgn <= 1; sgn += 2)
						for (int i = 0; i < 3; ++i)
						{
							const float x0 = c.x + sgn * 26.f, y0 = c.y - 4 + i * 8.f;
							gfx::Tri(Vector2{ x0, y0 - 6 }, Vector2{ x0, y0 + 6 }, Vector2{ x0 + sgn * (34.f - i * 7.f), y0 - 18.f + i * 4.f }, Shade(a, 1.f - i * 0.1f));
						}
				}
				else
				{
					const Vector2 pl{ c.x + 4, c.y - 34 };
					gfx::LineGrad(pl, Vector2{ c.x - 30, c.y - 46 }, 12.f, 4.f, a, Shade(a, 0.7f));
					gfx::LineGrad(pl, Vector2{ c.x - 34, c.y - 30 }, 9.f, 3.f, Shade(a, 0.9f), Shade(a, 0.6f));
				}
				gfx::Quad4(Vector2{ c.x - 28, c.y - 18 }, Vector2{ c.x + 28, c.y - 18 }, Vector2{ c.x + 24, c.y + 36 }, Vector2{ c.x - 24, c.y + 36 },
					Shade(m, 1.1f), Shade(m, 0.8f), Shade(m, 0.6f), Shade(m, 0.85f));
				gfx::Circle(Vector2{ c.x, c.y - 16 }, 28, Shade(m, 0.8f));
				gfx::Circle(Vector2{ c.x - 3, c.y - 19 }, 25, m);
				gfx::Line(Vector2{ c.x - 20, c.y + 2 }, Vector2{ c.x + 20, c.y + 2 }, 5.f, Rgb(0x14121A));
				gfx::Line(Vector2{ c.x, c.y - 42 }, Vector2{ c.x, c.y + 34 }, 4.f, t);
				for (int i = 0; i < 3; ++i) gfx::Line(Vector2{ c.x + 8, c.y + 12 + i * 7.f }, Vector2{ c.x + 18, c.y + 12 + i * 7.f }, 2.f, Rgb(0x14121A));
				gfx::Circle(Vector2{ c.x - 12, c.y - 28 }, 6, Shade(m, 1.35f));
				if (g.style == 3) gfx::CircleGrad(Vector2{ c.x, c.y - 8 }, 30, util::WithA(a, 0.35f), util::WithA(a, 0.f));
				break;
			}
			default: break;
			}
		}

		void ArmorIcon(Vector2 o, const GearLook& g)
		{
			const Color m = Rgb(g.main), t = Rgb(g.trim), a = Rgb(g.accent);
			const float cx = o.x + 64, top = o.y + 20;
			IconShadow(o, 40);
			const bool robe = g.style == 3 || g.style == 6;
			if (robe)
			{
				// 긴 로브: 어깨에서 아래로 넓어지는 옷자락
				gfx::Quad4(Vector2{ cx - 26, top + 10 }, Vector2{ cx + 26, top + 10 }, Vector2{ cx + 40, top + 94 }, Vector2{ cx - 40, top + 94 },
					Shade(m, 1.15f), Shade(m, 0.85f), Shade(m, 0.55f), Shade(m, 0.8f));
				gfx::Quad(Vector2{ cx - 5, top + 20 }, Vector2{ cx + 5, top + 20 }, Vector2{ cx + 8, top + 94 }, Vector2{ cx - 8, top + 94 }, Shade(m, 0.6f));
				gfx::Line(Vector2{ cx - 40, top + 92 }, Vector2{ cx + 40, top + 92 }, 5.f, t);
				gfx::Line(Vector2{ cx - 24, top + 48 }, Vector2{ cx + 24, top + 48 }, 6.f, Shade(t, 0.8f));
				gfx::Circle(Vector2{ cx, top + 48 }, 5.5f, a);
				// 어깨 + 깃
				gfx::Ellipse(Vector2{ cx - 26, top + 14 }, 14, 10, Shade(m, 0.9f));
				gfx::Ellipse(Vector2{ cx + 26, top + 14 }, 14, 10, Shade(m, 0.75f));
				gfx::Tri(Vector2{ cx - 14, top + 6 }, Vector2{ cx + 14, top + 6 }, Vector2{ cx, top + 26 }, Rgb(0x14101A));
				gfx::Line(Vector2{ cx - 14, top + 6 }, Vector2{ cx, top + 26 }, 3.f, t);
				gfx::Line(Vector2{ cx + 14, top + 6 }, Vector2{ cx, top + 26 }, 3.f, t);
				if (g.style == 6)
					for (int i = 0; i < 7; ++i)
					{
						const float sx = cx - 28 + (i * 37 % 56), sy = top + 56 + (i * 23 % 34);
						gfx::Star(Vector2{ sx, sy }, 1.f, 3.5f + (i % 3), 4, 0.f, util::WithA(a, 0.85f));
					}
				return;
			}
			// 몸통형 갑옷
			gfx::Quad4(Vector2{ cx - 30, top + 12 }, Vector2{ cx + 30, top + 12 }, Vector2{ cx + 24, top + 86 }, Vector2{ cx - 24, top + 86 },
				Shade(m, 1.15f), Shade(m, 0.85f), Shade(m, 0.6f), Shade(m, 0.9f));
			gfx::Tri(Vector2{ cx - 14, top + 10 }, Vector2{ cx + 14, top + 10 }, Vector2{ cx, top + 26 }, Rgb(0x16121A));
			switch (g.style)
			{
			case 0:   // 누빔: 마름모 누빔선
				for (int i = 0; i < 4; ++i)
				{
					const float y = top + 30 + i * 14.f;
					gfx::Line(Vector2{ cx - 24, y }, Vector2{ cx + 24, y + 8 }, 1.6f, Shade(t, 0.9f));
					gfx::Line(Vector2{ cx + 24, y }, Vector2{ cx - 24, y + 8 }, 1.6f, Shade(t, 0.9f));
				}
				break;
			case 1:   // 사슬: 고리 무늬
				for (int yy = 0; yy < 6; ++yy)
					for (int xx = 0; xx < 5; ++xx)
					{
						const float x = cx - 20 + xx * 10.f + (yy % 2) * 5.f, y = top + 30 + yy * 9.f;
						gfx::EllipseArc(Vector2{ x, y }, 4.f, 3.f, 0.f, util::kPi * 2.f, 1.4f, Shade(t, 1.1f));
					}
				break;
			case 2: case 5:   // 판금: 가슴판 + 복부판 + 금테
			{
				gfx::Quad(Vector2{ cx - 24, top + 28 }, Vector2{ cx + 24, top + 28 }, Vector2{ cx + 20, top + 52 }, Vector2{ cx - 20, top + 52 }, Shade(m, 1.12f));
				for (int i = 0; i < 3; ++i)
					gfx::Line(Vector2{ cx - 21 + i, top + 58 + i * 9.f }, Vector2{ cx + 21 - i, top + 58 + i * 9.f }, 2.2f, Shade(t, 0.9f));
				gfx::Line(Vector2{ cx, top + 26 }, Vector2{ cx, top + 84 }, 2.f, Shade(m, 0.6f));
				gfx::Quad(Vector2{ cx - 10, top + 30 }, Vector2{ cx - 2, top + 30 }, Vector2{ cx - 4, top + 48 }, Vector2{ cx - 10, top + 46 }, util::WithA(WHITE, 0.35f));
				if (g.style == 5)
				{
					gfx::Circle(Vector2{ cx, top + 40 }, 9, t);
					gfx::Star(Vector2{ cx, top + 40 }, 5.f, 13.f, 8, 0.f, util::WithA(a, 0.9f));
					gfx::CircleGrad(Vector2{ cx, top + 40 }, 26, util::WithA(a, 0.35f), util::WithA(a, 0.f));
				}
				else
					gfx::Circle(Vector2{ cx, top + 40 }, 4.5f, a);
				break;
			}
			case 4:   // 가죽 조끼: X자 끈
				gfx::Line(Vector2{ cx - 20, top + 28 }, Vector2{ cx + 18, top + 78 }, 3.5f, Shade(t, 0.8f));
				gfx::Line(Vector2{ cx + 20, top + 28 }, Vector2{ cx - 18, top + 78 }, 3.5f, Shade(t, 0.8f));
				gfx::Circle(Vector2{ cx, top + 53 }, 4.f, t);
				break;
			default: break;
			}
			// 어깨 보호대
			const float pr = g.style == 2 || g.style == 5 ? 17.f : 13.f;
			gfx::Ellipse(Vector2{ cx - 30, top + 16 }, pr, pr * 0.72f, Shade(m, 0.95f));
			gfx::Ellipse(Vector2{ cx + 30, top + 16 }, pr, pr * 0.72f, Shade(m, 0.75f));
			gfx::EllipseArc(Vector2{ cx - 30, top + 16 }, pr, pr * 0.72f, util::kPi, util::kPi * 2.f, 2.5f, t);
			gfx::EllipseArc(Vector2{ cx + 30, top + 16 }, pr, pr * 0.72f, util::kPi, util::kPi * 2.f, 2.5f, Shade(t, 0.8f));
			gfx::Line(Vector2{ cx - 24, top + 86 }, Vector2{ cx + 24, top + 86 }, 4.f, Shade(t, 0.7f));
		}

		void GlovesIcon(Vector2 o, const GearLook& g)
		{
			const Color m = Rgb(g.main), t = Rgb(g.trim);
			IconShadow(o, 36);
			for (int k = 0; k < 2; ++k)
			{
				const float dx = k == 0 ? -18.f : 20.f, dy = k == 0 ? 6.f : -4.f;
				const Color mm = k == 0 ? Shade(m, 0.8f) : m;
				const Vector2 c{ o.x + 64 + dx, o.y + 66 + dy };
				// 손목 커프
				gfx::Quad(Vector2{ c.x - 15, c.y + 18 }, Vector2{ c.x + 15, c.y + 18 }, Vector2{ c.x + 17, c.y + 40 }, Vector2{ c.x - 17, c.y + 40 }, Shade(t, k == 0 ? 0.8f : 1.f));
				// 손등
				gfx::Ellipse(Vector2{ c.x, c.y + 6 }, 16, 18, mm);
				// 손가락 4 + 엄지
				for (int f = 0; f < 4; ++f)
				{
					const float fx = c.x - 11 + f * 7.3f;
					gfx::Capsule(Vector2{ fx, c.y - 4 }, Vector2{ fx, c.y - 22 + std::abs(f - 1.5f) * 3.f }, 3.6f, Shade(mm, 1.05f - f * 0.04f));
					if (g.style == 1) gfx::Line(Vector2{ fx - 3, c.y - 12 }, Vector2{ fx + 3, c.y - 12 }, 1.2f, Shade(mm, 0.6f));
					if (g.style == 2) gfx::Tri(Vector2{ fx - 2.5f, c.y - 22 }, Vector2{ fx + 2.5f, c.y - 22 }, Vector2{ fx + 1.f, c.y - 32 }, t);
				}
				gfx::Capsule(Vector2{ c.x - 14, c.y + 8 }, Vector2{ c.x - 22, c.y - 4 }, 4.f, Shade(mm, 0.9f));
				if (g.style == 1)
					for (int i = 0; i < 3; ++i) gfx::Line(Vector2{ c.x - 13, c.y + 2 + i * 6.f }, Vector2{ c.x + 13, c.y + 2 + i * 6.f }, 1.4f, Shade(mm, 0.65f));
				gfx::Circle(Vector2{ c.x - 5, c.y + 1 }, 4, util::WithA(WHITE, 0.25f));
			}
		}

		void BootsIcon(Vector2 o, const GearLook& g)
		{
			const Color m = Rgb(g.main), t = Rgb(g.trim);
			IconShadow(o, 40);
			for (int k = 0; k < 2; ++k)
			{
				const float dx = k == 0 ? -16.f : 14.f, dy = k == 0 ? -2.f : 6.f;
				const Color mm = k == 0 ? Shade(m, 0.78f) : m;
				const float x = o.x + 56 + dx, y = o.y + 24 + dy;
				// 목(정강이) + 발
				gfx::Quad4(Vector2{ x - 12, y }, Vector2{ x + 12, y }, Vector2{ x + 12, y + 58 }, Vector2{ x - 12, y + 58 }, Shade(mm, 1.1f), Shade(mm, 0.85f), Shade(mm, 0.7f), Shade(mm, 0.95f));
				gfx::Quad4(Vector2{ x - 12, y + 52 }, Vector2{ x + 12, y + 50 }, Vector2{ x + 34, y + 70 }, Vector2{ x - 12, y + 74 }, Shade(mm, 1.f), Shade(mm, 0.9f), Shade(mm, 0.6f), Shade(mm, 0.7f));
				gfx::Ellipse(Vector2{ x + 26, y + 70 }, 10, 5, Shade(mm, 0.75f));
				gfx::Line(Vector2{ x - 13, y + 74 }, Vector2{ x + 32, y + 74 }, 4.f, Shade(t, 0.6f));
				gfx::Line(Vector2{ x - 13, y + 4 }, Vector2{ x + 13, y + 4 }, 5.f, t);
				if (g.style == 1)
				{
					gfx::Quad(Vector2{ x - 12, y + 10 }, Vector2{ x + 12, y + 10 }, Vector2{ x + 10, y + 34 }, Vector2{ x - 10, y + 34 }, Shade(mm, 1.2f));
					gfx::Circle(Vector2{ x, y + 38 }, 5.f, t);
				}
				else if (g.style == 2)
				{
					gfx::Tri(Vector2{ x + 10, y + 10 }, Vector2{ x + 10, y + 30 }, Vector2{ x + 22, y + 14 }, t);
				}
				else
				{
					for (int i = 0; i < 3; ++i) gfx::Line(Vector2{ x - 8, y + 16 + i * 11.f }, Vector2{ x + 8, y + 20 + i * 11.f }, 1.6f, Shade(t, 0.8f));
				}
			}
		}

		void CloakIcon(Vector2 o, const GearLook& g)
		{
			const Color m = Rgb(g.main), l = Rgb(g.trim), a = Rgb(g.accent);
			const float cx = o.x + 64, top = o.y + 20;
			IconShadow(o, 42);
			// 안감이 보이는 펼친 망토
			gfx::Quad4(Vector2{ cx - 20, top + 12 }, Vector2{ cx + 20, top + 12 }, Vector2{ cx + 44, top + 92 }, Vector2{ cx - 44, top + 92 },
				Shade(l, 1.1f), Shade(l, 0.9f), Shade(l, 0.6f), Shade(l, 0.75f));
			gfx::Quad4(Vector2{ cx - 20, top + 12 }, Vector2{ cx - 6, top + 14 }, Vector2{ cx - 18, top + 94 }, Vector2{ cx - 46, top + 92 },
				Shade(m, 1.15f), Shade(m, 1.f), Shade(m, 0.65f), Shade(m, 0.8f));
			gfx::Quad4(Vector2{ cx + 6, top + 14 }, Vector2{ cx + 20, top + 12 }, Vector2{ cx + 46, top + 92 }, Vector2{ cx + 18, top + 94 },
				Shade(m, 0.95f), Shade(m, 0.85f), Shade(m, 0.55f), Shade(m, 0.7f));
			// 주름
			for (int i = 0; i < 3; ++i)
			{
				gfx::Line(Vector2{ cx - 14 - i * 2.f, top + 30 }, Vector2{ cx - 24 - i * 6.f, top + 88 }, 1.6f, Shade(m, 0.6f));
				gfx::Line(Vector2{ cx + 14 + i * 2.f, top + 30 }, Vector2{ cx + 24 + i * 6.f, top + 88 }, 1.6f, Shade(m, 0.5f));
			}
			// 깃 + 걸쇠
			gfx::Ellipse(Vector2{ cx, top + 12 }, 24, 8, Shade(m, 0.8f));
			gfx::Circle(Vector2{ cx, top + 16 }, 6.5f, a);
			gfx::Circle(Vector2{ cx - 1.5f, top + 14.5f }, 2.5f, Shade(a, 1.4f));
			if (g.style == 1)
			{
				gfx::Circle(Vector2{ cx + 26, top + 58 }, 10, util::WithA(a, 0.9f));
				gfx::Circle(Vector2{ cx + 30, top + 55 }, 9, Shade(m, 0.95f));
				gfx::CircleGrad(Vector2{ cx + 24, top + 60 }, 22, util::WithA(a, 0.3f), util::WithA(a, 0.f));
			}
		}

		void NecklaceIcon(Vector2 o, const GearLook& g)
		{
			const Color ch = Rgb(g.main), gem = Rgb(g.trim), a = Rgb(g.accent);
			const Vector2 c{ o.x + 64, o.y + 40 };
			IconShadow(o, 26);
			gfx::EllipseArc(c, 36, 30, util::kPi * 0.05f, util::kPi * 0.95f, 3.5f, Shade(ch, 0.8f));
			for (int i = 0; i <= 12; ++i)
			{
				const float ang = util::kPi * (0.08f + 0.84f * i / 12.f);
				gfx::Circle(Vector2{ c.x + std::cos(ang) * 36.f, c.y + std::sin(ang) * 30.f }, 2.2f, Shade(ch, 1.2f));
			}
			const Vector2 p{ c.x, c.y + 44 };
			switch (g.style)
			{
			case 0:   // 뼈 이빨
				gfx::Tri(Vector2{ p.x - 9, p.y - 10 }, Vector2{ p.x + 9, p.y - 10 }, Vector2{ p.x + 2, p.y + 26 }, gem);
				gfx::Tri(Vector2{ p.x - 9, p.y - 10 }, Vector2{ p.x - 1, p.y - 10 }, Vector2{ p.x + 2, p.y + 26 }, Shade(gem, 1.1f));
				gfx::Circle(Vector2{ p.x, p.y - 12 }, 4.f, a);
				break;
			case 1:   // 물방울 보석
				gfx::Circle(Vector2{ p.x, p.y - 12 }, 3.5f, ch);
				gfx::CircleGrad(Vector2{ p.x, p.y + 6 }, 22, util::WithA(gem, 0.4f), util::WithA(gem, 0.f));
				gfx::Tri(Vector2{ p.x - 11, p.y + 4 }, Vector2{ p.x + 11, p.y + 4 }, Vector2{ p.x, p.y - 10 }, Shade(gem, 0.9f));
				gfx::Circle(Vector2{ p.x, p.y + 8 }, 11, gem);
				gfx::Circle(Vector2{ p.x - 4, p.y + 4 }, 3.5f, util::WithA(a, 0.9f));
				gfx::EllipseArc(Vector2{ p.x, p.y + 8 }, 11, 11, 0.f, util::kPi * 2.f, 2.f, ch);
				break;
			default:  // 별
				gfx::CircleGrad(p, 26, util::WithA(gem, 0.45f), util::WithA(gem, 0.f));
				gfx::Star(p, 7.f, 17.f, 5, -util::kPi * 0.5f, ch);
				gfx::Star(p, 4.f, 11.f, 5, -util::kPi * 0.5f, gem);
				gfx::Circle(Vector2{ p.x - 3, p.y - 3 }, 2.f, a);
				break;
			}
		}

		void RingIcon(Vector2 o, const GearLook& g)
		{
			const Color band = Rgb(g.main), gem = Rgb(g.trim), a = Rgb(g.accent);
			const Vector2 c{ o.x + 64, o.y + 74 };
			IconShadow(o, 30);
			gfx::EllipseArc(c, 30, 22, 0.f, util::kPi * 2.f, 9.f, Shade(band, 0.7f));
			gfx::EllipseArc(Vector2{ c.x, c.y - 2 }, 30, 22, util::kPi * 1.05f, util::kPi * 1.95f, 8.f, band);
			gfx::EllipseArc(Vector2{ c.x, c.y - 4 }, 28, 20, util::kPi * 1.2f, util::kPi * 1.5f, 2.5f, Shade(band, 1.5f));
			if (g.style == 0)
			{
				gfx::EllipseArc(Vector2{ c.x, c.y - 2 }, 30, 22, util::kPi * 0.1f, util::kPi * 0.9f, 8.f, Shade(band, 0.85f));
				return;
			}
			const Vector2 s{ c.x, c.y - 26 };
			gfx::Quad(Vector2{ s.x - 10, s.y + 4 }, Vector2{ s.x + 10, s.y + 4 }, Vector2{ s.x + 6, s.y + 12 }, Vector2{ s.x - 6, s.y + 12 }, Shade(band, 0.9f));
			gfx::CircleGrad(s, 22, util::WithA(gem, 0.4f), util::WithA(gem, 0.f));
			if (g.style == 2)
				gfx::Star(s, 5.f, 13.f, 4, 0.f, gem);
			else
			{
				gfx::Circle(s, 11, Shade(gem, 0.7f));
				gfx::Circle(Vector2{ s.x - 1, s.y - 1 }, 9, gem);
			}
			gfx::Circle(Vector2{ s.x - 3.5f, s.y - 3.5f }, 2.5f, util::WithA(a, 0.9f));
		}

		bool DrawGearIcon(uint8_t iconId, uint16_t itemId, Vector2 o)
		{
			const GearLook* g = FindGearLook(iconId);
			if (!g) return false;
			switch (ItemTableData::SlotOf(itemId))
			{
			case EquipSlot::Helmet:   HelmetIcon(o, *g); return true;
			case EquipSlot::Armor:    ArmorIcon(o, *g); return true;
			case EquipSlot::Gloves:   GlovesIcon(o, *g); return true;
			case EquipSlot::Boots:    BootsIcon(o, *g); return true;
			case EquipSlot::Cloak:    CloakIcon(o, *g); return true;
			case EquipSlot::Necklace: NecklaceIcon(o, *g); return true;
			case EquipSlot::Ring:     RingIcon(o, *g); return true;
			default: return false;
			}
		}

		void DrawIconInto(uint8_t iconId, uint16_t itemId, Vector2 o)
		{
			if (DrawGearIcon(iconId, itemId, o))
				return;
			switch (iconId)
			{
			case 1: Potion(o, Rgb(0xE0302A), false); break;
			case 2: Potion(o, Rgb(0xF08A20), false); break;
			case 3: Potion(o, Rgb(0xB8E8FF), true); break;
			case 4: Potion(o, Rgb(0x3AC05A), false); break;
			case 20: Scroll(o, Rgb(0x3A7AD8), 0); break;
			case 21: Scroll(o, Rgb(0x9A4AD8), 1); break;
			case 22: Scroll(o, Rgb(0xD89A2A), 2); break;
			case 23: Scroll(o, Rgb(0xF2D050), 3); break;
			case 30: Coins(o); break;
			default:
			{
				const BladeSpec s = SpecOf(itemId);
				const float total = s.len + s.grip;
				const float scale = 104.f / total;
				// 손잡이 쪽이 왼쪽 아래, 칼끝이 오른쪽 위
				const Vector2 hilt{ o.x + 20 + s.grip * scale * 0.35f, o.y + 108 - s.grip * scale * 0.35f };
				gfx::Ellipse(Vector2{ o.x + 64, o.y + 112 }, 40, 6, Rgb(0x000000, 50));
				DrawWeapon(itemId, hilt, -util::kPi * 0.25f, scale, 0.f);
				break;
			}
			}
		}

		// 투명 픽셀의 RGB를 이웃 불투명 픽셀 색으로 번지게(밉맵 가장자리 검은 테 방지)
		void BleedEdges(Image& img, int passes)
		{
			auto* px = static_cast<unsigned char*>(img.data);
			const int w = img.width, h = img.height;
			std::vector<unsigned char> src(px, px + w * h * 4);
			for (int pass = 0; pass < passes; ++pass)
			{
				std::vector<unsigned char> out = src;
				for (int y = 0; y < h; ++y)
					for (int x = 0; x < w; ++x)
					{
						unsigned char* p = &out[(y * w + x) * 4];
						if (src[(y * w + x) * 4 + 3] > 8) continue;
						int r = 0, g = 0, b = 0, n = 0;
						for (int dy = -1; dy <= 1; ++dy)
							for (int dx = -1; dx <= 1; ++dx)
							{
								const int nx = x + dx, ny = y + dy;
								if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
								const unsigned char* q = &src[(ny * w + nx) * 4];
								if (q[3] <= 8) continue;
								r += q[0]; g += q[1]; b += q[2]; ++n;
							}
						if (n > 0) { p[0] = static_cast<unsigned char>(r / n); p[1] = static_cast<unsigned char>(g / n); p[2] = static_cast<unsigned char>(b / n); p[3] = 1; }
					}
				src.swap(out);
			}
			for (int i = 0; i < w * h; ++i)
			{
				px[i * 4 + 0] = src[i * 4 + 0];
				px[i * 4 + 1] = src[i * 4 + 1];
				px[i * 4 + 2] = src[i * 4 + 2];
				if (px[i * 4 + 3] == 0 && src[i * 4 + 3] == 1) px[i * 4 + 3] = 0;
			}
		}
	}

	float WeaponLength(uint16_t itemId)
	{
		const BladeSpec s = SpecOf(itemId);
		return s.len + s.grip * 0.5f;
	}

	void DrawWeapon(uint16_t itemId, Vector2 hilt, float angle, float scale, float time)
	{
		const BladeSpec s = SpecOf(itemId);
		const Vector2 d{ std::cos(angle), std::sin(angle) };
		const Vector2 n{ -d.y, d.x };
		const float len = s.len * scale, w = s.width * scale * 0.5f, grip = s.grip * scale, gw = s.guard * scale * 0.5f;

		// 강화/특수 무기 빛
		if (s.glow.a > 0)
		{
			const float pulse = 0.8f + 0.2f * std::sin(time * 5.f);
			for (int i = 1; i <= 3; ++i)
			{
				const float k = i / 3.f;
				gfx::LineGrad(P(hilt, d, n, grip * 0.5f, 0), P(hilt, d, n, grip * 0.5f + len * (1.f + 0.05f * i), 0), w * (2.f + i * 1.3f), w * (1.f + i * 0.6f),
					util::MulA(s.glow, 0.35f * (1.f - k * 0.5f) * pulse), util::MulA(s.glow, 0.05f));
			}
		}

		// 손잡이 + 폼멜
		gfx::Line(P(hilt, d, n, -grip * 0.5f, 0), P(hilt, d, n, grip * 0.5f, 0), std::max(2.f, w * 0.9f), s.grip2);
		for (int i = 0; i < 3; ++i)
		{
			const float t = -grip * 0.35f + i * grip * 0.3f;
			gfx::Line(P(hilt, d, n, t, -w * 0.5f), P(hilt, d, n, t + grip * 0.12f, w * 0.5f), std::max(1.f, scale * 1.2f), Shade(s.grip2, 1.4f));
		}
		gfx::Circle(P(hilt, d, n, -grip * 0.55f, 0), std::max(2.f, w * 0.9f), s.guardCol);

		const Vector2 base = P(hilt, d, n, grip * 0.5f, 0);
		// 칼날
		switch (s.style)
		{
		case 7:  // 지팡이 — 나무 자루 + 끝의 룬 수정
		{
			const float sw = std::max(1.5f, w * 1.1f);
			gfx::Line(P(hilt, d, n, -grip, 0), P(base, d, n, len * 0.92f, 0), sw * 1.5f, s.blade);
			gfx::Line(P(hilt, d, n, -grip, -sw * 0.3f), P(base, d, n, len * 0.9f, -sw * 0.3f), std::max(1.f, sw * 0.5f), Shade(s.blade, 1.35f));
			// 감긴 끈
			for (int i = 0; i < 3; ++i)
				gfx::Line(P(base, d, n, len * (0.55f + i * 0.08f), -sw), P(base, d, n, len * (0.6f + i * 0.08f), sw), std::max(1.f, scale * 1.4f), s.guardCol);
			// 갈래 머리 + 수정
			const Vector2 top = P(base, d, n, len, 0);
			gfx::Line(P(base, d, n, len * 0.9f, 0), P(base, d, n, len * 1.02f, -sw * 2.2f), sw, Shade(s.blade, 0.9f));
			gfx::Line(P(base, d, n, len * 0.9f, 0), P(base, d, n, len * 1.02f, sw * 2.2f), sw, Shade(s.blade, 0.9f));
			const float cr = std::max(2.f, scale * 4.5f);
			const float pulse = 0.8f + 0.2f * std::sin(time * 4.f);
			gfx::CircleGrad(top, cr * 2.6f * pulse, util::MulA(s.edge, 0.45f), util::WithA(s.edge, 0.f));
			const Vector2 q[4] = { P(top, d, n, cr * 1.4f, 0), P(top, d, n, 0, cr), P(top, d, n, -cr * 1.1f, 0), P(top, d, n, 0, -cr) };
			gfx::Quad(q[0], q[1], q[2], q[3], s.edge);
			gfx::Tri(q[0], q[1], q[2], Shade(s.edge, 1.35f));
			return;
		}
		case 1:  // 카타나 — 가늘고 살짝 휜 날
		{
			const int seg = 8;
			for (int i = 0; i < seg; ++i)
			{
				const float t0 = static_cast<float>(i) / seg, t1 = static_cast<float>(i + 1) / seg;
				const float c0 = std::sin(t0 * 1.6f) * len * 0.07f, c1 = std::sin(t1 * 1.6f) * len * 0.07f;
				const float w0 = w * (1.f - t0 * 0.25f), w1 = (i == seg - 1) ? 0.f : w * (1.f - t1 * 0.25f);
				const Vector2 a0 = P(base, d, n, len * t0, c0 - w0), b0 = P(base, d, n, len * t0, c0 + w0);
				const Vector2 a1 = P(base, d, n, len * t1, c1 - w1), b1 = P(base, d, n, len * t1, c1 + w1);
				gfx::Quad(a0, a1, b1, b0, s.blade);
				gfx::Line(a0, a1, std::max(1.f, scale * 1.4f), s.edge);
			}
			gfx::Ellipse(base, gw * 1.1f, gw * 1.1f, s.guardCol);
			gfx::Circle(base, gw * 0.5f, Shade(s.guardCol, 1.6f));
			return;
		}
		case 6:  // 집행검 — 넓고 끝이 비스듬히 잘린 날
		{
			const Vector2 q[5] = { P(base, d, n, 0, -w * 0.8f), P(base, d, n, len * 0.95f, -w * 1.2f), P(base, d, n, len, w * 0.2f), P(base, d, n, len * 0.8f, w * 1.1f), P(base, d, n, 0, w * 0.8f) };
			gfx::Poly(q, 5, s.blade);
			gfx::Line(q[1], q[2], std::max(1.f, scale * 2.f), s.edge);
			gfx::Line(q[2], q[3], std::max(1.f, scale * 2.f), s.edge);
			gfx::Line(P(base, d, n, len * 0.1f, 0), P(base, d, n, len * 0.7f, 0), std::max(1.f, scale * 1.5f), Shade(s.blade, 0.7f));
			gfx::Circle(P(base, d, n, len * 0.35f, w * 0.1f), w * 0.3f, Rgb(0xB02030));
			break;
		}
		default:
		{
			const float tip = (s.style == 2) ? 0.7f : (s.style == 4 ? 0.85f : 0.8f);
			const float ww = (s.style == 4) ? w * 1.1f : w;
			const Vector2 bl = P(base, d, n, 0, -ww), br = P(base, d, n, 0, ww);
			const Vector2 sl = P(base, d, n, len * tip, -ww * 0.9f), sr = P(base, d, n, len * tip, ww * 0.9f);
			const Vector2 tp = P(base, d, n, len, 0);
			gfx::Quad(bl, sl, P(base, d, n, len * tip, 0), P(base, d, n, 0, 0), Shade(s.blade, 1.12f));
			gfx::Quad(P(base, d, n, 0, 0), P(base, d, n, len * tip, 0), sr, br, Shade(s.blade, 0.85f));
			gfx::Tri(sl, tp, P(base, d, n, len * tip, 0), Shade(s.blade, 1.18f));
			gfx::Tri(P(base, d, n, len * tip, 0), tp, sr, Shade(s.blade, 0.8f));
			gfx::Line(bl, sl, std::max(1.f, scale * 1.2f), s.edge);
			gfx::Line(sl, tp, std::max(1.f, scale * 1.2f), s.edge);
			if (s.style != 3)
				gfx::Line(P(base, d, n, len * 0.05f, 0), P(base, d, n, len * tip * 0.9f, 0), std::max(1.f, scale * 1.1f), Shade(s.blade, 0.68f));
			if (s.style == 3)
				for (int i = 0; i < 3; ++i)
					gfx::Line(P(base, d, n, len * (0.15f + i * 0.22f), -ww * 0.6f), P(base, d, n, len * (0.25f + i * 0.22f), ww * 0.2f), std::max(1.f, scale), Shade(s.blade, 0.8f));
			if (s.style == 5)
			{
				// 불꽃 가장자리
				const float flick = time * 9.f;
				for (int i = 0; i < 6; ++i)
				{
					const float t = 0.12f + i * 0.13f;
					const float h = (4.f + 3.f * std::sin(flick + i * 1.7f)) * scale;
					gfx::Tri(P(base, d, n, len * t, ww * 0.8f), P(base, d, n, len * (t + 0.1f), ww * 0.8f), P(base, d, n, len * (t + 0.02f), ww + h), util::WithA(Rgb(0xFFB040), 0.85f));
				}
			}
			break;
		}
		}

		// 가드
		const Vector2 g0 = P(base, d, n, 0, -gw), g1 = P(base, d, n, 0, gw);
		gfx::Line(g0, g1, std::max(2.f, scale * 3.2f), s.guardCol);
		gfx::Circle(g0, std::max(1.5f, scale * 2.2f), Shade(s.guardCol, 1.3f));
		gfx::Circle(g1, std::max(1.5f, scale * 2.2f), Shade(s.guardCol, 0.8f));
		if (s.style == 4)
			gfx::Circle(base, std::max(1.5f, scale * 2.6f), Rgb(0xE03A3A));
	}

	void Build()
	{
		RenderTexture2D rt = LoadRenderTexture(kCols * kCell, kRows * kCell);
		BeginTextureMode(rt);
		ClearBackground(BLANK);
		rlDisableBackfaceCulling();
		gfx::BeginWorld(0.f, 1.f);
		gfx::SetDepthRaw(-0.5f);

		struct Entry { uint8_t icon; uint16_t item; };
		std::vector<Entry> entries;
		for (int& v : g_slotOf) v = -1;
		for (const auto& row : ItemTableData::kRows)
		{
			bool dup = false;
			for (const auto& e : entries) if (e.icon == row.iconId) dup = true;
			if (!dup && static_cast<int>(entries.size()) < kUnknownSlot)
			{
				g_slotOf[row.iconId] = static_cast<int>(entries.size());
				entries.push_back({ row.iconId, row.id });
			}
		}
		for (const auto& e : entries)
		{
			const int slot = SlotOf(e.icon);
			const Vector2 o{ static_cast<float>((slot % kCols) * kCell), static_cast<float>((slot / kCols) * kCell) };
			DrawIconInto(e.icon, e.item, o);
		}
		// 알 수 없는 아이템(물음표 자루)
		{
			const int slot = kUnknownSlot;
			const Vector2 o{ static_cast<float>((slot % kCols) * kCell), static_cast<float>((slot / kCols) * kCell) };
			gfx::Circle(Vector2{ o.x + 64, o.y + 72 }, 36, Rgb(0x8A6A4A));
			gfx::Circle(Vector2{ o.x + 56, o.y + 64 }, 20, Rgb(0xA8845A));
		}
		gfx::Flush();
		EndTextureMode();

		// 뒤집힌 렌더 텍스처 → 정방향 이미지 → 가장자리 번짐 처리 → 밉맵 텍스처
		Image img = LoadImageFromTexture(rt.texture);
		ImageFlipVertical(&img);
		ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
		BleedEdges(img, 4);
		UnloadRenderTexture(rt);
		g_atlas = LoadTextureFromImage(img);
		UnloadImage(img);
		GenTextureMipmaps(&g_atlas);
		SetTextureFilter(g_atlas, TEXTURE_FILTER_TRILINEAR);
	}

	void Unload()
	{
		if (g_atlas.id != 0)
			UnloadTexture(g_atlas);
		g_atlas = Texture2D{};
	}

	static Rectangle SrcFor(uint16_t itemId)
	{
		const ItemTableRow* row = ItemTableData::Find(itemId);
		const int slot = SlotOf(row ? row->iconId : 0);
		return Rectangle{ static_cast<float>((slot % kCols) * kCell), static_cast<float>((slot / kCols) * kCell), static_cast<float>(kCell), static_cast<float>(kCell) };
	}

	void Draw(uint16_t itemId, Rectangle dst, Color tint)
	{
		if (g_atlas.id == 0) return;
		DrawTexturePro(g_atlas, SrcFor(itemId), dst, Vector2{ 0, 0 }, 0.f, tint);
	}

	void DrawWorld(uint16_t itemId, Vector2 center, float size, float rot, Color tint)
	{
		if (g_atlas.id == 0) return;
		gfx::TexQuadSrc(g_atlas, SrcFor(itemId), center, Vector2{ size, size }, rot, tint);
	}

	Color GradeColor(ItemGrade grade)
	{
		switch (grade)
		{
		case ItemGrade::eRare:      return Rgb(0x4AA8FF);
		case ItemGrade::eEpic:      return Rgb(0xB872FF);
		case ItemGrade::eUnique:    return Rgb(0xFFC23A);
		case ItemGrade::eLegendary: return Rgb(0xFF6A30);
		default:                    return Rgb(0xE8E4DA);
		}
	}

	Color RarityColor(Rarity r)
	{
		return Rgb(ItemRules::RarityColor(r));
	}

	Color ItemNameColor(uint16_t itemId)
	{
		return ItemNameColor(itemId, 0);
	}

	Color ItemNameColor(uint16_t itemId, uint64_t extra)
	{
		const ItemTableRow* row = ItemTableData::Find(itemId);
		if (!row) return WHITE;
		if (row->type == ItemType::eCurrency) return Rgb(0xFFD84A);
		if (row->type == ItemType::eEquipment) return RarityColor(ItemRules::EffectiveRarity(*row, extra));
		if (row->id == ItemTableData::Id::BlessedEnchant) return Rgb(0xFFE070);
		return Rgb(0xE8E4DA);
	}

	Color WeaponAccent(uint16_t itemId)
	{
		return SpecOf(itemId).edge;
	}

	bool GearStyle(uint16_t itemId, int& style, Color& main, Color& trim, Color& accent)
	{
		const ItemTableRow* row = ItemTableData::Find(itemId);
		if (!row) return false;
		const GearLook* g = FindGearLook(row->iconId);
		if (!g) return false;
		style = g->style;
		main = Rgb(g->main);
		trim = Rgb(g->trim);
		accent = Rgb(g->accent);
		return true;
	}

	Color EnchantGlow(int enchant)
	{
		if (enchant <= 0) return Color{ 0, 0, 0, 0 };
		if (enchant <= 3) return Rgb(0x9AD8FF, 70);
		if (enchant <= 6) return Rgb(0x6AB8FF, 120);
		if (enchant <= 8) return Rgb(0x4A7AFF, 170);
		if (enchant <= 10) return Rgb(0xB05AFF, 200);
		return Rgb(0xFF4A3A, 220);
	}
}
