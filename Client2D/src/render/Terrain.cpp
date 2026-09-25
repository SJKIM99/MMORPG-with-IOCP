#include "render/Terrain.h"
#include "render/TerrainShader.h"
#include "render/Props.h"
#include "render/Draw.h"
#include "render/Assets.h"
#include "render/EnvArt.h"
#include "net/Shared.h"
#include "core/Util.h"

#include "rlgl.h"
#include <cmath>
#include <cstdio>

namespace
{
	int TownCode(char c)
	{
		switch (c)
		{
		case '.': return 0;  case '=': return 1;  case ',': return 2;  case 'G': return 3;
		case '#': return 4;  case 'O': return 5;  case 'B': return 6;  case 'H': return 7;
		case 'F': return 8;  case 'T': return 9;  case 'L': return 10; case '*': return 11;
		case 'N': return 12; case 'S': return 13;
		default:  return 0;
		}
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

	// 마을 레이아웃 텍스처(R = 칸 종류) — 서버와 같은 WorldData::kTownLayout
	Image img{};
	img.width = WorldData::kTownSize;
	img.height = WorldData::kTownSize;
	img.mipmaps = 1;
	img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	img.data = MemAlloc(static_cast<unsigned int>(img.width * img.height * 4));
	auto* px = static_cast<unsigned char*>(img.data);
	for (int y = 0; y < WorldData::kTownSize; ++y)
		for (int x = 0; x < WorldData::kTownSize; ++x)
		{
			unsigned char* p = px + (y * img.width + x) * 4;
			p[0] = static_cast<unsigned char>(TownCode(WorldData::kTownLayout[y][x]));
			p[1] = p[2] = 0;
			p[3] = 255;
		}
	m_town = LoadTextureFromImage(img);
	UnloadImage(img);
	SetTextureFilter(m_town, TEXTURE_FILTER_POINT);

	const float origin[2] = { static_cast<float>(WorldData::kTownX0), static_cast<float>(WorldData::kTownY0) };
	const float size = static_cast<float>(WorldData::kTownSize);
	SetShaderValue(m_shader, m_locTownOrigin, origin, SHADER_UNIFORM_VEC2);
	SetShaderValue(m_shader, m_locTownSize, &size, SHADER_UNIFORM_FLOAT);
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

	BeginShaderMode(m_shader);
	SetShaderValueTexture(m_shader, m_locTown, m_town);
	SetShaderValueTexture(m_shader, m_locNoise, assets::Get().noise);
	if (ground)
		SetShaderValueTexture(m_shader, m_locGround, *ground);
	DrawRectangle(0, 0, screenW, screenH, WHITE);
	EndShaderMode();
}

void Terrain::DrawProps(const Camera2D& cam, int screenW, int screenH, Vector2 playerTile, float time, bool fog)
{
	// 화면 네 모서리 → 타일 좌표 범위(+ 키 큰 소품을 위한 여유)
	float minU = 1e9f, maxU = -1e9f, minV = 1e9f, maxV = -1e9f;
	const Vector2 corners[4] = { {0, 0}, {(float)screenW, 0}, {0, (float)screenH}, {(float)screenW, (float)screenH} };
	for (const Vector2& c : corners)
	{
		const Vector2 iso = GetScreenToWorld2D(c, cam);
		const Vector2 t = gfx::IsoToTile(iso);
		minU = std::min(minU, t.x); maxU = std::max(maxU, t.x);
		minV = std::min(minV, t.y); maxV = std::max(maxV, t.y);
	}
	const int x0 = std::max(0, static_cast<int>(std::floor(minU)) - 2);
	const int y0 = std::max(0, static_cast<int>(std::floor(minV)) - 2);
	const int x1 = std::min(WorldData::kWorldWidth - 1, static_cast<int>(std::ceil(maxU)) + 6);
	const int y1 = std::min(WorldData::kWorldHeight - 1, static_cast<int>(std::ceil(maxV)) + 6);

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
				if (cell == '.' || cell == '=' || cell == ',' || cell == '*' || cell == 'N' || cell == 'B' || cell == 'H')
					continue;
				gfx::SetTint(FogTint(static_cast<float>(x), static_cast<float>(y), playerTile, fog));
				props::DrawTownCell(x, y, cell, time);
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
		props::DrawBuilding(b, time);
	}
	gfx::ResetMod();
}
