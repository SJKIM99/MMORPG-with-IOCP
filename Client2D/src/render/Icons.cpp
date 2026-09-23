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
		constexpr int kRows = 4;
		Texture2D g_atlas{};

		// iconId → 아틀라스 칸
		int SlotOf(uint8_t iconId)
		{
			switch (iconId)
			{
			case 1: return 0;  case 2: return 1;  case 3: return 2;  case 4: return 3;
			case 10: return 8; case 11: return 9; case 12: return 10; case 13: return 11;
			case 14: return 12; case 15: return 13; case 16: return 14; case 17: return 15;
			case 18: return 16;
			case 20: return 4; case 21: return 5; case 22: return 6; case 23: return 7;
			case 30: return 17;
			default: return 31;
			}
		}

		struct BladeSpec
		{
			float len = 38.f, width = 7.f, grip = 11.f, guard = 16.f;
			Color blade{}, edge{}, guardCol{}, grip2{};
			int style = 0;   // 0 곧은 검, 1 카타나, 2 단검, 3 목검, 4 대검, 5 화염검, 6 집행검
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

		void DrawIconInto(uint8_t iconId, uint16_t itemId, Vector2 o)
		{
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
		for (const auto& row : ItemTableData::kRows)
		{
			bool dup = false;
			for (const auto& e : entries) if (e.icon == row.iconId) dup = true;
			if (!dup) entries.push_back({ row.iconId, row.id });
		}
		for (const auto& e : entries)
		{
			const int slot = SlotOf(e.icon);
			const Vector2 o{ static_cast<float>((slot % kCols) * kCell), static_cast<float>((slot / kCols) * kCell) };
			DrawIconInto(e.icon, e.item, o);
		}
		// 알 수 없는 아이템(물음표 자루)
		{
			const int slot = 31;
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

	Color ItemNameColor(uint16_t itemId)
	{
		const ItemTableRow* row = ItemTableData::Find(itemId);
		if (!row) return WHITE;
		if (row->type == ItemType::eCurrency) return Rgb(0xFFD84A);
		if (row->type == ItemType::eEquipment) return GradeColor(row->grade);
		if (row->id == ItemTableData::Id::BlessedEnchant) return Rgb(0xFFE070);
		return Rgb(0xE8E4DA);
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
