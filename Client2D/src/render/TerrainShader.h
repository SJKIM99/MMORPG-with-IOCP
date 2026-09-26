#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 바닥 셰이더(GLSL 330). 화면 전체를 덮는 사각형 하나를 그리면서, 픽셀마다
// 화면 좌표 → 아이소 좌표 → 타일 좌표로 되돌려 그 자리의 지형 색을 계산한다.
//   · 지역(=서버 Zone)마다 팔레트/스타일이 다르다(설원/화산/사막...).
//   · 마을은 레이아웃 텍스처(서버와 같은 TownMap 생성물 — R = 칸 종류, G = 층)로 길/광장을 깐다.
//   · 시야 밖(서버 VIEW_RANGE=5, 체비셰프 거리)은 어둡게 — 서버가 실제로 보내 주는
//     범위가 곧 "보이는 범위"라는 걸 화면으로 드러낸다.
// 주의: GLSL 소스는 ASCII만 쓴다(일부 드라이버가 비ASCII 주석에서 컴파일 실패).
// MSVC 문자열 리터럴 한도(16,380B): 원시 문자열 두 조각으로 나눠 이어 붙인다(townGround 앞에서) — 조각마다 한도 안.
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
uniform vec2  uTownSize;
uniform sampler2D uGround;     // painted ground atlas (5x4 cells of 512 + 32 wrap padding)
uniform float uGroundOn;
uniform vec2  uGroundSize;
uniform float uMesh;          // Phase 4-A: 1 = raised town tiles drawn as quads (uv = fragTexCoord = tile coords)
vec2 gDx;                      // screen derivatives of the tile uv (set once in main, uniform control flow)
vec2 gDy;

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

// one cell of the ground atlas, repeated. p = continuous repeat coords, dp = its screen derivatives
vec3 cellTex(int idx, vec2 p, vec2 dpx, vec2 dpy)
{
    vec2 org = vec2(float(idx % 5), float(idx / 5)) * 576.0 + 32.0;
    vec2 g = org + fract(p) * 512.0;
    vec2 dx = dpx * 512.0;
    vec2 dy = dpy * 512.0;
    float m = max(length(dx), length(dy));
    if (m > 20.0) { float k = 20.0 / m; dx *= k; dy *= k; }   // stay inside the 32px padding at far zoom
    return textureGrad(uGround, g / uGroundSize, dx / uGroundSize, dy / uGroundSize).rgb;
}
// painted ground: two rotated scales blended by noise so the repeat never lines up
vec3 groundTex(int idx, vec2 uv)
{
    const float S1 = 0.2;
    const float S2 = 0.149;
    vec2 p1 = uv * S1;
    vec2 p2 = vec2(uv.y, -uv.x) * S2 + vec2(0.37, 0.61);
    vec3 a = cellTex(idx, p1, gDx * S1, gDy * S1);
    vec3 b = cellTex(idx, p2, vec2(gDx.y, -gDx.x) * S2, vec2(gDy.y, -gDy.x) * S2);
    float m = smoothstep(0.32, 0.68, nz(uv * 0.037 + vec2(0.13, 0.71)));
    vec3 c = mix(a, b, m);
    return c * (0.9 + 0.2 * nz(uv * 0.011 + vec2(0.4, 0.2)));
}

vec3 ground(int style, vec3 A, vec3 B, vec3 C, vec2 uv, vec2 cell, vec2 loc)
{
    if (uGroundOn > 0.5)
    {
        vec3 t = groundTex(style, uv);
        float pk;
        if (style == 4 || style == 14)       // molten cracks breathe
        {
            float hot = smoothstep(0.25, 0.6, t.r - t.b);
            float pulse = sin(uTime * 2.0 + hashCell(floor(uv * 0.45)) * 6.28);
            t += t * hot * pulse * 0.35;
        }
        else if (style == 5)                 // fairy specks twinkle
        {
            float sp = dots(uv, 3.0, 0.08, 0.1, pk);
            float tw = 0.5 + 0.5 * sin(uTime * 3.0 + pk * 30.0);
            t = mix(t, C, sp * (0.3 + 0.5 * tw));
        }
        else if (style == 2)                 // snow glints
        {
            float tw = hashCell(floor(uv * 6.0) + floor(uTime * 1.5));
            float sp = dots(uv, 6.0, 0.05, 0.12, pk);
            t += vec3(0.6, 0.7, 0.8) * sp * step(0.6, tw) * 0.5;
        }
        return t;
    }
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

)GLSL" R"GLSL(
// ---- Phase 4 town surfaces (procedural in both modes) ----
int townCodeAt(ivec2 t)
{
    if (t.x < 0 || t.y < 0 || t.x >= int(uTownSize.x) || t.y >= int(uTownSize.y)) return 0;
    return int(texelFetch(uTown, t, 0).r * 255.0 + 0.5);
}
bool wetCode(int c) { return c == 16 || c == 17; }
// river: sits a little below the banks - the far banks (north / west, facing the camera) show a short earth face,
// dark water under the banks, drifting sky reflection, sparkles and a broken foam line at the waterline
vec3 townWater(vec2 uv, vec2 cell, vec2 loc)
{
    ivec2 t = ivec2(cell - uTownOrigin);
    float dE = !wetCode(townCodeAt(t + ivec2(1, 0)))  ? 0.5 - loc.x : 1.0;
    float dS = !wetCode(townCodeAt(t + ivec2(0, 1)))  ? 0.5 - loc.y : 1.0;
    float dW = !wetCode(townCodeAt(t + ivec2(-1, 0))) ? 0.5 + loc.x : 1.0;
    float dN = !wetCode(townCodeAt(t + ivec2(0, -1))) ? 0.5 + loc.y : 1.0;
    const float BANK = 0.17;
    float far = min(dW, dN);
    float d = min(min(dE, dS), far - BANK);
    float tm = uTime;
    float n1 = nz(uv * 0.23 + vec2(tm * 0.021, tm * 0.007));
    float n2 = nz(uv * 0.61 + vec2(-tm * 0.013, tm * 0.017) + 0.37);
    vec3 col = mix(vec3(0.06, 0.17, 0.22), vec3(0.14, 0.31, 0.35), n1 * 0.6 + n2 * 0.4);
    col += vec3(0.22, 0.28, 0.30) * smoothstep(0.45, 0.85, nz(uv * 0.07 + vec2(tm * 0.004, 0.0))) * 0.35;
    float spark = smoothstep(0.78, 0.95, n1 * 0.5 + nz(uv * 1.9 + vec2(tm * 0.09, -tm * 0.05)) * 0.5);
    col += vec3(0.6, 0.72, 0.72) * spark * 0.35;
    col *= 0.78 + 0.22 * smoothstep(0.0, 0.3, d);
    float wob = 0.02 * sin(tm * 1.7 + (uv.x + uv.y) * 4.0);
    float foam = (1.0 - smoothstep(0.015, 0.075, d + wob)) * (0.55 + 0.45 * nz(uv * 2.3 + vec2(tm * 0.06, 0.0)));
    col = mix(col, vec3(0.80, 0.87, 0.86), foam * 0.7);
    if (far < BANK)
        col = mix(vec3(0.33, 0.27, 0.19), vec3(0.20, 0.16, 0.11), far / BANK) * (0.85 + 0.3 * nz(uv * 4.0));
    return col;
}
// bridge deck: planks across the direction of travel, dark beams at the edges
vec3 townBridge(vec2 uv, vec2 cell, vec2 loc)
{
    ivec2 t = ivec2(cell - uTownOrigin);
    bool alongY = townCodeAt(t + ivec2(1, 0)) == 16 || townCodeAt(t + ivec2(-1, 0)) == 16;
    float a = alongY ? uv.y : uv.x;
    float b = alongY ? loc.x : loc.y;
    float p = fract(a * 3.0);
    float seam = smoothstep(0.0, 0.07, p) * smoothstep(1.0, 0.93, p);
    float id = floor(a * 3.0);
    vec3 wood = mix(vec3(0.40, 0.28, 0.17), vec3(0.55, 0.39, 0.24), hashCell(vec2(id, cell.x + cell.y)));
    wood *= 0.88 + 0.2 * nz(vec2(b * 5.0, a * 0.7) + id * 0.31);
    wood *= 0.55 + 0.45 * seam;
    return mix(wood, vec3(0.28, 0.20, 0.13), smoothstep(0.38, 0.47, abs(b)) * 0.85);
}
// crop field: furrows with sprouts on the ridges
vec3 townCrops(vec2 uv)
{
    float r = fract(uv.y * 3.0);
    float ridge = smoothstep(0.08, 0.5, r) * smoothstep(0.92, 0.5, r);
    vec3 col = mix(vec3(0.27, 0.19, 0.12), vec3(0.45, 0.33, 0.21), ridge);
    float row = floor(uv.y * 3.0);
    float sp = smoothstep(0.52, 0.62, nz(vec2(uv.x * 1.9, row * 0.73))) * ridge;
    return mix(col, mix(vec3(0.30, 0.52, 0.20), vec3(0.62, 0.66, 0.26), hashCell(vec2(row, 7.0))), sp);
}

vec3 townGround(int code, vec2 uv, vec2 cell, vec2 loc, vec3 grass)
{
    if (code == 16) return townWater(uv, cell, loc);
    if (code == 17) return townBridge(uv, cell, loc);
    if (code == 22) return townCrops(uv);
    if (uGroundOn > 0.5)
    {
        if (code == 1 || code == 3) return groundTex(15, uv) * (code == 3 ? 0.86 : 1.0);
        if (code == 2 || code == 8 || code == 10 || code == 12 || code == 13) return groundTex(16, uv);
        if (code == 11) return groundTex(17, uv);
        if (code == 4 || code == 5 || code == 6 || code == 7) return groundTex(19, uv) * 0.92;
        if (code == 14) return groundTex(18, uv);                                            // dirt lane
        if (code == 15) { vec3 sd = mix(groundTex(9, uv), groundTex(18, uv), 0.5); return mix(sd, vec3(dot(sd, vec3(0.333))), 0.3) * vec3(1.06, 1.02, 0.94); }   // yard sand
        if (code == 18) return mix(grass, groundTex(11, uv), 0.55);                          // graveyard grass
        if (code == 19 || code == 24) return groundTex(19, uv) * 0.94;                       // stairs / low wall base
        if (code == 20) return mix(grass, groundTex(18, uv), 0.45);                          // under rocks
        if (code == 21) return grass * 0.8;                                                  // under hedges
        return grass * vec3(1.03, 1.05, 1.0);
    }
    float fine = nz(uv * 2.3);
    if (code == 14 || code == 20) return mix(code == 20 ? grass : vec3(0.52, 0.42, 0.30), vec3(0.50, 0.40, 0.29), code == 20 ? 0.5 : 1.0) * (0.88 + fine * 0.22);
    if (code == 15) return vec3(0.78, 0.68, 0.50) * (0.9 + fine * 0.14) * (0.97 + 0.03 * sin(uv.x * 9.0 + nz(uv * 0.5) * 3.0));
    if (code == 18) return grass * vec3(0.80, 0.88, 0.80) * (0.9 + fine * 0.2);
    if (code == 19 || code == 24) return vec3(0.55, 0.52, 0.48) * (0.9 + fine * 0.15);
    if (code == 21) return grass * 0.78;
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
    vec2 uv;
    if (uMesh > 0.5)
    {
        uv = fragTexCoord;                          // raised tile quad: tile coords come from the vertex
    }
    else
    {
        vec2 screen = vec2(gl_FragCoord.x, uScreen.y - gl_FragCoord.y);
        vec2 d = (screen - uCamOffset) / uZoom;
        float cr = cos(-uRot);
        float sr = sin(-uRot);
        vec2 iso = vec2(d.x * cr - d.y * sr, d.x * sr + d.y * cr) + uCamTarget;
        float a = iso.x / 32.0;
        float b = iso.y / 16.0;
        uv = vec2((a + b) * 0.5, (b - a) * 0.5);    // continuous tile coords (centers at integers)
    }
    vec2 cell = floor(uv + 0.5);
    vec2 loc = uv - cell;                           // [-0.5, 0.5]
    gDx = dFdx(uv);
    gDy = dFdy(uv);

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
    float distOut = max(abs(uv.x - tc.x) - uTownSize.x * 0.5, abs(uv.y - tc.y) - uTownSize.y * 0.5);
    if (distOut > -0.5 && distOut < 140.0)
    {
        float fade = 1.0 - smoothstep(60.0, 140.0, distOut);
        float road = max((1.0 - smoothstep(1.6, 2.4, hx)) * step(abs(uv.y - tc.y), abs(uv.x - tc.x)),
                         (1.0 - smoothstep(1.6, 2.4, vx)) * step(abs(uv.x - tc.x), abs(uv.y - tc.y)));
        vec3 dirt = uGroundOn > 0.5 ? groundTex(18, uv) : vec3(0.55, 0.45, 0.33) * (0.9 + nz(uv * 1.3) * 0.2);
        col = mix(col, dirt, road * fade * 0.9);
    }

    // town
    vec2 tl = cell - uTownOrigin;
    if (tl.x >= 0.0 && tl.y >= 0.0 && tl.x < uTownSize.x && tl.y < uTownSize.y)
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

    if (uMesh > 0.5) col *= fragColor.rgb;          // face shading / tint from the vertex
    finalColor = vec4(col, 1.0);
}
)GLSL";
}
