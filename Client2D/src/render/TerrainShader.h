#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 바닥 셰이더(GLSL 330). 화면 전체를 덮는 사각형 하나를 그리면서, 픽셀마다
// 화면 좌표 → 아이소 좌표 → 타일 좌표로 되돌려 그 자리의 지형 색을 계산한다.
//   · 지역(=서버 Zone)마다 팔레트/스타일이 다르다(설원/화산/사막...).
//   · 마을은 44x44 레이아웃 텍스처(서버와 같은 WorldData::kTownLayout)로 길/광장을 깐다.
//   · 시야 밖(서버 VIEW_RANGE=5, 체비셰프 거리)은 어둡게 — 서버가 실제로 보내 주는
//     범위가 곧 "보이는 범위"라는 걸 화면으로 드러낸다.
// 주의: GLSL 소스는 ASCII만 쓴다(일부 드라이버가 비ASCII 주석에서 컴파일 실패).
// ─────────────────────────────────────────────────────────────────────────────
namespace shaders
{
	inline constexpr const char* kTerrainFs = R"GLSL(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform vec2  uScreen;
uniform vec2  uCamTarget;
uniform vec2  uCamOffset;
uniform float uZoom;
uniform float uRot;
uniform float uTime;
uniform vec2  uPlayer;
uniform float uFog;
uniform vec2 uFogRange;
uniform float uGrid;
uniform vec4  uPalA[16];   // rgb = base, a = style
uniform vec3  uPalB[16];
uniform vec3  uPalC[16];
uniform sampler2D uTown;
uniform sampler2D uNoise;
uniform vec2  uTownOrigin;
uniform float uTownSize;

float nz(vec2 p) { return texture(uNoise, p).r; }
float fbm(vec2 p)
{
    return nz(p * 0.031) * 0.55 + nz(p * 0.113 + vec2(0.37, 0.11)) * 0.3 + nz(p * 0.47 + vec2(0.71, 0.53)) * 0.15;
}
float hashCell(vec2 c)
{
    ivec2 ip = ivec2(mod(c, 256.0));
    return texelFetch(uNoise, ip, 0).g;
}
vec2 hash2(vec2 c)
{
    return vec2(hashCell(c), hashCell(c + vec2(97.0, 41.0)));
}
float cracks(vec2 p)
{
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    float d1 = 8.0;
    float d2 = 8.0;
    for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++)
    {
        vec2 g = vec2(float(i), float(j));
        vec2 r = g + hash2(ip + g) * 0.85 + 0.075 - fp;
        float d = dot(r, r);
        if (d < d1) { d2 = d1; d1 = d; }
        else if (d < d2) { d2 = d; }
    }
    return sqrt(d2) - sqrt(d1);
}
bool legacyBlocked(vec2 cell)
{
    int x = int(cell.x);
    int y = int(cell.y);
    int a = (x / 3 + y / 3) % 3;
    if (a != 2) return false;
    return ((x / 2 + y / 2) % 3) == 0;
}
float dots(vec2 uv, float scale, float density, float radius, out float pick)
{
    vec2 p = uv * scale;
    vec2 c = floor(p);
    vec2 f = fract(p);
    float h = hashCell(c + vec2(13.0, 7.0));
    pick = hashCell(c + vec2(3.0, 71.0));
    if (h > density) return 0.0;
    vec2 o = hash2(c + vec2(5.0, 19.0)) * 0.6 + 0.2;
    float d = length(f - o);
    return 1.0 - smoothstep(radius * 0.6, radius, d);
}

vec3 ground(int style, vec3 A, vec3 B, vec3 C, vec2 uv, vec2 cell, vec2 loc)
{
    float n = fbm(uv);
    float fine = nz(uv * 1.9);
    vec3 col = mix(A, B, smoothstep(0.3, 0.75, n));
    float pick;
    if (style == 0)        // meadow
    {
        col *= 0.92 + fine * 0.16;
        float fl = dots(uv, 2.3, 0.07, 0.09, pick);
        vec3 flower = pick < 0.33 ? C : (pick < 0.66 ? vec3(0.97, 0.93, 0.55) : vec3(0.96, 0.62, 0.75));
        col = mix(col, flower, fl);
    }
    else if (style == 1)   // forest
    {
        float litter = smoothstep(0.55, 0.7, nz(uv * 0.19 + 0.3));
        col = mix(col, C, litter * 0.75);
        col *= 0.88 + fine * 0.2;
    }
    else if (style == 2)   // snow
    {
        col = mix(A, B, smoothstep(0.35, 0.8, n) * 0.8);
        col *= 0.95 + fine * 0.08;
        float tw = hashCell(floor(uv * 6.0) + floor(uTime * 1.5));
        float sp = dots(uv, 6.0, 0.05, 0.12, pick);
        col += vec3(0.6, 0.7, 0.8) * sp * step(0.6, tw) * 0.6;
    }
    else if (style == 3)   // canyon strata
    {
        float s = fract((uv.x + uv.y) * 0.09 + n * 1.7);
        col = mix(A, B, step(0.5, s) * 0.7 + fine * 0.15);
        float cr = cracks(uv * 0.55);
        col = mix(C, col, smoothstep(0.02, 0.07, cr));
    }
    else if (style == 4 || style == 14)   // volcanic / hell
    {
        col *= 0.85 + fine * 0.25;
        float cr = cracks(uv * (style == 4 ? 0.45 : 0.6));
        float pulse = 0.75 + 0.25 * sin(uTime * 2.0 + hashCell(floor(uv * 0.45)) * 6.28);
        float glow = 1.0 - smoothstep(0.0, 0.09, cr);
        col = mix(col, C * pulse * 1.3, glow);
        col += C * (1.0 - smoothstep(0.0, 0.25, cr)) * 0.18 * pulse;
    }
    else if (style == 5)   // fairy forest
    {
        col *= 0.9 + fine * 0.18;
        float sp = dots(uv, 3.0, 0.08, 0.1, pick);
        float tw = 0.5 + 0.5 * sin(uTime * 3.0 + pick * 30.0);
        col = mix(col, C, sp * (0.4 + 0.6 * tw));
    }
    else if (style == 6)   // ruins: paving
    {
        vec2 f = abs(loc);
        float edge = smoothstep(0.43, 0.49, max(f.x, f.y));
        float broken = step(0.8, hashCell(cell + vec2(11.0, 3.0)));
        vec3 stone = mix(A, B, hashCell(cell) * 0.6 + fine * 0.3);
        col = mix(stone, C, broken * smoothstep(0.4, 0.7, n + fine * 0.3));
        col *= 1.0 - edge * 0.35;
    }
    else if (style == 7)   // orc fort dirt
    {
        col *= 0.85 + fine * 0.3;
        float tuft = dots(uv, 2.0, 0.12, 0.14, pick);
        col = mix(col, C, tuft * 0.6);
    }
    else if (style == 8)   // swamp
    {
        col *= 0.88 + fine * 0.2;
        float w = smoothstep(0.56, 0.6, nz(uv * 0.083 + 0.2));
        vec3 water = C * (0.85 + 0.15 * sin(uTime * 1.5 + (uv.x - uv.y) * 3.0 + fine * 4.0));
        col = mix(col, water, w);
        float pad = dots(uv, 1.6, 0.25, 0.16, pick) * w;
        col = mix(col, vec3(0.28, 0.45, 0.18), pad);
    }
    else if (style == 9)   // desert dunes
    {
        float r = sin((uv.x * 0.9 - uv.y * 0.4) * 5.0 + n * 9.0);
        col = mix(A, B, 0.5 + 0.5 * r * 0.6);
        col = mix(col, C, smoothstep(0.62, 0.8, n) * 0.5);
        col *= 0.96 + fine * 0.08;
    }
    else if (style == 10)  // wasteland cracks
    {
        col *= 0.9 + fine * 0.18;
        float cr = cracks(uv * 0.9);
        col = mix(C, col, smoothstep(0.01, 0.05, cr));
    }
    else if (style == 11)  // graveyard
    {
        col *= 0.85 + fine * 0.25;
        float bone = dots(uv, 4.0, 0.03, 0.1, pick);
        col = mix(col, C, bone * 0.8);
    }
    else if (style == 12)  // beach
    {
        float wet = smoothstep(0.5, 0.65, n);
        col = mix(A, B, wet);
        float pool = smoothstep(0.66, 0.7, nz(uv * 0.07 + 0.5));
        vec3 sea = C * (0.9 + 0.1 * sin(uTime * 2.0 + uv.x * 2.0));
        col = mix(col, sea, pool);
        float shell = dots(uv, 3.0, 0.05, 0.08, pick);
        col = mix(col, vec3(0.98, 0.9, 0.85), shell * (1.0 - pool));
    }
    else if (style == 13)  // ant hill
    {
        col *= 0.85 + fine * 0.3;
        float hole = dots(uv, 0.9, 0.18, 0.2, pick);
        col = mix(col, C, hole * 0.85);
    }
    return col;
}

vec3 regionColor(int rid, vec2 uv, vec2 cell, vec2 loc)
{
    vec4 A = uPalA[rid];
    return ground(int(A.a + 0.5), A.rgb, uPalB[rid], uPalC[rid], uv, cell, loc);
}

int regionAt(vec2 p)
{
    vec2 r = clamp(floor(p / 500.0), vec2(0.0), vec2(3.0));
    return int(r.y) * 4 + int(r.x);
}

vec3 townGround(int code, vec2 uv, vec2 cell, vec2 loc, vec3 grass)
{
    float fine = nz(uv * 2.3);
    if (code == 1 || code == 3)       // road / gate: cobbles
    {
        vec2 p = uv * 3.0;
        vec2 c = floor(p);
        vec2 f = fract(p) - 0.5;
        float e = smoothstep(0.34, 0.47, max(abs(f.x), abs(f.y)));
        vec3 stone = mix(vec3(0.55, 0.50, 0.44), vec3(0.66, 0.61, 0.53), hashCell(c + 7.0));
        if (code == 3) stone *= 0.85;
        return stone * (1.0 - e * 0.45) * (0.92 + fine * 0.12);
    }
    if (code == 2 || code == 8 || code == 10 || code == 12 || code == 13)  // plaza slabs
    {
        vec2 p = uv * 2.0;
        vec2 c = floor(p);
        vec2 f = fract(p) - 0.5;
        float e = smoothstep(0.42, 0.49, max(abs(f.x), abs(f.y)));
        vec3 slab = mix(vec3(0.74, 0.71, 0.64), vec3(0.83, 0.80, 0.72), hashCell(c + 3.0));
        // subtle bevel light from top-left
        slab *= 1.0 + (f.x + f.y) * -0.06;
        return slab * (1.0 - e * 0.35) * (0.95 + fine * 0.07);
    }
    if (code == 11)                   // flower bed
    {
        float pick;
        vec3 soil = vec3(0.36, 0.25, 0.17) * (0.85 + fine * 0.3);
        float fl = dots(uv, 5.0, 0.55, 0.14, pick);
        vec3 flower = pick < 0.25 ? vec3(0.95, 0.3, 0.35) : (pick < 0.5 ? vec3(1.0, 0.85, 0.3) : (pick < 0.75 ? vec3(0.75, 0.45, 0.95) : vec3(1.0, 1.0, 1.0)));
        vec3 leaf = vec3(0.25, 0.5, 0.2);
        float lf = dots(uv + 0.37, 5.0, 0.8, 0.2, pick);
        soil = mix(soil, leaf, lf);
        return mix(soil, flower, fl);
    }
    if (code == 4 || code == 5 || code == 6 || code == 7)   // under walls/buildings
    {
        return vec3(0.46, 0.43, 0.40) * (0.9 + fine * 0.15);
    }
    // grass (., T)
    return grass * vec3(1.05, 1.08, 1.0) * (0.93 + fine * 0.12);
}

void main()
{
    vec2 screen = vec2(gl_FragCoord.x, uScreen.y - gl_FragCoord.y);
    vec2 d = (screen - uCamOffset) / uZoom;
    float cr = cos(-uRot);
    float sr = sin(-uRot);
    vec2 iso = vec2(d.x * cr - d.y * sr, d.x * sr + d.y * cr) + uCamTarget;

    float a = iso.x / 32.0;
    float b = iso.y / 16.0;
    vec2 uv = vec2((a + b) * 0.5, (b - a) * 0.5);   // continuous tile coords (centers at integers)
    vec2 cell = floor(uv + 0.5);
    vec2 loc = uv - cell;                           // [-0.5, 0.5]

    if (cell.x < 0.0 || cell.y < 0.0 || cell.x > 1999.0 || cell.y > 1999.0)
    {
        finalColor = vec4(0.02, 0.025, 0.04, 1.0);
        return;
    }

    // region with wavy borders
    vec2 warp = (vec2(nz(uv * 0.021), nz(uv * 0.021 + 0.5)) - 0.5) * 9.0;
    vec2 wp = uv + warp;
    int rid = regionAt(wp);
    vec3 col = regionColor(rid, uv, cell, loc);

    // soft blend across region borders
    vec2 m = mod(wp, 500.0);
    float bx = min(m.x, 500.0 - m.x);
    float by = min(m.y, 500.0 - m.y);
    if (bx < 2.0 && wp.x > 2.0 && wp.x < 1998.0)
    {
        int other = regionAt(wp + vec2(m.x < 250.0 ? -2.5 : 2.5, 0.0));
        if (other != rid) col = mix(regionColor(other, uv, cell, loc), col, 0.5 + 0.5 * smoothstep(0.0, 2.0, bx));
    }
    if (by < 2.0 && wp.y > 2.0 && wp.y < 1998.0)
    {
        int other = regionAt(wp + vec2(0.0, m.y < 250.0 ? -2.5 : 2.5));
        if (other != rid) col = mix(regionColor(other, uv, cell, loc), col, 0.5 + 0.5 * smoothstep(0.0, 2.0, by));
    }

    // roads leading out of town gates
    vec2 tc = uTownOrigin + uTownSize * 0.5;
    float roadN = nz(uv * 0.21) - 0.5;
    float hx = abs(uv.y - (tc.y - 0.5)) + roadN * 1.2;
    float vx = abs(uv.x - (tc.x - 0.5)) + roadN * 1.2;
    float distOut = max(abs(uv.x - tc.x), abs(uv.y - tc.y)) - uTownSize * 0.5;
    if (distOut > -0.5 && distOut < 140.0)
    {
        float fade = 1.0 - smoothstep(60.0, 140.0, distOut);
        float road = max((1.0 - smoothstep(1.6, 2.4, hx)) * step(abs(uv.y - tc.y), abs(uv.x - tc.x)),
                         (1.0 - smoothstep(1.6, 2.4, vx)) * step(abs(uv.x - tc.x), abs(uv.y - tc.y)));
        vec3 dirt = vec3(0.55, 0.45, 0.33) * (0.9 + nz(uv * 1.3) * 0.2);
        col = mix(col, dirt, road * fade * 0.9);
    }

    // town
    vec2 tl = cell - uTownOrigin;
    if (tl.x >= 0.0 && tl.y >= 0.0 && tl.x < uTownSize && tl.y < uTownSize)
    {
        int code = int(texelFetch(uTown, ivec2(tl), 0).r * 255.0 + 0.5);
        col = townGround(code, uv, cell, loc, col);
    }
    else if (legacyBlocked(cell))
    {
        // contact shadow under field obstacles
        float r = length(loc * vec2(1.0, 1.0));
        col *= 0.62 + 0.38 * smoothstep(0.12, 0.5, r);
    }

    // faint tile grid near the player
    if (uGrid > 0.0)
    {
        vec2 f = abs(loc);
        float g = smoothstep(0.47, 0.5, max(f.x, f.y));
        float near = 1.0 - smoothstep(4.0, 7.0, max(abs(uv.x - uPlayer.x), abs(uv.y - uPlayer.y)));
        col *= 1.0 - g * uGrid * near;
    }

    // view-range fog (server VIEW_RANGE, chebyshev)
    if (uFog > 0.0)
    {
        float dist = max(abs(uv.x - uPlayer.x), abs(uv.y - uPlayer.y));
        float f = smoothstep(uFogRange.x, uFogRange.y, dist) * uFog * 0.9;
        float lum = dot(col, vec3(0.3, 0.59, 0.11));
        vec3 dim = mix(vec3(lum), col, 0.55) * 0.56 + vec3(0.02, 0.025, 0.05);
        col = mix(col, dim, f);
    }

    finalColor = vec4(col, 1.0);
}
)GLSL";
}
