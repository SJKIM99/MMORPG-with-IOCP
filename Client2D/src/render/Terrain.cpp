#include "render/Terrain.h"
#include "render/TerrainShader.h"
#include "render/Props.h"
#include "render/Draw.h"
#include "render/Assets.h"
#include "render/EnvArt.h"
#include "render/Height.h"
#include "net/Shared.h"
#include "core/Util.h"

#include "rlgl.h"
#include <cmath>
#include <cstdio>

namespace
{
	// 마을 칸 재질 코드(셰이더 townGround와 같은 번호) — 문자 뜻은 tools/mapgen/gen_town.py LEGEND
	int TownCode(char c)
	{
		switch (c)
		{
		case '.': return 0;  case '=': return 1;  case ',': return 2;  case 'G': return 3;
		case '#': return 4;  case 'O': return 5;  case 'B': return 6;  case 'H': return 7;
		case 'F': return 8;  case 'T': return 9;  case 'L': return 10; case '*': return 11;
		case 'N': return 12; case 'S': return 13;
		// Phase 4
		case ':': return 14; case 's': return 15; case '~': return 16; case 'b': return 17;
		case 'g': return 18; case '^': case 'v': case '<': case '>': return 19;
		case 'X': return 20; case 'h': return 21; case 'c': return 22; case 'P': return 23; case 'W': return 24;
		default:  return 0;
		}
	}

	// 옹벽 · 절벽 면의 재질: 판석 · 길 · 계단 위라면 다듬은 돌(성벽 돌), 풀 · 흙이면 거친 돌
	const Texture2D* FaceTexFor(char surface)
	{
		const bool dressed = surface == ',' || surface == '=' || surface == 'N' || surface == 'G' || surface == 'B'
			|| surface == '^' || surface == 'v' || surface == '<' || surface == '>' || surface == 'F' || surface == 'S' || surface == 'L';
		const Texture2D* t = envart::Get(dressed ? envart::Tex::TownWall : envart::Tex::Stone);
		return t ? t : envart::Get(envart::Tex::Stone);
	}

	int Loc(const Shader& s, const char* name)
	{
		int loc = GetShaderLocation(s, name);
		if (loc < 0)
		{
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%s[0]", name);
			loc = GetShaderLocation(s, buf);
		}
		return loc;
	}
}

bool Terrain::Init()
{
	m_shader = LoadShaderFromMemory(nullptr, shaders::kTerrainFs);
	if (m_shader.id == 0 || m_shader.id == rlGetShaderIdDefault())
	{
		TraceLog(LOG_WARNING, "Terrain shader failed to compile - falling back to flat ground");
		m_shader = Shader{};
		return false;
	}

	m_locScreen = Loc(m_shader, "uScreen");
	m_locCamTarget = Loc(m_shader, "uCamTarget");
	m_locCamOffset = Loc(m_shader, "uCamOffset");
	m_locZoom = Loc(m_shader, "uZoom");
	m_locRot = Loc(m_shader, "uRot");
	m_locTime = Loc(m_shader, "uTime");
	m_locPlayer = Loc(m_shader, "uPlayer");
	m_locFog = Loc(m_shader, "uFog");
	m_locFogRange = Loc(m_shader, "uFogRange");
	m_locGrid = Loc(m_shader, "uGrid");
	m_locPalA = Loc(m_shader, "uPalA");
	m_locPalB = Loc(m_shader, "uPalB");
	m_locPalC = Loc(m_shader, "uPalC");
	m_locTown = Loc(m_shader, "uTown");
	m_locNoise = Loc(m_shader, "uNoise");
	m_locTownOrigin = Loc(m_shader, "uTownOrigin");
	m_locTownSize = Loc(m_shader, "uTownSize");
	m_locGround = Loc(m_shader, "uGround");
	m_locGroundOn = Loc(m_shader, "uGroundOn");
	m_locGroundSize = Loc(m_shader, "uGroundSize");
	m_locMesh = Loc(m_shader, "uMesh");
	hgt::Install();

	// 지역 팔레트(16개) — 한 번만 올리면 된다
	float palA[16 * 4], palB[16 * 3], palC[16 * 3];
	for (int i = 0; i < WorldData::kRegionCount; ++i)
	{
		const props::Palette p = props::PaletteFor(i);
		const auto* region = WorldData::FindRegion(i);
		for (int k = 0; k < 3; ++k)
		{
			palA[i * 4 + k] = p.a[k];
			palB[i * 3 + k] = p.b[k];
			palC[i * 3 + k] = p.c[k];
		}
		palA[i * 4 + 3] = static_cast<float>(region ? static_cast<int>(region->biome) : 0);
	}
	SetShaderValueV(m_shader, m_locPalA, palA, SHADER_UNIFORM_VEC4, 16);
	SetShaderValueV(m_shader, m_locPalB, palB, SHADER_UNIFORM_VEC3, 16);
	SetShaderValueV(m_shader, m_locPalC, palC, SHADER_UNIFORM_VEC3, 16);

	// 마을 레이아웃 텍스처(R = 칸 종류, G = 층) — 서버와 같은 TownMap(생성물)
	Image img{};
	img.width = WorldData::kTownW;
	img.height = WorldData::kTownH;
	img.mipmaps = 1;
	img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	img.data = MemAlloc(static_cast<unsigned int>(img.width * img.height * 4));
	auto* px = static_cast<unsigned char*>(img.data);
	for (int y = 0; y < WorldData::kTownH; ++y)
		for (int x = 0; x < WorldData::kTownW; ++x)
		{
			unsigned char* p = px + (y * img.width + x) * 4;
			p[0] = static_cast<unsigned char>(TownCode(TownMap::kSurface[y][x]));
			p[1] = static_cast<unsigned char>(TownMap::kLevel[y][x] - '0');
			p[2] = 0;
			p[3] = 255;
		}
	m_town = LoadTextureFromImage(img);
	UnloadImage(img);
	SetTextureFilter(m_town, TEXTURE_FILTER_POINT);

	const float origin[2] = { static_cast<float>(WorldData::kTownX0), static_cast<float>(WorldData::kTownY0) };
	const float size[2] = { static_cast<float>(WorldData::kTownW), static_cast<float>(WorldData::kTownH) };
	SetShaderValue(m_shader, m_locTownOrigin, origin, SHADER_UNIFORM_VEC2);
	SetShaderValue(m_shader, m_locTownSize, size, SHADER_UNIFORM_VEC2);
	return true;
}

void Terrain::Shutdown()
{
	if (m_shader.id != 0) UnloadShader(m_shader);
	if (m_town.id != 0) UnloadTexture(m_town);
	m_shader = Shader{};
	m_town = Texture2D{};
}

void Terrain::SetViewRange(float range)
{
	// 서버가 보내 주는 마지막 칸(range)보다 조금 바깥에서 어두워지기 시작한다.
	s_fogStart = range + 0.8f;
	s_fogEnd = range + 5.5f;
}

Color Terrain::FogTint(float tileX, float tileY, Vector2 playerTile, bool fog)
{
	if (!fog) return WHITE;
	const float d = std::max(std::fabs(tileX - playerTile.x), std::fabs(tileY - playerTile.y));
	const float f = util::Clamp01((d - s_fogStart) / (s_fogEnd - s_fogStart));
	const float s = f * f * (3.f - 2.f * f) * 0.9f;
	return util::Mix(WHITE, Color{ 138, 144, 166, 255 }, s);
}

void Terrain::DrawGround(const Camera2D& cam, int screenW, int screenH, Vector2 playerTile, float time, bool fog, float grid)
{
	if (m_shader.id == 0)
	{
		DrawRectangle(0, 0, screenW, screenH, Color{ 70, 110, 60, 255 });
		return;
	}
	const float screen[2] = { static_cast<float>(screenW), static_cast<float>(screenH) };
	const float target[2] = { cam.target.x, cam.target.y };
	const float offset[2] = { cam.offset.x, cam.offset.y };
	const float rot = cam.rotation * DEG2RAD;
	const float player[2] = { playerTile.x, playerTile.y };
	const float fogOn = fog ? 1.f : 0.f;

	SetShaderValue(m_shader, m_locScreen, screen, SHADER_UNIFORM_VEC2);
	SetShaderValue(m_shader, m_locCamTarget, target, SHADER_UNIFORM_VEC2);
	SetShaderValue(m_shader, m_locCamOffset, offset, SHADER_UNIFORM_VEC2);
	SetShaderValue(m_shader, m_locZoom, &cam.zoom, SHADER_UNIFORM_FLOAT);
	SetShaderValue(m_shader, m_locRot, &rot, SHADER_UNIFORM_FLOAT);
	SetShaderValue(m_shader, m_locTime, &time, SHADER_UNIFORM_FLOAT);
	SetShaderValue(m_shader, m_locPlayer, player, SHADER_UNIFORM_VEC2);
	SetShaderValue(m_shader, m_locFog, &fogOn, SHADER_UNIFORM_FLOAT);
	const float fogRange[2] = { s_fogStart, s_fogEnd };
	SetShaderValue(m_shader, m_locFogRange, fogRange, SHADER_UNIFORM_VEC2);
	SetShaderValue(m_shader, m_locGrid, &grid, SHADER_UNIFORM_FLOAT);

	// 그린 바닥(있으면): 지역 · 마을 바닥을 텍스처로, 없으면 절차적 바닥
	const Texture2D* ground = envart::GroundAtlas();
	const float groundOn = ground ? 1.f : 0.f;
	SetShaderValue(m_shader, m_locGroundOn, &groundOn, SHADER_UNIFORM_FLOAT);
	if (ground)
	{
		const float gs[2] = { static_cast<float>(ground->width), static_cast<float>(ground->height) };
		SetShaderValue(m_shader, m_locGroundSize, gs, SHADER_UNIFORM_VEC2);
	}

	const float meshOff = 0.f;
	SetShaderValue(m_shader, m_locMesh, &meshOff, SHADER_UNIFORM_FLOAT);
	BeginShaderMode(m_shader);
	SetShaderValueTexture(m_shader, m_locTown, m_town);
	SetShaderValueTexture(m_shader, m_locNoise, assets::Get().noise);
	if (ground)
		SetShaderValueTexture(m_shader, m_locGround, *ground);
	DrawRectangle(0, 0, screenW, screenH, WHITE);
	EndShaderMode();
}

void Terrain::VisibleTiles(const Camera2D& cam, int screenW, int screenH, int& x0, int& y0, int& x1, int& y1) const
{
	// 화면 네 모서리 → 타일 좌표 범위(+ 키 큰 소품 · 높은 지형을 위한 여유)
	float minU = 1e9f, maxU = -1e9f, minV = 1e9f, maxV = -1e9f;
	const Vector2 corners[4] = { {0, 0}, {(float)screenW, 0}, {0, (float)screenH}, {(float)screenW, (float)screenH} };
	for (const Vector2& c : corners)
	{
		const Vector2 iso = GetScreenToWorld2D(c, cam);
		const Vector2 t = gfx::IsoToTile(iso);
		minU = std::min(minU, t.x); maxU = std::max(maxU, t.x);
		minV = std::min(minV, t.y); maxV = std::max(maxV, t.y);
	}
	x0 = std::max(0, static_cast<int>(std::floor(minU)) - 2);
	y0 = std::max(0, static_cast<int>(std::floor(minV)) - 2);
	x1 = std::min(WorldData::kWorldWidth - 1, static_cast<int>(std::ceil(maxU)) + 6);
	y1 = std::min(WorldData::kWorldHeight - 1, static_cast<int>(std::ceil(maxV)) + 6);
}

void Terrain::DrawRelief(const Camera2D& cam, int screenW, int screenH, Vector2 playerTile, float time, bool fog)
{
	(void)time;
	if (!hgt::Any())
		return;
	int x0, y0, x1, y1;
	VisibleTiles(cam, screenW, screenH, x0, y0, x1, y1);
	// 높은 칸은 화면에서 위로 올라가므로 남 · 동쪽으로 더 본다(4층 = 108px ≈ X+Y 7칸)
	x1 += 4; y1 += 4;
	x0 = std::max(x0, WorldData::kTownX0);
	y0 = std::max(y0, WorldData::kTownY0);
	x1 = std::min(x1, WorldData::kTownX0 + WorldData::kTownW - 1);
	y1 = std::min(y1, WorldData::kTownY0 + WorldData::kTownH - 1);
	if (x0 > x1 || y0 > y1)
		return;
	constexpr float kL = hgt::kLevelZ;

	// ── 1) 윗면: 바닥 셰이더를 면 모드로(텍스처 좌표 = 칸 좌표 → 들판 · 0층과 같은 재질 · 무늬) ──
	if (m_shader.id != 0)
	{
		const float meshOn = 1.f;
		SetShaderValue(m_shader, m_locMesh, &meshOn, SHADER_UNIFORM_FLOAT);
		BeginShaderMode(m_shader);
		SetShaderValueTexture(m_shader, m_locTown, m_town);
		SetShaderValueTexture(m_shader, m_locNoise, assets::Get().noise);
		if (const Texture2D* ground = envart::GroundAtlas())
			SetShaderValueTexture(m_shader, m_locGround, *ground);
		rlBegin(RL_QUADS);
		rlColor4ub(255, 255, 255, 255);
		for (int y = y0; y <= y1; ++y)
			for (int x = x0; x <= x1; ++x)
			{
				const int L = WorldData::TownLevel(x, y);
				int sdx, sdy;
				if (L <= 0 || WorldData::StairsDir(WorldData::TownCell(x, y), sdx, sdy))
					continue;
				const float z = static_cast<float>(L) * kL;
				const float X = static_cast<float>(x), Y = static_cast<float>(y);
				const float cx[4] = { X - 0.5f, X + 0.5f, X + 0.5f, X - 0.5f };
				const float cy[4] = { Y - 0.5f, Y - 0.5f, Y + 0.5f, Y + 0.5f };
				for (int k = 0; k < 4; ++k)
				{
					const Vector2 p = gfx::IsoPx(cx[k], cy[k], z);
					rlTexCoord2f(cx[k], cy[k]);
					rlVertex3f(p.x, p.y, gfx::DepthOf(cx[k] + cy[k]));
				}
			}
		rlEnd();
		rlDrawRenderBatchActive();
		EndShaderMode();
		const float meshOff = 0.f;
		SetShaderValue(m_shader, m_locMesh, &meshOff, SHADER_UNIFORM_FLOAT);
	}

	// ── 2) 옹벽 · 절벽 면(남 · 동 — 카메라가 보는 두 면) · 계단 · 가장자리 난간 ──
	auto edgeZ = [](float ex, float ey) { return hgt::GroundZ(ex, ey); };
	for (int y = y0; y <= y1; ++y)
		for (int x = x0; x <= x1; ++x)
		{
			const int L = WorldData::TownLevel(x, y);
			const char cell = WorldData::TownCell(x, y);
			int sdx = 0, sdy = 0;
			const bool stairs = WorldData::StairsDir(cell, sdx, sdy);
			if (L <= 0 && !stairs)
				continue;
			gfx::SetTint(FogTint(static_cast<float>(x), static_cast<float>(y), playerTile, fog));
			const float X = static_cast<float>(x), Y = static_cast<float>(y);
			const float top = static_cast<float>(L) * kL;
			const char surf = TownMap::kSurface[y - WorldData::kTownY0][x - WorldData::kTownX0];
			const Texture2D* face = FaceTexFor(surf);
			// 남(+Y) 면: 이웃 가장자리 높이까지 내려간다
			const float zs = edgeZ(X, Y + 0.5f + 0.01f);
			if (zs < top - 0.01f)
			{
				if (face) gfx::FaceTex(*face, { X - 0.5f, Y + 0.5f, zs }, { X + 0.5f, Y + 0.5f, zs }, { X + 0.5f, Y + 0.5f, top }, { X - 0.5f, Y + 0.5f, top }, 1.f, top - zs, Color{ 208, 202, 194, 255 });
				else gfx::Face({ X - 0.5f, Y + 0.5f, zs }, { X + 0.5f, Y + 0.5f, zs }, { X + 0.5f, Y + 0.5f, top }, { X - 0.5f, Y + 0.5f, top }, Color{ 120, 112, 102, 255 });
				// 윗가장자리 그늘선(판이 끝나는 곳)
				gfx::Face({ X - 0.5f, Y + 0.5f, top - 0.04f }, { X + 0.5f, Y + 0.5f, top - 0.04f }, { X + 0.5f, Y + 0.5f, top }, { X - 0.5f, Y + 0.5f, top }, Color{ 40, 36, 32, 150 });
			}
			// 동(+X) 면
			const float ze = edgeZ(X + 0.5f + 0.01f, Y);
			if (ze < top - 0.01f)
			{
				if (face) gfx::FaceTex(*face, { X + 0.5f, Y + 0.5f, ze }, { X + 0.5f, Y - 0.5f, ze }, { X + 0.5f, Y - 0.5f, top }, { X + 0.5f, Y + 0.5f, top }, 1.f, top - ze, Color{ 168, 162, 156, 255 });
				else gfx::Face({ X + 0.5f, Y + 0.5f, ze }, { X + 0.5f, Y - 0.5f, ze }, { X + 0.5f, Y - 0.5f, top }, { X + 0.5f, Y + 0.5f, top }, Color{ 96, 90, 82, 255 });
				gfx::Face({ X + 0.5f, Y + 0.5f, top - 0.04f }, { X + 0.5f, Y - 0.5f, top - 0.04f }, { X + 0.5f, Y - 0.5f, top }, { X + 0.5f, Y + 0.5f, top }, Color{ 40, 36, 32, 150 });
			}
			// 계단: 디딤판 네 단(각 단 = 상자). 오르는 쪽으로 높아진다
			if (stairs)
			{
				const Texture2D* st = envart::Get(envart::Tex::TownWall);
				constexpr int kSteps = 4;
				for (int i = 0; i < kSteps; ++i)
				{
					const float a = static_cast<float>(i) / kSteps, b = static_cast<float>(i + 1) / kSteps;
					const float zt = (static_cast<float>(L) + b) * kL;
					// 단의 띠: 칸 안에서 오르는 방향으로 [a, b] (칸의 낮은 가장자리에서 잰다)
					float bx0 = X - 0.5f, bx1 = X + 0.5f, by0 = Y - 0.5f, by1 = Y + 0.5f;
					if (sdy < 0) { by0 = Y + 0.5f - b; by1 = Y + 0.5f - a; }
					else if (sdy > 0) { by0 = Y - 0.5f + a; by1 = Y - 0.5f + b; }
					else if (sdx < 0) { bx0 = X + 0.5f - b; bx1 = X + 0.5f - a; }
					else { bx0 = X - 0.5f + a; bx1 = X - 0.5f + b; }
					if (st) gfx::BoxTex(*st, bx0, by0, bx1, by1, top, zt, 0.9f, Color{ 222, 216, 204, 255 }, Color{ 176, 168, 156, 255 }, Color{ 150, 142, 132, 255 });
					else gfx::Box(bx0, by0, bx1, by1, top, zt, Color{ 190, 182, 168, 255 }, Color{ 150, 142, 130, 255 }, Color{ 128, 120, 110, 255 });
				}
			}
			// 가장자리(R): 낮은 쪽을 향한 변에 낮은 난간(돌)
			if (cell == 'R')
			{
				const Texture2D* rt = envart::Get(envart::Tex::TownWall);
				constexpr float kH = 0.3f, kT = 0.2f;
				auto rail = [&](float ax0, float ay0, float ax1, float ay1)
				{
					if (rt) gfx::BoxTex(*rt, ax0, ay0, ax1, ay1, top, top + kH, 0.7f, Color{ 214, 208, 198, 255 }, Color{ 178, 170, 160, 255 }, Color{ 150, 144, 134, 255 });
					else gfx::Box(ax0, ay0, ax1, ay1, top, top + kH, Color{ 176, 170, 160, 255 }, Color{ 140, 134, 124, 255 }, Color{ 120, 114, 104, 255 });
				};
				if (edgeZ(X, Y + 0.51f) < top - 0.01f) rail(X - 0.5f, Y + 0.5f - kT, X + 0.5f, Y + 0.5f);
				if (edgeZ(X + 0.51f, Y) < top - 0.01f) rail(X + 0.5f - kT, Y - 0.5f, X + 0.5f, Y + 0.5f);
				if (edgeZ(X, Y - 0.51f) < top - 0.01f) rail(X - 0.5f, Y - 0.5f, X + 0.5f, Y - 0.5f + kT);
				if (edgeZ(X - 0.51f, Y) < top - 0.01f) rail(X - 0.5f, Y - 0.5f, X - 0.5f + kT, Y + 0.5f);
			}
		}
	gfx::ResetMod();
}

void Terrain::DrawProps(const Camera2D& cam, int screenW, int screenH, Vector2 playerTile, float time, bool fog)
{
	int x0, y0, x1, y1;
	VisibleTiles(cam, screenW, screenH, x0, y0, x1, y1);
	if (hgt::Any()) { x1 += 4; y1 += 4; }   // 높은 칸의 소품은 화면에서 위로 올라간다

	const float marginX = 140.f / cam.zoom, marginTop = 60.f / cam.zoom, marginBottom = 260.f / cam.zoom;
	const Vector2 tl = GetScreenToWorld2D(Vector2{ 0, 0 }, cam);
	const Vector2 br = GetScreenToWorld2D(Vector2{ (float)screenW, (float)screenH }, cam);
	const float left = std::min(tl.x, br.x) - marginX, right = std::max(tl.x, br.x) + marginX;
	const float top = std::min(tl.y, br.y) - marginTop, bottom = std::max(tl.y, br.y) + marginBottom;

	auto onScreen = [&](int x, int y)
	{
		const Vector2 p = gfx::IsoPx(static_cast<float>(x), static_cast<float>(y));
		return p.x >= left && p.x <= right && p.y >= top && p.y <= bottom;
	};

	for (int y = y0; y <= y1; ++y)
	{
		for (int x = x0; x <= x1; ++x)
		{
			if (!onScreen(x, y))
				continue;
			const char cell = WorldData::TownCell(x, y);
			if (cell != '\0')
			{
				if (cell == '.' || cell == '=' || cell == ',' || cell == '*' || cell == 'N' || cell == 'B' || cell == 'H'
					|| cell == 'R' || cell == ':' || cell == 's' || cell == 'g' || cell == '^' || cell == 'v' || cell == '<' || cell == '>')
					continue;
				gfx::SetTint(FogTint(static_cast<float>(x), static_cast<float>(y), playerTile, fog));
				gfx::SetLift(hgt::LevelZ(x, y) * gfx::kUnitZ);
				props::DrawTownCell(x, y, cell, time);
				gfx::SetLift(0.f);
				continue;
			}
			if (!WorldData::LegacyPatternBlocked(x, y))
				continue;
			const int region = WorldData::RegionIdAt(x, y);
			const auto* row = WorldData::FindRegion(region);
			gfx::SetTint(FogTint(static_cast<float>(x), static_cast<float>(y), playerTile, fog));
			props::DrawFieldObstacle(x, y, row ? row->biome : WorldData::Biome::eMeadow, time);
		}
	}

	// 마을 건물(여러 칸 — 사각형이 화면에 걸치면 그린다)
	for (const auto& b : WorldData::kBuildings)
	{
		if (b.x1 < x0 || b.x0 > x1 || b.y1 < y0 || b.y0 > y1)
			continue;
		const float cx = (b.x0 + b.x1) * 0.5f, cy = (b.y0 + b.y1) * 0.5f;
		gfx::SetTint(FogTint(cx, cy, playerTile, fog));
		gfx::SetLift(static_cast<float>(b.level) * hgt::kLevelZ * gfx::kUnitZ);
		props::DrawBuilding(b, time);
		gfx::SetLift(0.f);
	}
	gfx::ResetMod();
}
