// ─────────────────────────────────────────────────────────────────────────────
// 건물 키트(Phase 4-C) — 종류 19가지 · 주택 변형 10가지를 조각(벽 · 지붕 · 창 · 문 · 발코니 · 차양 · 굴뚝 · 간판 · 깃발 …)으로 짓는다.
//
// · 실루엣: 종류마다 지붕 모양(박공 · 모임 · 뾰족 · 평지붕 성가퀴 · 유리) · 층수 · 탑 · 바퀴 · 날개가 달라 한눈에 구분된다.
// · 구획 규칙: 지붕 재질 · 색, 벽 재질, 밤 창빛 색은 건물이 선 구획(TownMap::kDistricts 가장 가까운 곳)이 정한다(LEVEL_DENICA 3장).
// · 바람(render/Wind): 간판이 흔들리고 깃발 · 빨래가 펄럭이며 굴뚝 연기가 바람 쪽으로 눕고 풍차 · 물레방아가 돈다.
// · 밤: 창에 불이 켜지고(구획 색) 등불 · 샛별 탑 등불이 빛난다.
// 좌표는 모두 월드 칸(바닥 평면) + z(단위 36px) — 건물이 선 층의 높이는 부르는 쪽(Terrain)이 gfx::SetLift로 올려 둔다.
// 막힌 칸(발자국 'B')은 서버 충돌과 1:1 — 이 파일은 그 사각형 안에서만 그린다(차양 · 간판처럼 튀어나온 장식 제외).
// ─────────────────────────────────────────────────────────────────────────────
#include "render/Props.h"
#include "render/Draw.h"
#include "render/EnvArt.h"
#include "render/Wind.h"
#include "core/Util.h"
#include "Data/TownMap.h"
#include "Data/WorldData.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace props
{
	namespace
	{
		using gfx::P3;
		using util::Rgb;
		using util::Shade;
		using util::Mix;
		using TownMap::BuildingKind;

		constexpr float kPi = 3.14159265f;

		// ── 재질 · 구획 ───────────────────────────────────────────────────────────
		enum class Wall : uint8_t { Plaster, Stone, Dressed, Planks, Mud, White };
		enum class Roof : uint8_t { Red, Orange, DarkRed, Slate, DarkSlate, BlueSlate, Shingle, Thatch, Glass };

		struct Look
		{
			Wall wall = Wall::Plaster;
			Roof roof = Roof::Red;
			Color wallMul = WHITE;     // 텍스처에 곱하는 색
			Color roofMul = WHITE;
			Color trim = Rgb(0x5A3E26);   // 나무 테두리 · 문틀
			Color glow = Rgb(0xFFD27A);   // 밤 창빛
		};

		Look DistrictLook(const char* name)
		{
			auto is = [&](const char* s) { return std::strcmp(name, s) == 0; };
			Look L;
			if (is("남문 광장"))        { L.wall = Wall::Stone;   L.roof = Roof::DarkRed;   L.wallMul = Rgb(0xE6E2DA); L.roofMul = Rgb(0xC89088); L.trim = Rgb(0x4A3A2E); L.glow = Rgb(0xFFA050); }
			else if (is("분수 광장"))   { L.wall = Wall::Plaster; L.roof = Roof::Red;       L.wallMul = Rgb(0xFFFFFF); L.roofMul = Rgb(0xFFFFFF); L.trim = Rgb(0x5A3E26); L.glow = Rgb(0xFFD27A); }
			else if (is("시장 거리"))   { L.wall = Wall::Planks;  L.roof = Roof::Orange;    L.wallMul = Rgb(0xF0D4B0); L.roofMul = Rgb(0xFFD2A0); L.trim = Rgb(0x6A4A2E); L.glow = Rgb(0xFF9A58); }
			else if (is("서쪽 들"))     { L.wall = Wall::Mud;     L.roof = Roof::Thatch;    L.wallMul = Rgb(0xECD2A8); L.roofMul = Rgb(0xFFFFFF); L.trim = Rgb(0x5A4028); L.glow = Rgb(0xFFC870); }
			else if (is("장인 거리"))   { L.wall = Wall::Stone;   L.roof = Roof::DarkSlate; L.wallMul = Rgb(0xCCC8C0); L.roofMul = Rgb(0xC0C0CA); L.trim = Rgb(0x3A2E24); L.glow = Rgb(0xFF8A48); }
			else if (is("훈련장"))      { L.wall = Wall::Planks;  L.roof = Roof::Shingle;   L.wallMul = Rgb(0xE8CCA8); L.roofMul = Rgb(0xA89078); L.trim = Rgb(0x4A3A2E); L.glow = Rgb(0xFFA050); }
			else if (is("여관 골목"))   { L.wall = Wall::Plaster; L.roof = Roof::Shingle;   L.wallMul = Rgb(0xF6E6CC); L.roofMul = Rgb(0xB09880); L.trim = Rgb(0x4A3020); L.glow = Rgb(0xFFC060); }
			else if (is("결사 회관 단")) { L.wall = Wall::Dressed; L.roof = Roof::BlueSlate; L.wallMul = Rgb(0xECECEC); L.roofMul = Rgb(0xC8D4F0); L.trim = Rgb(0x3A3A44); L.glow = Rgb(0xE8F0FF); }
			else if (is("윗마을"))      { L.wall = Wall::Plaster; L.roof = Roof::Red;       L.wallMul = Rgb(0xFFF6EC); L.roofMul = Rgb(0xFFE6D6); L.trim = Rgb(0x6A4A2E); L.glow = Rgb(0xFFD27A); }
			else if (is("예배당 언덕")) { L.wall = Wall::White;   L.roof = Roof::BlueSlate; L.wallMul = Rgb(0xFFFCF4); L.roofMul = Rgb(0xC0D0F4); L.trim = Rgb(0xC8B070); L.glow = Rgb(0xC0DCFF); }
			else if (is("풍차 언덕"))   { L.wall = Wall::Planks;  L.roof = Roof::Thatch;    L.wallMul = Rgb(0xECD4B4); L.roofMul = Rgb(0xFFFFFF); L.trim = Rgb(0x5A4028); L.glow = Rgb(0xFFC870); }
			return L;
		}

		// 건물 한가운데와 가장 가까운 구획(0번 = 마을 전경은 빼고)
		const char* DistrictAt(float wx, float wy)
		{
			const char* best = "분수 광장";
			float bd = 1e9f;
			for (size_t i = 1; i < std::size(TownMap::kDistricts); ++i)
			{
				const auto& d = TownMap::kDistricts[i];
				const float dx = d.x - wx, dy = d.y - wy, dd = dx * dx + dy * dy;
				if (dd < bd) { bd = dd; best = d.name; }
			}
			return best;
		}

		const Texture2D* WallTex(Wall w)
		{
			switch (w)
			{
			case Wall::Plaster: case Wall::Mud: return envart::Get(envart::Tex::Plaster);
			case Wall::Stone: return envart::Get(envart::Tex::Stone);
			case Wall::Dressed: case Wall::White: return envart::Get(envart::Tex::TownWall);
			case Wall::Planks: return envart::Get(envart::Tex::Wood);
			}
			return nullptr;
		}
		Color WallFallback(Wall w)
		{
			switch (w)
			{
			case Wall::Plaster: return Rgb(0xE8DCC4);
			case Wall::Mud: return Rgb(0xC8A878);
			case Wall::Stone: return Rgb(0x8A857A);
			case Wall::Dressed: return Rgb(0xB8B4AA);
			case Wall::White: return Rgb(0xF0ECE4);
			case Wall::Planks: return Rgb(0x7A5A3E);
			}
			return GRAY;
		}
		const Texture2D* RoofTex(Roof r)
		{
			switch (r)
			{
			case Roof::Red: case Roof::Orange: case Roof::DarkRed: case Roof::Shingle: return envart::Get(envart::Tex::RoofRed);
			case Roof::Slate: case Roof::DarkSlate: case Roof::BlueSlate: return envart::Get(envart::Tex::RoofSlate);
			default: return nullptr;
			}
		}
		Color RoofFallback(Roof r)
		{
			switch (r)
			{
			case Roof::Red: return Rgb(0xB0483A);
			case Roof::Orange: return Rgb(0xC8683A);
			case Roof::DarkRed: return Rgb(0x8A3A30);
			case Roof::Slate: return Rgb(0x4A5260);
			case Roof::DarkSlate: return Rgb(0x3A3E48);
			case Roof::BlueSlate: return Rgb(0x4A5E80);
			case Roof::Shingle: return Rgb(0x7A5638);
			case Roof::Thatch: return Rgb(0xC8A860);
			case Roof::Glass: return Rgb(0xB8E0F0, 110);
			}
			return GRAY;
		}

		Color Jitter(Color c, uint32_t h, float amount)
		{
			const float k = 1.f + ((h & 0xFF) / 255.f - 0.5f) * 2.f * amount;
			auto ch = [&](unsigned char v) { return static_cast<unsigned char>(std::clamp(v * k, 0.f, 255.f)); };
			return Color{ ch(c.r), ch(c.g), ch(c.b), c.a };
		}

		// ── 한 프레임에 모아 두었다가 건물을 다 그린 뒤 한 번에(깊이 쓰기 끔 · 더하기 혼합) ──
		struct Puff { Vector2 iso; float depth, r; Color c; };
		std::vector<Puff> g_smoke;
		std::vector<Puff> g_glow;

		float Night() { return props::NightLevel(); }

		// 연기: 굴뚝 꼭대기에서 바람 쪽으로 누우며 오른다(시간만의 함수)
		void Smoke(P3 top, float time, float seed, float amount = 1.f)
		{
			const Vector4 wp = wind::ShaderParams();
			const float s = wind::Strength();
			for (int i = 0; i < 6; ++i)
			{
				const float age = std::fmod(time * 0.22f + i / 6.f + seed * 0.37f, 1.f);
				const float drift = age * (0.6f + 2.2f * s);
				const P3 p{ top.x + wp.x * drift, top.y + wp.y * drift, top.z + 0.2f + age * (1.9f - 0.8f * s) };
				Vector2 iso = gfx::IsoPx(p.x, p.y, p.z);
				iso.y -= gfx::Lift();
				const float a = (age < 0.15f ? age / 0.15f : 1.f - (age - 0.15f) / 0.85f) * 0.34f * amount;
				g_smoke.push_back(Puff{ iso, top.x + top.y + 0.3f, 5.f + age * 15.f, Color{ 214, 210, 204, static_cast<unsigned char>(a * 255) } });
			}
		}

		void Glow(P3 p, float r, Color c, float intensity)
		{
			Vector2 iso = gfx::IsoPx(p.x, p.y, p.z);
			iso.y -= gfx::Lift();
			g_glow.push_back(Puff{ iso, p.x + p.y + 0.4f, r, util::MulA(c, std::clamp(intensity, 0.f, 1.f)) });
		}

		// ── 조각 ─────────────────────────────────────────────────────────────────
		// 보이는 두 면(남 · 동)만 — 윗면은 지붕이 덮는다. 뒤 두 면은 깊이에 가려 보이지 않는다.
		void Walls(float x0, float y0, float x1, float y1, float z0, float z1, const Look& L, float unit = 2.f)
		{
			const Texture2D* t = WallTex(L.wall);
			if (t)
			{
				const float k = 1.f / unit;
				gfx::FaceTex(*t, { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 }, (x1 - x0) * k, (z1 - z0) * k, Shade(L.wallMul, 0.96f));
				gfx::FaceTex(*t, { x1, y1, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x1, y1, z1 }, (y1 - y0) * k, (z1 - z0) * k, Shade(L.wallMul, 0.72f));
			}
			else
			{
				const Color c = WallFallback(L.wall);
				gfx::Face({ x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 }, Shade(c, 0.96f));
				gfx::Face({ x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x1, y0, z1 }, Shade(c, 0.72f));
			}
		}
		// 뚜껑(평지붕 · 탑 윗면)
		void Lid(float x0, float y0, float x1, float y1, float z, Color c)
		{
			gfx::Face({ x0, y0, z }, { x1, y0, z }, { x1, y1, z }, { x0, y1, z }, c);
		}
		// 팔각 뚜껑(탑 발코니 바닥 · 풍차 받침)
		void OctLid(float cx, float cy, float r, int sides, float z, Color c)
		{
			for (int i = 0; i < sides; ++i)
			{
				const float a0 = (i + 0.5f) / sides * 2.f * kPi, a1 = (i + 1.5f) / sides * 2.f * kPi;
				gfx::FaceTri({ cx, cy, z }, { cx + std::cos(a0) * r, cy + std::sin(a0) * r, z }, { cx + std::cos(a1) * r, cy + std::sin(a1) * r, z }, c);
			}
		}
		void Plinth(float x0, float y0, float x1, float y1, float h = 0.18f)
		{
			if (const Texture2D* s = envart::Get(envart::Tex::Stone))
				gfx::BoxTex(*s, x0 - 0.05f, y0 - 0.05f, x1 + 0.05f, y1 + 0.05f, 0.f, h, 1.0f, Rgb(0xC8C4BC), Rgb(0x9A968E), Rgb(0x7A766E));
			else
				gfx::Box(x0 - 0.05f, y0 - 0.05f, x1 + 0.05f, y1 + 0.05f, 0.f, h, Rgb(0x8A857A), Rgb(0x6E6A62), Rgb(0x5A564E));
		}
		// 층 사이 띠(나무 들보)
		void Band(float x0, float y0, float x1, float y1, float z, Color c, float t = 0.1f)
		{
			gfx::Face({ x0, y1 + 0.015f, z }, { x1 + 0.015f, y1 + 0.015f, z }, { x1 + 0.015f, y1 + 0.015f, z + t }, { x0, y1 + 0.015f, z + t }, c);
			gfx::Face({ x1 + 0.015f, y1 + 0.015f, z }, { x1 + 0.015f, y0, z }, { x1 + 0.015f, y0, z + t }, { x1 + 0.015f, y1 + 0.015f, z + t }, Shade(c, 0.78f));
		}

		// 초가: 짚 다발 줄 + 두꺼운 용마루 + 들쭉날쭉한 처마
		void ThatchSlope(P3 eaveA, P3 eaveB, P3 ridgeB, P3 ridgeA, float shade)
		{
			const Color base = Shade(Rgb(0xC8A860), shade), dark = Shade(Rgb(0x9A7E44), shade), lite = Shade(Rgb(0xE0C888), shade);
			gfx::Face4(eaveA, eaveB, ridgeB, ridgeA, dark, dark, lite, lite);
			for (int i = 1; i < 6; ++i)
			{
				const float t = i / 6.f;
				auto L = [&](P3 a, P3 b) { return P3{ util::Lerp(a.x, b.x, t), util::Lerp(a.y, b.y, t), util::Lerp(a.z, b.z, t) }; };
				const P3 a = L(eaveA, ridgeA), b = L(eaveB, ridgeB);
				gfx::Face(a, b, P3{ b.x, b.y, b.z + 0.035f }, P3{ a.x, a.y, a.z + 0.035f }, Shade(base, 0.82f));
			}
			// 처마 끝 들쭉날쭉
			const int n = std::max(3, static_cast<int>(std::hypot(eaveB.x - eaveA.x, eaveB.y - eaveA.y) * 3.f));
			for (int i = 0; i < n; ++i)
			{
				const float t0 = static_cast<float>(i) / n, t1 = static_cast<float>(i + 1) / n, tm = (t0 + t1) * 0.5f;
				auto E = [&](float t, float dz) { return P3{ util::Lerp(eaveA.x, eaveB.x, t), util::Lerp(eaveA.y, eaveB.y, t), util::Lerp(eaveA.z, eaveB.z, t) + dz }; };
				gfx::FaceTri(E(t0, 0.f), E(t1, 0.f), E(tm, -0.08f - 0.05f * ((i * 7) % 3)), dark);
			}
		}

		// 박공지붕: alongX면 용마루가 X 방향(남 · 북 사면), 아니면 Y 방향(동 · 서 사면). 보이는 박공(동 끝 · 남 끝)은 벽색.
		void Gable(float x0, float y0, float x1, float y1, float h, float rise, float o, bool alongX, const Look& L, Color gableWall)
		{
			const float top = h + rise;
			const Texture2D* tex = RoofTex(L.roof);
			constexpr float kUnit = 1.4f;
			if (alongX)
			{
				const float yc = (y0 + y1) * 0.5f;
				const P3 nA{ x0 - o, y0 - o, h }, nB{ x1 + o, y0 - o, h }, rB{ x1 + o, yc, top }, rA{ x0 - o, yc, top };
				const P3 sA{ x0 - o, y1 + o, h }, sB{ x1 + o, y1 + o, h };
				gfx::FaceTri({ x1, y0, h }, { x1, y1, h }, { x1, yc, top }, gableWall);
				if (L.roof == Roof::Thatch) { ThatchSlope(nA, nB, rB, rA, 0.7f); ThatchSlope(sA, sB, rB, rA, 1.0f); }
				else if (tex)
				{
					const float slope = std::sqrt((yc - y0 + o) * (yc - y0 + o) + rise * rise), len = x1 - x0 + 2.f * o;
					gfx::FaceTex(*tex, nA, nB, rB, rA, len / kUnit, slope / kUnit, Shade(L.roofMul, 0.7f));
					gfx::FaceTex(*tex, sA, sB, rB, rA, len / kUnit, slope / kUnit, L.roofMul);
				}
				else
				{
					const Color c = RoofFallback(L.roof);
					gfx::Face(nA, nB, rB, rA, Shade(c, 0.72f));
					gfx::Face(sA, sB, rB, rA, c);
				}
				// 박공 끝 판(처마 두께)
				gfx::Face(rB, sB, P3{ sB.x + 0.02f, sB.y, h - 0.09f }, P3{ rB.x + 0.02f, rB.y, top - 0.09f }, Shade(L.trim, 0.9f));
				gfx::Face(sA, sB, P3{ sB.x, sB.y + 0.02f, h - 0.09f }, P3{ sA.x, sA.y + 0.02f, h - 0.09f }, Shade(L.trim, 1.05f));
				// 용마루 덮개
				gfx::Face(rA, rB, P3{ rB.x, rB.y + 0.08f, top - 0.05f }, P3{ rA.x, rA.y + 0.08f, top - 0.05f }, Shade(L.roof == Roof::Thatch ? Rgb(0x8A6E3A) : RoofFallback(L.roof), 0.8f));
			}
			else
			{
				const float xc = (x0 + x1) * 0.5f;
				const P3 wA{ x0 - o, y1 + o, h }, wB{ x0 - o, y0 - o, h }, rB{ xc, y0 - o, top }, rA{ xc, y1 + o, top };
				const P3 eA{ x1 + o, y1 + o, h }, eB{ x1 + o, y0 - o, h };
				gfx::FaceTri({ x0, y1, h }, { x1, y1, h }, { xc, y1, top }, gableWall);
				if (L.roof == Roof::Thatch) { ThatchSlope(wA, wB, rB, rA, 0.8f); ThatchSlope(eA, eB, rB, rA, 0.86f); }
				else if (tex)
				{
					const float slope = std::sqrt((xc - x0 + o) * (xc - x0 + o) + rise * rise), len = y1 - y0 + 2.f * o;
					gfx::FaceTex(*tex, wA, wB, rB, rA, len / kUnit, slope / kUnit, Shade(L.roofMul, 0.78f));
					gfx::FaceTex(*tex, eA, eB, rB, rA, len / kUnit, slope / kUnit, Shade(L.roofMul, 0.86f));
				}
				else
				{
					const Color c = RoofFallback(L.roof);
					gfx::Face(wA, wB, rB, rA, Shade(c, 0.8f));
					gfx::Face(eA, eB, rB, rA, Shade(c, 0.86f));
				}
				gfx::Face(rA, eA, P3{ eA.x, eA.y + 0.02f, h - 0.09f }, P3{ rA.x, rA.y + 0.02f, top - 0.09f }, Shade(L.trim, 1.0f));
				gfx::Face(rA, wA, P3{ wA.x, wA.y + 0.02f, h - 0.09f }, P3{ rA.x, rA.y + 0.02f, top - 0.09f }, Shade(L.trim, 1.0f));
				gfx::Face(rA, rB, P3{ rB.x + 0.08f, rB.y, top - 0.05f }, P3{ rA.x + 0.08f, rA.y, top - 0.05f }, Shade(L.roof == Roof::Thatch ? Rgb(0x8A6E3A) : RoofFallback(L.roof), 0.85f));
			}
		}

		// 모임지붕: 네 사면(용마루는 긴 축, 짧은 폭만큼 줄어든다). ridgeFrac = 0이면 뾰족(사각뿔)
		void Hip(float x0, float y0, float x1, float y1, float h, float rise, float o, const Look& L, float ridgeFrac = 1.f)
		{
			const float X0 = x0 - o, Y0 = y0 - o, X1 = x1 + o, Y1 = y1 + o;
			const float top = h + rise, xc = (X0 + X1) * 0.5f, yc = (Y0 + Y1) * 0.5f;
			const bool alongX = (X1 - X0) >= (Y1 - Y0);
			const float half = std::max(0.f, ((alongX ? X1 - X0 : Y1 - Y0) - (alongX ? Y1 - Y0 : X1 - X0)) * 0.5f * ridgeFrac);
			const P3 rA = alongX ? P3{ xc - half, yc, top } : P3{ xc, yc - half, top };
			const P3 rB = alongX ? P3{ xc + half, yc, top } : P3{ xc, yc + half, top };
			const P3 nw{ X0, Y0, h }, ne{ X1, Y0, h }, se{ X1, Y1, h }, sw{ X0, Y1, h };
			const Texture2D* tex = RoofTex(L.roof);
			auto slope = [&](P3 a, P3 b, P3 rb, P3 ra, float shade)
			{
				if (L.roof == Roof::Thatch) { ThatchSlope(a, b, rb, ra, shade); return; }
				if (tex)
				{
					const float len = std::hypot(b.x - a.x, b.y - a.y);
					const float sl = std::hypot(std::hypot((ra.x + rb.x) * 0.5f - (a.x + b.x) * 0.5f, (ra.y + rb.y) * 0.5f - (a.y + b.y) * 0.5f), ra.z - a.z);
					gfx::FaceTex(*tex, a, b, rb, ra, std::max(0.3f, len / 1.4f), std::max(0.3f, sl / 1.4f), Shade(L.roofMul, shade));
				}
				else
					gfx::Face(a, b, rb, ra, Shade(RoofFallback(L.roof), shade));
			};
			if (alongX)
			{
				slope(nw, ne, rB, rA, 0.7f);    // 북
				slope(sw, se, rB, rA, 1.0f);    // 남
				slope(se, ne, rB, rB, 0.84f);   // 동(세모)
				slope(sw, nw, rA, rA, 0.9f);    // 서
			}
			else
			{
				slope(nw, sw, rB, rA, 0.9f);    // 서
				slope(ne, se, rB, rA, 0.84f);   // 동
				slope(sw, se, rB, rB, 1.0f);    // 남(세모)
				slope(nw, ne, rA, rA, 0.7f);    // 북
			}
			gfx::Face(sw, se, P3{ se.x, se.y + 0.02f, h - 0.08f }, P3{ sw.x, sw.y + 0.02f, h - 0.08f }, Shade(L.trim, 1.05f));
			gfx::Face(se, ne, P3{ ne.x + 0.02f, ne.y, h - 0.08f }, P3{ se.x + 0.02f, se.y, h - 0.08f }, Shade(L.trim, 0.85f));
		}

		// 각기둥(팔각 탑 · 풍차 몸통): 면마다 법선으로 음영, 위로 갈수록 좁아질 수 있다
		void Prism(float cx, float cy, float r0, float r1, int sides, float z0, float z1, const Texture2D* tex, Color mul, float unit = 1.6f)
		{
			for (int i = 0; i < sides; ++i)
			{
				const float a0 = (i + 0.5f) / sides * 2.f * kPi, a1 = (i + 1.5f) / sides * 2.f * kPi, am = (a0 + a1) * 0.5f;
				const float nx = std::cos(am), ny = std::sin(am);
				if (nx + ny < -0.45f) continue;   // 뒤쪽 면
				const float sh = std::clamp(0.84f + 0.12f * ny - 0.12f * nx, 0.55f, 1.05f);
				const P3 b0{ cx + std::cos(a0) * r0, cy + std::sin(a0) * r0, z0 }, b1{ cx + std::cos(a1) * r0, cy + std::sin(a1) * r0, z0 };
				const P3 t1{ cx + std::cos(a1) * r1, cy + std::sin(a1) * r1, z1 }, t0{ cx + std::cos(a0) * r1, cy + std::sin(a0) * r1, z1 };
				if (tex)
				{
					const float w = std::hypot(b1.x - b0.x, b1.y - b0.y);
					gfx::FaceTex(*tex, b0, b1, t1, t0, std::max(0.3f, w / unit), std::max(0.3f, (z1 - z0) / unit), Shade(mul, sh));
				}
				else
					gfx::Face(b0, b1, t1, t0, Shade(mul, sh));
			}
		}
		// 뿔 지붕(각뿔)
		void Cone(float cx, float cy, float r, int sides, float z, float apexZ, const Look& L, float o = 0.12f)
		{
			const P3 apex{ cx, cy, apexZ };
			const Texture2D* tex = RoofTex(L.roof);
			for (int i = 0; i < sides; ++i)
			{
				const float a0 = (i + 0.5f) / sides * 2.f * kPi, a1 = (i + 1.5f) / sides * 2.f * kPi, am = (a0 + a1) * 0.5f;
				const float nx = std::cos(am), ny = std::sin(am);
				if (nx + ny < -0.6f) continue;
				const float sh = std::clamp(0.86f + 0.14f * ny - 0.14f * nx, 0.55f, 1.05f);
				const P3 b0{ cx + std::cos(a0) * (r + o), cy + std::sin(a0) * (r + o), z }, b1{ cx + std::cos(a1) * (r + o), cy + std::sin(a1) * (r + o), z };
				if (L.roof == Roof::Thatch) { ThatchSlope(b0, b1, apex, apex, sh); continue; }
				if (tex)
				{
					const float w = std::hypot(b1.x - b0.x, b1.y - b0.y), s = std::hypot(r + o, apexZ - z);
					gfx::FaceTex(*tex, b0, b1, apex, apex, std::max(0.3f, w / 1.2f), std::max(0.3f, s / 1.2f), Shade(L.roofMul, sh));
				}
				else
					gfx::FaceTri(b0, b1, apex, Shade(RoofFallback(L.roof), sh));
			}
		}

		// 성가퀴(평지붕 둘레의 이 모양 담) — 앞 두 변 + 뒤 두 변(윤곽)
		void Crenel(float x0, float y0, float x1, float y1, float z, const Look& L, float mh = 0.32f)
		{
			const Texture2D* t = WallTex(Wall::Dressed);
			auto merlon = [&](float ax, float ay, float bx, float by)
			{
				if (t) gfx::BoxTex(*t, ax, ay, bx, by, z, z + mh, 1.2f, Shade(L.wallMul, 1.02f), Shade(L.wallMul, 0.92f), Shade(L.wallMul, 0.7f));
				else gfx::Box(ax, ay, bx, by, z, z + mh, Rgb(0xC8C4BC), Rgb(0xA8A49C), Rgb(0x88847C));
			};
			const float th = 0.22f, step = 0.55f;
			for (float x = x0; x < x1 - 0.05f; x += step)
			{
				merlon(x, y1 - th, std::min(x + step * 0.55f, x1), y1);
				merlon(x, y0, std::min(x + step * 0.55f, x1), y0 + th);
			}
			for (float y = y0; y < y1 - 0.05f; y += step)
			{
				merlon(x1 - th, y, x1, std::min(y + step * 0.55f, y1));
				merlon(x0, y, x0 + th, std::min(y + step * 0.55f, y1));
			}
		}

		// 창: 틀 · 유리(밤이면 구획 색으로 켜진다) · 가운데 살 · 덧문 · 꽃 상자
		void WindowFace(bool east, float a, float b, float at, float z0, float z1, const Look& L, bool shutters, bool flowers, uint32_t h)
		{
			const float n = Night();
			const bool lit = n > 0.05f && (h % 5) != 0;   // 다섯에 하나는 불이 꺼진 방
			const Color glass = Mix(Rgb(0x34424E), L.glow, lit ? n * 0.95f : n * 0.15f);
			const Color frame = L.trim;
			auto F = [&](float u0, float u1, float w0, float w1, float d, Color c)
			{
				if (east) gfx::Face({ at + d, u0, w0 }, { at + d, u1, w0 }, { at + d, u1, w1 }, { at + d, u0, w1 }, Shade(c, 0.78f));
				else gfx::Face({ u0, at + d, w0 }, { u1, at + d, w0 }, { u1, at + d, w1 }, { u0, at + d, w1 }, c);
			};
			if (east) std::swap(a, b);   // 동쪽 면은 y가 줄어드는 쪽이 화면 오른쪽 — 순서는 상관없다
			const float lo = std::min(a, b), hi = std::max(a, b);
			F(lo - 0.05f, hi + 0.05f, z0 - 0.05f, z1 + 0.05f, 0.012f, frame);
			F(lo, hi, z0, z1, 0.024f, glass);
			F((lo + hi) * 0.5f - 0.02f, (lo + hi) * 0.5f + 0.02f, z0, z1, 0.034f, frame);
			F(lo, hi, (z0 + z1) * 0.5f - 0.015f, (z0 + z1) * 0.5f + 0.015f, 0.034f, frame);
			if (shutters)
			{
				const Color sc = Jitter(Mix(L.trim, Rgb(0x3A6A5A), (h >> 4) % 3 == 0 ? 0.6f : 0.f), h >> 8, 0.08f);
				F(lo - 0.3f, lo - 0.06f, z0, z1, 0.02f, sc);
				F(hi + 0.06f, hi + 0.3f, z0, z1, 0.02f, sc);
			}
			if (flowers)
			{
				F(lo - 0.04f, hi + 0.04f, z0 - 0.2f, z0 - 0.04f, 0.1f, Shade(L.trim, 1.1f));
				for (int i = 0; i < 4; ++i)
				{
					const float u = util::Lerp(lo + 0.05f, hi - 0.05f, (i + 0.5f) / 4.f);
					const P3 p = east ? P3{ at + 0.14f, u, z0 - 0.02f } : P3{ u, at + 0.14f, z0 - 0.02f };
					gfx::SetDepth(p.x + p.y);
					const Color fc = (i + h) % 3 == 0 ? Rgb(0xE85A6A) : ((i + h) % 3 == 1 ? Rgb(0xF2D060) : Rgb(0xF0F0F8));
					gfx::Circle(gfx::IsoPx(p.x, p.y, p.z), 2.6f, Rgb(0x4A7A3A));
					gfx::Circle(gfx::IsoPx(p.x, p.y, p.z + 0.06f), 1.8f, fc);
				}
			}
			if (lit)
			{
				const P3 c = east ? P3{ at + 0.4f, (lo + hi) * 0.5f, (z0 + z1) * 0.5f } : P3{ (lo + hi) * 0.5f, at + 0.4f, (z0 + z1) * 0.5f };
				Vector2 iso = gfx::IsoPx(c.x, c.y, 0.f);
				PushLight({ iso, 70.f, L.glow, 0.65f * n });
			}
		}
		// 한 면에 창을 고르게(문 자리는 비운다)
		void WindowRow(bool east, float a, float b, float at, float z0, float z1, const Look& L, float spacing, float skipA, float skipB, bool shutters, bool flowers, uint32_t h)
		{
			const float len = b - a;
			const int n = std::max(1, static_cast<int>(len / spacing));
			const float w = std::min(0.62f, spacing * 0.45f);
			for (int i = 0; i < n; ++i)
			{
				const float c = a + len * (i + 0.5f) / n;
				if (c + w * 0.5f > skipA && c - w * 0.5f < skipB) continue;
				WindowFace(east, c - w * 0.5f, c + w * 0.5f, at, z0, z1, L, shutters, flowers && ((h >> i) & 1), util::Hash32(h + i * 31));
			}
		}

		void DoorFace(bool east, float c, float at, float w, float hgt, const Look& L, bool arch)
		{
			auto F = [&](float u0, float u1, float w0, float w1, float d, Color col)
			{
				if (east) gfx::Face({ at + d, u0, w0 }, { at + d, u1, w0 }, { at + d, u1, w1 }, { at + d, u0, w1 }, Shade(col, 0.8f));
				else gfx::Face({ u0, at + d, w0 }, { u1, at + d, w0 }, { u1, at + d, w1 }, { u0, at + d, w1 }, col);
			};
			const float a = c - w * 0.5f, b = c + w * 0.5f;
			F(a - 0.08f, b + 0.08f, 0.f, hgt + 0.08f, 0.01f, Shade(L.trim, 0.8f));
			F(a, b, 0.f, hgt, 0.02f, Rgb(0x7A5232));
			for (int i = 1; i < 4; ++i)
			{
				const float u = util::Lerp(a, b, i / 4.f);
				F(u - 0.012f, u + 0.012f, 0.05f, hgt - 0.05f, 0.03f, Rgb(0x5A3A22));
			}
			if (arch)
			{
				// 아치: 문 위 반원(부채꼴 세모들)
				const int n = 8;
				for (int i = 0; i < n; ++i)
				{
					const float t0 = kPi * i / n, t1 = kPi * (i + 1) / n;
					auto P = [&](float t, float r) { const float u = c - std::cos(t) * r, z = hgt + std::sin(t) * r; return east ? P3{ at + 0.02f, u, z } : P3{ u, at + 0.02f, z }; };
					const P3 o = east ? P3{ at + 0.02f, c, hgt } : P3{ c, at + 0.02f, hgt };
					gfx::FaceTri(o, P(t0, w * 0.5f), P(t1, w * 0.5f), east ? Shade(Rgb(0x6A4428), 0.8f) : Rgb(0x6A4428));
					const P3 q0 = P(t0, w * 0.5f + 0.1f), q1 = P(t1, w * 0.5f + 0.1f);
					gfx::Face(P(t0, w * 0.5f), P(t1, w * 0.5f), q1, q0, east ? Shade(L.trim, 0.7f) : L.trim);
				}
			}
			const P3 k = east ? P3{ at + 0.04f, b - 0.12f, hgt * 0.5f } : P3{ b - 0.12f, at + 0.04f, hgt * 0.5f };
			gfx::SetDepth(k.x + k.y);
			gfx::Circle(gfx::IsoPx(k.x, k.y, k.z), 1.8f, Rgb(0xE0C060));
		}

		// 차양: 줄무늬 천이 벽에서 비스듬히 나온다 + 앞 가장자리 물결 술
		void Awning(bool east, float a, float b, float at, float z, float depth, Color c0, Color c1)
		{
			const int stripes = std::max(2, static_cast<int>((b - a) / 0.32f));
			for (int i = 0; i < stripes; ++i)
			{
				const float u0 = util::Lerp(a, b, static_cast<float>(i) / stripes), u1 = util::Lerp(a, b, static_cast<float>(i + 1) / stripes);
				const Color c = (i % 2 == 0) ? c0 : c1;
				if (east)
				{
					gfx::Face({ at, u0, z }, { at, u1, z }, { at + depth, u1, z - 0.42f }, { at + depth, u0, z - 0.42f }, Shade(c, 0.86f));
					gfx::Face({ at + depth, u0, z - 0.42f }, { at + depth, u1, z - 0.42f }, { at + depth, u1, z - 0.62f }, { at + depth, u0, z - 0.62f }, Shade(c, 0.74f));
				}
				else
				{
					gfx::Face({ u0, at, z }, { u1, at, z }, { u1, at + depth, z - 0.42f }, { u0, at + depth, z - 0.42f }, c);
					gfx::Face({ u0, at + depth, z - 0.42f }, { u1, at + depth, z - 0.42f }, { u1, at + depth, z - 0.62f }, { u0, at + depth, z - 0.62f }, Shade(c, 0.9f));
				}
			}
		}

		// 발코니: 바닥 판 + 난간 기둥 + 손잡이(남 · 동 면)
		void Balcony(bool east, float a, float b, float at, float z, float depth, const Look& L)
		{
			const Color wood = Shade(L.trim, 1.25f);
			if (east) gfx::Box(at, a, at + depth, b, z - 0.08f, z, Shade(wood, 1.1f), wood, Shade(wood, 0.75f));
			else gfx::Box(a, at, b, at + depth, z - 0.08f, z, Shade(wood, 1.1f), wood, Shade(wood, 0.75f));
			const int n = std::max(3, static_cast<int>((b - a) / 0.22f));
			for (int i = 0; i <= n; ++i)
			{
				const float u = util::Lerp(a, b, static_cast<float>(i) / n);
				if (east) gfx::Box(at + depth - 0.05f, u - 0.025f, at + depth, u + 0.025f, z, z + 0.5f, wood, wood, Shade(wood, 0.7f));
				else gfx::Box(u - 0.025f, at + depth - 0.05f, u + 0.025f, at + depth, z, z + 0.5f, wood, wood, Shade(wood, 0.7f));
			}
			if (east) gfx::Box(at + depth - 0.07f, a, at + depth + 0.02f, b, z + 0.5f, z + 0.58f, Shade(wood, 1.15f), wood, Shade(wood, 0.75f));
			else gfx::Box(a, at + depth - 0.07f, b, at + depth + 0.02f, z + 0.5f, z + 0.58f, Shade(wood, 1.15f), wood, Shade(wood, 0.75f));
		}

		void Chimney(float cx, float cy, float z0, float z1, float time, float seed, bool smoke, bool brick = false)
		{
			const Texture2D* s = envart::Get(envart::Tex::Stone);
			const Color m = brick ? Rgb(0xC08878) : Rgb(0xC8B4A4);
			if (s) gfx::BoxTex(*s, cx - 0.24f, cy - 0.24f, cx + 0.24f, cy + 0.24f, z0, z1, 0.8f, Shade(m, 0.9f), m, Shade(m, 0.72f));
			else gfx::Box(cx - 0.24f, cy - 0.24f, cx + 0.24f, cy + 0.24f, z0, z1, Rgb(0x8A5A4A), Rgb(0x7A4A3A), Rgb(0x5A3A2E));
			gfx::Box(cx - 0.29f, cy - 0.29f, cx + 0.29f, cy + 0.29f, z1, z1 + 0.08f, Rgb(0x5A5048), Rgb(0x4A423C), Rgb(0x3A342E));
			if (smoke) Smoke({ cx, cy, z1 }, time, seed);
		}

		// ── 간판(아이콘) — 벽에서 튀어나온 쇠팔에 매달려 바람에 흔들린다 ──
		enum class Icon : uint8_t { None, Potion, Swords, Shield, Bed, Mug, Scales, Quill, Hammer, Horseshoe, Leaf, Sack, Crate };
		void DrawIcon(Icon ic, Vector2 c, float s)
		{
			switch (ic)
			{
			case Icon::Potion:
				gfx::Circle({ c.x, c.y + 2 * s }, 4.2f * s, Rgb(0xC83A3A));
				gfx::Quad({ c.x - 1.4f * s, c.y - 3.5f * s }, { c.x + 1.4f * s, c.y - 3.5f * s }, { c.x + 1.4f * s, c.y - 1 * s }, { c.x - 1.4f * s, c.y - 1 * s }, Rgb(0xE8E0D0));
				gfx::Circle({ c.x - 1.3f * s, c.y + 0.8f * s }, 1.2f * s, Rgb(0xFFB0A0));
				break;
			case Icon::Swords:
				gfx::Line({ c.x - 4 * s, c.y + 4 * s }, { c.x + 4 * s, c.y - 4 * s }, 1.6f * s, Rgb(0xE0E4EC));
				gfx::Line({ c.x + 4 * s, c.y + 4 * s }, { c.x - 4 * s, c.y - 4 * s }, 1.6f * s, Rgb(0xE0E4EC));
				gfx::Line({ c.x - 4.5f * s, c.y + 2 * s }, { c.x - 2 * s, c.y + 4.5f * s }, 1.4f * s, Rgb(0xC8A040));
				gfx::Line({ c.x + 4.5f * s, c.y + 2 * s }, { c.x + 2 * s, c.y + 4.5f * s }, 1.4f * s, Rgb(0xC8A040));
				break;
			case Icon::Shield:
			{
				const Vector2 p[5] = { { c.x - 4 * s, c.y - 4 * s }, { c.x + 4 * s, c.y - 4 * s }, { c.x + 3.6f * s, c.y + 1 * s }, { c.x, c.y + 5 * s }, { c.x - 3.6f * s, c.y + 1 * s } };
				gfx::Poly(p, 5, Rgb(0x4A6AA8));
				gfx::Line({ c.x, c.y - 3.5f * s }, { c.x, c.y + 4 * s }, 1.2f * s, Rgb(0xE8D080));
				gfx::Line({ c.x - 3.4f * s, c.y - 1 * s }, { c.x + 3.4f * s, c.y - 1 * s }, 1.2f * s, Rgb(0xE8D080));
				break;
			}
			case Icon::Bed:
				gfx::Quad({ c.x - 4.5f * s, c.y }, { c.x + 4.5f * s, c.y }, { c.x + 4.5f * s, c.y + 2.5f * s }, { c.x - 4.5f * s, c.y + 2.5f * s }, Rgb(0xE8E0D0));
				gfx::Quad({ c.x - 4.5f * s, c.y - 3 * s }, { c.x - 3 * s, c.y - 3 * s }, { c.x - 3 * s, c.y + 4 * s }, { c.x - 4.5f * s, c.y + 4 * s }, Rgb(0x8A5A34));
				gfx::Circle({ c.x - 1.4f * s, c.y - 0.8f * s }, 1.5f * s, Rgb(0xFFFFFF));
				gfx::Circle({ c.x + 3.2f * s, c.y - 3.2f * s }, 1.3f * s, Rgb(0xF2D060));
				break;
			case Icon::Mug:
				gfx::Quad({ c.x - 3 * s, c.y - 2.5f * s }, { c.x + 2 * s, c.y - 2.5f * s }, { c.x + 2 * s, c.y + 4 * s }, { c.x - 3 * s, c.y + 4 * s }, Rgb(0xC8903A));
				gfx::Ring({ c.x + 2.6f * s, c.y + 0.8f * s }, 1.2f * s, 2.3f * s, Rgb(0xC8903A));
				gfx::Circle({ c.x - 1.8f * s, c.y - 3 * s }, 1.6f * s, Rgb(0xFFFAF0));
				gfx::Circle({ c.x + 0.6f * s, c.y - 3.2f * s }, 1.7f * s, Rgb(0xFFFAF0));
				break;
			case Icon::Scales:
				gfx::Line({ c.x, c.y - 4.5f * s }, { c.x, c.y + 4 * s }, 1.3f * s, Rgb(0xE8C860));
				gfx::Line({ c.x - 4 * s, c.y - 3 * s }, { c.x + 4 * s, c.y - 3 * s }, 1.3f * s, Rgb(0xE8C860));
				gfx::EllipseArc({ c.x - 4 * s, c.y + 0.5f * s }, 2.2f * s, 1.3f * s, 0.f, kPi, 1.2f * s, Rgb(0xE8C860));
				gfx::EllipseArc({ c.x + 4 * s, c.y + 0.5f * s }, 2.2f * s, 1.3f * s, 0.f, kPi, 1.2f * s, Rgb(0xE8C860));
				gfx::Line({ c.x - 2.5f * s, c.y + 4 * s }, { c.x + 2.5f * s, c.y + 4 * s }, 1.3f * s, Rgb(0xE8C860));
				break;
			case Icon::Quill:
				gfx::Quad({ c.x - 3.5f * s, c.y - 3 * s }, { c.x + 3.5f * s, c.y - 3 * s }, { c.x + 3.5f * s, c.y + 3.5f * s }, { c.x - 3.5f * s, c.y + 3.5f * s }, Rgb(0xF0E6D0));
				gfx::Line({ c.x - 2 * s, c.y + 3 * s }, { c.x + 4 * s, c.y - 4.5f * s }, 1.5f * s, Rgb(0xFFFFFF));
				gfx::Line({ c.x - 2.5f * s, c.y - 0.5f * s }, { c.x + 1.5f * s, c.y - 0.5f * s }, 0.8f * s, Rgb(0x5A5048));
				gfx::Line({ c.x - 2.5f * s, c.y + 1.2f * s }, { c.x + 0.8f * s, c.y + 1.2f * s }, 0.8f * s, Rgb(0x5A5048));
				break;
			case Icon::Hammer:
				gfx::Line({ c.x - 3 * s, c.y + 4.5f * s }, { c.x + 2 * s, c.y - 2 * s }, 1.4f * s, Rgb(0x8A5A34));
				gfx::Quad({ c.x - 0.5f * s, c.y - 5 * s }, { c.x + 4.5f * s, c.y - 1 * s }, { c.x + 3 * s, c.y + 0.8f * s }, { c.x - 2 * s, c.y - 3.2f * s }, Rgb(0xB8BCC4));
				break;
			case Icon::Horseshoe:
				gfx::Ring({ c.x, c.y }, 2.4f * s, 4.2f * s, Rgb(0xB8BCC4), kPi * 0.95f, kPi * 2.05f);
				gfx::Ring({ c.x, c.y }, 2.4f * s, 4.2f * s, Rgb(0xB8BCC4), -kPi * 0.05f, kPi * 0.05f);
				break;
			case Icon::Leaf:
				gfx::Ellipse({ c.x, c.y }, 3.2f * s, 4.6f * s, Rgb(0x5AA848));
				gfx::Line({ c.x, c.y + 4.5f * s }, { c.x, c.y - 3.5f * s }, 0.9f * s, Rgb(0x2E6A28));
				break;
			case Icon::Sack:
				gfx::Circle({ c.x, c.y + 1.5f * s }, 4 * s, Rgb(0xE0CCA0));
				gfx::Tri({ c.x - 2 * s, c.y - 2 * s }, { c.x + 2 * s, c.y - 2 * s }, { c.x, c.y - 4.5f * s }, Rgb(0xE0CCA0));
				gfx::Line({ c.x - 1.8f * s, c.y - 2 * s }, { c.x + 1.8f * s, c.y - 2 * s }, 1 * s, Rgb(0x8A6A3A));
				break;
			case Icon::Crate:
				gfx::Quad({ c.x - 4 * s, c.y - 4 * s }, { c.x + 4 * s, c.y - 4 * s }, { c.x + 4 * s, c.y + 4 * s }, { c.x - 4 * s, c.y + 4 * s }, Rgb(0xB8844C));
				gfx::Line({ c.x - 4 * s, c.y - 4 * s }, { c.x + 4 * s, c.y + 4 * s }, 1.2f * s, Rgb(0x6A4424));
				gfx::Line({ c.x + 4 * s, c.y - 4 * s }, { c.x - 4 * s, c.y + 4 * s }, 1.2f * s, Rgb(0x6A4424));
				break;
			default: break;
			}
		}

		// face 'S'면 벽(y = at)에서 남쪽으로, 'E'면 벽(x = at)에서 동쪽으로 팔이 나온다. u = 면 위의 자리, z = 팔 높이
		void Sign(char face, float u, float at, float z, Icon icon, const Look& L)
		{
			const bool east = face == 'E';
			const float arm = 0.62f;
			auto W = [&](float out, float along, float zz) { return east ? P3{ at + out, u + along, zz } : P3{ u + along, at + out, zz }; };
			const Color iron = Rgb(0x2E2A28);
			// 쇠팔 + 받침
			gfx::Face(W(0.f, -0.025f, z), W(arm, -0.025f, z), W(arm, -0.025f, z + 0.05f), W(0.f, -0.025f, z + 0.05f), iron);
			gfx::Face(W(0.f, 0.f, z - 0.3f), W(0.02f, 0.f, z - 0.3f), W(arm * 0.6f, 0.f, z), W(arm * 0.6f - 0.03f, 0.f, z), iron);
			// 판: 팔 아래에 매달려 바람에 흔들린다(아랫변이 바람 쪽으로)
			const P3 hinge = W(arm * 0.55f, 0.f, z);
			const Vector2 hIso = gfx::IsoPx(hinge.x, hinge.y, hinge.z);
			const float sway = wind::Sway(hIso, 3.2f, u * 1.7f) * 0.1f;
			const float bw = 0.46f, bh = 0.46f;
			const P3 tl = W(arm * 0.55f - bw * 0.5f, 0.f, z - 0.05f), tr = W(arm * 0.55f + bw * 0.5f, 0.f, z - 0.05f);
			const P3 br = W(arm * 0.55f + bw * 0.5f + sway, 0.f, z - 0.05f - bh), bl = W(arm * 0.55f - bw * 0.5f + sway, 0.f, z - 0.05f - bh);
			const Color board = Shade(L.trim, 1.35f);
			gfx::Face(bl, br, tr, tl, east ? Shade(board, 0.95f) : Shade(board, 0.8f));
			gfx::Face(W(arm * 0.55f - bw * 0.5f + 0.04f, 0.01f, z - 0.09f), W(arm * 0.55f + bw * 0.5f - 0.04f, 0.01f, z - 0.09f),
				W(arm * 0.55f + bw * 0.5f - 0.04f + sway, 0.01f, z - 0.01f - bh), W(arm * 0.55f - bw * 0.5f + 0.04f + sway, 0.01f, z - 0.01f - bh), Rgb(0x3A2E26));
			const P3 mid = W(arm * 0.55f + sway * 0.5f, 0.02f, z - 0.05f - bh * 0.5f);
			gfx::SetDepth(mid.x + mid.y + 0.02f);
			DrawIcon(icon, gfx::IsoPx(mid.x, mid.y, mid.z), 1.25f);
		}

		// 깃발: 장대 + 바람 쪽으로 뻗어 펄럭이는 천(가운데에 문장 띠)
		void Flag(P3 base, float poleH, Color cloth, Color emblem, float seed, float len = 1.1f, float clothH = 0.62f)
		{
			const Color pole = Rgb(0x5A4632);
			gfx::Box(base.x - 0.04f, base.y - 0.04f, base.x + 0.04f, base.y + 0.04f, base.z, base.z + poleH, Shade(pole, 1.1f), pole, Shade(pole, 0.7f));
			const Vector2 td{ wind::ShaderParams().x, wind::ShaderParams().y };
			const Vector2 iso = gfx::IsoPx(base.x, base.y, 0.f);
			const float spread = wind::FlagSpread(iso);
			const int n = 8;
			const float top = base.z + poleH - 0.04f;
			P3 prevT{}, prevB{};
			for (int i = 0; i <= n; ++i)
			{
				const float u = static_cast<float>(i) / n;
				const float wv = wind::FlagWave(iso, u, seed);
				const float reach = u * len * spread;
				const float droop = u * len * (1.f - spread) * 0.8f;
				// 천이 바람 방향으로 뻗고(수평), 물결은 옆(바람과 수직)으로 흔들린다
				const P3 t{ base.x + td.x * reach - td.y * wv * 0.18f, base.y + td.y * reach + td.x * wv * 0.18f, top - droop };
				const P3 b{ t.x, t.y, t.z - clothH };
				if (i > 0)
				{
					const float slope = (t.x - prevT.x) * -0.7f + (t.y - prevT.y) * 0.7f;   // 물결 기울기로 명암
					const float sh = std::clamp(0.86f + slope * 3.f, 0.6f, 1.08f);
					const Color c = (i == n / 2 || i == n / 2 + 1) ? emblem : cloth;
					gfx::Face(prevB, b, t, prevT, Shade(c, sh));
				}
				prevT = t;
				prevB = b;
			}
			gfx::SetDepth(base.x + base.y);
			gfx::Circle(gfx::IsoPx(base.x, base.y, base.z + poleH + 0.06f), 2.2f, Rgb(0xE8C860));
		}

		// 벽에 드리운 긴 깃발(결사 회관): 아랫단이 바람에 살짝 흔들린다
		void WallBanner(float u, float at, float zTop, float w, float h, Color cloth, Color emblem, float seed)
		{
			const Vector2 iso = gfx::IsoPx(u, at, 0.f);
			const float sw = wind::Sway(iso, 2.2f, seed) * 0.12f;
			const P3 tl{ u - w * 0.5f, at + 0.03f, zTop }, tr{ u + w * 0.5f, at + 0.03f, zTop };
			const P3 br{ u + w * 0.5f + sw, at + 0.05f, zTop - h }, bl{ u - w * 0.5f + sw, at + 0.05f, zTop - h };
			gfx::Face(bl, br, tr, tl, cloth);
			gfx::FaceTri(bl, br, P3{ u + sw, at + 0.05f, zTop - h - 0.25f }, cloth);
			gfx::Face({ u - 0.05f, at + 0.04f, zTop - 0.1f }, { u + 0.05f, at + 0.04f, zTop - 0.1f }, { u + 0.05f + sw * 0.8f, at + 0.06f, zTop - h + 0.1f }, { u - 0.05f + sw * 0.8f, at + 0.06f, zTop - h + 0.1f }, emblem);
			const P3 m{ u + sw * 0.4f, at + 0.07f, zTop - h * 0.38f };
			gfx::SetDepth(m.x + m.y);
			gfx::Circle(gfx::IsoPx(m.x, m.y, m.z), 4.f, emblem);
			gfx::Circle(gfx::IsoPx(m.x, m.y, m.z), 2.2f, cloth);
			gfx::Box(u - w * 0.5f - 0.08f, at, u + w * 0.5f + 0.08f, at + 0.08f, zTop, zTop + 0.06f, Rgb(0x6A5232), Rgb(0x5A4428), Rgb(0x4A3820));
		}

		// 등불(밤에 빛난다): 쇠 테두리 상자 + 빛
		void Lantern(P3 p, Color light)
		{
			gfx::Box(p.x - 0.07f, p.y - 0.07f, p.x + 0.07f, p.y + 0.07f, p.z - 0.18f, p.z, Mix(Rgb(0x5A5040), light, Night() * 0.8f), Rgb(0x2E2A26), Rgb(0x2E2A26));
			if (Night() > 0.05f)
			{
				Glow({ p.x, p.y, p.z - 0.09f }, 16.f, light, 0.8f * Night());
				PushLight({ gfx::IsoPx(p.x, p.y, 0.f), 120.f, light, 0.75f * Night() });
			}
		}

		void Barrel(float x, float y, float z = 0.f)
		{
			const Color w = Rgb(0x8A5A34);
			for (int i = 0; i < 6; ++i)
			{
				const float a0 = i / 6.f * 2.f * kPi, a1 = (i + 1) / 6.f * 2.f * kPi;
				const float nx = std::cos((a0 + a1) * 0.5f), ny = std::sin((a0 + a1) * 0.5f);
				if (nx + ny < -0.3f) continue;
				const float sh = 0.84f + 0.12f * ny - 0.12f * nx;
				const P3 b0{ x + std::cos(a0) * 0.22f, y + std::sin(a0) * 0.22f, z }, b1{ x + std::cos(a1) * 0.22f, y + std::sin(a1) * 0.22f, z };
				gfx::Face(b0, b1, { b1.x, b1.y, z + 0.5f }, { b0.x, b0.y, z + 0.5f }, Shade(w, sh));
				gfx::Face({ b0.x, b0.y, z + 0.12f }, { b1.x, b1.y, z + 0.12f }, { b1.x, b1.y, z + 0.16f }, { b0.x, b0.y, z + 0.16f }, Shade(Rgb(0x3A3430), sh));
				gfx::Face({ b0.x, b0.y, z + 0.36f }, { b1.x, b1.y, z + 0.36f }, { b1.x, b1.y, z + 0.4f }, { b0.x, b0.y, z + 0.4f }, Shade(Rgb(0x3A3430), sh));
			}
			gfx::SetDepth(x + y);
			gfx::Ellipse(gfx::IsoPx(x, y, z + 0.5f), 0.22f * 32.f * 1.3f, 0.22f * 16.f * 1.3f, Rgb(0x7A5030));
		}
		void Crate(float x, float y, float z, float s, uint32_t h)
		{
			const Color w = Jitter(Rgb(0xA87848), h, 0.1f);
			gfx::Box(x - s, y - s, x + s, y + s, z, z + s * 2.f, Shade(w, 1.1f), w, Shade(w, 0.72f));
			gfx::Face({ x - s, y + s + 0.01f, z }, { x - s + 0.05f, y + s + 0.01f, z }, { x + s, y + s + 0.01f, z + s * 2.f }, { x + s - 0.05f, y + s + 0.01f, z + s * 2.f }, Shade(w, 0.7f));
		}
		void Hay(float x, float y, float z = 0.f)
		{
			gfx::Box(x - 0.34f, y - 0.24f, x + 0.34f, y + 0.24f, z, z + 0.4f, Rgb(0xE8CC78), Rgb(0xD0B060), Rgb(0xA88C48));
			gfx::Face({ x - 0.34f, y + 0.25f, z + 0.13f }, { x + 0.34f, y + 0.25f, z + 0.13f }, { x + 0.34f, y + 0.25f, z + 0.16f }, { x - 0.34f, y + 0.25f, z + 0.16f }, Rgb(0x8A6A3A));
		}

		// 빨랫줄: 두 점 사이 늘어진 줄에 천 몇 장 — 바람에 펄럭인다
		void Laundry(P3 a, P3 b, float seed)
		{
			const int n = 10;
			P3 prev = a;
			for (int i = 1; i <= n; ++i)
			{
				const float t = static_cast<float>(i) / n;
				const P3 p{ util::Lerp(a.x, b.x, t), util::Lerp(a.y, b.y, t), util::Lerp(a.z, b.z, t) - std::sin(t * kPi) * 0.18f };
				gfx::SetDepth(p.x + p.y);
				gfx::Line(gfx::IsoPx(prev.x, prev.y, prev.z), gfx::IsoPx(p.x, p.y, p.z), 1.f, Rgb(0xD8D0C0));
				prev = p;
			}
			constexpr uint32_t kCloth[] = { 0xF4F0E8, 0xC85A4A, 0x5A7AB0, 0xE8D8A0, 0x7A9A6A };
			for (int k = 0; k < 4; ++k)
			{
				const float t = 0.18f + k * 0.2f;
				const P3 p{ util::Lerp(a.x, b.x, t), util::Lerp(a.y, b.y, t), util::Lerp(a.z, b.z, t) - std::sin(t * kPi) * 0.18f };
				const Vector2 iso = gfx::IsoPx(p.x, p.y, 0.f);
				const float sw = wind::Sway(iso, 3.f, seed + k) * 0.16f;
				const float w = 0.13f, h = 0.32f + 0.08f * (k % 2);
				const Vector4 wp = wind::ShaderParams();
				const Color c = Rgb(kCloth[(k + static_cast<int>(seed)) % 5]);
				gfx::Face({ p.x - (b.x - a.x) * w * 0.3f, p.y - (b.y - a.y) * w * 0.3f, p.z }, { p.x + (b.x - a.x) * w * 0.3f, p.y + (b.y - a.y) * w * 0.3f, p.z },
					{ p.x + (b.x - a.x) * w * 0.3f + wp.x * sw, p.y + (b.y - a.y) * w * 0.3f + wp.y * sw, p.z - h },
					{ p.x - (b.x - a.x) * w * 0.3f + wp.x * sw, p.y - (b.y - a.y) * w * 0.3f + wp.y * sw, p.z - h }, c);
			}
		}

		// ── 건물 공통: 몸통 사각형 · 문 · 창 · 간판 자리 ────────────────────────────────
		struct Frame
		{
			float x0, y0, x1, y1;   // 벽 사각형(칸 가장자리)
			char door;
			float doorU;            // 문이 난 면 위의 가운데
			uint32_t h;             // 건물마다 고정된 난수
			float time;
		};

		Frame MakeFrame(const TownMap::BuildingRow& b, float time)
		{
			Frame f{ b.x0 - 0.5f, b.y0 - 0.5f, b.x1 + 0.5f, b.y1 + 0.5f, b.door, 0.f, util::Hash2(b.x0, b.y0, 4242), time };
			f.doorU = (f.door == 'S' || f.door == 'N') ? (f.x0 + f.x1) * 0.5f : (f.y0 + f.y1) * 0.5f;
			return f;
		}

		// 문 · 간판: 문이 보이는 면(남 · 동)이면 그 자리에, 보이지 않는 면(북 · 서)이면 간판만 가까운 모서리의 보이는 면에
		void FrontDressing(const Frame& f, const Look& L, float doorH, Icon icon, float signZ, bool arch = false, float doorW = 0.8f)
		{
			if (f.door == 'S') DoorFace(false, f.doorU, f.y1, doorW, doorH, L, arch);
			else if (f.door == 'E') DoorFace(true, f.doorU, f.x1, doorW, doorH, L, arch);
			if (icon == Icon::None) return;
			switch (f.door)
			{
			case 'S': Sign('S', std::min(f.x1 - 0.35f, f.doorU + doorW * 0.5f + 0.45f), f.y1, signZ, icon, L); break;
			case 'E': Sign('E', std::min(f.y1 - 0.35f, f.doorU + doorW * 0.5f + 0.45f), f.x1, signZ, icon, L); break;
			case 'N': Sign('E', f.y0 + 0.45f, f.x1, signZ, icon, L); break;
			case 'W': Sign('S', f.x0 + 0.45f, f.y1, signZ, icon, L); break;
			}
		}
		// 보이는 두 면의 창(문 자리를 비운다)
		void Windows(const Frame& f, const Look& L, float z0, float z1, float spacing, bool shutters, bool flowers, float doorW = 1.1f)
		{
			const float skipS0 = f.door == 'S' ? f.doorU - doorW * 0.5f : 1e9f, skipS1 = f.door == 'S' ? f.doorU + doorW * 0.5f : -1e9f;
			const float skipE0 = f.door == 'E' ? f.doorU - doorW * 0.5f : 1e9f, skipE1 = f.door == 'E' ? f.doorU + doorW * 0.5f : -1e9f;
			WindowRow(false, f.x0 + 0.3f, f.x1 - 0.3f, f.y1, z0, z1, L, spacing, skipS0, skipS1, shutters, flowers, f.h);
			WindowRow(true, f.y0 + 0.3f, f.y1 - 0.3f, f.x1, z0, z1, L, spacing, skipE0, skipE1, shutters, flowers, f.h >> 3);
		}

		// ─────────────────────────────────────────────────────────────────────────
		// 종류별
		// ─────────────────────────────────────────────────────────────────────────
		void House(const TownMap::BuildingRow& b, const Frame& f, Look L)
		{
			const float W = f.x1 - f.x0, D = f.y1 - f.y0;
			const bool alongX = W >= D;
			const Color gw = Shade(WallFallback(L.wall), 0.72f);
			const Color gableWall = WallTex(L.wall) ? Shade(Mix(WallFallback(L.wall), L.wallMul, 0.5f), 0.74f) : gw;
			const float t = f.time;
			const uint32_t h = f.h;
			Plinth(f.x0, f.y0, f.x1, f.y1);
			switch (b.variant % 10)
			{
			case 0:   // 단층 박공
			default:
			{
				const float hh = 1.5f;
				Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, hh, L);
				Windows(f, L, 0.72f, 1.2f, 1.7f, true, (h & 1) != 0);
				FrontDressing(f, L, 1.12f, Icon::None, 0.f);
				Gable(f.x0, f.y0, f.x1, f.y1, hh, 1.1f, 0.28f, alongX, L, gableWall);
				Chimney(alongX ? f.x1 - 1.0f : (f.x0 + f.x1) * 0.5f + 0.5f, alongX ? (f.y0 + f.y1) * 0.5f - 0.5f : f.y0 + 0.9f, 1.8f, 3.0f, t, (h & 7) * 0.13f, true);
				break;
			}
			case 1:   // 2층(윗층이 조금 튀어나온 목조)
			{
				const float h1 = 1.4f, h2 = 2.6f, j = 0.12f;
				Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, h1, L);
				Band(f.x0, f.y0, f.x1, f.y1, h1 - 0.02f, L.trim, 0.12f);
				Walls(f.x0, f.y0, f.x1 + j, f.y1 + j, h1 + 0.1f, h2, L);
				Windows(f, L, 0.62f, 1.08f, 1.6f, false, false);
				Frame up = f; up.x1 += j; up.y1 += j; up.door = 'X';
				Windows(up, L, h1 + 0.52f, h1 + 0.98f, 1.4f, true, true);
				FrontDressing(f, L, 1.05f, Icon::None, 0.f);
				Gable(f.x0, f.y0, f.x1 + j, f.y1 + j, h2, 1.2f, 0.26f, alongX, L, gableWall);
				Chimney(f.x0 + W * 0.28f, f.y0 + D * 0.3f, 2.4f, 4.2f, t, (h & 7) * 0.11f, true);
				break;
			}
			case 2:   // ㄱ자: 긴 채 + 옆 채, 남은 모퉁이는 울타리 친 마당
			{
				const float hh = 1.5f;
				const float wing = alongX ? std::max(1.6f, D * 0.55f) : std::max(1.6f, W * 0.55f);
				if (alongX)
				{
					// 긴 채(북쪽 줄) + 동쪽 날개(남쪽까지)
					const float yM = f.y0 + wing, xW = f.x1 - std::max(1.8f, W * 0.38f);
					Walls(f.x0, f.y0, xW, yM, 0.18f, hh, L);
					Walls(xW, f.y0, f.x1, f.y1, 0.18f, hh, L);
					Frame a = f; a.x1 = xW; a.y1 = yM; a.door = 'S'; a.doorU = (f.x0 + xW) * 0.5f;
					DoorFace(false, a.doorU, yM, 0.8f, 1.12f, L, false);
					WindowRow(false, f.x0 + 0.3f, xW - 0.3f, yM, 0.72f, 1.2f, L, 1.6f, a.doorU - 0.55f, a.doorU + 0.55f, true, true, h);
					WindowRow(false, xW + 0.3f, f.x1 - 0.3f, f.y1, 0.72f, 1.2f, L, 1.6f, 1e9f, -1e9f, true, false, h >> 2);
					WindowRow(true, f.y0 + 0.3f, f.y1 - 0.3f, f.x1, 0.72f, 1.2f, L, 1.7f, 1e9f, -1e9f, true, false, h >> 4);
					Gable(xW, f.y0, f.x1, f.y1, hh, 1.1f, 0.26f, false, L, gableWall);
					Gable(f.x0, f.y0, xW + 0.1f, yM, hh, 1.0f, 0.26f, true, L, gableWall);
					// 마당: 낮은 울타리 + 통
					const Color wd = Shade(L.trim, 1.3f);
					for (float x = f.x0 + 0.15f; x < xW - 0.05f; x += 0.45f)
						gfx::Box(x - 0.03f, f.y1 - 0.12f, x + 0.03f, f.y1 - 0.06f, 0.f, 0.45f, wd, wd, Shade(wd, 0.7f));
					gfx::Box(f.x0 + 0.1f, f.y1 - 0.12f, xW - 0.1f, f.y1 - 0.08f, 0.3f, 0.36f, wd, wd, Shade(wd, 0.7f));
					Barrel(f.x0 + 0.5f, yM + 0.45f);
				}
				else
				{
					const float xM = f.x0 + wing, yW = f.y1 - std::max(1.8f, D * 0.38f);
					Walls(f.x0, f.y0, xM, f.y1, 0.18f, hh, L);
					Walls(xM, yW, f.x1, f.y1, 0.18f, hh, L);
					DoorFace(true, (f.y0 + yW) * 0.5f, xM, 0.8f, 1.12f, L, false);
					WindowRow(false, f.x0 + 0.3f, f.x1 - 0.3f, f.y1, 0.72f, 1.2f, L, 1.6f, 1e9f, -1e9f, true, true, h);
					WindowRow(true, yW + 0.3f, f.y1 - 0.3f, f.x1, 0.72f, 1.2f, L, 1.6f, 1e9f, -1e9f, true, false, h >> 2);
					Gable(f.x0, f.y0, xM, f.y1, hh, 1.1f, 0.26f, false, L, gableWall);
					Gable(xM - 0.1f, yW, f.x1, f.y1, hh, 1.0f, 0.26f, true, L, gableWall);
					const Color wd = Shade(L.trim, 1.3f);
					for (float y = f.y0 + 0.15f; y < yW - 0.05f; y += 0.45f)
						gfx::Box(f.x1 - 0.12f, y - 0.03f, f.x1 - 0.06f, y + 0.03f, 0.f, 0.45f, wd, wd, Shade(wd, 0.7f));
					Hay(xM + 0.6f, f.y0 + 0.5f);
				}
				Chimney(f.x0 + 0.8f, f.y0 + 0.6f, 1.8f, 2.9f, t, (h & 7) * 0.17f, true);
				break;
			}
			case 3:   // 발코니 2층 + 빨래
			{
				const float h1 = 1.45f, h2 = 2.7f;
				Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, h2, L);
				Band(f.x0, f.y0, f.x1, f.y1, h1, L.trim);
				Windows(f, L, 0.64f, 1.1f, 1.6f, true, false);
				Frame up = f; up.door = 'X';
				Windows(up, L, h1 + 0.5f, h1 + 1.0f, 1.5f, true, true);
				FrontDressing(f, L, 1.08f, Icon::None, 0.f);
				const bool balcS = !(f.door == 'E');
				if (balcS) { Balcony(false, f.x0 + 0.4f, f.x1 - 0.4f, f.y1, h1 + 0.1f, 0.55f, L); Laundry({ f.x0 + 0.5f, f.y1 + 0.5f, h1 + 0.72f }, { f.x1 - 0.5f, f.y1 + 0.5f, h1 + 0.72f }, (h & 15) * 1.f); }
				else { Balcony(true, f.y0 + 0.4f, f.y1 - 0.4f, f.x1, h1 + 0.1f, 0.55f, L); Laundry({ f.x1 + 0.5f, f.y0 + 0.5f, h1 + 0.72f }, { f.x1 + 0.5f, f.y1 - 0.5f, h1 + 0.72f }, (h & 15) * 1.f); }
				Hip(f.x0, f.y0, f.x1, f.y1, h2, 1.1f, 0.26f, L);
				Chimney(f.x1 - 0.9f, f.y0 + 0.7f, 2.4f, 4.1f, t, (h & 7) * 0.19f, true);
				break;
			}
			case 4:   // 오두막: 낮은 벽 · 가파른 초가
			{
				Look T = L; T.roof = Roof::Thatch; if (T.wall == Wall::Stone || T.wall == Wall::Dressed) T.wall = Wall::Mud, T.wallMul = Rgb(0xECD2A8);
				const float hh = 1.15f;
				Walls(f.x0 + 0.1f, f.y0 + 0.1f, f.x1 - 0.1f, f.y1 - 0.1f, 0.12f, hh, T);
				Frame in = f; in.x0 += 0.1f; in.y0 += 0.1f; in.x1 -= 0.1f; in.y1 -= 0.1f;
				Windows(in, T, 0.52f, 0.9f, 1.8f, false, true);
				FrontDressing(in, T, 0.98f, Icon::None, 0.f);
				Gable(in.x0, in.y0, in.x1, in.y1, hh, 1.5f, 0.38f, alongX, T, Shade(Rgb(0xC8A878), 0.74f));
				Chimney(in.x0 + 0.7f, in.y0 + 0.6f, 1.6f, 2.6f, t, (h & 7) * 0.23f, true);
				Hay(in.x1 + 0.1f, in.y1 + 0.1f);
				break;
			}
			case 5:   // 돌집: 모임지붕 · 작은 창
			{
				Look S = L; S.wall = Wall::Stone; S.wallMul = Jitter(Rgb(0xDCD6CC), h, 0.05f);
				const float hh = 1.7f;
				Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, hh, S, 1.4f);
				Windows(f, S, 0.85f, 1.25f, 1.8f, true, false);
				FrontDressing(f, S, 1.15f, Icon::None, 0.f, true);
				Hip(f.x0, f.y0, f.x1, f.y1, hh, 1.2f, 0.24f, S);
				Chimney(f.x0 + 0.8f, (f.y0 + f.y1) * 0.5f, 2.2f, 3.4f, t, (h & 7) * 0.29f, true);
				break;
			}
			case 6:   // 좁고 높은 집: 2층 반 · 가파른 지붕 · 지붕창
			{
				const float h2 = 2.9f;
				Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, h2, L);
				Band(f.x0, f.y0, f.x1, f.y1, 1.45f, L.trim, 0.08f);
				Windows(f, L, 0.62f, 1.08f, 1.5f, true, false);
				Frame up = f; up.door = 'X';
				Windows(up, L, 1.9f, 2.45f, 1.3f, false, true);
				FrontDressing(f, L, 1.05f, Icon::None, 0.f);
				Gable(f.x0, f.y0, f.x1, f.y1, h2, 1.7f, 0.22f, alongX, L, gableWall);
				// 지붕창(남쪽 사면 가운데)
				if (alongX)
				{
					const float cx = (f.x0 + f.x1) * 0.5f, yc = (f.y0 + f.y1) * 0.5f, ye = f.y1 - 0.3f;
					Walls(cx - 0.45f, yc, cx + 0.45f, ye, h2 + 0.3f, h2 + 1.0f, L);
					WindowFace(false, cx - 0.22f, cx + 0.22f, ye, h2 + 0.45f, h2 + 0.85f, L, false, false, h);
					Gable(cx - 0.45f, yc, cx + 0.45f, ye, h2 + 1.0f, 0.45f, 0.08f, false, L, gableWall);
				}
				Chimney(f.x1 - 0.7f, f.y0 + 0.6f, 3.2f, 4.9f, t, (h & 7) * 0.31f, true);
				break;
			}
			case 7:   // 긴 농가 + 옆 헛간(외쪽지붕) + 건초
			{
				const float hh = 1.4f;
				const float split = alongX ? f.x1 - std::min(2.0f, W * 0.35f) : f.y1 - std::min(2.0f, D * 0.35f);
				if (alongX)
				{
					Walls(f.x0, f.y0, split, f.y1, 0.18f, hh, L);
					Frame a = f; a.x1 = split;
					Windows(a, L, 0.7f, 1.15f, 1.7f, true, (h & 2) != 0);
					FrontDressing(a, L, 1.08f, Icon::None, 0.f);
					Gable(f.x0, f.y0, split, f.y1, hh, 1.2f, 0.28f, true, L, gableWall);
					// 헛간: 기둥 셋 + 외쪽지붕(동쪽으로 낮아진다)
					const Color wd = Shade(L.trim, 1.25f);
					for (float y : { f.y0 + 0.2f, (f.y0 + f.y1) * 0.5f, f.y1 - 0.2f })
						gfx::Box(f.x1 - 0.22f, y - 0.06f, f.x1 - 0.1f, y + 0.06f, 0.f, 1.05f, wd, wd, Shade(wd, 0.7f));
					gfx::Face({ split, f.y0 - 0.1f, 1.3f }, { split, f.y1 + 0.15f, 1.3f }, { f.x1 + 0.12f, f.y1 + 0.15f, 1.0f }, { f.x1 + 0.12f, f.y0 - 0.1f, 1.0f }, Shade(RoofFallback(L.roof == Roof::Thatch ? Roof::Shingle : L.roof), 0.9f));
					Hay(split + 0.6f, f.y1 - 0.5f);
					Hay(split + 0.6f, f.y1 - 1.1f, 0.f);
					Hay(split + 0.6f, f.y1 - 0.8f, 0.4f);
				}
				else
				{
					Walls(f.x0, f.y0, f.x1, split, 0.18f, hh, L);
					Frame a = f; a.y1 = split;
					Windows(a, L, 0.7f, 1.15f, 1.7f, true, (h & 2) != 0);
					FrontDressing(a, L, 1.08f, Icon::None, 0.f);
					Gable(f.x0, f.y0, f.x1, split, hh, 1.2f, 0.28f, false, L, gableWall);
					const Color wd = Shade(L.trim, 1.25f);
					for (float x : { f.x0 + 0.2f, (f.x0 + f.x1) * 0.5f, f.x1 - 0.2f })
						gfx::Box(x - 0.06f, f.y1 - 0.22f, x + 0.06f, f.y1 - 0.1f, 0.f, 1.05f, wd, wd, Shade(wd, 0.7f));
					gfx::Face({ f.x0 - 0.1f, split, 1.3f }, { f.x1 + 0.15f, split, 1.3f }, { f.x1 + 0.15f, f.y1 + 0.12f, 1.0f }, { f.x0 - 0.1f, f.y1 + 0.12f, 1.0f }, RoofFallback(L.roof == Roof::Thatch ? Roof::Shingle : L.roof));
					Hay(f.x0 + 0.7f, split + 0.6f);
					Hay(f.x0 + 1.4f, split + 0.6f);
				}
				Chimney(f.x0 + 0.7f, f.y0 + 0.6f, 1.8f, 2.9f, t, (h & 7) * 0.37f, true);
				break;
			}
			case 8:   // 작은 탑이 붙은 집
			{
				const float hh = 1.6f;
				Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, hh, L);
				Windows(f, L, 0.74f, 1.2f, 1.7f, true, true);
				FrontDressing(f, L, 1.1f, Icon::None, 0.f);
				Gable(f.x0, f.y0, f.x1, f.y1, hh, 1.1f, 0.26f, alongX, L, gableWall);
				// 남동 모서리의 둥근 탑(팔각) + 뿔 지붕
				const float cx = f.x1 - 0.55f, cy = f.y1 - 0.55f;
				Prism(cx, cy, 0.62f, 0.58f, 8, 0.18f, 3.0f, WallTex(L.wall == Wall::Planks ? Wall::Stone : L.wall), L.wall == Wall::Planks ? Rgb(0xD8D2C8) : L.wallMul, 1.2f);
				WindowFace(false, cx - 0.15f, cx + 0.15f, cy + 0.6f, 2.1f, 2.55f, L, false, false, h);
				Cone(cx, cy, 0.62f, 8, 3.0f, 4.3f, L);
				break;
			}
			case 9:   // 상점 겸 주택: 아래층은 활짝 연 가게(차양 · 물건), 위층은 살림
			{
				const float h1 = 1.35f, h2 = 2.55f;
				Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, h2, L);
				Band(f.x0, f.y0, f.x1, f.y1, h1, L.trim, 0.12f);
				const bool east = f.door == 'E';
				// 가게 앞 열린 칸(어두운 안쪽 + 진열대)
				auto shopFront = [&](bool e, float a, float c, float at)
				{
					if (e) gfx::Face({ at + 0.01f, a, 0.2f }, { at + 0.01f, c, 0.2f }, { at + 0.01f, c, h1 - 0.1f }, { at + 0.01f, a, h1 - 0.1f }, Rgb(0x2A2220));
					else gfx::Face({ a, at + 0.01f, 0.2f }, { c, at + 0.01f, 0.2f }, { c, at + 0.01f, h1 - 0.1f }, { a, at + 0.01f, h1 - 0.1f }, Rgb(0x2A2220));
					for (float u = a + 0.3f; u < c - 0.2f; u += 0.55f)
					{
						const P3 p = e ? P3{ at + 0.28f, u, 0.f } : P3{ u, at + 0.28f, 0.f };
						Crate(p.x, p.y, 0.f, 0.17f, util::Hash32(static_cast<uint32_t>(u * 100.f)));
						gfx::SetDepth(p.x + p.y + 0.05f);
						const Color fruit = (static_cast<int>(u * 10.f) % 3 == 0) ? Rgb(0xE8A030) : (static_cast<int>(u * 10.f) % 3 == 1 ? Rgb(0xC83A3A) : Rgb(0x7AB040));
						gfx::Circle(gfx::IsoPx(p.x, p.y, 0.36f), 4.f, fruit);
					}
				};
				if (east) { shopFront(true, f.y0 + 0.4f, f.y1 - 0.4f, f.x1); Awning(true, f.y0 + 0.2f, f.y1 - 0.2f, f.x1, h1 + 0.05f, 0.7f, Rgb(0x3A7A6A), Rgb(0xF0E8D8)); }
				else { shopFront(false, f.x0 + 0.4f, f.x1 - 0.4f, f.y1); Awning(false, f.x0 + 0.2f, f.x1 - 0.2f, f.y1, h1 + 0.05f, 0.7f, Rgb(0x3A7A6A), Rgb(0xF0E8D8)); }
				Frame up = f; up.door = 'X';
				Windows(up, L, h1 + 0.45f, h1 + 0.95f, 1.5f, true, true);
				if (!east) WindowRow(true, f.y0 + 0.3f, f.y1 - 0.3f, f.x1, 0.62f, 1.08f, L, 1.6f, 1e9f, -1e9f, true, false, f.h);
				else WindowRow(false, f.x0 + 0.3f, f.x1 - 0.3f, f.y1, 0.62f, 1.08f, L, 1.6f, 1e9f, -1e9f, true, false, f.h);
				Sign(east ? 'E' : 'S', east ? f.y0 + 0.35f : f.x1 - 0.35f, east ? f.x1 : f.y1, h1 + 0.25f, Icon::Sack, L);
				Gable(f.x0, f.y0, f.x1, f.y1, h2, 1.15f, 0.26f, !alongX ? false : true, L, gableWall);
				Chimney(f.x0 + 0.7f, f.y0 + 0.7f, 2.4f, 3.8f, t, (h & 7) * 0.41f, true);
				break;
			}
			}
		}

		void Store(const Frame& f, Look L)
		{
			const float h1 = 1.55f, h2 = 2.75f;
			Plinth(f.x0, f.y0, f.x1, f.y1);
			Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, h2, L);
			Band(f.x0, f.y0, f.x1, f.y1, h1, L.trim, 0.12f);
			Windows(f, L, 0.6f, 1.15f, 1.5f, true, false, 1.6f);
			Frame up = f; up.door = 'X';
			Windows(up, L, h1 + 0.45f, h1 + 0.95f, 1.35f, true, true);
			// 보이는 두 면에 줄무늬 차양 + 앞에 상자 · 통
			Awning(false, f.x0 + 0.3f, f.x1 - 0.3f, f.y1, h1 + 0.02f, 0.75f, Rgb(0xC8483A), Rgb(0xF4ECDC));
			Awning(true, f.y0 + 0.3f, f.y1 - 0.3f, f.x1, h1 + 0.02f, 0.75f, Rgb(0xC8483A), Rgb(0xF4ECDC));
			Crate(f.x0 + 0.7f, f.y1 + 0.35f, 0.f, 0.2f, f.h);
			Crate(f.x0 + 1.15f, f.y1 + 0.35f, 0.f, 0.16f, f.h >> 3);
			Barrel(f.x1 + 0.35f, f.y1 - 0.8f);
			FrontDressing(f, L, 1.2f, Icon::Potion, 1.35f, false, 1.0f);
			Gable(f.x0, f.y0, f.x1, f.y1, h2, 1.35f, 0.3f, (f.x1 - f.x0) >= (f.y1 - f.y0), L, Shade(Mix(WallFallback(L.wall), L.wallMul, 0.5f), 0.74f));
			Chimney(f.x0 + 0.9f, f.y0 + 0.8f, 3.0f, 4.6f, f.time, 0.3f, true, true);
		}

		void Smithy(const Frame& f, Look L)
		{
			// 몸채(돌) + 동쪽 열린 대장간 헛간(외쪽지붕 · 화덕 · 모루) + 높은 굴뚝(연기 · 불똥)
			const float shed = std::min(2.2f, (f.x1 - f.x0) * 0.38f);
			const float xb = f.x1 - shed;
			const float hh = 2.0f;
			Look S = L; S.wall = Wall::Stone; S.roof = Roof::DarkSlate; S.roofMul = Rgb(0xB4B4BE);
			Plinth(f.x0, f.y0, xb, f.y1);
			Walls(f.x0, f.y0, xb, f.y1, 0.18f, hh, S, 1.3f);
			Frame body = f; body.x1 = xb; if (body.door == 'E') body.door = 'S', body.doorU = (f.x0 + xb) * 0.5f;
			Windows(body, S, 0.9f, 1.4f, 1.8f, false, false);
			FrontDressing(body, S, 1.3f, Icon::Swords, 1.5f, true, 1.0f);
			Gable(f.x0, f.y0, xb, f.y1, hh, 1.2f, 0.26f, false, S, Shade(Rgb(0x9A958C), 0.72f));
			// 헛간
			const Color wd = Shade(S.trim, 1.3f);
			for (float y : { f.y0 + 0.2f, (f.y0 + f.y1) * 0.5f, f.y1 - 0.2f })
				gfx::Box(f.x1 - 0.2f, y - 0.07f, f.x1 - 0.06f, y + 0.07f, 0.f, 1.5f, wd, wd, Shade(wd, 0.7f));
			gfx::FaceTex(*envart::Get(envart::Tex::RoofSlate), { xb, f.y1 + 0.2f, 1.9f }, { f.x1 + 0.15f, f.y1 + 0.2f, 1.5f }, { f.x1 + 0.15f, f.y0 - 0.1f, 1.5f }, { xb, f.y0 - 0.1f, 1.9f }, 2.f, 3.f, Rgb(0x8A8A94));
			// 화덕(빛나는 입) + 모루
			const float fx = xb + 0.6f, fy = f.y0 + 0.9f;
			gfx::Box(fx - 0.45f, fy - 0.45f, fx + 0.45f, fy + 0.45f, 0.f, 0.8f, Rgb(0x6A605A), Rgb(0x5A504A), Rgb(0x4A423C));
			const float pulse = 0.75f + 0.25f * std::sin(f.time * 3.1f) * std::sin(f.time * 1.7f + 1.f);
			gfx::Face({ fx - 0.25f, fy + 0.46f, 0.25f }, { fx + 0.25f, fy + 0.46f, 0.25f }, { fx + 0.25f, fy + 0.46f, 0.6f }, { fx - 0.25f, fy + 0.46f, 0.6f }, Mix(Rgb(0xFF6A20), Rgb(0xFFD070), pulse * 0.5f));
			Glow({ fx, fy + 0.5f, 0.45f }, 26.f, Rgb(0xFF7A30), 0.55f + 0.35f * pulse);
			PushLight({ gfx::IsoPx(fx, fy + 0.8f), 170.f, Rgb(0xFF8A30), 0.75f * pulse });
			const float ax = xb + 0.8f, ay = f.y1 - 0.9f;
			gfx::Box(ax - 0.12f, ay - 0.1f, ax + 0.12f, ay + 0.1f, 0.f, 0.42f, Rgb(0x3A3634), Rgb(0x2E2A28), Rgb(0x22201E));
			gfx::Box(ax - 0.3f, ay - 0.14f, ax + 0.3f, ay + 0.14f, 0.42f, 0.56f, Rgb(0x5A5A62), Rgb(0x44444C), Rgb(0x34343A));
			// 굴뚝(높다) — 연기 + 가끔 불똥
			const float cx = xb - 0.5f, cy = f.y0 + 0.7f;
			Chimney(cx, cy, 2.4f, 4.4f, f.time, 0.7f, true);
			const float spark = std::fmod(f.time * 0.9f, 1.f);
			if (spark < 0.5f) Glow({ cx + std::sin(f.time * 7.f) * 0.15f, cy, 4.5f + spark * 1.4f }, 4.f, Rgb(0xFFB040), 1.f - spark * 2.f);
		}

		void Armorer(const Frame& f, Look L)
		{
			Look S = L; S.wall = Wall::Stone;
			L.roof = Roof::BlueSlate; L.roofMul = Rgb(0xB8C8E8); L.wall = Wall::Plaster; L.wallMul = Rgb(0xF4ECDC);
			const float h1 = 1.5f, h2 = 2.6f;
			Plinth(f.x0, f.y0, f.x1, f.y1);
			Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, h1, S, 1.3f);
			Walls(f.x0, f.y0, f.x1, f.y1, h1, h2, L);
			Band(f.x0, f.y0, f.x1, f.y1, h1 - 0.04f, L.trim, 0.1f);
			Windows(f, S, 0.7f, 1.2f, 1.7f, false, false, 1.5f);
			Frame up = f; up.door = 'X';
			Windows(up, L, h1 + 0.4f, h1 + 0.85f, 1.5f, true, false);
			FrontDressing(f, S, 1.2f, Icon::Shield, 1.4f, true, 1.0f);
			// 벽에 건 방패 셋 + 문 옆 갑옷 걸이
			for (int i = 0; i < 3; ++i)
			{
				const float u = util::Lerp(f.x0 + 0.8f, f.x1 - 0.8f, (i + 0.5f) / 3.f);
				if (f.door == 'S' && std::fabs(u - f.doorU) < 0.8f) continue;
				const P3 c{ u, f.y1 + 0.04f, h1 + 0.55f };
				gfx::SetDepth(c.x + c.y);
				const Vector2 s = gfx::IsoPx(c.x, c.y, c.z);
				const Color sc = i == 0 ? Rgb(0x8A3A30) : (i == 1 ? Rgb(0x3A5A8A) : Rgb(0x6A6A70));
				const Vector2 p[5] = { { s.x - 8, s.y - 9 }, { s.x + 8, s.y - 9 }, { s.x + 7, s.y + 2 }, { s.x, s.y + 10 }, { s.x - 7, s.y + 2 } };
				gfx::Poly(p, 5, sc);
				gfx::Ring(s, 3.f, 5.f, Rgb(0xD8C070));
			}
			const float mx = f.x1 + 0.45f, my = f.y1 - 0.6f;
			gfx::Box(mx - 0.04f, my - 0.04f, mx + 0.04f, my + 0.04f, 0.f, 1.25f, Rgb(0x5A4632), Rgb(0x5A4632), Rgb(0x3A2E22));
			gfx::Box(mx - 0.2f, my - 0.14f, mx + 0.2f, my + 0.14f, 0.6f, 1.15f, Rgb(0xB8BCC4), Rgb(0x9AA0A8), Rgb(0x7A8088));
			gfx::SetDepth(mx + my);
			gfx::Circle(gfx::IsoPx(mx, my, 1.3f), 5.5f, Rgb(0x9AA0A8));
			Hip(f.x0, f.y0, f.x1, f.y1, h2, 1.15f, 0.26f, L);
			Chimney(f.x0 + 0.8f, f.y0 + 0.8f, 2.8f, 4.1f, f.time, 0.9f, true);
		}

		void Inn(const Frame& f, Look L)
		{
			// 2층 목조(윗층 튀어나옴) · 긴 발코니 · 굴뚝 둘 · 등불 · 간판
			const float h1 = 1.55f, h2 = 2.95f, j = 0.14f;
			Plinth(f.x0, f.y0, f.x1, f.y1);
			Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, h1, L);
			Band(f.x0, f.y0, f.x1, f.y1, h1 - 0.02f, L.trim, 0.14f);
			Walls(f.x0, f.y0, f.x1 + j, f.y1 + j, h1 + 0.12f, h2, L);
			Windows(f, L, 0.62f, 1.15f, 1.45f, true, true, 1.4f);
			Frame up = f; up.x1 += j; up.y1 += j; up.door = 'X';
			Windows(up, L, h1 + 0.5f, h1 + 1.05f, 1.3f, true, false);
			Balcony(false, f.x0 + 0.5f, f.x1 - 0.5f, f.y1 + j, h1 + 0.14f, 0.5f, L);
			FrontDressing(f, L, 1.2f, Icon::Bed, 1.45f, true, 1.1f);
			Lantern({ f.x1 + 0.2f, f.y1 + 0.2f, 1.45f }, L.glow);
			Hip(f.x0, f.y0, f.x1 + j, f.y1 + j, h2, 1.25f, 0.3f, L);
			Chimney(f.x0 + 1.0f, f.y0 + 0.8f, 3.2f, 5.0f, f.time, 0.2f, true, true);
			Chimney(f.x1 - 1.2f, f.y0 + 0.8f, 3.2f, 4.8f, f.time, 0.6f, true, true);
		}

		void Tavern(const Frame& f, Look L)
		{
			// 나무 판자 · 너와, 문 앞 현관 지붕 · 술통 · 벤치 · 등불 줄
			Look T = L; T.wall = Wall::Planks; T.wallMul = Rgb(0xE8C8A0);
			const float hh = 1.9f;
			Plinth(f.x0, f.y0, f.x1, f.y1);
			Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, hh, T, 1.6f);
			Windows(f, T, 0.75f, 1.3f, 1.4f, false, false, 1.4f);
			FrontDressing(f, T, 1.3f, Icon::Mug, 1.6f, false, 1.1f);
			Gable(f.x0, f.y0, f.x1, f.y1, hh, 1.4f, 0.3f, (f.x1 - f.x0) >= (f.y1 - f.y0), L, Shade(Rgb(0x7A5A3E), 0.74f));
			// 보이는 모퉁이에 술통 · 벤치
			Barrel(f.x1 + 0.3f, f.y1 - 0.4f);
			Barrel(f.x1 + 0.3f, f.y1 - 0.95f);
			Barrel(f.x1 + 0.3f, f.y1 - 0.7f, 0.5f);
			gfx::Box(f.x0 + 0.6f, f.y1 + 0.15f, f.x0 + 1.8f, f.y1 + 0.45f, 0.35f, 0.45f, Rgb(0xA87848), Rgb(0x8A6038), Rgb(0x6A4828));
			// 등불 줄(처마 따라)
			for (int i = 0; i < 5; ++i)
			{
				const float u = util::Lerp(f.x0 + 0.4f, f.x1 - 0.4f, (i + 0.5f) / 5.f);
				const P3 p{ u, f.y1 + 0.28f, hh + 0.05f - std::sin((i + 0.5f) / 5.f * kPi) * 0.15f };
				gfx::SetDepth(p.x + p.y);
				const Color lc = Mix(Rgb(0xC8A060), Rgb(0xFFC860), Night());
				gfx::Circle(gfx::IsoPx(p.x, p.y, p.z), 2.6f, lc);
				if (Night() > 0.05f) Glow(p, 9.f, Rgb(0xFFB850), 0.7f * Night());
			}
			if (Night() > 0.05f) PushLight({ gfx::IsoPx((f.x0 + f.x1) * 0.5f, f.y1 + 0.6f), 160.f, Rgb(0xFFB850), 0.6f * Night() });
			Chimney(f.x0 + 0.8f, f.y0 + 0.7f, 2.5f, 3.9f, f.time, 0.45f, true, true);
		}

		void Temple(const Frame& f, Look L)
		{
			Look T = L; T.wall = Wall::White; T.wallMul = Rgb(0xFFFCF4); T.roof = Roof::BlueSlate; T.roofMul = Rgb(0xC0D0F4); T.trim = Rgb(0xC8B070);
			const float h = 2.6f, roofH = 1.9f;
			Plinth(f.x0, f.y0, f.x1, f.y1, 0.26f);
			Walls(f.x0, f.y0, f.x1, f.y1, 0.26f, h, T, 1.5f);
			// 버팀벽(동쪽 면) · 높은 아치 창
			for (float y = f.y0 + 1.2f; y < f.y1 - 0.8f; y += 1.8f)
			{
				gfx::Box(f.x1, y - 0.14f, f.x1 + 0.3f, y + 0.14f, 0.f, h * 0.8f, Rgb(0xF4F0E8), Rgb(0xE0DCD4), Rgb(0xC8C4BC));
				WindowFace(true, y + 0.35f, y + 0.85f, f.x1, 0.9f, 2.0f, T, false, false, f.h + static_cast<uint32_t>(y * 10.f));
			}
			// 남쪽 현관 기둥 + 박공 머리
			for (float x = f.x0 + 0.6f; x < f.x1 - 0.3f; x += 1.2f)
				gfx::Box(x - 0.15f, f.y1 + 0.05f, x + 0.15f, f.y1 + 0.38f, 0.26f, h, Rgb(0xFFFFFF), Rgb(0xE8E4DC), Rgb(0xC8C4BC));
			gfx::Box(f.x0 - 0.1f, f.y1 - 0.1f, f.x1 + 0.1f, f.y1 + 0.46f, h, h + 0.2f, Rgb(0xFFFFFF), Rgb(0xE6E0D6), Rgb(0xC8C2B8));
			FrontDressing(f, T, 1.6f, Icon::None, 0.f, true, 1.1f);
			// 장미창(남쪽 박공)
			Gable(f.x0, f.y0, f.x1, f.y1, h + 0.2f, roofH, 0.3f, false, T, Rgb(0xF0ECE4));
			{
				const P3 c{ (f.x0 + f.x1) * 0.5f, f.y1 + 0.02f, h + 0.2f + roofH * 0.42f };
				gfx::SetDepth(c.x + c.y);
				const Vector2 s = gfx::IsoPx(c.x, c.y, c.z);
				gfx::Circle(s, 11.f, T.trim);
				gfx::Circle(s, 9.f, Mix(Rgb(0x3A5A8A), Rgb(0xC0DCFF), Night() * 0.9f));
				for (int i = 0; i < 8; ++i)
				{
					const float a = i / 8.f * 2.f * kPi;
					gfx::Line(s, { s.x + std::cos(a) * 9.f, s.y + std::sin(a) * 9.f }, 1.2f, T.trim);
				}
				if (Night() > 0.05f) Glow(c, 18.f, Rgb(0xC0DCFF), 0.6f * Night());
			}
			// 종탑 + 첨탑(랜드마크 — 높이 6.4)
			const float cx = f.x0 + 1.2f, cy = (f.y0 + f.y1) * 0.5f, top = h + roofH;
			const Texture2D* wt = WallTex(Wall::White);
			if (wt) gfx::BoxTex(*wt, cx - 0.62f, cy - 0.62f, cx + 0.62f, cy + 0.62f, h, top + 1.3f, 1.2f, Rgb(0xFFFFFF), Rgb(0xF4F0E8), Rgb(0xC8C4BC));
			else gfx::Box(cx - 0.62f, cy - 0.62f, cx + 0.62f, cy + 0.62f, h, top + 1.3f, Rgb(0xFFFFFF), Rgb(0xE8E4DC), Rgb(0xC8C4BC));
			// 종이 보이는 열린 칸
			gfx::Face({ cx - 0.35f, cy + 0.63f, top + 0.35f }, { cx + 0.35f, cy + 0.63f, top + 0.35f }, { cx + 0.35f, cy + 0.63f, top + 1.05f }, { cx - 0.35f, cy + 0.63f, top + 1.05f }, Rgb(0x2E3440));
			gfx::SetDepth(cx + cy + 0.62f);
			gfx::Circle(gfx::IsoPx(cx, cy + 0.62f, top + 0.62f), 6.f, Rgb(0xC8A040));
			Cone(cx, cy, 0.66f, 4, top + 1.3f, h + 6.4f - 0.35f, T, 0.1f);
			const Vector2 sym = gfx::IsoPx(cx, cy, h + 6.4f - 0.35f);
			gfx::SetDepth(cx + cy + 1.f);
			gfx::Line(sym, { sym.x, sym.y - 14.f }, 2.5f, Rgb(0xF2D060));
			gfx::Line({ sym.x - 5.f, sym.y - 9.f }, { sym.x + 5.f, sym.y - 9.f }, 2.5f, Rgb(0xF2D060));
			PushLight({ gfx::IsoPx((f.x0 + f.x1) * 0.5f, f.y1 + 0.8f), 180.f, Rgb(0xFFF0C0), 0.7f });
		}

		void StarTower(const Frame& f, Look L)
		{
			// 샛별 탑: 팔각 돌탑(위로 좁아짐) → 발코니 고리 → 유리 등불 방 → 뾰족 지붕 + 별. 밤에 꼭대기 등불이 멀리 빛난다.
			const float cx = (f.x0 + f.x1) * 0.5f, cy = (f.y0 + f.y1) * 0.5f;
			const Texture2D* stone = WallTex(Wall::Dressed);
			const Color sm = Rgb(0xE8E4DA);
			gfx::Box(f.x0 + 0.1f, f.y0 + 0.1f, f.x1 - 0.1f, f.y1 - 0.1f, 0.f, 0.3f, Rgb(0xB8B4AA), Rgb(0x9A968C), Rgb(0x7A766E));
			Prism(cx, cy, 1.3f, 1.12f, 8, 0.3f, 2.4f, stone, sm, 1.1f);
			Prism(cx, cy, 1.12f, 0.9f, 8, 2.4f, 5.0f, stone, sm, 1.1f);
			// 층을 가르는 띠 + 좁은 창 · 문
			Prism(cx, cy, 1.2f, 1.2f, 8, 2.35f, 2.5f, nullptr, Rgb(0xC8C0B0));
			DoorFace(false, cx, cy + 1.2f, 0.7f, 1.2f, L, true);
			WindowFace(false, cx - 0.12f, cx + 0.12f, cy + 0.98f, 3.3f, 4.1f, L, false, false, f.h);
			WindowFace(true, cy - 0.12f, cy + 0.12f, cx + 1.02f, 1.3f, 2.0f, L, false, false, f.h >> 2);
			// 발코니 고리
			Prism(cx, cy, 1.3f, 1.3f, 8, 5.0f, 5.15f, nullptr, Rgb(0xB8B0A0));
			OctLid(cx, cy, 1.3f, 8, 5.15f, Rgb(0xC8C0B0));
			for (int i = 0; i < 16; ++i)
			{
				const float a = (i + 0.5f) / 16.f * 2.f * kPi;
				const float px = cx + std::cos(a) * 1.24f, py = cy + std::sin(a) * 1.24f;
				if (std::cos(a) + std::sin(a) < -0.9f) continue;
				gfx::Box(px - 0.03f, py - 0.03f, px + 0.03f, py + 0.03f, 5.15f, 5.55f, Rgb(0x5A4A3A), Rgb(0x4A3A2A), Rgb(0x3A2E22));
			}
			// 등불 방: 기둥 여덟 + 빛나는 속
			const float n = Night();
			const float flick = 0.85f + 0.15f * std::sin(f.time * 5.3f) * std::sin(f.time * 2.1f + 0.7f);
			const Color core = Mix(Rgb(0xF0D890), Rgb(0xFFE8A0), n);
			Prism(cx, cy, 0.66f, 0.66f, 8, 5.15f, 6.05f, nullptr, Mix(Rgb(0x8A9AA8), core, 0.35f + 0.6f * n));
			for (int i = 0; i < 8; ++i)
			{
				const float a = (i + 0.5f) / 8.f * 2.f * kPi;
				const float px = cx + std::cos(a) * 0.7f, py = cy + std::sin(a) * 0.7f;
				if (std::cos(a) + std::sin(a) < -0.9f) continue;
				gfx::Box(px - 0.05f, py - 0.05f, px + 0.05f, py + 0.05f, 5.15f, 6.05f, Rgb(0x3A3430), Rgb(0x2E2A26), Rgb(0x221E1C));
			}
			Look R = L; R.roof = Roof::DarkSlate; R.roofMul = Rgb(0x8A92A8);
			Cone(cx, cy, 0.82f, 8, 6.05f, 7.0f, R, 0.1f);
			const Vector2 star = gfx::IsoPx(cx, cy, 7.15f);
			gfx::SetDepth(cx + cy + 1.2f);
			gfx::Star(star, 3.f, 7.f, 4, f.time * 0.3f, Rgb(0xFFE8A0));
			Glow({ cx, cy, 5.6f }, 24.f + 28.f * n, Rgb(0xFFE0A0), (0.35f + 0.65f * n) * flick);
			Glow({ cx, cy, 7.15f }, 10.f, Rgb(0xFFF0C0), 0.4f + 0.5f * n);
			if (n > 0.02f) PushLight({ gfx::IsoPx(cx, cy, 0.f), 360.f, Rgb(0xFFD890), 0.9f * n * flick });
		}

		void AuctionHall(const Frame& f, Look L)
		{
			// 교회가 남긴 옛 회관을 고친 경매장: 다듬은 돌 · 높은 아치 창 · 모임지붕(주황 기와) · 빈 종루 · 현관 기둥 · 저울 간판
			Look A = L; A.wall = Wall::Dressed; A.wallMul = Rgb(0xF0E8DC); A.roof = Roof::Orange; A.roofMul = Rgb(0xFFD2A0); A.trim = Rgb(0x5A4030);
			const float h = 2.5f;
			Plinth(f.x0, f.y0, f.x1, f.y1, 0.3f);
			Walls(f.x0, f.y0, f.x1, f.y1, 0.3f, h, A, 1.4f);
			// 아치 창(남 · 동)
			for (float x = f.x0 + 0.9f; x < f.x1 - 0.6f; x += 1.5f)
			{
				if (f.door == 'S' && std::fabs(x - f.doorU) < 1.1f) continue;
				WindowFace(false, x - 0.28f, x + 0.28f, f.y1, 0.9f, 1.9f, A, false, false, f.h + static_cast<uint32_t>(x * 7.f));
				const P3 c{ x, f.y1 + 0.03f, 1.9f };
				gfx::SetDepth(c.x + c.y);
				gfx::EllipseArc(gfx::IsoPx(c.x, c.y, c.z), 10.f, 9.f, kPi, 2.f * kPi, 2.f, A.trim);
			}
			for (float y = f.y0 + 0.9f; y < f.y1 - 0.6f; y += 1.5f)
				WindowFace(true, y - 0.28f, y + 0.28f, f.x1, 0.9f, 1.9f, A, false, false, f.h + static_cast<uint32_t>(y * 13.f));
			// 처마 아래 돌림띠
			Band(f.x0, f.y0, f.x1, f.y1, h - 0.18f, Rgb(0xD8D0C0), 0.14f);
			// 현관: 기둥 둘 + 작은 박공
			if (f.door == 'S')
			{
				const float c = f.doorU;
				for (float x : { c - 0.9f, c + 0.9f })
					gfx::Box(x - 0.14f, f.y1 + 0.55f, x + 0.14f, f.y1 + 0.83f, 0.3f, 2.1f, Rgb(0xFFFFFF), Rgb(0xE8E0D2), Rgb(0xC8C0B2));
				Gable(c - 1.15f, f.y1, c + 1.15f, f.y1 + 0.9f, 2.1f, 0.6f, 0.1f, false, A, Rgb(0xF4ECE0));
			}
			FrontDressing(f, A, 1.6f, Icon::Scales, 1.75f, true, 1.2f);
			// 걸어 둔 물건 깃발 두 장
			WallBanner(f.x0 + 0.9f, f.y1, h - 0.2f, 0.5f, 1.0f, Rgb(0x8A3A30), Rgb(0xE8C860), 1.f);
			WallBanner(f.x1 - 0.9f, f.y1, h - 0.2f, 0.5f, 1.0f, Rgb(0x2E5A6A), Rgb(0xE8C860), 2.f);
			Hip(f.x0, f.y0, f.x1, f.y1, h, 1.5f, 0.3f, A, 0.7f);
			// 빈 종루(용마루 한가운데 — 랜드마크 높이 5.0)
			const float bx = (f.x0 + f.x1) * 0.5f, by = (f.y0 + f.y1) * 0.5f, bz = h + 1.2f;
			for (float ox : { -0.35f, 0.35f })
				for (float oy : { -0.35f, 0.35f })
					gfx::Box(bx + ox - 0.06f, by + oy - 0.06f, bx + ox + 0.06f, by + oy + 0.06f, bz, bz + 0.8f, Rgb(0xE8E0D2), Rgb(0xD8D0C2), Rgb(0xB8B0A2));
			gfx::Box(bx - 0.45f, by - 0.45f, bx + 0.45f, by + 0.45f, bz - 0.2f, bz, Rgb(0xE0D8CA), Rgb(0xD0C8BA), Rgb(0xB0A89A));
			Cone(bx, by, 0.45f, 4, bz + 0.8f, h + 5.0f - 0.2f, A, 0.12f);
		}

		void GuildHall(const Frame& f, Look L)
		{
			// 결사 회관(서약 기사단의 옛 막사): 다듬은 돌 · 청회 슬레이트 박공 · 앞 모서리 두 탑(성가퀴) · 벽 깃발 · 깃대 둘 · 큰 아치 문
			Look G = L; G.wall = Wall::Dressed; G.wallMul = Rgb(0xE6E6E6); G.roof = Roof::BlueSlate; G.roofMul = Rgb(0xC8D4F0); G.trim = Rgb(0x3A3A44);
			const float h = 2.6f;
			Plinth(f.x0, f.y0, f.x1, f.y1, 0.3f);
			Walls(f.x0 + 0.9f, f.y0, f.x1 - 0.9f, f.y1, 0.3f, h, G, 1.4f);
			Windows(f, G, 1.0f, 1.9f, 1.8f, false, false, 2.6f);
			FrontDressing(f, G, 1.9f, Icon::None, 0.f, true, 1.6f);
			Gable(f.x0 + 0.9f, f.y0, f.x1 - 0.9f, f.y1, h, 1.4f, 0.26f, true, G, Rgb(0xC8C8CC));
			// 벽 깃발(결사 색 넷)
			constexpr uint32_t kCloth[] = { 0x7A2E2A, 0x2E4A7A, 0x2E6A4A, 0x6A5A2A };
			int k = 0;
			for (float x = f.x0 + 2.2f; x < f.x1 - 2.0f; x += 2.4f, ++k)
			{
				if (std::fabs(x - f.doorU) < 1.4f) continue;
				WallBanner(x, f.y1, h - 0.15f, 0.62f, 1.45f, Rgb(kCloth[k % 4]), Rgb(0xE8C860), k * 1.3f);
			}
			// 모서리 탑 둘(앞) — 성가퀴
			const Texture2D* st = WallTex(Wall::Dressed);
			for (float tx : { f.x0 + 0.95f, f.x1 - 0.95f })
			{
				if (st) gfx::BoxTex(*st, tx - 0.95f, f.y1 - 1.9f, tx + 0.95f, f.y1 + 0.1f, 0.f, h + 0.9f, 1.2f, Rgb(0xE8E8E8), Rgb(0xE0E0E0), Rgb(0xAAAAAE));
				else gfx::Box(tx - 0.95f, f.y1 - 1.9f, tx + 0.95f, f.y1 + 0.1f, 0.f, h + 0.9f, Rgb(0xC8C8C8), Rgb(0xB8B8B8), Rgb(0x98989C));
				Lid(tx - 0.95f, f.y1 - 1.9f, tx + 0.95f, f.y1 + 0.1f, h + 0.9f, Rgb(0xB8B4AC));
				Crenel(tx - 0.95f, f.y1 - 1.9f, tx + 0.95f, f.y1 + 0.1f, h + 0.9f, G);
				WindowFace(false, tx - 0.12f, tx + 0.12f, f.y1 + 0.1f, 2.0f, 2.8f, G, false, false, f.h);
			}
			// 뒤 모서리 탑(북 · 윤곽만)
			for (float tx : { f.x0 + 0.95f, f.x1 - 0.95f })
			{
				if (st) gfx::BoxTex(*st, tx - 0.8f, f.y0, tx + 0.8f, f.y0 + 1.6f, 0.f, h + 0.5f, 1.2f, Rgb(0xE0E0E0), Rgb(0xD8D8D8), Rgb(0xA8A8AC));
				Lid(tx - 0.8f, f.y0, tx + 0.8f, f.y0 + 1.6f, h + 0.5f, Rgb(0xB0ACA4));
				Crenel(tx - 0.8f, f.y0, tx + 0.8f, f.y0 + 1.6f, h + 0.5f, G, 0.28f);
			}
			// 동쪽 면 창(탑 사이)
			// 깃대 둘은 앞뜰의 소품(props.txt flagpole — 막히는 칸)
			Lantern({ f.doorU - 1.0f, f.y1 + 0.1f, 1.9f }, L.glow);
			Lantern({ f.doorU + 1.0f, f.y1 + 0.1f, 1.9f }, L.glow);
		}

		void Warehouse(const Frame& f, Look L)
		{
			Look Wh = L; Wh.wall = Wall::Planks; Wh.wallMul = Jitter(Rgb(0xE0C4A0), f.h, 0.06f);
			const float hh = 2.0f;
			Plinth(f.x0, f.y0, f.x1, f.y1);
			Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, hh, Wh, 1.6f);
			// 큰 두 짝 문(문 면 — 보이면)
			if (f.door == 'S' || f.door == 'E')
			{
				const bool e = f.door == 'E';
				DoorFace(e, f.doorU - 0.45f, e ? f.x1 : f.y1, 0.85f, 1.5f, Wh, false);
				DoorFace(e, f.doorU + 0.45f, e ? f.x1 : f.y1, 0.85f, 1.5f, Wh, false);
			}
			WindowRow(false, f.x0 + 0.4f, f.x1 - 0.4f, f.y1, 1.35f, 1.7f, Wh, 1.8f, f.door == 'S' ? f.doorU - 1.f : 1e9f, f.door == 'S' ? f.doorU + 1.f : -1e9f, false, false, f.h);
			const bool alongX = (f.x1 - f.x0) >= (f.y1 - f.y0);
			Gable(f.x0, f.y0, f.x1, f.y1, hh, 0.9f, 0.3f, alongX, L, Shade(Rgb(0x8A6A48), 0.74f));
			// 박공 끝 도르래 들보 + 밧줄 + 상자
			const P3 beam = alongX ? P3{ f.x1 + 0.1f, (f.y0 + f.y1) * 0.5f, hh + 0.55f } : P3{ (f.x0 + f.x1) * 0.5f, f.y1 + 0.1f, hh + 0.55f };
			if (alongX) gfx::Box(beam.x - 0.1f, beam.y - 0.07f, beam.x + 0.7f, beam.y + 0.07f, beam.z, beam.z + 0.12f, Rgb(0x6A4A2A), Rgb(0x5A3E22), Rgb(0x4A321A));
			else gfx::Box(beam.x - 0.07f, beam.y - 0.1f, beam.x + 0.07f, beam.y + 0.7f, beam.z, beam.z + 0.12f, Rgb(0x6A4A2A), Rgb(0x5A3E22), Rgb(0x4A321A));
			const P3 hook = alongX ? P3{ beam.x + 0.6f, beam.y, beam.z } : P3{ beam.x, beam.y + 0.6f, beam.z };
			gfx::SetDepth(hook.x + hook.y);
			gfx::Line(gfx::IsoPx(hook.x, hook.y, hook.z), gfx::IsoPx(hook.x, hook.y, 0.7f), 1.2f, Rgb(0xC8B890));
			Crate(hook.x, hook.y, 0.2f, 0.22f, f.h);
			Crate(alongX ? f.x1 + 0.35f : f.x0 + 0.5f, alongX ? f.y1 - 0.5f : f.y1 + 0.35f, 0.f, 0.25f, f.h >> 5);
			Crate(alongX ? f.x1 + 0.35f : f.x0 + 1.1f, alongX ? f.y1 - 1.1f : f.y1 + 0.35f, 0.f, 0.2f, f.h >> 9);
		}

		void Stable(const Frame& f, Look L)
		{
			// 마구간: 앞면이 칸막이 열린 칸(반문) · 외쪽 너와 지붕 · 건초 · 여물통
			Look S = L; S.wall = Wall::Planks; S.wallMul = Rgb(0xE0C4A0);
			const float hh = 1.8f;
			Plinth(f.x0, f.y0, f.x1, f.y1);
			Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, hh, S, 1.6f);
			const bool e = f.door == 'E';
			const float a = e ? f.y0 : f.x0, c = e ? f.y1 : f.x1, at = e ? f.x1 : f.y1;
			const int bays = std::max(2, static_cast<int>((c - a) / 1.4f));
			for (int i = 0; i < bays; ++i)
			{
				const float u0 = util::Lerp(a + 0.2f, c - 0.2f, static_cast<float>(i) / bays) + 0.1f;
				const float u1 = util::Lerp(a + 0.2f, c - 0.2f, static_cast<float>(i + 1) / bays) - 0.1f;
				auto F = [&](float p0, float p1, float z0, float z1, float d, Color col)
				{
					if (e) gfx::Face({ at + d, p0, z0 }, { at + d, p1, z0 }, { at + d, p1, z1 }, { at + d, p0, z1 }, Shade(col, 0.8f));
					else gfx::Face({ p0, at + d, z0 }, { p1, at + d, z0 }, { p1, at + d, z1 }, { p0, at + d, z1 }, col);
				};
				F(u0, u1, 0.2f, 1.45f, 0.01f, Rgb(0x2A221C));                 // 어두운 칸 안
				F(u0, u1, 0.2f, 0.75f, 0.03f, Rgb(0x8A6038));                 // 반문
				F(u0, u1, 0.72f, 0.8f, 0.04f, Rgb(0x5A3E22));
				// 말머리(가끔)
				if (((f.h >> i) & 3) != 0)
				{
					const float u = (u0 + u1) * 0.5f;
					const P3 p = e ? P3{ at + 0.12f, u, 1.0f } : P3{ u, at + 0.12f, 1.0f };
					gfx::SetDepth(p.x + p.y);
					const Vector2 s = gfx::IsoPx(p.x, p.y, p.z);
					const Color hc = ((f.h >> (i * 2)) & 1) ? Rgb(0x7A4A2A) : Rgb(0x3A2A22);
					gfx::Ellipse({ s.x + 3.f, s.y + 1.f }, 5.f, 8.f, hc);
					gfx::Ellipse({ s.x + 6.f, s.y + 7.f }, 3.5f, 3.f, Shade(hc, 0.8f));
					gfx::Tri({ s.x - 1.f, s.y - 7.f }, { s.x + 2.f, s.y - 7.f }, { s.x, s.y - 11.f }, hc);
				}
			}
			const bool alongX = (f.x1 - f.x0) >= (f.y1 - f.y0);
			Gable(f.x0, f.y0, f.x1, f.y1, hh, 1.1f, 0.35f, alongX, L, Shade(Rgb(0x8A6A48), 0.74f));
			Hay(e ? f.x1 + 0.45f : f.x0 + 0.5f, e ? f.y0 + 0.4f : f.y1 + 0.4f);
			Hay(e ? f.x1 + 0.45f : f.x0 + 1.25f, e ? f.y0 + 1.1f : f.y1 + 0.4f);
			const float tx = e ? f.x1 + 0.35f : f.x1 - 1.3f, ty = e ? f.y1 - 1.0f : f.y1 + 0.35f;
			gfx::Box(tx - (e ? 0.18f : 0.55f), ty - (e ? 0.55f : 0.18f), tx + (e ? 0.18f : 0.55f), ty + (e ? 0.55f : 0.18f), 0.f, 0.32f, Rgb(0x5A7A9A), Rgb(0x7A5A3A), Rgb(0x5A4028));
			Sign(e ? 'E' : 'S', e ? f.y1 - 0.35f : f.x1 - 0.35f, e ? f.x1 : f.y1, 1.65f, Icon::Horseshoe, L);
		}

		void WaterMill(const Frame& f, Look L)
		{
			// 물레방아간: 돌 아래층 + 판자 위층 · 박공 · 동쪽(개울 쪽) 큰 물레바퀴가 돈다
			Look M = L; M.wall = Wall::Planks; M.wallMul = Rgb(0xE4C8A4);
			const float h1 = 1.1f, h2 = 2.4f;
			Plinth(f.x0, f.y0, f.x1, f.y1, 0.25f);
			Walls(f.x0, f.y0, f.x1, f.y1, 0.25f, h1, Look{ Wall::Stone, M.roof, Rgb(0xD8D2C8), M.roofMul, M.trim, M.glow }, 1.2f);
			Walls(f.x0, f.y0, f.x1, f.y1, h1, h2, M, 1.6f);
			Windows(f, M, 1.45f, 1.9f, 1.5f, true, false);
			FrontDressing(f, M, 1.1f, Icon::Sack, 1.4f, false, 0.9f);
			Gable(f.x0, f.y0, f.x1, f.y1, h2, 1.2f, 0.3f, false, L, Shade(Rgb(0x9A7A58), 0.74f));
			// 물레바퀴: 동쪽 면 바깥(개울 위), 바퀴 면은 YZ 평면
			const float wx = f.x1 + 0.5f, wy = f.y0 + 1.55f, wz = 1.3f, R = 1.4f;
			const float rot = f.time * 0.9f;
			const Color wood = Rgb(0x9A7048), dark = Rgb(0x6A4A2A);
			gfx::Box(f.x1, wy - 0.08f, wx + 0.1f, wy + 0.08f, wz - 0.08f, wz + 0.08f, dark, dark, Shade(dark, 0.8f));   // 굴대
			const int spokes = 8;
			for (int i = 0; i < spokes; ++i)
			{
				const float a = rot + i * 2.f * kPi / spokes;
				const P3 o{ wx, wy, wz }, p{ wx, wy + std::cos(a) * R, wz + std::sin(a) * R };
				gfx::Face({ o.x, o.y - 0.03f, o.z }, { o.x, o.y + 0.03f, o.z }, { p.x, p.y + 0.03f, p.z }, { p.x, p.y - 0.03f, p.z }, Shade(wood, 0.9f));
				// 물받이 판
				const float a2 = a + 0.08f;
				const P3 q0{ wx - 0.24f, wy + std::cos(a) * R, wz + std::sin(a) * R }, q1{ wx + 0.24f, wy + std::cos(a) * R, wz + std::sin(a) * R };
				const P3 q2{ wx + 0.24f, wy + std::cos(a2) * (R - 0.38f), wz + std::sin(a2) * (R - 0.38f) }, q3{ wx - 0.24f, wy + std::cos(a2) * (R - 0.38f), wz + std::sin(a2) * (R - 0.38f) };
				gfx::Face(q0, q1, q2, q3, Shade(wood, 0.8f + 0.2f * std::sin(a)));
			}
			// 바퀴 테(두 겹)
			for (float ox : { -0.24f, 0.24f })
				for (int i = 0; i < 20; ++i)
				{
					const float a0 = i * 2.f * kPi / 20.f, a1 = (i + 1) * 2.f * kPi / 20.f;
					const P3 p0{ wx + ox, wy + std::cos(a0) * R, wz + std::sin(a0) * R }, p1{ wx + ox, wy + std::cos(a1) * R, wz + std::sin(a1) * R };
					gfx::Face(p0, p1, { p1.x, p1.y, p1.z + 0.12f }, { p0.x, p0.y, p0.z + 0.12f }, ox > 0.f ? wood : dark);
				}
			// 물보라(바퀴 아래)
			for (int i = 0; i < 4; ++i)
			{
				const float age = std::fmod(f.time * 1.3f + i * 0.25f, 1.f);
				Glow({ wx + 0.1f, wy - 0.4f + i * 0.28f, 0.1f + age * 0.3f }, 3.f + age * 3.f, Rgb(0xDCF0FF), 0.35f * (1.f - age));
			}
		}

		void WindMill(const Frame& f, Look L)
		{
			// 풍차: 위로 좁아지는 팔각 몸통(판자) + 초가 모자 + 남동쪽을 보는 날개 넷(바람 세기로 돈다)
			const float cx = (f.x0 + f.x1) * 0.5f, cy = (f.y0 + f.y1) * 0.5f;
			const Texture2D* wood = WallTex(Wall::Planks);
			const Texture2D* stone = WallTex(Wall::Stone);
			Prism(cx, cy, 1.45f, 1.3f, 8, 0.f, 0.7f, stone, Rgb(0xD8D2C8), 1.0f);
			Prism(cx, cy, 1.3f, 0.92f, 8, 0.7f, 3.9f, wood, Rgb(0xE8D0B0), 1.4f);
			DoorFace(false, cx, cy + 1.25f, 0.65f, 1.15f, L, true);
			WindowFace(false, cx - 0.12f, cx + 0.12f, cy + 1.05f, 2.2f, 2.7f, L, false, false, f.h);
			WindowFace(false, cx - 0.1f, cx + 0.1f, cy + 0.98f, 3.1f, 3.5f, L, false, false, f.h >> 3);
			Look T = L; T.roof = Roof::Thatch;
			Cone(cx, cy, 0.98f, 8, 3.9f, 4.9f, T, 0.15f);
			// 날개: 남동(+X+Y) 쪽 허브, 면은 (1,-1)/√2 · z 평면
			const float s2 = 0.70710678f;
			const P3 hub{ cx + 0.98f * s2, cy + 0.98f * s2, 3.95f };
			const float rot = f.time * (0.6f + 2.4f * wind::Strength());
			const Color frame = Rgb(0x5A4028), sail = Mix(Rgb(0xF0E8D8), Rgb(0x9A9488), Night() * 0.5f);
			for (int i = 0; i < 4; ++i)
			{
				const float a = rot + i * kPi * 0.5f;
				const float dx = std::cos(a), dz = std::sin(a);           // 날개 방향(면 안에서)
				const float px = -std::sin(a), pz = std::cos(a);          // 날개 폭 방향
				auto W = [&](float r, float w) { const float u = dx * r + px * w, z = dz * r + pz * w; return P3{ hub.x + u * s2, hub.y - u * s2, hub.z + z }; };
				gfx::Face(W(0.15f, -0.05f), W(2.75f, -0.05f), W(2.75f, 0.05f), W(0.15f, 0.05f), frame);
				gfx::Face(W(0.6f, 0.07f), W(2.7f, 0.07f), W(2.7f, 0.6f), W(0.6f, 0.46f), sail);
				for (float r : { 1.0f, 1.5f, 2.0f, 2.45f })
					gfx::Face(W(r - 0.025f, 0.05f), W(r + 0.025f, 0.05f), W(r + 0.025f, 0.58f), W(r - 0.025f, 0.5f), frame);
			}
			gfx::SetDepth(hub.x + hub.y + 0.1f);
			gfx::Circle(gfx::IsoPx(hub.x, hub.y, hub.z), 6.f, Rgb(0x3A2E22));
		}

		void GuardPost(const Frame& f, Look L)
		{
			// 경비 초소: 돌 · 평지붕 성가퀴 · 깃대 · 횃불
			Look G = L; G.wall = Wall::Stone; G.wallMul = Rgb(0xD8D4CC);
			const float hh = 2.3f;
			Plinth(f.x0, f.y0, f.x1, f.y1, 0.25f);
			Walls(f.x0, f.y0, f.x1, f.y1, 0.25f, hh, G, 1.2f);
			Lid(f.x0, f.y0, f.x1, f.y1, hh, Rgb(0x8A8680));
			Crenel(f.x0, f.y0, f.x1, f.y1, hh, G, 0.34f);
			Windows(f, G, 1.3f, 1.75f, 1.9f, false, false);
			FrontDressing(f, G, 1.3f, Icon::None, 0.f, true, 0.9f);
			Flag({ f.x0 + 0.5f, f.y0 + 0.5f, hh }, 1.8f, Rgb(0x2E4A7A), Rgb(0xE8C860), 0.9f, 0.9f, 0.5f);
			// 문 옆 횃불(밤)
			const P3 torch = f.door == 'E' ? P3{ f.x1 + 0.08f, f.doorU + 0.75f, 1.5f } : P3{ f.doorU + 0.75f, f.y1 + 0.08f, 1.5f };
			gfx::Box(torch.x - 0.04f, torch.y - 0.04f, torch.x + 0.04f, torch.y + 0.04f, torch.z - 0.3f, torch.z, Rgb(0x4A3A2A), Rgb(0x3A2E22), Rgb(0x2A221A));
			const float fl = 0.8f + 0.2f * std::sin(f.time * 11.f) * std::sin(f.time * 7.3f);
			Glow({ torch.x, torch.y, torch.z + 0.12f }, 7.f + 3.f * fl, Rgb(0xFFA040), 0.6f + 0.4f * Night());
			if (Night() > 0.05f) PushLight({ gfx::IsoPx(torch.x, torch.y, 0.f), 150.f, Rgb(0xFF9A40), 0.8f * Night() * fl });
		}

		void Scriptorium(const Frame& f, Look L)
		{
			// 필경소: 돌 아래층 + 회벽 위층 · 가파른 청석판 지붕 · 퇴창(내민 창) · 깃펜 간판
			Look S = L; S.roof = Roof::Slate; S.roofMul = Rgb(0xB8C0D0);
			const float h1 = 1.45f, h2 = 2.8f;
			Plinth(f.x0, f.y0, f.x1, f.y1);
			Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, h1, Look{ Wall::Stone, S.roof, Rgb(0xDCD6CC), S.roofMul, S.trim, S.glow }, 1.3f);
			Walls(f.x0, f.y0, f.x1, f.y1, h1, h2, S);
			Band(f.x0, f.y0, f.x1, f.y1, h1 - 0.04f, S.trim, 0.1f);
			Windows(f, S, 0.65f, 1.15f, 1.6f, false, false, 1.4f);
			// 퇴창(남쪽 위층 가운데)
			const float cx = (f.x0 + f.x1) * 0.5f;
			gfx::Box(cx - 0.6f, f.y1, cx + 0.6f, f.y1 + 0.35f, h1 + 0.25f, h1 + 1.1f, Shade(Rgb(0x8A6A48), 1.1f), Rgb(0x7A5A3A), Rgb(0x5A4028));
			WindowFace(false, cx - 0.45f, cx + 0.45f, f.y1 + 0.35f, h1 + 0.4f, h1 + 0.95f, S, false, false, f.h);
			gfx::Face({ cx - 0.65f, f.y1 - 0.02f, h1 + 1.1f }, { cx + 0.65f, f.y1 - 0.02f, h1 + 1.1f }, { cx + 0.65f, f.y1 + 0.42f, h1 + 1.0f }, { cx - 0.65f, f.y1 + 0.42f, h1 + 1.0f }, Rgb(0x4A5260));
			WindowRow(true, f.y0 + 0.3f, f.y1 - 0.3f, f.x1, h1 + 0.4f, h1 + 0.95f, S, 1.3f, 1e9f, -1e9f, true, false, f.h);
			FrontDressing(f, S, 1.15f, Icon::Quill, 1.3f, true, 0.9f);
			Gable(f.x0, f.y0, f.x1, f.y1, h2, 1.8f, 0.26f, (f.x1 - f.x0) >= (f.y1 - f.y0), S, Shade(Mix(WallFallback(S.wall), S.wallMul, 0.5f), 0.74f));
			Chimney(f.x0 + 0.7f, f.y0 + 0.7f, 3.4f, 4.9f, f.time, 0.55f, true);
		}

		void Greenhouse(const Frame& f, Look L)
		{
			// 약초 온실: 낮은 돌 기단 + 흰 틀의 유리 벽 · 유리 박공지붕, 안에 초록 약초
			const float base = 0.5f, hh = 1.7f, rise = 0.9f;
			Plinth(f.x0, f.y0, f.x1, f.y1, base);
			// 안의 약초(유리보다 먼저)
			for (float x = f.x0 + 0.5f; x < f.x1 - 0.3f; x += 0.55f)
				for (float y = f.y0 + 0.5f; y < f.y1 - 0.3f; y += 0.7f)
				{
					gfx::SetDepth(x + y);
					const uint32_t hh2 = util::Hash2(static_cast<int>(x * 10.f), static_cast<int>(y * 10.f), 77);
					const Color g = (hh2 & 3) == 0 ? Rgb(0x8AC860) : ((hh2 & 3) == 1 ? Rgb(0x4A9A48) : Rgb(0x6AB070));
					gfx::Circle(gfx::IsoPx(x, y, base + 0.35f), 7.f, Shade(g, 0.8f));
					gfx::Circle(gfx::IsoPx(x - 0.05f, y - 0.05f, base + 0.45f), 5.f, g);
					if ((hh2 & 12) == 0) gfx::Circle(gfx::IsoPx(x, y, base + 0.6f), 2.f, Rgb(0xE8A0C0));
				}
			// 유리(반투명) + 흰 틀
			const Color glass = Mix(Rgb(0xB8E0F0, 90), Rgb(0xFFE8B0, 120), Night() * 0.6f);
			const Color frame = Rgb(0xF0F0EC);
			gfx::Face({ f.x0, f.y1, base }, { f.x1, f.y1, base }, { f.x1, f.y1, hh }, { f.x0, f.y1, hh }, glass);
			gfx::Face({ f.x1, f.y1, base }, { f.x1, f.y0, base }, { f.x1, f.y0, hh }, { f.x1, f.y1, hh }, Shade(glass, 0.85f));
			for (float x = f.x0; x <= f.x1 + 0.01f; x += 0.7f)
				gfx::Face({ x - 0.03f, f.y1 + 0.01f, base }, { x + 0.03f, f.y1 + 0.01f, base }, { x + 0.03f, f.y1 + 0.01f, hh }, { x - 0.03f, f.y1 + 0.01f, hh }, frame);
			for (float y = f.y0; y <= f.y1 + 0.01f; y += 0.7f)
				gfx::Face({ f.x1 + 0.01f, y - 0.03f, base }, { f.x1 + 0.01f, y + 0.03f, base }, { f.x1 + 0.01f, y + 0.03f, hh }, { f.x1 + 0.01f, y - 0.03f, hh }, Shade(frame, 0.85f));
			Band(f.x0, f.y0, f.x1, f.y1, (base + hh) * 0.5f, frame, 0.05f);
			// 유리 지붕(용마루 X 방향)
			const float yc = (f.y0 + f.y1) * 0.5f;
			gfx::Face({ f.x0, f.y1, hh }, { f.x1, f.y1, hh }, { f.x1, yc, hh + rise }, { f.x0, yc, hh + rise }, Shade(glass, 1.1f));
			gfx::Face({ f.x0, f.y0, hh }, { f.x1, f.y0, hh }, { f.x1, yc, hh + rise }, { f.x0, yc, hh + rise }, Shade(glass, 0.9f));
			gfx::FaceTri({ f.x1, f.y0, hh }, { f.x1, f.y1, hh }, { f.x1, yc, hh + rise }, Shade(glass, 0.85f));
			for (float x = f.x0; x <= f.x1 + 0.01f; x += 0.7f)
				gfx::Face({ x - 0.03f, f.y1, hh }, { x + 0.03f, f.y1, hh }, { x + 0.03f, yc, hh + rise }, { x - 0.03f, yc, hh + rise }, frame);
			gfx::Face({ f.x0, yc - 0.04f, hh + rise }, { f.x1, yc - 0.04f, hh + rise }, { f.x1, yc + 0.04f, hh + rise }, { f.x0, yc + 0.04f, hh + rise }, frame);
			// 햇빛 반사(낮)
			if (Night() < 0.5f)
			{
				const float sh = 0.5f + 0.5f * std::sin(f.time * 0.4f + f.x0);
				Glow({ f.x0 + (f.x1 - f.x0) * 0.35f, yc + 0.6f, hh + rise * 0.5f }, 14.f, Rgb(0xFFFFFF), 0.25f * sh * (1.f - Night()));
			}
			FrontDressing(f, L, 1.3f, Icon::Leaf, 1.5f, false, 0.8f);
		}

		void Workshop(const Frame& f, Look L)
		{
			// 공방(목공): 판자 · 큰 열린 문 · 목재 더미 · 톱질 모탕 · 외쪽 헛간
			Look Wk = L; Wk.wall = Wall::Planks; Wk.wallMul = Rgb(0xE8CCA8);
			const float hh = 2.0f;
			Plinth(f.x0, f.y0, f.x1, f.y1);
			Walls(f.x0, f.y0, f.x1, f.y1, 0.18f, hh, Wk, 1.6f);
			const bool e = f.door == 'E';
			const float at = e ? f.x1 : f.y1;
			// 활짝 연 큰 문(안이 어둡다) + 안쪽 작업대
			if (e || f.door == 'S')
			{
				auto F = [&](float p0, float p1, float z0, float z1, float d, Color col)
				{
					if (e) gfx::Face({ at + d, p0, z0 }, { at + d, p1, z0 }, { at + d, p1, z1 }, { at + d, p0, z1 }, Shade(col, 0.8f));
					else gfx::Face({ p0, at + d, z0 }, { p1, at + d, z0 }, { p1, at + d, z1 }, { p0, at + d, z1 }, col);
				};
				F(f.doorU - 0.8f, f.doorU + 0.8f, 0.18f, 1.6f, 0.01f, Rgb(0x2A221C));
				F(f.doorU - 1.3f, f.doorU - 0.8f, 0.18f, 1.6f, 0.05f, Rgb(0x8A6038));   // 열린 문짝
				F(f.doorU + 0.8f, f.doorU + 1.3f, 0.18f, 1.6f, 0.05f, Rgb(0x8A6038));
			}
			Windows(f, Wk, 0.9f, 1.4f, 1.8f, true, false, 2.8f);
			Gable(f.x0, f.y0, f.x1, f.y1, hh, 1.1f, 0.3f, (f.x1 - f.x0) >= (f.y1 - f.y0), L, Shade(Rgb(0x8A6A48), 0.74f));
			// 목재 더미(보이는 모퉁이)
			const float lx = e ? f.x1 + 0.4f : f.x0 + 0.6f, ly = e ? f.y0 + 0.5f : f.y1 + 0.4f;
			for (int r = 0; r < 3; ++r)
				for (int i = 0; i < 3 - r; ++i)
				{
					const float o = (i - (2 - r) * 0.5f) * 0.18f;
					const P3 a = e ? P3{ lx, ly + o, 0.1f + r * 0.16f } : P3{ lx + o, ly, 0.1f + r * 0.16f };
					if (e) gfx::Box(a.x - 0.3f, a.y - 0.08f, a.x + 0.3f, a.y + 0.08f, a.z - 0.08f, a.z + 0.08f, Rgb(0xC89A68), Rgb(0xA87848), Rgb(0xE8C090));
					else gfx::Box(a.x - 0.08f, a.y - 0.3f, a.x + 0.08f, a.y + 0.3f, a.z - 0.08f, a.z + 0.08f, Rgb(0xC89A68), Rgb(0xE8C090), Rgb(0xA87848));
				}
			// 모탕(톱질 받침)
			const float sx = e ? f.x1 + 0.45f : f.x1 - 0.8f, sy = e ? f.y1 - 0.8f : f.y1 + 0.45f;
			gfx::Box(sx - 0.3f, sy - 0.05f, sx + 0.3f, sy + 0.05f, 0.45f, 0.52f, Rgb(0xA87848), Rgb(0x8A6038), Rgb(0x6A4828));
			for (float o : { -0.25f, 0.25f })
				gfx::Box(sx + o - 0.03f, sy - 0.12f, sx + o + 0.03f, sy + 0.12f, 0.f, 0.45f, Rgb(0x8A6038), Rgb(0x7A5030), Rgb(0x5A3A22));
			FrontDressing(f, Wk, 1.5f, Icon::Hammer, 1.8f, false, 1.6f);
			Chimney(f.x0 + 0.7f, f.y0 + 0.7f, 2.3f, 3.5f, f.time, 0.95f, (f.h & 1) != 0);
		}

		void Lookout(const Frame& f, Look L)
		{
			// 북쪽 전망대: 돌 받침 → 나무 기둥 넷 · 가새 → 난간 두른 판 → 각뿔 지붕 · 화톳불
			const float cx = (f.x0 + f.x1) * 0.5f, cy = (f.y0 + f.y1) * 0.5f;
			Look S = L; S.wall = Wall::Stone; S.wallMul = Rgb(0xD0CAC0);
			Walls(f.x0 + 0.2f, f.y0 + 0.2f, f.x1 - 0.2f, f.y1 - 0.2f, 0.f, 1.3f, S, 1.2f);
			Lid(f.x0 + 0.2f, f.y0 + 0.2f, f.x1 - 0.2f, f.y1 - 0.2f, 1.3f, Rgb(0x9A948A));
			const Color wood = Rgb(0x6A4A2E);
			for (float ox : { -1.0f, 1.0f })
				for (float oy : { -1.0f, 1.0f })
					gfx::Box(cx + ox - 0.08f, cy + oy - 0.08f, cx + ox + 0.08f, cy + oy + 0.08f, 1.3f, 3.35f, Shade(wood, 1.1f), wood, Shade(wood, 0.7f));
			// 가새(남 · 동 면 X)
			for (int s = 0; s < 2; ++s)
			{
				const bool e = s == 1;
				auto P = [&](float u, float z) { return e ? P3{ cx + 1.02f, cy + u, z } : P3{ cx + u, cy + 1.02f, z }; };
				gfx::Face(P(-1.f, 1.35f), P(-0.9f, 1.35f), P(1.f, 3.2f), P(0.9f, 3.2f), Shade(wood, e ? 0.75f : 1.f));
				gfx::Face(P(1.f, 1.35f), P(0.9f, 1.35f), P(-1.f, 3.2f), P(-0.9f, 3.2f), Shade(wood, e ? 0.7f : 0.92f));
			}
			// 판 + 난간
			gfx::Box(cx - 1.25f, cy - 1.25f, cx + 1.25f, cy + 1.25f, 3.3f, 3.42f, Rgb(0xA07850), Rgb(0x8A6440), Rgb(0x6A4A2E));
			for (int i = 0; i <= 8; ++i)
			{
				const float u = -1.2f + 2.4f * i / 8.f;
				gfx::Box(cx + u - 0.025f, cy + 1.18f, cx + u + 0.025f, cy + 1.24f, 3.42f, 3.85f, wood, wood, Shade(wood, 0.7f));
				gfx::Box(cx + 1.18f, cy + u - 0.025f, cx + 1.24f, cy + u + 0.025f, 3.42f, 3.85f, wood, wood, Shade(wood, 0.7f));
			}
			gfx::Box(cx - 1.25f, cy + 1.16f, cx + 1.25f, cy + 1.26f, 3.85f, 3.92f, Shade(wood, 1.2f), wood, Shade(wood, 0.7f));
			gfx::Box(cx + 1.16f, cy - 1.25f, cx + 1.26f, cy + 1.25f, 3.85f, 3.92f, Shade(wood, 1.2f), wood, Shade(wood, 0.7f));
			for (float ox : { -1.0f, 1.0f })
				for (float oy : { -1.0f, 1.0f })
					gfx::Box(cx + ox - 0.06f, cy + oy - 0.06f, cx + ox + 0.06f, cy + oy + 0.06f, 3.42f, 4.2f, wood, wood, Shade(wood, 0.7f));
			Look R = L; R.roof = Roof::Shingle; R.roofMul = Rgb(0xB09880);
			Cone(cx, cy, 1.25f, 4, 4.2f, 4.9f, R, 0.12f);
			// 화톳불(밤에 멀리 보인다)
			const float fl = 0.8f + 0.2f * std::sin(f.time * 9.f) * std::sin(f.time * 5.1f);
			gfx::Box(cx - 0.2f, cy - 0.2f, cx + 0.2f, cy + 0.2f, 3.42f, 3.6f, Rgb(0x4A4440), Rgb(0x3A3430), Rgb(0x2A2622));
			Glow({ cx, cy, 3.75f }, 10.f + 6.f * Night() * fl, Rgb(0xFFA040), 0.45f + 0.55f * Night());
			if (Night() > 0.05f) PushLight({ gfx::IsoPx(cx, cy, 0.f), 220.f, Rgb(0xFF9A40), 0.8f * Night() * fl });
		}
	}

	namespace
	{
		// ── 소품(props.txt) ──────────────────────────────────────────────────────────
		void Grave(float x, float y, uint32_t h)
		{
			const Color st = Jitter(Rgb(0xB8B4AC), h, 0.08f);
			const float w = 0.24f + 0.06f * ((h >> 3) & 1), t = 0.07f, hh = 0.55f + 0.12f * ((h >> 5) & 1);
			gfx::Box(x - 0.3f, y - 0.14f, x + 0.3f, y + 0.26f, 0.f, 0.06f, Rgb(0x8A8A7A), Rgb(0x6A6A5E), Rgb(0x5A5A50));   // 흙 둔덕
			if ((h & 3) == 0)
			{
				// 십자형 묘비
				gfx::Box(x - 0.05f, y - t, x + 0.05f, y + t, 0.f, hh + 0.2f, Shade(st, 1.05f), st, Shade(st, 0.72f));
				gfx::Box(x - 0.2f, y - t, x + 0.2f, y + t, hh - 0.05f, hh + 0.05f, Shade(st, 1.05f), st, Shade(st, 0.72f));
			}
			else
			{
				gfx::Box(x - w, y - t, x + w, y + t, 0.f, hh, Shade(st, 1.05f), st, Shade(st, 0.72f));
				// 둥근 머리(남쪽 면에 반원)
				const P3 c{ x, y + t + 0.005f, hh };
				gfx::SetDepth(c.x + c.y);
				gfx::Ellipse(gfx::IsoPx(c.x, c.y, c.z), w * 32.f * 0.72f, w * 32.f * 0.5f, st);
			}
			if ((h >> 7) % 3 == 0)
			{
				gfx::SetDepth(x + y + 0.3f);
				gfx::Circle(gfx::IsoPx(x + 0.12f, y + 0.22f, 0.05f), 2.2f, Rgb(0xE8E0F0));
				gfx::Circle(gfx::IsoPx(x - 0.1f, y + 0.24f, 0.05f), 1.8f, Rgb(0xF0D060));
			}
		}

		void Board(float x, float y)
		{
			const Color wood = Rgb(0x7A5634);
			for (float o : { -0.34f, 0.34f })
				gfx::Box(x + o - 0.05f, y - 0.05f, x + o + 0.05f, y + 0.05f, 0.f, 1.55f, Shade(wood, 1.1f), wood, Shade(wood, 0.7f));
			gfx::Box(x - 0.42f, y - 0.04f, x + 0.42f, y + 0.04f, 0.6f, 1.45f, Rgb(0x9A7650), Rgb(0x8A6644), Rgb(0x6A4A30));
			gfx::Face({ x - 0.5f, y - 0.2f, 1.6f }, { x + 0.5f, y - 0.2f, 1.6f }, { x + 0.5f, y + 0.14f, 1.45f }, { x - 0.5f, y + 0.14f, 1.45f }, Rgb(0x6A4A2E));
			// 붙인 종이 몇 장(의뢰 · 공지)
			constexpr uint32_t kPaper[] = { 0xF4ECD8, 0xE8DCC0, 0xF8F4EA, 0xECD8B0 };
			for (int i = 0; i < 4; ++i)
			{
				const float u = x - 0.3f + i * 0.2f, z0 = 0.75f + (i % 2) * 0.28f;
				gfx::Face({ u - 0.07f, y + 0.045f, z0 }, { u + 0.07f, y + 0.045f, z0 }, { u + 0.07f, y + 0.045f, z0 + 0.3f }, { u - 0.07f, y + 0.045f, z0 + 0.3f }, Rgb(kPaper[i]));
			}
		}

		void Bench(float x, float y, bool alongX)
		{
			const Color wood = Rgb(0xA87848), dark = Rgb(0x6A4828);
			if (alongX)
			{
				gfx::Box(x - 0.42f, y - 0.14f, x + 0.42f, y + 0.14f, 0.34f, 0.42f, Shade(wood, 1.1f), wood, Shade(wood, 0.75f));
				gfx::Box(x - 0.42f, y - 0.16f, x + 0.42f, y - 0.1f, 0.42f, 0.78f, Shade(wood, 1.05f), wood, Shade(wood, 0.72f));
				for (float o : { -0.34f, 0.34f })
					gfx::Box(x + o - 0.04f, y - 0.12f, x + o + 0.04f, y + 0.12f, 0.f, 0.34f, dark, dark, Shade(dark, 0.7f));
			}
			else
			{
				gfx::Box(x - 0.14f, y - 0.42f, x + 0.14f, y + 0.42f, 0.34f, 0.42f, Shade(wood, 1.1f), wood, Shade(wood, 0.75f));
				gfx::Box(x - 0.16f, y - 0.42f, x - 0.1f, y + 0.42f, 0.42f, 0.78f, Shade(wood, 1.05f), wood, Shade(wood, 0.72f));
				for (float o : { -0.34f, 0.34f })
					gfx::Box(x - 0.12f, y + o - 0.04f, x + 0.12f, y + o + 0.04f, 0.f, 0.34f, dark, dark, Shade(dark, 0.7f));
			}
		}

		void Statue(float x, float y, float time)
		{
			// 서약 기사 석상: 받침 두 단 + 방패를 든 몸 + 머리 · 치켜든 칼
			const Texture2D* st = WallTex(Wall::Dressed);
			if (st) gfx::BoxTex(*st, x - 0.42f, y - 0.42f, x + 0.42f, y + 0.42f, 0.f, 0.55f, 0.8f, Rgb(0xE0DCD4), Rgb(0xD0CCC4), Rgb(0xA8A49C));
			else gfx::Box(x - 0.42f, y - 0.42f, x + 0.42f, y + 0.42f, 0.f, 0.55f, Rgb(0xC8C4BC), Rgb(0xB8B4AC), Rgb(0x98948C));
			gfx::Box(x - 0.3f, y - 0.3f, x + 0.3f, y + 0.3f, 0.55f, 0.72f, Rgb(0xD8D4CC), Rgb(0xC8C4BC), Rgb(0xA8A49C));
			const Color bronze = Rgb(0x6A8A7A), bl = Rgb(0x8AAA96), bd = Rgb(0x4A6456);
			gfx::Box(x - 0.16f, y - 0.1f, x + 0.16f, y + 0.1f, 0.72f, 1.55f, bl, bronze, bd);                  // 몸
			gfx::Box(x - 0.2f, y - 0.12f, x + 0.2f, y + 0.12f, 1.35f, 1.6f, bl, bronze, bd);                   // 어깨
			gfx::SetDepth(x + y + 0.1f);
			gfx::Circle(gfx::IsoPx(x, y, 1.78f), 6.f, bronze);                                                  // 머리
			gfx::Circle(gfx::IsoPx(x - 0.03f, y - 0.03f, 1.82f), 3.5f, bl);
			// 방패(남쪽)
			const P3 sc{ x - 0.08f, y + 0.14f, 1.1f };
			gfx::SetDepth(sc.x + sc.y + 0.05f);
			const Vector2 s = gfx::IsoPx(sc.x, sc.y, sc.z);
			const Vector2 sp[5] = { { s.x - 7, s.y - 8 }, { s.x + 7, s.y - 8 }, { s.x + 6, s.y + 2 }, { s.x, s.y + 9 }, { s.x - 6, s.y + 2 } };
			gfx::Poly(sp, 5, bd);
			gfx::Ring(s, 2.5f, 4.f, Rgb(0xC8A860));
			// 치켜든 칼
			gfx::Line(gfx::IsoPx(x + 0.18f, y, 1.5f), gfx::IsoPx(x + 0.24f, y, 2.45f), 2.4f, Rgb(0xB8C8C0));
			(void)time;
		}

		void Well(float x, float y, float time)
		{
			const Texture2D* st = WallTex(Wall::Stone);
			Prism(x, y, 0.42f, 0.42f, 8, 0.f, 0.55f, st, Rgb(0xD0CAC0), 0.8f);
			gfx::SetDepth(x + y);
			gfx::Ellipse(gfx::IsoPx(x, y, 0.55f), 0.34f * 32.f * 1.35f, 0.34f * 16.f * 1.35f, Rgb(0x2A3A48));
			const float sh = 0.5f + 0.5f * std::sin(time * 1.5f + x);
			gfx::Ellipse(gfx::IsoPx(x - 0.05f, y - 0.05f, 0.52f), 6.f, 3.f, Mix(Rgb(0x4A6A88), Rgb(0x8AB0C8), sh * 0.5f));
			const Color wood = Rgb(0x6A4A2E);
			for (float o : { -0.38f, 0.38f })
				gfx::Box(x + o - 0.05f, y - 0.05f, x + o + 0.05f, y + 0.05f, 0.5f, 1.55f, Shade(wood, 1.1f), wood, Shade(wood, 0.7f));
			gfx::Box(x - 0.42f, y - 0.04f, x + 0.42f, y + 0.04f, 1.25f, 1.33f, Shade(wood, 1.15f), wood, Shade(wood, 0.72f));
			Look R; R.roof = Roof::Shingle; R.roofMul = Rgb(0xB09880);
			Gable(x - 0.55f, y - 0.4f, x + 0.55f, y + 0.4f, 1.55f, 0.45f, 0.06f, true, R, Rgb(0x6A4A2E));
			gfx::SetDepth(x + y + 0.05f);
			gfx::Line(gfx::IsoPx(x, y, 1.25f), gfx::IsoPx(x, y, 0.8f), 1.f, Rgb(0xC8B890));
			gfx::Circle(gfx::IsoPx(x, y, 0.78f), 3.f, Rgb(0x7A5A3A));
		}

		void Cart(float x, float y, uint32_t h)
		{
			const Color wood = Rgb(0x9A7048), dark = Rgb(0x5A3E22);
			gfx::Box(x - 0.42f, y - 0.26f, x + 0.3f, y + 0.26f, 0.32f, 0.44f, Shade(wood, 1.1f), wood, Shade(wood, 0.75f));
			gfx::Box(x - 0.42f, y + 0.22f, x + 0.3f, y + 0.26f, 0.44f, 0.64f, wood, wood, Shade(wood, 0.75f));
			gfx::Box(x + 0.26f, y - 0.26f, x + 0.3f, y + 0.26f, 0.44f, 0.64f, wood, wood, Shade(wood, 0.75f));
			// 바퀴(남쪽 면에 보이는 원)
			gfx::SetDepth(x + y + 0.3f);
			const Vector2 wc = gfx::IsoPx(x - 0.05f, y + 0.3f, 0.28f);
			gfx::Ring(wc, 7.f, 9.5f, dark);
			gfx::Line({ wc.x - 8.f, wc.y }, { wc.x + 8.f, wc.y }, 1.4f, dark);
			gfx::Line({ wc.x, wc.y - 8.f }, { wc.x, wc.y + 8.f }, 1.4f, dark);
			// 손잡이 + 짐(자루 · 과일)
			gfx::Box(x - 0.9f, y - 0.2f, x - 0.42f, y - 0.16f, 0.36f, 0.4f, dark, dark, dark);
			gfx::Box(x - 0.9f, y + 0.16f, x - 0.42f, y + 0.2f, 0.36f, 0.4f, dark, dark, dark);
			gfx::SetDepth(x + y + 0.1f);
			for (int i = 0; i < 4; ++i)
			{
				const float ox = -0.25f + (i % 2) * 0.28f, oy = -0.1f + (i / 2) * 0.2f;
				const Color c = ((h >> i) & 1) ? Rgb(0xE0CCA0) : Rgb(0xC8A060);
				gfx::Circle(gfx::IsoPx(x + ox, y + oy, 0.62f), 6.f, c);
			}
			gfx::Circle(gfx::IsoPx(x + 0.05f, y, 0.82f), 4.f, (h & 16) ? Rgb(0xD84A3A) : Rgb(0x7AB040));
		}

		void Signpost(float x, float y, float time)
		{
			const Color wood = Rgb(0x7A5634);
			gfx::Box(x - 0.05f, y - 0.05f, x + 0.05f, y + 0.05f, 0.f, 1.7f, Shade(wood, 1.1f), wood, Shade(wood, 0.7f));
			// 팔 셋(방향 판) — 끝이 뾰족
			const float angles[3] = { -0.6f, 0.9f, 2.5f };
			for (int i = 0; i < 3; ++i)
			{
				const float a = angles[i] + std::sin(time * 0.7f + x) * 0.01f;
				const float z = 1.5f - i * 0.22f;
				const float dx = std::cos(a), dy = std::sin(a);
				const P3 p0{ x + dx * 0.05f, y + dy * 0.05f, z }, p1{ x + dx * 0.55f, y + dy * 0.55f, z }, tip{ x + dx * 0.68f, y + dy * 0.68f, z - 0.07f };
				gfx::Face(p0, p1, { p1.x, p1.y, p1.z - 0.14f }, { p0.x, p0.y, p0.z - 0.14f }, Shade(Rgb(0xC8A070), 0.8f + 0.2f * dy));
				gfx::FaceTri(p1, { p1.x, p1.y, p1.z - 0.14f }, tip, Shade(Rgb(0xC8A070), 0.75f));
			}
		}

		void Target(float x, float y)
		{
			// 과녁: 짚 둥근 판(동심원) + 받침 다리
			const Color wood = Rgb(0x6A4A2E);
			gfx::Box(x - 0.3f, y - 0.04f, x - 0.24f, y + 0.04f, 0.f, 1.0f, wood, wood, Shade(wood, 0.7f));
			gfx::Box(x + 0.24f, y - 0.04f, x + 0.3f, y + 0.04f, 0.f, 1.0f, wood, wood, Shade(wood, 0.7f));
			gfx::Box(x - 0.05f, y + 0.1f, x + 0.05f, y + 0.4f, 0.f, 0.08f, wood, wood, Shade(wood, 0.7f));
			const P3 c{ x, y + 0.06f, 1.0f };
			gfx::SetDepth(c.x + c.y);
			const Vector2 s = gfx::IsoPx(c.x, c.y, c.z);
			gfx::Ellipse(s, 15.f, 16.f, Rgb(0xD8C080));
			gfx::Ellipse(s, 11.f, 12.f, Rgb(0xF0ECE0));
			gfx::Ellipse(s, 7.5f, 8.f, Rgb(0xC83A3A));
			gfx::Ellipse(s, 3.5f, 3.8f, Rgb(0xF2D060));
			gfx::Line({ s.x + 2.f, s.y - 3.f }, { s.x + 11.f, s.y - 9.f }, 1.4f, Rgb(0x5A4028));   // 꽂힌 화살
		}

		void Rack(float x, float y)
		{
			const Color wood = Rgb(0x7A5634);
			for (float o : { -0.36f, 0.36f })
				gfx::Box(x + o - 0.05f, y - 0.05f, x + o + 0.05f, y + 0.05f, 0.f, 1.2f, Shade(wood, 1.1f), wood, Shade(wood, 0.7f));
			gfx::Box(x - 0.42f, y - 0.04f, x + 0.42f, y + 0.04f, 1.05f, 1.13f, Shade(wood, 1.1f), wood, Shade(wood, 0.72f));
			gfx::Box(x - 0.42f, y - 0.04f, x + 0.42f, y + 0.04f, 0.3f, 0.36f, Shade(wood, 1.1f), wood, Shade(wood, 0.72f));
			// 기대 세운 창 · 칼 · 방패
			gfx::SetDepth(x + y + 0.1f);
			for (int i = 0; i < 4; ++i)
			{
				const float u = x - 0.28f + i * 0.19f;
				gfx::Line(gfx::IsoPx(u, y + 0.08f, 0.02f), gfx::IsoPx(u + 0.03f, y + 0.02f, i % 2 ? 1.5f : 1.3f), 1.6f, i % 2 ? Rgb(0x8A6A48) : Rgb(0xB8BCC4));
			}
			const Vector2 s = gfx::IsoPx(x + 0.3f, y + 0.12f, 0.55f);
			const Vector2 sp[5] = { { s.x - 6, s.y - 7 }, { s.x + 6, s.y - 7 }, { s.x + 5, s.y + 2 }, { s.x, s.y + 8 }, { s.x - 5, s.y + 2 } };
			gfx::Poly(sp, 5, Rgb(0x8A3A30));
		}

		void Bunting(P3 a, P3 b, float seed)
		{
			// 줄깃발: 늘어진 줄 + 세모 깃발(바람에 흔들린다)
			const int n = std::max(4, static_cast<int>(std::hypot(b.x - a.x, b.y - a.y) * 1.8f));
			constexpr uint32_t kCol[] = { 0xC8483A, 0xF2D060, 0x3A7A6A, 0xF4ECDC, 0x4A6AA8 };
			P3 prev = a;
			for (int i = 1; i <= n; ++i)
			{
				const float t = static_cast<float>(i) / n;
				const P3 p{ util::Lerp(a.x, b.x, t), util::Lerp(a.y, b.y, t), util::Lerp(a.z, b.z, t) - std::sin(t * kPi) * 0.35f };
				gfx::SetDepth(p.x + p.y);
				gfx::Line(gfx::IsoPx(prev.x, prev.y, prev.z), gfx::IsoPx(p.x, p.y, p.z), 1.f, Rgb(0x4A3A2A));
				const float tm = (static_cast<float>(i) - 0.5f) / n;
				const P3 m{ util::Lerp(a.x, b.x, tm), util::Lerp(a.y, b.y, tm), util::Lerp(a.z, b.z, tm) - std::sin(tm * kPi) * 0.35f };
				const Vector2 mi = gfx::IsoPx(m.x, m.y, m.z);
				const float sw = wind::Sway(mi, 4.f, seed + i) * 5.f;
				const Vector2 l = gfx::IsoPx(util::Lerp(prev.x, p.x, 0.2f), util::Lerp(prev.y, p.y, 0.2f), util::Lerp(prev.z, p.z, 0.2f));
				const Vector2 r = gfx::IsoPx(util::Lerp(prev.x, p.x, 0.8f), util::Lerp(prev.y, p.y, 0.8f), util::Lerp(prev.z, p.z, 0.8f));
				gfx::Tri(l, r, { mi.x + sw, mi.y + 13.f }, Rgb(kCol[(i + static_cast<int>(seed)) % 5]));
				prev = p;
			}
		}

		void Post(float x, float y, float h)
		{
			const Color wood = Rgb(0x6A4A2E);
			gfx::Box(x - 0.04f, y - 0.04f, x + 0.04f, y + 0.04f, 0.f, h, Shade(wood, 1.1f), wood, Shade(wood, 0.7f));
		}

		const TownMap::PropRow* PropAt(int x, int y)
		{
			for (const auto& p : TownMap::kProps)
				if (p.x == x && p.y == y && p.kind != TownMap::PropKind::eLaundry && p.kind != TownMap::PropKind::eBunting)
					return &p;
			return nullptr;
		}
	}

	bool DrawTownProp(int x, int y, float time)
	{
		const TownMap::PropRow* p = PropAt(x, y);
		if (!p) return false;
		using TownMap::PropKind;
		const float X = static_cast<float>(x), Y = static_cast<float>(y);
		const uint32_t h = util::Hash2(x, y, 911);
		gfx::SetDepth(X + Y);
		switch (p->kind)
		{
		case PropKind::eGrave: Grave(X, Y, h); break;
		case PropKind::eBoard: Board(X, Y); break;
		case PropKind::eBench: Bench(X, Y, (h & 1) != 0); break;
		case PropKind::eStatue: Statue(X, Y, time); break;
		case PropKind::eFlagpole:
		{
			constexpr uint32_t kCloth[] = { 0x2E4A7A, 0x7A2E2A, 0x2E6A4A };
			gfx::Box(X - 0.22f, Y - 0.22f, X + 0.22f, Y + 0.22f, 0.f, 0.25f, Rgb(0xC8C4BC), Rgb(0xA8A49C), Rgb(0x88847C));
			Flag({ X, Y, 0.25f }, 4.2f, Rgb(kCloth[h % 3]), Rgb(0xE8C860), static_cast<float>(h % 7), 1.35f, 0.72f);
			break;
		}
		case PropKind::eWell: Well(X, Y, time); break;
		case PropKind::eCart: Cart(X, Y, h); break;
		case PropKind::eHay: Hay(X, Y); if (h & 1) Hay(X + 0.05f, Y - 0.05f, 0.4f); break;
		case PropKind::eBarrels: Barrel(X - 0.2f, Y - 0.15f); Barrel(X + 0.2f, Y + 0.12f); Barrel(X - 0.15f, Y + 0.2f); if (h & 2) Barrel(X, Y, 0.5f); break;
		case PropKind::eCrates: Crate(X - 0.15f, Y - 0.1f, 0.f, 0.22f, h); Crate(X + 0.2f, Y + 0.18f, 0.f, 0.18f, h >> 4); Crate(X - 0.1f, Y, 0.44f, 0.17f, h >> 8); break;
		case PropKind::eSignpost: Signpost(X, Y, time); break;
		case PropKind::eTarget: Target(X, Y); break;
		case PropKind::eRack: Rack(X, Y); break;
		default: return false;
		}
		return true;
	}

	void DrawLineProps(int x0, int y0, int x1, int y1, float time)
	{
		(void)time;
		for (const auto& p : TownMap::kProps)
		{
			if (p.kind != TownMap::PropKind::eLaundry && p.kind != TownMap::PropKind::eBunting) continue;
			if (std::max(p.x, p.x2) < x0 - 2 || std::min(p.x, p.x2) > x1 + 2 || std::max(p.y, p.y2) < y0 - 2 || std::min(p.y, p.y2) > y1 + 2) continue;
			gfx::SetLift(static_cast<float>(WorldData::TownLevel(p.x, p.y)) * 0.75f * gfx::kUnitZ);
			const float ax = static_cast<float>(p.x), ay = static_cast<float>(p.y), bx = static_cast<float>(p.x2), by = static_cast<float>(p.y2);
			if (p.kind == TownMap::PropKind::eLaundry)
			{
				Post(ax, ay, 1.55f);
				Post(bx, by, 1.55f);
				Laundry({ ax, ay, 1.5f }, { bx, by, 1.5f }, static_cast<float>((p.x * 7 + p.y) % 11));
			}
			else
				Bunting({ ax, ay, 2.75f }, { bx, by, 2.75f }, static_cast<float>((p.x * 3 + p.y) % 13));
			gfx::SetLift(0.f);
		}
	}

	namespace
	{
		// ── 그린 건물(Higgsfield — tools/assetgen/buildings.py) ─────────────────────────
		// 키트 건물을 설계도로 다시 그린 그림. 깊이는 두 벽면의 천막꼴: 남쪽 면(깊이 = X + Y1)과 동쪽 면(X1 + Y)이 만나는
		// 앞 모서리 열이 가장 가깝다 — 상자꼴 건물에서 캐릭터가 뒤(북 · 서)면 가려지고 앞(남 · 동)이면 그 위에 그려진다.
		struct Art
		{
			std::string name;
			int x0 = 0, y0 = 0;                     // 마을 기준 건물 자리(buildings.txt의 키)
			float rx = 0, ry = 0, rw = 0, rh = 0;   // 기초 남동 모서리 기준 그림 사각형(아이소 px)
			Texture2D tex{};
			bool tried = false;
		};
		std::vector<Art> g_art;
		std::string g_artDir;
		bool g_artParsed = false;
		bool g_artOn = true;

		Art* FindArt(const TownMap::BuildingRow& b)
		{
			if (!g_artOn || g_artDir.empty()) return nullptr;
			if (!g_artParsed)
			{
				g_artParsed = true;
				if (FILE* fp = std::fopen((g_artDir + "/buildings.txt").c_str(), "r"))
				{
					char line[256];
					while (std::fgets(line, sizeof(line), fp))
					{
						if (line[0] == '#') continue;
						char name[64];
						Art a;
						if (std::sscanf(line, "%63s %d %d %f %f %f %f", name, &a.x0, &a.y0, &a.rx, &a.ry, &a.rw, &a.rh) == 7)
						{
							a.name = name;
							g_art.push_back(a);
						}
					}
					std::fclose(fp);
				}
			}
			for (Art& a : g_art)
			{
				if (a.x0 != b.x0 - TownMap::kX0 || a.y0 != b.y0 - TownMap::kY0) continue;
				if (!a.tried)
				{
					a.tried = true;
					const std::string png = g_artDir + "/" + a.name + ".png";
					if (FileExists(png.c_str()))
					{
						a.tex = LoadTexture(png.c_str());
						if (a.tex.id != 0)
						{
							GenTextureMipmaps(&a.tex);
							SetTextureFilter(a.tex, TEXTURE_FILTER_TRILINEAR);
							SetTextureWrap(a.tex, TEXTURE_WRAP_CLAMP);
						}
					}
				}
				return a.tex.id != 0 ? &a : nullptr;
			}
			return nullptr;
		}

		void DrawArt(const Art& a, const Frame& f)
		{
			const Vector2 c = gfx::IsoPx(f.x1 + 0.05f, f.y1 + 0.05f);
			const float L = c.x + a.rx, T = c.y + a.ry, R = L + a.rw, B = T + a.rh;
			const float xc = (f.x1 - f.y1) * 32.f;   // 앞 모서리 열
			auto depthAt = [&](float x) { return std::min(x / 32.f + 2.f * f.y1, 2.f * f.x1 - x / 32.f); };
			const Color t = gfx::Tint();
			const float lift = gfx::Lift();
			rlSetTexture(a.tex.id);
			rlBegin(RL_QUADS);
			rlColor4ub(t.r, t.g, t.b, t.a);
			rlNormal3f(0.f, 0.f, 1.f);
			// 텍스처 좌표를 가장자리에서 조금 안쪽으로(m): 월드 셰이더는 좌표의 정수부로 알파 시험 · 번쩍임 색을 고르는데,
			// 사각형 테두리 바깥의 보조 픽셀이 정수 경계를 넘으면 미분이 튀어 가장 작은 밉(평균색)이 선으로 그려진다.
			// 여백(투명 6px)만 잘리므로 그림은 그대로다.
			constexpr float m = 0.004f;
			const float W = R - L, H = B - T;
			const float qL = L + W * m, qR = R - W * m, qT = T + H * m, qB = B - H * m;
			auto quad = [&](float xa, float xb)
			{
				const float u0 = (xa - L) / W, u1 = (xb - L) / W;
				const float za = gfx::DepthOf(depthAt(xa)), zb = gfx::DepthOf(depthAt(xb));
				// v + 16: 월드 셰이더의 알파 시험(투명 텍셀은 깊이도 쓰지 않는다 — Sprites.cpp)
				rlTexCoord2f(u0, 16.f + m); rlVertex3f(xa, qT - lift, za);
				rlTexCoord2f(u0, 17.f - m); rlVertex3f(xa, qB - lift, za);
				rlTexCoord2f(u1, 17.f - m); rlVertex3f(xb, qB - lift, zb);
				rlTexCoord2f(u1, 16.f + m); rlVertex3f(xb, qT - lift, zb);
			};
			if (xc > qL + 1.f && xc < qR - 1.f) { quad(qL, xc); quad(xc, qR); }
			else quad(qL, qR);
			rlEnd();
			rlTexCoord2f(0.f, 0.f);
			rlSetTexture(0);
		}

		// 밤 창빛: 보이는 두 면을 따라 간격마다(그림의 창 자리와 키트 배치가 같다 — 키트를 설계도로 그렸다)
		void NightWindows(const Frame& f, const Look& L, float z, float spacing, float margin = 0.8f)
		{
			const float n = Night();
			if (n <= 0.05f) return;
			auto lit = [&](P3 p, uint32_t h)
			{
				if (h % 5 == 0) return;   // 다섯에 하나는 불 꺼진 방
				Glow(p, 10.f, Rgb(0xFFC870), 0.7f * n);
				PushLight({ gfx::IsoPx(p.x, p.y, 0.f), 70.f, L.glow, 0.6f * n });
			};
			for (float x = f.x0 + margin; x < f.x1 - margin * 0.7f; x += spacing)
				if (!(f.door == 'S' && std::fabs(x - f.doorU) < 0.9f)) lit({ x, f.y1 + 0.05f, z }, util::Hash32(f.h + static_cast<uint32_t>(x * 7.f + z * 31.f)));
			for (float y = f.y0 + margin; y < f.y1 - margin * 0.7f; y += spacing)
				if (!(f.door == 'E' && std::fabs(y - f.doorU) < 0.9f)) lit({ f.x1 + 0.05f, y, z }, util::Hash32(f.h + static_cast<uint32_t>(y * 13.f + z * 17.f)));
		}

		// 그린 건물 위에 키트의 움직이는 · 빛나는 조각만(간판 · 깃발 · 차양은 그림에 들어 있다). 자리는 키트와 같다.
		void ArtExtras(const TownMap::BuildingRow& b, const Frame& f, const Look& L)
		{
			const float n = Night();
			switch (b.kind)
			{
			case BuildingKind::eAuctionHall:
				NightWindows(f, L, 1.4f, 1.5f, 0.9f);
				break;
			case BuildingKind::eStarTower:
			{
				const float cx = (f.x0 + f.x1) * 0.5f, cy = (f.y0 + f.y1) * 0.5f;
				const float flick = 0.85f + 0.15f * std::sin(f.time * 5.3f) * std::sin(f.time * 2.1f + 0.7f);
				Glow({ cx, cy, 5.6f }, 24.f + 28.f * n, Rgb(0xFFE0A0), (0.3f + 0.7f * n) * flick);
				Glow({ cx, cy, 7.15f }, 9.f, Rgb(0xFFF0C0), 0.3f + 0.5f * n);
				if (n > 0.02f) PushLight({ gfx::IsoPx(cx, cy, 0.f), 360.f, Rgb(0xFFD890), 0.9f * n * flick });
				break;
			}
			case BuildingKind::eStore:
				NightWindows(f, L, 0.9f, 1.5f);
				NightWindows(f, L, 2.25f, 1.35f);
				Smoke({ f.x0 + 0.9f, f.y0 + 0.8f, 4.68f }, f.time, 0.3f);
				break;
			case BuildingKind::eWeaponShop:
			{
				const float shed = std::min(2.2f, (f.x1 - f.x0) * 0.38f), xb = f.x1 - shed;
				const float pulse = 0.75f + 0.25f * std::sin(f.time * 3.1f) * std::sin(f.time * 1.7f + 1.f);
				const float fx = xb + 0.6f, fy = f.y0 + 0.9f;
				Glow({ fx, fy + 0.5f, 0.45f }, 26.f, Rgb(0xFF7A30), 0.5f + 0.35f * pulse);
				PushLight({ gfx::IsoPx(fx, fy + 0.8f), 170.f, Rgb(0xFF8A30), 0.75f * pulse });
				const float cx = xb - 0.5f, cy = f.y0 + 0.7f;
				Smoke({ cx, cy, 4.48f }, f.time, 0.7f);
				const float spark = std::fmod(f.time * 0.9f, 1.f);
				if (spark < 0.5f) Glow({ cx + std::sin(f.time * 7.f) * 0.15f, cy, 4.5f + spark * 1.4f }, 4.f, Rgb(0xFFB040), 1.f - spark * 2.f);
				break;
			}
			case BuildingKind::eInn:
				NightWindows(f, L, 0.9f, 1.45f);
				NightWindows(f, L, 2.3f, 1.3f);
				Smoke({ f.x0 + 1.0f, f.y0 + 0.8f, 5.08f }, f.time, 0.2f);
				Smoke({ f.x1 - 1.2f, f.y0 + 0.8f, 4.88f }, f.time, 0.6f);
				if (n > 0.05f)
				{
					Glow({ f.x1 + 0.2f, f.y1 + 0.2f, 1.36f }, 16.f, L.glow, 0.8f * n);
					PushLight({ gfx::IsoPx(f.x1 + 0.2f, f.y1 + 0.2f, 0.f), 120.f, L.glow, 0.75f * n });
				}
				break;
			case BuildingKind::eGuildHall:
				NightWindows(f, L, 1.45f, 1.8f, 1.2f);
				if (n > 0.05f)
					for (float o : { -1.0f, 1.0f })
					{
						Glow({ f.doorU + o, f.y1 + 0.1f, 1.81f }, 16.f, L.glow, 0.8f * n);
						PushLight({ gfx::IsoPx(f.doorU + o, f.y1 + 0.1f, 0.f), 120.f, L.glow, 0.7f * n });
					}
				break;
			case BuildingKind::eTemple:
				if (n > 0.05f)
				{
					Glow({ (f.x0 + f.x1) * 0.5f, f.y1 + 0.02f, 2.8f + 1.9f * 0.42f }, 18.f, Rgb(0xC0DCFF), 0.6f * n);
					for (float y = f.y0 + 1.2f; y < f.y1 - 0.8f; y += 1.8f) Glow({ f.x1 + 0.05f, y + 0.6f, 1.45f }, 12.f, Rgb(0xC0DCFF), 0.55f * n);
				}
				PushLight({ gfx::IsoPx((f.x0 + f.x1) * 0.5f, f.y1 + 0.8f), 180.f, Rgb(0xFFF0C0), 0.7f });
				break;
			default:
				NightWindows(f, L, 1.0f, 1.6f);
				break;
			}
		}
	}

	void SetBuildingArt(const std::string& dir, bool on)
	{
		g_artDir = dir;
		g_artOn = on;
	}

	void DrawBuilding(const WorldData::BuildingRow& b, float time)
	{
		const Frame f = MakeFrame(b, time);
		if (const Art* a = FindArt(b))
		{
			Look L = DistrictLook(DistrictAt((b.x0 + b.x1) * 0.5f, (b.y0 + b.y1) * 0.5f));
			DrawArt(*a, f);
			ArtExtras(b, f, L);
			return;
		}
		Look L = DistrictLook(DistrictAt((b.x0 + b.x1) * 0.5f, (b.y0 + b.y1) * 0.5f));
		L.wallMul = Jitter(L.wallMul, f.h, 0.04f);
		L.roofMul = Jitter(L.roofMul, f.h >> 8, 0.06f);
		switch (b.kind)
		{
		case BuildingKind::eStore:       Store(f, L); break;
		case BuildingKind::eWeaponShop:  Smithy(f, L); break;
		case BuildingKind::eTemple:      Temple(f, L); break;
		case BuildingKind::eInn:         Inn(f, L); break;
		case BuildingKind::eArmorer:     Armorer(f, L); break;
		case BuildingKind::eTavern:      Tavern(f, L); break;
		case BuildingKind::eStarTower:   StarTower(f, L); break;
		case BuildingKind::eAuctionHall: AuctionHall(f, L); break;
		case BuildingKind::eGuildHall:   GuildHall(f, L); break;
		case BuildingKind::eWarehouse:   Warehouse(f, L); break;
		case BuildingKind::eStable:      Stable(f, L); break;
		case BuildingKind::eWaterMill:   WaterMill(f, L); break;
		case BuildingKind::eWindMill:    WindMill(f, L); break;
		case BuildingKind::eGuardPost:   GuardPost(f, L); break;
		case BuildingKind::eScriptorium: Scriptorium(f, L); break;
		case BuildingKind::eGreenhouse:  Greenhouse(f, L); break;
		case BuildingKind::eWorkshop:    Workshop(f, L); break;
		case BuildingKind::eLookout:     Lookout(f, L); break;
		case BuildingKind::eHouse:
		default:                         House(b, f, L); break;
		}
	}

	void DrawBuildingFx()
	{
		// 연기: 깊이 시험은 하되 쓰지 않는다(반투명이 뒤의 캐릭터를 지우지 않게)
		if (!g_smoke.empty())
		{
			gfx::DepthWrite(false);
			for (const Puff& p : g_smoke)
			{
				gfx::SetDepth(p.depth);
				gfx::CircleGrad(p.iso, p.r, p.c, util::MulA(p.c, 0.f));
			}
			gfx::DepthWrite(true);
			g_smoke.clear();
		}
		if (!g_glow.empty())
		{
			gfx::DepthWrite(false);
			gfx::Additive(true);
			for (const Puff& p : g_glow)
			{
				gfx::SetDepth(p.depth);
				gfx::CircleGrad(p.iso, p.r, p.c, util::MulA(p.c, 0.f));
			}
			gfx::Additive(false);
			gfx::DepthWrite(true);
			g_glow.clear();
		}
	}
}
