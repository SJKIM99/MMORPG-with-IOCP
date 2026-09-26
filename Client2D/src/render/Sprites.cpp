#include "render/Sprites.h"

#include "rlgl.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <unordered_map>

namespace sprites
{
	namespace
	{
		// 셰이더 소스는 ASCII만(일부 드라이버가 비ASCII 주석에서 컴파일에 실패한다)
		constexpr const char* kFs = R"GLSL(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float uAlphaTest;
uniform vec3 uFlashPal[8];
void main()
{
    vec2 tc = fragTexCoord;
    float fl = floor(tc.x * 0.5 + 0.0001);
    float fc = floor(tc.y * 0.5 + 0.0001);
    tc -= vec2(fl, fc) * 2.0;
    vec4 texel = texture(texture0, tc);
    bool cut = fc > 7.5;
    float pal = cut ? fc - 8.0 : fc;
    if (cut && texel.a < uAlphaTest) discard;
    vec4 c = texel * fragColor * colDiffuse;
    if (fl > 0.5)
    {
        int idx = int(clamp(pal, 0.0, 7.0));
        c.rgb = mix(c.rgb, uFlashPal[idx], min(fl / 16.0, 1.0));
    }
    finalColor = c;
}
)GLSL";

		std::unordered_map<std::string, Sprite> g_sprites;
		std::deque<Texture2D> g_pages;   // 주소가 바뀌지 않게(Sprite가 포인터를 든다)
		Shader g_shader{};
		int g_locAlpha = -1;
		bool g_active = false;
		bool g_ready = false;

		bool LoadSet(const std::string& dir, const char* stem)
		{
			const std::string list = dir + "/" + stem + ".txt";
			if (!FileExists(list.c_str()))
				return false;
			char* text = LoadFileText(list.c_str());
			if (!text)
				return false;
			// 쪽(page) 번호 → 이 집합의 텍스처
			std::unordered_map<int, const Texture2D*> pages;
			int count = 0;
			for (char* line = std::strtok(text, "\r\n"); line; line = std::strtok(nullptr, "\r\n"))
			{
				if (line[0] == '#' || line[0] == '\0')
					continue;
				char name[96];
				int page = 0;
				float x, y, w, h, ax, ay, ref;
				if (std::sscanf(line, "%95s %d %f %f %f %f %f %f %f", name, &page, &x, &y, &w, &h, &ax, &ay, &ref) != 9)
					continue;
				auto it = pages.find(page);
				if (it == pages.end())
				{
					const std::string png = dir + "/" + stem + "_" + std::to_string(page) + ".png";
					Texture2D tex = FileExists(png.c_str()) ? LoadTexture(png.c_str()) : Texture2D{};
					if (tex.id == 0)
					{
						pages[page] = nullptr;
						continue;
					}
					GenTextureMipmaps(&tex);
					SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
					// 번쩍임은 텍스처 좌표에 2의 배수를 더해 싣는다 — 셰이더 밖(UI 미리보기)에서도 같은 곳을 읽도록 반복
					SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
					g_pages.push_back(tex);
					it = pages.emplace(page, &g_pages.back()).first;
				}
				if (!it->second)
					continue;
				Sprite s;
				s.tex = it->second;
				s.src = Rectangle{ x, y, w, h };
				s.anchor = Vector2{ ax, ay };
				s.ref = ref > 1.f ? ref : h;
				g_sprites[name] = s;
				++count;
			}
			UnloadFileText(text);
			TraceLog(LOG_INFO, "SPRITES: %s — %d sprites, %d pages", stem, count, static_cast<int>(pages.size()));
			return count > 0;
		}
	}

	bool Init(const std::string& dir)
	{
		Shutdown();
		const bool a = LoadSet(dir, "actors");
		const bool b = LoadSet(dir, "props");
		LoadSet(dir, "motion");   // 동작 영상에서 뽑은 프레임(Phase 1-D, tools/assetgen/motion.py) — 없으면 걷기 두 장으로
		g_shader = LoadShaderFromMemory(nullptr, kFs);
		if (g_shader.id == 0 || g_shader.id == rlGetShaderIdDefault())
		{
			TraceLog(LOG_WARNING, "SPRITES: world shader failed — sprites draw without alpha test");
			g_shader = Shader{};
		}
		else
		{
			g_locAlpha = GetShaderLocation(g_shader, "uAlphaTest");
			int locPal = GetShaderLocation(g_shader, "uFlashPal");
			if (locPal < 0) locPal = GetShaderLocation(g_shader, "uFlashPal[0]");
			if (locPal >= 0)
				SetShaderValueV(g_shader, locPal, gfx::FlashPalette(), SHADER_UNIFORM_VEC3, gfx::kFlashPaletteSize);
			const float zero = 0.f;
			SetShaderValue(g_shader, g_locAlpha, &zero, SHADER_UNIFORM_FLOAT);
		}
		g_ready = a || b;
		return g_ready;
	}

	void Shutdown()
	{
		for (auto& t : g_pages)
			if (t.id != 0) UnloadTexture(t);
		g_pages.clear();
		g_sprites.clear();
		if (g_shader.id != 0) UnloadShader(g_shader);
		g_shader = Shader{};
		g_ready = false;
		g_active = false;
	}

	bool Ready() { return g_ready; }

	const Sprite* Find(std::string_view name)
	{
		if (g_sprites.empty()) return nullptr;
		auto it = g_sprites.find(std::string(name));
		return it == g_sprites.end() ? nullptr : &it->second;
	}

	void Draw(const Sprite& s, Vector2 base, float worldH, const gfx::SpriteXform& xf)
	{
		if (!s.tex) return;
		gfx::SpriteMesh(*s.tex, s.src, s.anchor, base, worldH / s.ref, xf);
	}

	void DrawFit(const Sprite& s, Rectangle dst, Color tint)
	{
		if (!s.tex) return;
		const float k = std::min(dst.width / s.src.width, dst.height / s.src.height);
		const float w = s.src.width * k, h = s.src.height * k;
		DrawTexturePro(*s.tex, s.src, Rectangle{ dst.x + (dst.width - w) * 0.5f, dst.y + dst.height - h, w, h }, Vector2{ 0, 0 }, 0.f, tint);
	}

	void BeginWorld(float alphaTest)
	{
		if (g_shader.id == 0) return;
		rlDrawRenderBatchActive();
		BeginShaderMode(g_shader);
		SetShaderValue(g_shader, g_locAlpha, &alphaTest, SHADER_UNIFORM_FLOAT);
		g_active = true;
	}

	void SetAlphaTest(float alphaTest)
	{
		if (!g_active) return;
		rlDrawRenderBatchActive();   // 이미 쌓인 도형은 예전 값으로 그려야 한다
		SetShaderValue(g_shader, g_locAlpha, &alphaTest, SHADER_UNIFORM_FLOAT);
	}

	void EndWorld()
	{
		if (!g_active) return;
		rlDrawRenderBatchActive();
		EndShaderMode();
		g_active = false;
	}

	bool ShaderActive() { return g_active; }

	bool Suspend()
	{
		if (!g_active) return false;
		rlDrawRenderBatchActive();
		EndShaderMode();
		g_active = false;
		return true;
	}

	void Resume(bool wasOn)
	{
		if (!wasOn || g_shader.id == 0) return;
		rlDrawRenderBatchActive();
		BeginShaderMode(g_shader);   // 알파 테스트 값은 프로그램에 남아 있다
		g_active = true;
	}
}
