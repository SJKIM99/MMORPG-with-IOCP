#include "render/Post.h"
#include "fx/FxScale.h"
#include "render/Draw.h"
#include "core/Util.h"

#include "rlgl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace post
{
	namespace
	{
		// 셰이더 소스는 ASCII만(일부 드라이버가 비ASCII 주석에서 컴파일에 실패한다)
		constexpr const char* kCompositeFs = R"GLSL(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform sampler2D uBloom;
uniform float uBloomK;
uniform float uBright;
uniform float uHasScene;
uniform vec4 uShock[8];
uniform float uShockW[8];
uniform int uShockN;
uniform vec4 uHeat[4];
uniform int uHeatN;
uniform vec4 uWarp[2];
uniform int uWarpN;
uniform vec3 uRadial;
uniform float uChroma;
uniform float uDesat;
uniform vec4 uDark;
uniform float uAspect;
uniform float uTime;
vec2 toP(vec2 d) { return vec2(d.x * uAspect, d.y); }
vec2 fromP(vec2 d) { return vec2(d.x / uAspect, d.y); }
void main()
{
    vec2 uv = fragTexCoord;
    vec2 off = vec2(0.0);
    for (int i = 0; i < 8; i++)
    {
        if (i >= uShockN) break;
        vec2 d = toP(uv - uShock[i].xy);
        float dist = length(d) + 1e-5;
        float x = (dist - uShock[i].z) / max(uShockW[i], 1e-4);
        float k = exp(-x * x * 2.5) * uShock[i].w;
        off += fromP(d / dist) * k;
    }
    for (int i = 0; i < 4; i++)
    {
        if (i >= uHeatN) break;
        vec2 d = toP(uv - uHeat[i].xy);
        float f = 1.0 - smoothstep(uHeat[i].z * 0.35, uHeat[i].z, length(d));
        off += vec2(sin(uv.y * 140.0 + uTime * 11.0 + float(i) * 3.1), cos(uv.x * 110.0 + uTime * 8.3 + float(i))) * uHeat[i].w * f;
    }
    for (int i = 0; i < 2; i++)
    {
        if (i >= uWarpN) break;
        vec2 d = toP(uv - uWarp[i].xy);
        float dist = length(d);
        float f = 1.0 - smoothstep(0.0, uWarp[i].z, dist);
        off -= fromP(d) * f * f * uWarp[i].w;
    }
    vec2 suv = uv + off;
    vec3 col = vec3(0.0);
    if (uHasScene > 0.5)
    {
        if (uRadial.z > 0.001)
        {
            vec2 dir = suv - uRadial.xy;
            float s = uRadial.z * 0.09;
            for (int k = 0; k < 8; k++) col += texture(texture0, suv - dir * s * float(k) / 7.0).rgb;
            col *= 0.125;
        }
        else
            col = texture(texture0, suv).rgb;
        if (uChroma > 0.0001)
        {
            vec2 cd = (uv - 0.5) * uChroma;
            col.r = texture(texture0, suv + cd).r;
            col.b = texture(texture0, suv - cd).b;
        }
    }
    col += texture(uBloom, uv).rgb * uBloomK * (1.0 - uBright * clamp(col, 0.0, 1.0));
    if (uDesat > 0.0)
    {
        float l = dot(col, vec3(0.299, 0.587, 0.114));
        col = mix(col, vec3(l), uDesat);
    }
    if (uDark.z > 0.0)
    {
        float f = smoothstep(uDark.w * 0.3, uDark.w, length(toP(uv - uDark.xy)));
        col *= 1.0 - uDark.z * (0.5 + 0.5 * f);
    }
    finalColor = vec4(col, 1.0);
}
)GLSL";

		// 9탭 가우스(선형 샘플 5번)
		constexpr const char* kBlurFs = R"GLSL(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 uDir;
void main()
{
    vec3 c = texture(texture0, fragTexCoord).rgb * 0.227027;
    c += texture(texture0, fragTexCoord + uDir * 1.3846154).rgb * 0.3162162;
    c += texture(texture0, fragTexCoord - uDir * 1.3846154).rgb * 0.3162162;
    c += texture(texture0, fragTexCoord + uDir * 3.2307692).rgb * 0.0702703;
    c += texture(texture0, fragTexCoord - uDir * 3.2307692).rgb * 0.0702703;
    finalColor = vec4(c, 1.0);
}
)GLSL";

		struct Shock { Vector2 iso; float r0, r1, width, strength, t, life; };
		struct Heat { Vector2 iso; float r, strength, t, life; };
		struct WarpEv { Vector2 iso; float r, strength, t, life; };
		struct Radial { Vector2 iso; float strength, t, life; };
		struct Scalar { float amount, t, life; };
		struct Dark { Vector2 iso; float amount, attack, hold, release, t; };

		std::vector<Shock> g_shocks;
		std::vector<Heat> g_heats;
		std::vector<WarpEv> g_warps;
		std::vector<Radial> g_radials;
		std::vector<Scalar> g_chromas, g_desats;
		std::vector<Dark> g_darks;

		Quality g_quality = Quality::High;
		bool g_photoSafe = false;
		float g_time = 0.f;
		Stats g_stats;

		Shader g_comp{}, g_blur{};
		bool g_shadersTried = false;
		int L(const char* n) { return GetShaderLocation(g_comp, n); }
		struct Locs
		{
			int bloom = -1, bloomK = -1, bright = -1, hasScene = -1, shock = -1, shockW = -1, shockN = -1, heat = -1, heatN = -1, warp = -1, warpN = -1,
				radial = -1, chroma = -1, desat = -1, dark = -1, aspect = -1, time = -1, blurDir = -1;
		} g_loc;

		RenderTexture2D g_scene{}, g_emis{}, g_blurA{}, g_blurB{}, g_small{}, g_smallB{};
		int g_w = 0, g_h = 0, g_ew = 0, g_eh = 0;

		void Unload(RenderTexture2D& rt)
		{
			if (rt.id != 0) UnloadRenderTexture(rt);
			rt = RenderTexture2D{};
		}

		bool EnsureShaders()
		{
			if (g_shadersTried) return g_comp.id != 0 && g_blur.id != 0;
			g_shadersTried = true;
			g_comp = LoadShaderFromMemory(nullptr, kCompositeFs);
			g_blur = LoadShaderFromMemory(nullptr, kBlurFs);
			if (g_comp.id == 0 || g_comp.id == rlGetShaderIdDefault() || g_blur.id == 0 || g_blur.id == rlGetShaderIdDefault())
			{
				TraceLog(LOG_WARNING, "POST: shader failed - post effects off");
				return false;
			}
			g_loc.bloom = L("uBloom"); g_loc.bloomK = L("uBloomK"); g_loc.bright = L("uBright"); g_loc.hasScene = L("uHasScene");
			g_loc.shock = L("uShock"); g_loc.shockW = L("uShockW"); g_loc.shockN = L("uShockN");
			g_loc.heat = L("uHeat"); g_loc.heatN = L("uHeatN"); g_loc.warp = L("uWarp"); g_loc.warpN = L("uWarpN");
			g_loc.radial = L("uRadial"); g_loc.chroma = L("uChroma"); g_loc.desat = L("uDesat"); g_loc.dark = L("uDark");
			g_loc.aspect = L("uAspect"); g_loc.time = L("uTime");
			g_loc.blurDir = GetShaderLocation(g_blur, "uDir");
			// 배열 uniform은 드라이버에 따라 "이름[0]"으로만 찾힌다
			if (g_loc.shock < 0) g_loc.shock = L("uShock[0]");
			if (g_loc.shockW < 0) g_loc.shockW = L("uShockW[0]");
			if (g_loc.heat < 0) g_loc.heat = L("uHeat[0]");
			if (g_loc.warp < 0) g_loc.warp = L("uWarp[0]");
			return true;
		}

		RenderTexture2D MakeRt(int w, int h)
		{
			RenderTexture2D rt = LoadRenderTexture(std::max(1, w), std::max(1, h));
			SetTextureFilter(rt.texture, TEXTURE_FILTER_BILINEAR);
			SetTextureWrap(rt.texture, TEXTURE_WRAP_CLAMP);
			return rt;
		}

		void EnsureTargets(int w, int h, int div)
		{
			if (g_w != w || g_h != h || g_scene.id == 0)
			{
				Unload(g_scene);
				g_scene = MakeRt(w, h);
				g_w = w;
				g_h = h;
				g_ew = 0;
			}
			const int ew = std::max(1, w / div), eh = std::max(1, h / div);
			if (ew != g_ew || eh != g_eh || g_emis.id == 0)
			{
				Unload(g_emis); Unload(g_blurA); Unload(g_blurB); Unload(g_small); Unload(g_smallB);
				g_emis = MakeRt(ew, eh);
				g_blurA = MakeRt(ew, eh);
				g_blurB = MakeRt(ew, eh);
				g_small = MakeRt(ew / 2, eh / 2);
				g_smallB = MakeRt(ew / 2, eh / 2);
				g_ew = ew;
				g_eh = eh;
			}
		}

		// src 전체를 dst에 셰이더로(뒤집힘 없이 — 두 텍스처 모두 렌더 타깃이라 같은 방향)
		void Pass(const RenderTexture2D& src, RenderTexture2D& dst, Vector2 dir)
		{
			BeginTextureMode(dst);
			ClearBackground(BLACK);
			BeginShaderMode(g_blur);
			const float d[2] = { dir.x, dir.y };
			SetShaderValue(g_blur, g_loc.blurDir, d, SHADER_UNIFORM_VEC2);
			// 렌더 타깃끼리 옮길 때는 원본 높이를 음수로 — 위아래가 그대로 유지된다
			DrawTexturePro(src.texture, Rectangle{ 0, 0, (float)src.texture.width, -(float)src.texture.height },
				Rectangle{ 0, 0, (float)dst.texture.width, (float)dst.texture.height }, Vector2{ 0, 0 }, 0.f, WHITE);
			EndShaderMode();
			EndTextureMode();
		}

		float Ease(float u) { u = util::Clamp01(u); return 1.f - (1.f - u) * (1.f - u) * (1.f - u); }
		float Photo(float v) { return g_photoSafe ? v * 0.5f : v; }
	}

	void SetQuality(Quality q) { g_quality = q; }
	Quality GetQuality() { return g_quality; }
	void SetPhotoSafe(bool on) { g_photoSafe = on; }
	bool PhotoSafe() { return g_photoSafe; }
	const Stats& LastStats() { return g_stats; }

	// 시연 무대(기술 창)의 연출은 화면 전체 후처리에 닿지 않는다
	void Shockwave(Vector2 iso, float r0, float r1, float width, float strength, float life)
	{
		if (fxscale::Stage()) return;
		strength *= fxscale::Alpha();
		if (strength <= 0.02f) return;
		if (g_shocks.size() >= 16) g_shocks.erase(g_shocks.begin());
		g_shocks.push_back(Shock{ iso, r0, r1, width, strength, 0.f, std::max(0.05f, life) });
	}
	void HeatHaze(Vector2 iso, float radius, float strength, float life)
	{
		if (fxscale::Stage()) return;
		strength *= fxscale::Alpha();
		if (strength <= 0.02f) return;
		if (g_heats.size() >= 8) g_heats.erase(g_heats.begin());
		g_heats.push_back(Heat{ iso, radius, strength, 0.f, std::max(0.05f, life) });
	}
	void Warp(Vector2 iso, float radius, float strength, float life)
	{
		if (fxscale::Stage()) return;
		strength *= fxscale::Alpha();
		if (strength <= 0.02f) return;
		if (g_warps.size() >= 4) g_warps.erase(g_warps.begin());
		g_warps.push_back(WarpEv{ iso, radius, strength, 0.f, std::max(0.05f, life) });
	}
	// 화면 전체 효과는 내 연출에만(다른 사람의 각성기가 내 화면을 가리지 않게)
	void RadialBlur(Vector2 iso, float strength, float life) { if (fxscale::Mine()) g_radials.push_back(Radial{ iso, strength, 0.f, std::max(0.05f, life) }); }
	void Chroma(float strength, float life) { if (fxscale::Mine()) g_chromas.push_back(Scalar{ strength, 0.f, std::max(0.05f, life) }); }
	void Desaturate(float amount, float life) { if (fxscale::Mine()) g_desats.push_back(Scalar{ amount, 0.f, std::max(0.05f, life) }); }
	void Darken(Vector2 focusIso, float amount, float attack, float hold, float release)
	{
		if (!fxscale::Mine()) return;
		g_darks.push_back(Dark{ focusIso, amount, std::max(0.01f, attack), std::max(0.f, hold), std::max(0.01f, release), 0.f });
	}

	void Update(float realDt, float worldDt)
	{
		g_time += worldDt;
		auto age = [](auto& v, float dt, auto done)
		{
			for (auto& e : v) e.t += dt;
			v.erase(std::remove_if(v.begin(), v.end(), done), v.end());
		};
		age(g_shocks, worldDt, [](const Shock& e) { return e.t >= e.life; });
		age(g_heats, worldDt, [](const Heat& e) { return e.t >= e.life; });
		age(g_warps, worldDt, [](const WarpEv& e) { return e.t >= e.life; });
		age(g_radials, worldDt, [](const Radial& e) { return e.t >= e.life; });
		age(g_chromas, worldDt, [](const Scalar& e) { return e.t >= e.life; });
		age(g_desats, realDt, [](const Scalar& e) { return e.t >= e.life; });
		age(g_darks, realDt, [](const Dark& e) { return e.t >= e.attack + e.hold + e.release; });
	}

	void Clear()
	{
		g_shocks.clear(); g_heats.clear(); g_warps.clear(); g_radials.clear();
		g_chromas.clear(); g_desats.clear(); g_darks.clear();
	}

	void Apply(const Camera2D& cam, int screenW, int screenH, const std::function<void()>& drawEmissive)
	{
		g_stats = Stats{};
		if (g_quality == Quality::Low || screenW <= 0 || screenH <= 0 || !EnsureShaders())
			return;
		const auto t0 = std::chrono::steady_clock::now();
		const int rw = GetRenderWidth() > 0 ? GetRenderWidth() : screenW;
		const int rh = GetRenderHeight() > 0 ? GetRenderHeight() : screenH;
		const int div = g_quality == Quality::High ? 2 : 4;
		EnsureTargets(rw, rh, div);
		const float W = static_cast<float>(screenW), H = static_cast<float>(screenH);
		auto toUv = [&](Vector2 iso)
		{
			const Vector2 s = GetWorldToScreen2D(iso, cam);
			return Vector2{ s.x / W, 1.f - s.y / H };   // 렌더 타깃은 위아래가 뒤집혀 있다
		};
		const float zpx = cam.zoom / H;   // 월드 px → 화면 높이 비율

		// ── 이번 프레임 값 ──
		float shock[8 * 4] = {}, shockW[8] = {}, heat[4 * 4] = {}, warp[2 * 4] = {};
		int ns = 0, nh = 0, nw = 0;
		for (auto it = g_shocks.rbegin(); it != g_shocks.rend() && ns < 8; ++it)
		{
			const float u = it->t / it->life;
			const Vector2 c = toUv(it->iso);
			shock[ns * 4 + 0] = c.x;
			shock[ns * 4 + 1] = c.y;
			shock[ns * 4 + 2] = util::Lerp(it->r0, it->r1, Ease(u)) * zpx;
			shock[ns * 4 + 3] = it->strength * (1.f - u) * (1.f - u) * 0.03f;
			shockW[ns] = it->width * zpx * (0.6f + 0.8f * u);
			++ns;
		}
		for (auto it = g_heats.rbegin(); it != g_heats.rend() && nh < 4; ++it)
		{
			const float u = it->t / it->life;
			const Vector2 c = toUv(it->iso);
			const float env = std::min(1.f, u * 6.f) * (1.f - u);
			heat[nh * 4 + 0] = c.x; heat[nh * 4 + 1] = c.y;
			heat[nh * 4 + 2] = it->r * zpx;
			heat[nh * 4 + 3] = it->strength * env * 0.004f;
			++nh;
		}
		for (auto it = g_warps.rbegin(); it != g_warps.rend() && nw < 2; ++it)
		{
			const float u = it->t / it->life;
			const Vector2 c = toUv(it->iso);
			warp[nw * 4 + 0] = c.x; warp[nw * 4 + 1] = c.y;
			warp[nw * 4 + 2] = it->r * zpx;
			warp[nw * 4 + 3] = it->strength * std::sin(u * util::kPi) * 0.6f;
			++nw;
		}
		float radial[3] = { 0.5f, 0.5f, 0.f };
		for (const auto& r : g_radials)
		{
			const float k = r.strength * (1.f - r.t / r.life);
			if (k > radial[2]) { const Vector2 c = toUv(r.iso); radial[0] = c.x; radial[1] = c.y; radial[2] = Photo(k); }
		}
		float chroma = 0.f, desat = 0.f;
		for (const auto& c : g_chromas) chroma = std::max(chroma, c.amount * (1.f - c.t / c.life));
		for (const auto& d : g_desats)
		{
			const float u = d.t / d.life;
			desat = std::max(desat, d.amount * std::min(1.f, u * 8.f) * (1.f - u * u));
		}
		chroma = Photo(chroma) * 0.012f;
		desat = Photo(desat);
		float dark[4] = { 0.5f, 0.5f, 0.f, 0.5f };
		for (const auto& d : g_darks)
		{
			float k;
			if (d.t < d.attack) k = d.t / d.attack;
			else if (d.t < d.attack + d.hold) k = 1.f;
			else k = 1.f - (d.t - d.attack - d.hold) / d.release;
			k = util::Clamp01(k) * d.amount;
			if (k > dark[2]) { const Vector2 c = toUv(d.iso); dark[0] = c.x; dark[1] = c.y; dark[2] = Photo(k); dark[3] = 0.45f; }
		}
		const bool grab = ns > 0 || nh > 0 || nw > 0 || radial[2] > 0.001f || chroma > 0.f || desat > 0.001f || dark[2] > 0.001f;

		// ── 1) 빛 버퍼(효과만, 반 · ¼ 해상도) → 흐림 ──
		rlDrawRenderBatchActive();
		BeginTextureMode(g_emis);
		ClearBackground(BLACK);
		Camera2D ec = cam;
		const float es = static_cast<float>(g_ew) / W;
		ec.offset = Vector2{ cam.offset.x * es, cam.offset.y * es };
		ec.zoom = cam.zoom * es;
		BeginMode2D(ec);
		gfx::EmissiveOnly(true);
		if (drawEmissive) drawEmissive();
		gfx::EmissiveOnly(false);
		gfx::Additive(false);
		EndMode2D();
		EndTextureMode();
		const Vector2 px{ 1.f / g_ew, 1.f / g_eh };
		Pass(g_emis, g_blurA, Vector2{ px.x, 0.f });
		Pass(g_blurA, g_blurB, Vector2{ 0.f, px.y });
		if (g_quality == Quality::High)
		{
			// 두 번째 단: ¼로 줄여 넓게 한 번 더 → 반 해상도 결과에 더한다(가까이는 또렷, 멀리는 은은)
			const Vector2 spx{ 2.f / g_ew, 2.f / g_eh };
			Pass(g_blurB, g_small, Vector2{ spx.x, 0.f });
			Pass(g_small, g_smallB, Vector2{ 0.f, spx.y });
			BeginTextureMode(g_blurB);
			BeginBlendMode(BLEND_ADDITIVE);
			DrawTexturePro(g_smallB.texture, Rectangle{ 0, 0, (float)g_smallB.texture.width, -(float)g_smallB.texture.height },
				Rectangle{ 0, 0, (float)g_ew, (float)g_eh }, Vector2{ 0, 0 }, 0.f, Color{ 200, 200, 200, 255 });
			EndBlendMode();
			EndTextureMode();
		}
		const float bloomK = 1.25f;
		// 밝은 바닥(gfx::SetBrightGround): 번짐을 밝은 곳일수록 덜 더한다(눈밭이 번짐으로 하얗게 뜨지 않게)
		const float bright = gfx::BrightGround();

		// ── 2) 합성 ──
		if (grab)
		{
			// 화면(MSAA)을 장면 텍스처로 복사 — 여러 표본이 이때 하나로 풀린다
			rlDrawRenderBatchActive();
			rlBindFramebuffer(RL_READ_FRAMEBUFFER, 0);
			rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, g_scene.id);
			rlBlitFramebuffer(0, 0, rw, rh, 0, 0, rw, rh, 0x00004000);   // GL_COLOR_BUFFER_BIT
			rlBindFramebuffer(0x8D40, 0);                               // GL_FRAMEBUFFER
			BeginShaderMode(g_comp);
			const float one = 1.f, aspect = W / H;
			SetShaderValue(g_comp, g_loc.hasScene, &one, SHADER_UNIFORM_FLOAT);
			SetShaderValue(g_comp, g_loc.bloomK, &bloomK, SHADER_UNIFORM_FLOAT);
			if (g_loc.bright >= 0) SetShaderValue(g_comp, g_loc.bright, &bright, SHADER_UNIFORM_FLOAT);
			SetShaderValueTexture(g_comp, g_loc.bloom, g_blurB.texture);
			if (g_loc.shock >= 0) SetShaderValueV(g_comp, g_loc.shock, shock, SHADER_UNIFORM_VEC4, 8);
			if (g_loc.shockW >= 0) SetShaderValueV(g_comp, g_loc.shockW, shockW, SHADER_UNIFORM_FLOAT, 8);
			SetShaderValue(g_comp, g_loc.shockN, &ns, SHADER_UNIFORM_INT);
			if (g_loc.heat >= 0) SetShaderValueV(g_comp, g_loc.heat, heat, SHADER_UNIFORM_VEC4, 4);
			SetShaderValue(g_comp, g_loc.heatN, &nh, SHADER_UNIFORM_INT);
			if (g_loc.warp >= 0) SetShaderValueV(g_comp, g_loc.warp, warp, SHADER_UNIFORM_VEC4, 2);
			SetShaderValue(g_comp, g_loc.warpN, &nw, SHADER_UNIFORM_INT);
			SetShaderValue(g_comp, g_loc.radial, radial, SHADER_UNIFORM_VEC3);
			SetShaderValue(g_comp, g_loc.chroma, &chroma, SHADER_UNIFORM_FLOAT);
			SetShaderValue(g_comp, g_loc.desat, &desat, SHADER_UNIFORM_FLOAT);
			SetShaderValue(g_comp, g_loc.dark, dark, SHADER_UNIFORM_VEC4);
			SetShaderValue(g_comp, g_loc.aspect, &aspect, SHADER_UNIFORM_FLOAT);
			SetShaderValue(g_comp, g_loc.time, &g_time, SHADER_UNIFORM_FLOAT);
			DrawTexturePro(g_scene.texture, Rectangle{ 0, 0, (float)rw, -(float)rh }, Rectangle{ 0, 0, W, H }, Vector2{ 0, 0 }, 0.f, WHITE);
			EndShaderMode();
		}
		else
		{
			// 셰이더 없이 더할 때: 밝은 바닥이면 스크린 혼합(바닥 + 번짐 × (1 − 바닥)) — 합성 셰이더의 식과 비슷하게
			if (bright > 0.3f)
			{
				rlSetBlendFactors(RL_ONE_MINUS_DST_COLOR, RL_ONE, RL_FUNC_ADD);
				BeginBlendMode(BLEND_CUSTOM);
			}
			else
				BeginBlendMode(BLEND_ADDITIVE);
			const unsigned char k = static_cast<unsigned char>(std::min(255.f, 255.f * bloomK * 0.8f));
			DrawTexturePro(g_blurB.texture, Rectangle{ 0, 0, (float)g_ew, -(float)g_eh }, Rectangle{ 0, 0, W, H }, Vector2{ 0, 0 }, 0.f, Color{ k, k, k, 255 });
			EndBlendMode();
		}
		g_stats.shocks = ns;
		g_stats.heats = nh;
		g_stats.grabbed = grab;
		g_stats.bloom = true;
		g_stats.bloomMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
	}

	void Shutdown()
	{
		Unload(g_scene); Unload(g_emis); Unload(g_blurA); Unload(g_blurB); Unload(g_small); Unload(g_smallB);
		if (g_comp.id != 0 && g_comp.id != rlGetShaderIdDefault()) UnloadShader(g_comp);
		if (g_blur.id != 0 && g_blur.id != rlGetShaderIdDefault()) UnloadShader(g_blur);
		g_comp = Shader{};
		g_blur = Shader{};
		g_shadersTried = false;
		g_w = g_h = g_ew = g_eh = 0;
	}
}
