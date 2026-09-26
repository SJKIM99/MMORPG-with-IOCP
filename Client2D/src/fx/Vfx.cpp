#include "fx/Vfx.h"
#include "fx/FxScale.h"
#include "render/Draw.h"
#include "render/Sprites.h"
#include "core/Util.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace vfx
{
	namespace
	{
		// ── 셰이더(ASCII만 — 일부 드라이버가 비ASCII 주석에서 컴파일에 실패한다) ──
		// 좌표: p = 네모 안 (-1..1), x 오른쪽 · y 위. 나이 uAge 0..1. 결과 = (색, 알파) — 더하기 혼합이면 색 × 알파가 더해진다.
		constexpr const char* kHead = R"GLSL(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform int uKind;
uniform float uAge;
uniform float uSeed;
uniform float uTime;
uniform vec3 uHot;
uniform vec3 uMid;
uniform vec3 uCool;
uniform float uInt;
uniform float uAspect;
uniform vec2 uRot;
const float PI = 3.14159265;
const float TAU = 6.2831853;
float h21(vec2 p)
{
    p = fract(p * vec2(234.34, 435.345));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}
float vnoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = h21(i);
    float b = h21(i + vec2(1.0, 0.0));
    float c = h21(i + vec2(0.0, 1.0));
    float d = h21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; i++)
    {
        v += a * vnoise(p);
        p = p * 2.03 + vec2(11.7, 5.3);
        a *= 0.5;
    }
    return v;
}
float fbm3(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 3; i++)
    {
        v += a * vnoise(p);
        p = p * 2.07 + vec2(3.1, 17.9);
        a *= 0.5;
    }
    return v * 1.14;
}
vec3 ramp(float t)
{
    t = clamp(t, 0.0, 1.0);
    return t < 0.5 ? mix(uCool, uMid, t * 2.0) : mix(uMid, uHot, t * 2.0 - 1.0);
}
float seg(vec2 p, vec2 a, vec2 b)
{
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}
float fadeOut(float age, float from) { return 1.0 - smoothstep(from, 1.0, age); }
float easeOut(float x) { x = clamp(x, 0.0, 1.0); return 1.0 - (1.0 - x) * (1.0 - x) * (1.0 - x); }
)GLSL";

		// 공중 효과 ①
		constexpr const char* kAirA = R"GLSL(
vec4 kFire(vec2 p, float age)
{
    float t = uTime * 1.3 + uSeed * 7.0;
    float yy = p.y * 0.5 + 0.5;
    float grow = easeOut(age / 0.2);
    float n1 = fbm(vec2(p.x * 2.0, yy * 1.7 - t * 2.3) + uSeed * 3.0);
    float n2 = fbm3(vec2(p.x * 4.6, yy * 3.3 - t * 3.9) + uSeed * 9.0);
    float qx = p.x + (n1 - 0.5) * 0.7 * yy;
    float height = (0.45 + 0.55 * grow) * (0.25 + 0.75 * fadeOut(age, 0.5));
    float w = 0.78 * (1.0 - smoothstep(0.0, height, yy) * 0.8) * (0.7 + 0.3 * grow);
    float body = smoothstep(w, w * 0.2, abs(qx));
    float tongues = smoothstep(height, height * 0.25, yy + (n2 - 0.45) * 0.8 * yy);
    float bot = smoothstep(0.0, 0.07, yy);
    float f = body * tongues * bot;
    float heat = f * (0.98 - yy / max(height, 0.1) * 0.9 - abs(qx) * 0.8) + (n2 - 0.5) * 0.35;
    float a = clamp(f * 1.5, 0.0, 1.0) * (0.45 + 0.55 * clamp(heat * 1.6, 0.0, 1.0)) * smoothstep(0.0, 0.04, age);
    return vec4(ramp(heat), a);
}
vec4 kExplosion(vec2 p, float age)
{
    float r = length(p);
    float grow = easeOut(age / 0.3);
    float R = mix(0.1, 0.9, grow);
    float ang = atan(p.y, p.x);
    float n = fbm(vec2(ang * 1.9 + uSeed * 4.0, r * 2.6 - age * 2.4) + uSeed * 5.0);
    float n2 = fbm(p * 3.4 + vec2(0.0, -age * 2.0) + uSeed * 11.0);
    float edge = R * (0.62 + 0.62 * n);
    float ball = smoothstep(edge, edge * 0.45, r);
    float billow = 0.55 + 0.9 * n2;
    float heat = (1.0 - r / max(edge, 0.001)) * (1.3 - age * 1.35) * billow - age * 0.35;
    float core = smoothstep(0.38, 0.0, r) * (1.0 - smoothstep(0.0, 0.18, age));
    float a = ball * fadeOut(age, 0.4) * clamp(0.4 + heat * 1.4, 0.0, 1.0) + core;
    vec3 col = ramp(heat) * (0.7 + 0.6 * clamp(heat, 0.0, 1.0)) + vec3(core) * 0.8;
    return vec4(col, clamp(a, 0.0, 1.0));
}
vec4 kSmoke(vec2 p, float age)
{
    float grow = easeOut(age / 0.55);
    float d = 0.0;
    float sd = floor(uSeed * 100.0);
    float lit = 0.0;
    for (int i = 0; i < 6; i++)
    {
        float fi = float(i);
        vec2 o = (vec2(h21(vec2(fi, sd)), h21(vec2(fi + 7.0, sd))) - 0.5) * vec2(0.85, 0.6);
        o.y += age * (0.2 + 0.35 * h21(vec2(fi + 3.0, sd)));
        float rr = (0.3 + 0.18 * h21(vec2(fi + 11.0, sd))) * (0.55 + 0.75 * grow);
        vec2 dv = p - o * (0.4 + 0.6 * grow);
        float c = 1.0 - length(dv) / rr;
        if (c > d) { d = c; lit = dv.y / rr; }
    }
    float n = fbm(p * 2.6 + vec2(uSeed * 3.1, -age * 1.3 - uTime * 0.1));
    float dens = smoothstep(-0.25, 0.85, d + (n - 0.5) * 0.9);
    float a = dens * dens * fadeOut(age, 0.25) * smoothstep(0.0, 0.05, age) * 0.7;
    float light = clamp(0.5 + lit * 0.35 + (n - 0.5) * 0.9, 0.0, 1.0);
    vec3 col = mix(uCool, uMid, light);
    col = mix(col, uHot, pow(light, 3.0) * 0.55);
    return vec4(col, a);
}
vec4 kIce(vec2 p, float age)
{
    float r = length(p);
    float ang = atan(p.y, p.x) + uSeed * 6.0;
    float sd = floor(uSeed * 50.0);
    float grow = easeOut(age / 0.12);
    float best = 0.0;
    float shade = 0.0;
    for (int layer = 0; layer < 2; layer++)
    {
        float N = layer == 0 ? 8.0 : 13.0;
        float x = (ang + float(layer) * 0.21) / TAU * N;
        float cell = floor(x);
        float fa = fract(x) - 0.5;
        float len = (layer == 0 ? mix(0.55, 1.0, h21(vec2(cell, sd))) : mix(0.25, 0.55, h21(vec2(cell, sd + 3.0)))) * grow;
        float taper = max(1.0 - r / max(len, 0.001), 0.0);
        float width = taper * (layer == 0 ? 0.5 : 0.35);
        float spike = step(r, len) * smoothstep(width, width * 0.5, abs(fa));
        if (spike > best) { best = spike; shade = (fa > 0.0 ? 0.85 : 0.35) * (0.4 + 0.6 * taper); }
    }
    float shatter = step(age, 0.62) + (1.0 - step(age, 0.62)) * step(0.35 + (age - 0.62) * 2.0, h21(floor(p * 9.0) + sd));
    float core = smoothstep(0.3, 0.0, r) * (1.0 - age);
    float edgeHi = best * step(0.6, shade) * 0.35;
    float a = clamp(best * (0.55 + 0.45 * shade) * shatter + core, 0.0, 1.0) * fadeOut(age, 0.55);
    return vec4(ramp(shade * 0.6 + edgeHi + core), a);
}
vec4 kInk(vec2 p, float age)
{
    float r = length(p);
    float ang = atan(p.y, p.x);
    float grow = easeOut(age / 0.16);
    float n = fbm3(vec2(ang * 1.6 + uSeed * 7.0, uSeed));
    float R = grow * (0.38 + 0.32 * n);
    float blob = smoothstep(R, R - 0.05, r);
    float sp = pow(vnoise(vec2(ang * 5.0 + uSeed * 13.0, uSeed)), 3.0);
    float spike = smoothstep(R + sp * 0.6 * grow, R, r) * step(R - 0.02, r);
    vec2 g = p * 5.5;
    vec2 id = floor(g);
    float dh = h21(id + floor(uSeed * 40.0));
    vec2 c = id + 0.5 + (vec2(h21(id + 3.1), h21(id + 7.7)) - 0.5) * 0.5;
    float drop = step(0.8, dh) * smoothstep(0.2, 0.1, length(g - c)) * step(R * 1.05, r) * step(r, grow * 0.98);
    float a = clamp(blob + spike + drop, 0.0, 1.0) * fadeOut(age, 0.5);
    float shine = smoothstep(0.4, 0.0, length(p - vec2(-0.12, 0.12)) / max(R, 0.01)) * blob;
    vec3 col = mix(uCool, uMid, 0.25 + 0.5 * shine);
    return vec4(col, a * 0.95);
}
vec4 kDust(vec2 p, float age)
{
    float grow = easeOut(age / 0.6);
    float d = 0.0;
    float lit = 0.0;
    float sd = floor(uSeed * 100.0);
    for (int i = 0; i < 7; i++)
    {
        float fi = float(i);
        float side = (h21(vec2(fi, sd + 1.0)) - 0.5) * 2.0;
        vec2 o = vec2(side * (0.2 + 0.6 * grow), -0.55 + 0.25 * h21(vec2(fi + 5.0, sd)) + age * 0.15);
        float rr = (0.2 + 0.14 * h21(vec2(fi + 9.0, sd))) * (0.6 + 0.8 * grow);
        vec2 dv = (p - o) * vec2(0.8, 1.25);
        float c = 1.0 - length(dv) / rr;
        if (c > d) { d = c; lit = dv.y / rr; }
    }
    float n = fbm(p * 3.0 + vec2(uSeed * 5.0, -age * 0.7));
    float dens = smoothstep(0.0, 0.45, d + (n - 0.5) * 0.4);
    float a = dens * (1.0 - age) * smoothstep(0.0, 0.05, age) * 0.8;
    float light = clamp(0.45 + lit * 0.4 + (n - 0.5) * 0.6, 0.0, 1.0);
    vec3 col = mix(uCool, uMid, light);
    col = mix(col, uHot, pow(light, 3.0) * 0.4);
    return vec4(col, a);
}
vec4 kPillar(vec2 p, float age)
{
    float yy = p.y * 0.5 + 0.5;
    float grow = easeOut(age / 0.1);
    float shrink = fadeOut(age, 0.4);
    float w = 0.62 * (0.3 + 0.7 * shrink);
    float x = abs(p.x) / max(w, 0.001);
    float streak = fbm3(vec2(p.x * 6.0, p.y * 1.2 - uTime * 3.4 - uSeed * 5.0));
    float core = exp(-x * x * 5.0);
    float glow = exp(-x * x * 1.3) * 0.35;
    float top = smoothstep(1.0, 0.45, yy);
    float rise = smoothstep(grow * 1.05, grow * 1.05 - 0.15, yy);
    float base = smoothstep(0.18, 0.0, yy) * exp(-abs(p.x) * 2.0) * 0.8;
    float a = (core * (0.45 + 0.75 * streak) + glow * (0.5 + streak) + base) * top * rise;
    return vec4(ramp(core * 0.8 + streak * 0.3 + base * 0.3), clamp(a, 0.0, 1.0) * shrink);
}
)GLSL";

		// 공중 효과 ② + main
		constexpr const char* kAirB = R"GLSL(
vec4 kVortex(vec2 p, float age)
{
    float r = length(p);
    float ang = atan(p.y, p.x);
    float spin = uTime * 5.0 + age * 7.0 + uSeed * 6.0;
    float band = sin(ang * 3.0 + log(r + 0.05) * 7.0 - spin);
    float n = fbm3(vec2(ang * 2.0 + spin * 0.25, r * 4.0));
    float ring = smoothstep(1.0, 0.72, r) * smoothstep(0.06, 0.28, r);
    float s = smoothstep(0.25, 0.95, band * 0.5 + 0.5 + (n - 0.5) * 0.6);
    float life = smoothstep(0.0, 0.12, age) * fadeOut(age, 0.7);
    float a = s * ring * life;
    return vec4(ramp(s * (1.1 - r) + 0.15), a);
}
float boltPath(float y, float s)
{
    return (vnoise(vec2(y * 3.6, s)) - 0.5) * 0.55 + (vnoise(vec2(y * 10.0, s + 7.0)) - 0.5) * 0.2 + (vnoise(vec2(y * 27.0, s + 3.0)) - 0.5) * 0.06;
}
vec4 kBolt(vec2 p, float age)
{
    float s = floor(uSeed * 97.0) + floor(uTime * 30.0) * 1.37;
    float yy = p.y * 0.5 + 0.5;
    float x0 = boltPath(p.y, s) * (0.3 + 0.7 * yy);
    float d = abs(p.x - x0) * uAspect;
    float core = exp(-d * d * 1100.0);
    float glow = exp(-d * 14.0) * 0.35;
    float by = 0.35 - h21(vec2(s, 1.0)) * 0.7;
    float bdir = h21(vec2(s, 2.0)) > 0.5 ? 1.0 : -1.0;
    float t = clamp((by - p.y) / 0.7, 0.0, 1.0);
    float xb = boltPath(by, s) * (0.3 + 0.7 * (by * 0.5 + 0.5)) + bdir * t * 0.5 + (vnoise(vec2(p.y * 13.0, s + 3.0)) - 0.5) * 0.14;
    float db = abs(p.x - xb) * uAspect;
    float br = exp(-db * db * 1600.0) * step(p.y, by) * step(by - 0.7, p.y) * (1.0 - t);
    float flick = 0.7 + 0.3 * h21(vec2(floor(uTime * 45.0), uSeed));
    float strike = step(1.0 - easeOut(age / 0.06) * 1.02, yy);
    float a = clamp(core + glow + br * 0.85, 0.0, 1.0) * flick * fadeOut(age, 0.3) * strike;
    return vec4(ramp(core + br), a);
}
vec4 kSlash(vec2 p, float age)
{
    float r = length(p);
    float ang = atan(p.y, p.x);
    float span = 1.4;
    float u = ang / span * 0.5 + 0.5;
    float head = easeOut(age / 0.25) * 1.25;
    float trail = smoothstep(head - 0.95, head, u) * step(u, head);
    float thick = sin(clamp(u, 0.0, 1.0) * PI) * 0.3 + 0.015;
    float R = 0.8;
    float band = smoothstep(thick, 0.0, abs(r - R + thick * 0.5));
    float soft = exp(-pow((r - R + thick * 0.4) / (thick + 0.05), 2.0)) * 0.35;
    float inside = step(0.0, u) * step(u, 1.0);
    float edge = smoothstep(0.025, 0.0, abs(r - R)) * 0.9;
    float a = (band * 0.8 + soft + edge) * trail * inside * fadeOut(age, 0.35);
    float heat = band * 0.6 + edge + soft * 0.3;
    return vec4(ramp(heat), clamp(a, 0.0, 1.0));
}
vec4 kFlare(vec2 p, float age)
{
    float r = length(p);
    float ang = atan(p.y, p.x) + uSeed * 3.0;
    float env = smoothstep(0.0, 0.06, age) * fadeOut(age, 0.12);
    float core = exp(-r * r * 28.0);
    float rays = pow(abs(cos(ang * 2.0)), 48.0) * exp(-r * 2.6) + pow(abs(cos(ang * 2.0 + PI * 0.25)), 70.0) * exp(-r * 4.5) * 0.6;
    float halo = exp(-r * 3.6) * 0.35;
    float a = clamp(core + rays + halo, 0.0, 1.0) * env;
    return vec4(ramp(core * 1.6 + rays + 0.15), a);
}
vec4 kBeam(vec2 p, float age)
{
    float flow = fbm3(vec2(p.x * uAspect * 0.8 - uTime * 7.0, p.y * 1.5 + uSeed * 9.0));
    float wob = (vnoise(vec2(p.x * uAspect * 0.6 - uTime * 9.0, uSeed * 5.0)) - 0.5) * 0.4;
    float y = abs(p.y - wob);
    float core = exp(-y * y * 30.0);
    float glow = exp(-y * 3.0) * 0.45;
    float ends = smoothstep(1.0, 0.8, abs(p.x));
    float life = smoothstep(0.0, 0.08, age) * fadeOut(age, 0.5);
    float a = clamp(core * (0.55 + 0.9 * flow) + glow * (0.5 + flow), 0.0, 1.0) * ends * life;
    return vec4(ramp(core + flow * 0.35), a);
}
vec4 kGlyph(vec2 p, float age)
{
    float prog = easeOut(age / 0.38) * 4.0;
    float d = 1e3;
    float sd = floor(uSeed * 1000.0);
    for (int k = 0; k < 4; k++)
    {
        float fk = float(k);
        vec2 a = (vec2(floor(h21(vec2(fk * 3.7, sd)) * 3.0), floor(h21(vec2(fk * 5.1 + 1.0, sd)) * 3.0)) - 1.0) * 0.52;
        vec2 b = (vec2(floor(h21(vec2(fk * 7.3 + 2.0, sd)) * 3.0), floor(h21(vec2(fk * 1.9 + 3.0, sd)) * 3.0)) - 1.0) * 0.52;
        if (length(b - a) < 0.1) b = a + vec2(0.0, a.y > 0.0 ? -1.04 : 1.04);
        float f = clamp(prog - fk, 0.0, 1.0);
        if (f <= 0.0) continue;
        d = min(d, seg(p, a, mix(a, b, f)));
    }
    float core = smoothstep(0.075, 0.025, d);
    float glow = exp(-d * 8.0) * 0.55;
    float ringP = clamp(prog - 3.0, 0.0, 1.0);
    float ring = smoothstep(0.035, 0.0, abs(length(p) - 0.92)) * step(fract(atan(p.y, p.x) / TAU + 0.25), ringP) * 0.8;
    float a = clamp(core + glow + ring, 0.0, 1.0) * fadeOut(age, 0.7) * smoothstep(0.0, 0.04, age);
    return vec4(ramp(core + ring * 0.6 + 0.2), a);
}
void main()
{
    vec2 p = vec2(fragTexCoord.x * 2.0 - 1.0, 1.0 - fragTexCoord.y * 2.0);
    vec4 c;
    if (uKind == 0) c = kFire(p, uAge);
    else if (uKind == 1) c = kExplosion(p, uAge);
    else if (uKind == 2) c = kSmoke(p, uAge);
    else if (uKind == 3) c = kIce(p, uAge);
    else if (uKind == 4) c = kInk(p, uAge);
    else if (uKind == 5) c = kDust(p, uAge);
    else if (uKind == 6) c = kPillar(p, uAge);
    else if (uKind == 7) c = kVortex(p, uAge);
    else if (uKind == 8) c = kBolt(p, uAge);
    else if (uKind == 9) c = kSlash(p, uAge);
    else if (uKind == 10) c = kFlare(p, uAge);
    else if (uKind == 11) c = kBeam(p, uAge);
    else c = kGlyph(p, uAge);
    finalColor = vec4(c.rgb * uInt * fragColor.rgb, clamp(c.a, 0.0, 1.0) * fragColor.a);
}
)GLSL";

		// 바닥 효과 + main — p는 바닥 평면(원 = 원), uRot로 돌린다
		constexpr const char* kGround = R"GLSL(
float shieldD(vec2 q)
{
    float y = q.y;
    float w = y > 0.0 ? 0.34 : 0.34 * (1.0 - pow(clamp(-y / 0.5, 0.0, 1.0), 1.7));
    float side = (y > -0.5 && y < 0.32) ? abs(abs(q.x) - w) : 1.0;
    float top = abs(q.x) < 0.34 ? abs(y - 0.32) : 1.0;
    float crossV = (y > -0.3 && y < 0.22) ? abs(q.x) : 1.0;
    float crossH = abs(q.x) < 0.18 ? abs(y - 0.06) : 1.0;
    return min(min(side, top), min(crossV, crossH) + 0.004);
}
float cellEdge(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    float d1 = 8.0;
    float d2 = 8.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            vec2 g = vec2(float(x), float(y));
            vec2 o = vec2(h21(i + g), h21(i + g + 17.0));
            float d = length(g + o - f);
            if (d < d1) { d2 = d1; d1 = d; }
            else if (d < d2) { d2 = d; }
        }
    }
    return d2 - d1;
}
vec4 gRune(vec2 p, float age)
{
    float rot = uTime * 0.22 + uSeed * 6.0;
    float c = cos(rot);
    float s = sin(rot);
    vec2 q = vec2(p.x * c - p.y * s, p.x * s + p.y * c);
    float r = length(q);
    float a01 = atan(q.y, q.x) / TAU + 0.5;
    float draw = easeOut(age / 0.4);
    float px = fwidth(r) * 1.2 + 0.003;
    float ring1 = smoothstep(px + 0.014, 0.0, abs(r - 0.95)) * step(a01, draw * 2.2);
    float ring2 = smoothstep(px + 0.008, 0.0, abs(r - 0.80)) * step(1.0 - a01, draw * 2.2 - 0.35);
    float ring3 = smoothstep(px + 0.008, 0.0, abs(r - 0.40)) * step(a01, draw * 2.2 - 0.7);
    float N = 14.0;
    float cell = floor(a01 * N);
    float fa = fract(a01 * N);
    float gh = h21(vec2(cell, floor(uSeed * 100.0)));
    float mark = 0.0;
    if (r > 0.83 && r < 0.92)
    {
        float rr = (r - 0.83) / 0.09;
        mark += smoothstep(0.1, 0.0, abs(fa - 0.5)) * step(0.3, gh);
        mark += smoothstep(0.14, 0.0, abs(rr - 0.5)) * step(abs(fa - 0.5), 0.28) * step(0.55, gh);
        mark += smoothstep(0.1, 0.0, abs(fa - rr)) * step(gh, 0.45);
        mark += smoothstep(0.1, 0.0, abs(fa - 1.0 + rr)) * step(0.75, gh);
    }
    mark *= step(cell / N, draw * 1.6 - 0.35);
    float sa = fract(a01 * 6.0 + 0.5) - 0.5;
    float spoke = smoothstep(0.03, 0.0, abs(sa) * r) * step(0.40, r) * step(r, 0.80) * step(0.55, draw);
    float star = 1e3;
    for (int k = 0; k < 6; k++)
    {
        float t0 = float(k) / 6.0 * TAU;
        float t1 = float(k + 2) / 6.0 * TAU;
        star = min(star, seg(q, vec2(cos(t0), sin(t0)) * 0.8, vec2(cos(t1), sin(t1)) * 0.8));
    }
    float starL = smoothstep(px + 0.01, 0.0, star) * step(0.75, draw) * 0.8;
    float lines = clamp(ring1 + ring2 + ring3 + mark + spoke + starL, 0.0, 1.0);
    float pulse = 0.8 + 0.2 * sin(uTime * 6.0 + uSeed * 9.0);
    float fill = smoothstep(1.0, 0.1, r) * 0.16 * draw;
    float a = (lines * pulse + fill) * fadeOut(age, 0.72);
    return vec4(ramp(lines * 0.85 + 0.15), clamp(a, 0.0, 1.0));
}
vec4 gSeal(vec2 p, float age)
{
    float r = length(p);
    float a01 = atan(p.y, p.x) / TAU + 0.5;
    float draw = easeOut(age / 0.3);
    float px = fwidth(r) * 1.2 + 0.003;
    float ring1 = smoothstep(px + 0.02, 0.0, abs(r - 0.93)) * step(a01, draw * 1.6);
    float ring2 = smoothstep(px + 0.01, 0.0, abs(r - 0.78)) * step(1.0 - a01, draw * 1.6 - 0.25);
    float sa = abs(fract(a01 * 8.0) - 0.5);
    float ray = smoothstep(0.05, 0.0, sa * r) * step(0.78, r) * step(r, 0.93) * step(0.5, draw);
    float sh = smoothstep(px + 0.02, 0.0, shieldD(p * 1.05)) * step(0.35, draw);
    float rip = 0.0;
    for (int k = 0; k < 3; k++)
    {
        float rk = fract(uTime * 0.9 + float(k) / 3.0 + uSeed);
        rip += exp(-pow((r - rk) / 0.035, 2.0)) * (1.0 - rk) * 0.55;
    }
    float lines = clamp(ring1 + ring2 + ray + sh, 0.0, 1.0);
    float fill = smoothstep(0.95, 0.2, r) * 0.14 * draw;
    float a = (lines + rip * draw + fill) * fadeOut(age, 0.7);
    return vec4(ramp(lines * 0.9 + rip * 0.5 + 0.1), clamp(a, 0.0, 1.0));
}
vec4 gCrack(vec2 p, float age)
{
    float r = length(p);
    float ang = atan(p.y, p.x);
    float best = 1.0;
    float sd = floor(uSeed * 100.0);
    for (int i = 0; i < 8; i++)
    {
        float fi = float(i);
        float th = (fi + h21(vec2(fi, sd)) * 0.7) / 8.0 * TAU;
        float len = 0.6 + 0.4 * h21(vec2(fi, sd + 4.0));
        float jag = (vnoise(vec2(r * 7.0, fi * 3.0 + sd)) - 0.5) * 0.55 + (vnoise(vec2(r * 21.0, fi * 5.0 + sd)) - 0.5) * 0.18;
        float da = abs(mod(ang - th - jag + PI, TAU) - PI) * r;
        float w = 0.045 * max(1.0 - r / len, 0.0) + 0.006;
        best = min(best, r < len ? da / w : 1.0);
    }
    float line = smoothstep(1.0, 0.0, best);
    line *= step(r, easeOut(age / 0.1) * 1.05);
    float heat = 1.05 - age * 1.2 - r * 0.3;
    float glow = smoothstep(0.3, 0.0, r) * (1.0 - age) * 0.4;
    float a = clamp(line * (0.45 + 0.55 * clamp(heat + 0.3, 0.0, 1.0)) + glow, 0.0, 1.0) * fadeOut(age, 0.6);
    return vec4(ramp(heat + line * 0.2), a);
}
vec4 gScorch(vec2 p, float age)
{
    float r = length(p);
    float n = fbm(p * 2.6 + uSeed * 7.0);
    float n2 = fbm3(p * 7.0 + uSeed * 3.0);
    float d = smoothstep(0.95, 0.3, r + (n - 0.5) * 0.6);
    float a = clamp(d * (0.7 + 0.35 * n2), 0.0, 1.0) * fadeOut(age, 0.55) * smoothstep(0.0, 0.04, age);
    return vec4(uCool * (0.5 + 0.6 * n2), a * 0.88);
}
vec4 gFrost(vec2 p, float age)
{
    float r = length(p);
    float spread = easeOut(age / 0.18);
    float n = fbm3(p * 3.0 + uSeed * 5.0);
    float cells = smoothstep(0.09, 0.0, cellEdge(p * 5.5 + uSeed * 13.0));
    float fine = smoothstep(0.06, 0.0, cellEdge(p * 13.0 + uSeed * 7.0)) * 0.5;
    float ang = atan(p.y, p.x);
    float spikes = pow(abs(cos(ang * 3.0 + uSeed * 4.0)), 80.0) * smoothstep(1.0, 0.1, r);
    float edge = spread * (0.72 + 0.35 * n);
    float mask = smoothstep(edge, edge - 0.18, r);
    float lines = max(max(cells, fine * (1.0 - r)), spikes);
    float a = mask * (0.14 + 0.8 * lines) * fadeOut(age, 0.6);
    return vec4(ramp(lines * 0.75 + 0.1), clamp(a, 0.0, 1.0) * 0.9);
}
vec4 gInkStain(vec2 p, float age)
{
    float r = length(p);
    float ang = atan(p.y, p.x);
    float spread = easeOut(age / 0.22);
    float n = fbm3(vec2(ang * 1.4 + uSeed * 9.0, uSeed) + p * 1.4);
    float R = spread * (0.52 + 0.42 * n);
    float blob = smoothstep(R, R - 0.05, r);
    float rim = smoothstep(0.1, 0.0, abs(r - R + 0.04)) * 0.45 * blob;
    float a = clamp(blob * 0.72 + rim, 0.0, 1.0) * fadeOut(age, 0.6);
    return vec4(mix(uCool, uMid, rim), a);
}
vec4 gRing(vec2 p, float age)
{
    float r = length(p);
    float R = easeOut(age);
    float w = mix(0.11, 0.025, age);
    float ring = exp(-pow((r - R) / w, 2.0));
    float inner = smoothstep(R, 0.0, r) * 0.22 * (1.0 - age);
    float n = vnoise(vec2(atan(p.y, p.x) * 6.0 + uSeed * 20.0, uSeed));
    float a = (ring * (0.65 + 0.35 * n) + inner) * (1.0 - age);
    return vec4(ramp(ring), clamp(a, 0.0, 1.0));
}
void main()
{
    vec2 p0 = vec2(fragTexCoord.x * 2.0 - 1.0, 1.0 - fragTexCoord.y * 2.0);
    vec2 p = vec2(p0.x * uRot.x + p0.y * uRot.y, -p0.x * uRot.y + p0.y * uRot.x);
    vec4 c;
    if (uKind == 0) c = gRune(p, uAge);
    else if (uKind == 1) c = gSeal(p, uAge);
    else if (uKind == 2) c = gCrack(p, uAge);
    else if (uKind == 3) c = gScorch(p, uAge);
    else if (uKind == 4) c = gFrost(p, uAge);
    else if (uKind == 5) c = gInkStain(p, uAge);
    else c = gRing(p, uAge);
    c.a *= step(length(p0), 1.0);
    finalColor = vec4(c.rgb * uInt * fragColor.rgb, clamp(c.a, 0.0, 1.0) * fragColor.a);
}
)GLSL";

		struct Locs { int kind = -1, age = -1, seed = -1, time = -1, hot = -1, mid = -1, cool = -1, inten = -1, aspect = -1, rot = -1; };
		struct Prog { Shader sh{}; Locs loc; bool ok = false; };
		Prog g_air, g_ground;
		bool g_tried = false;

		struct Inst { Spec s; float t = 0.f; float seed = 0.f; };
		std::vector<Inst> g_list;
		float g_time = 0.f;

		constexpr size_t kMax = 192;

		bool IsGround(Kind k) { return k >= Kind::Rune && k < Kind::Count; }
		bool IsAlphaBlend(Kind k)
		{
			return k == Kind::Smoke || k == Kind::InkBurst || k == Kind::Dust || k == Kind::Scorch || k == Kind::InkStain;
		}
		bool BottomAnchored(Kind k) { return k == Kind::Fire || k == Kind::Pillar || k == Kind::Lightning || k == Kind::Dust; }

		bool Load(Prog& p, const std::string& fs)
		{
			p.sh = LoadShaderFromMemory(nullptr, fs.c_str());
			if (p.sh.id == 0 || p.sh.id == rlGetShaderIdDefault())
			{
				p.ok = false;
				return false;
			}
			auto L = [&](const char* n) { return GetShaderLocation(p.sh, n); };
			p.loc.kind = L("uKind"); p.loc.age = L("uAge"); p.loc.seed = L("uSeed"); p.loc.time = L("uTime");
			p.loc.hot = L("uHot"); p.loc.mid = L("uMid"); p.loc.cool = L("uCool"); p.loc.inten = L("uInt");
			p.loc.aspect = L("uAspect"); p.loc.rot = L("uRot");
			p.ok = true;
			return true;
		}

		void EnsureShaders()
		{
			if (g_tried) return;
			g_tried = true;
			const bool a = Load(g_air, std::string(kHead) + kAirA + kAirB);
			const bool b = Load(g_ground, std::string(kHead) + kGround);
			if (!a || !b)
				TraceLog(LOG_WARNING, "VFX: shader failed (air %d ground %d) - procedural effects off", a ? 1 : 0, b ? 1 : 0);
			else
				TraceLog(LOG_INFO, "VFX: shaders ready");
		}

		void Vec3(const Prog& p, int loc, Color c)
		{
			const float v[3] = { c.r / 255.f, c.g / 255.f, c.b / 255.f };
			SetShaderValue(p.sh, loc, v, SHADER_UNIFORM_VEC3);
		}

		void Uniforms(const Prog& p, const Inst& in, float age, float aspect, int kindIdx)
		{
			SetShaderValue(p.sh, p.loc.kind, &kindIdx, SHADER_UNIFORM_INT);
			SetShaderValue(p.sh, p.loc.age, &age, SHADER_UNIFORM_FLOAT);
			SetShaderValue(p.sh, p.loc.seed, &in.seed, SHADER_UNIFORM_FLOAT);
			SetShaderValue(p.sh, p.loc.time, &g_time, SHADER_UNIFORM_FLOAT);
			Vec3(p, p.loc.hot, in.s.hot);
			Vec3(p, p.loc.mid, in.s.mid);
			Vec3(p, p.loc.cool, in.s.cool);
			SetShaderValue(p.sh, p.loc.inten, &in.s.intensity, SHADER_UNIFORM_FLOAT);
			SetShaderValue(p.sh, p.loc.aspect, &aspect, SHADER_UNIFORM_FLOAT);
			const float rot[2] = { std::cos(in.s.rot), std::sin(in.s.rot) };
			SetShaderValue(p.sh, p.loc.rot, rot, SHADER_UNIFORM_VEC2);
		}

		float EaseOut(float u) { u = util::Clamp01(u); return 1.f - (1.f - u) * (1.f - u) * (1.f - u); }

		// 네모 하나(정점 색 = 흰색 × 알파). 모서리 a(왼쪽 위) b(왼쪽 아래) c(오른쪽 아래) d(오른쪽 위)
		void Quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, float z, float alpha)
		{
			rlBegin(RL_QUADS);
			rlColor4ub(255, 255, 255, static_cast<unsigned char>(util::Clamp01(alpha) * 255.f));
			rlNormal3f(0.f, 0.f, 1.f);
			rlTexCoord2f(0.f, 0.f); rlVertex3f(a.x, a.y, z);
			rlTexCoord2f(0.f, 1.f); rlVertex3f(b.x, b.y, z);
			rlTexCoord2f(1.f, 1.f); rlVertex3f(c.x, c.y, z);
			rlTexCoord2f(1.f, 0.f); rlVertex3f(d.x, d.y, z);
			rlEnd();
		}

		void DrawList(bool ground)
		{
			if (g_list.empty()) return;
			EnsureShaders();
			Prog& p = ground ? g_ground : g_air;
			if (!p.ok) return;
			const bool emis = gfx::EmissiveOnlyOn();
			const bool spritesOn = sprites::Suspend();
			const bool wasAdditive = gfx::AdditiveOn();
			rlDrawRenderBatchActive();
			BeginShaderMode(p.sh);
			// 두 번: 보통 혼합(그을음 · 먹물 · 연기) 먼저, 빛은 그 위에
			for (int pass = 0; pass < 2; ++pass)
			{
				const bool additivePass = pass == 1;
				if (!additivePass && emis) continue;
				rlSetBlendMode(additivePass || emis ? RL_BLEND_ADDITIVE : RL_BLEND_ALPHA);
				bool overOn = false;
				for (const Inst& in : g_list)
				{
					const Spec& s = in.s;
					if (IsGround(s.kind) != ground) continue;
					if (IsAlphaBlend(s.kind) == additivePass) continue;
					if (in.t < s.delay) continue;
					const float age = util::Clamp01((in.t - s.delay) / s.life);
					const float scale = 1.f + (s.grow - 1.f) * EaseOut(age * 1.6f);
					if (!emis && s.over != overOn)
					{
						rlDrawRenderBatchActive();
						s.over ? rlDisableDepthTest() : rlEnableDepthTest();
						overOn = s.over;
					}
					if (ground)
					{
						const float rx = s.w * 0.5f * scale, ry = s.w * 0.25f * scale;
						const float z = -0.965f;
						Uniforms(p, in, age, 2.f, static_cast<int>(s.kind) - static_cast<int>(Kind::Rune));
						Quad(Vector2{ s.iso.x - rx, s.iso.y - ry }, Vector2{ s.iso.x - rx, s.iso.y + ry },
							Vector2{ s.iso.x + rx, s.iso.y + ry }, Vector2{ s.iso.x + rx, s.iso.y - ry }, z, s.alpha);
					}
					else
					{
						const float hw = s.w * 0.5f * scale, hh = s.h * 0.5f * scale;
						const Vector2 c{ s.iso.x, s.iso.y - s.z };
						const float cs = std::cos(s.rot), sn = std::sin(s.rot);
						// 아랫변 기준이면 네모 가운데가 기준점 위로 hh
						const float oy = BottomAnchored(s.kind) ? -hh : 0.f;
						auto P = [&](float lx, float ly)
						{
							ly += oy;
							return Vector2{ c.x + lx * cs - ly * sn, c.y + lx * sn + ly * cs };
						};
						const float z = gfx::DepthOf(s.iso.y / gfx::kHalfH + s.depthBias);
						Uniforms(p, in, age, s.w / std::max(1.f, s.h), static_cast<int>(s.kind));
						Quad(P(-hw, -hh), P(-hw, hh), P(hw, hh), P(hw, -hh), z, s.alpha);
					}
					rlDrawRenderBatchActive();   // 다음 효과의 uniform을 바꾸기 전에 그린다
				}
				if (overOn) { rlDrawRenderBatchActive(); rlEnableDepthTest(); }
			}
			rlDrawRenderBatchActive();
			EndShaderMode();
			// 혼합 · 셰이더 상태 되돌리기
			gfx::Additive(wasAdditive);
			sprites::Resume(spritesOn);
		}
	}

	void Spawn(const Spec& s)
	{
		if (s.life <= 0.f) return;
		const float k = fxscale::Alpha();
		if (k <= 0.02f) return;
		if (g_list.size() >= kMax) g_list.erase(g_list.begin());
		Inst in;
		in.s = s;
		in.s.alpha *= k;
		in.seed = s.seed >= 0.f ? s.seed : util::RandF(0.f, 1.f);
		g_list.push_back(in);
	}

	void BeamBetween(Vector2 aIso, float az, Vector2 bIso, float bz, float thick, float life, const Spec& style)
	{
		Spec s = style;
		s.kind = Kind::Beam;
		const Vector2 a{ aIso.x, aIso.y - az }, b{ bIso.x, bIso.y - bz };
		const float dx = b.x - a.x, dy = b.y - a.y;
		const float len = std::sqrt(dx * dx + dy * dy);
		if (len < 2.f) return;
		// 가운데 = 두 점의 가운데(바닥 위치도 가운데 — 깊이)
		s.iso = Vector2{ (aIso.x + bIso.x) * 0.5f, (aIso.y + bIso.y) * 0.5f };
		s.z = (az + bz) * 0.5f;
		s.w = len * 1.1f;
		s.h = thick;
		s.rot = std::atan2(dy, dx);
		s.life = life;
		Spawn(s);
	}

	void Update(float dt)
	{
		g_time += dt;
		if (g_time > 3600.f) g_time -= 3600.f;
		for (auto& in : g_list) in.t += dt;
		g_list.erase(std::remove_if(g_list.begin(), g_list.end(), [](const Inst& in) { return in.t >= in.s.delay + in.s.life; }), g_list.end());
	}

	void Clear() { g_list.clear(); }
	void DrawGround() { DrawList(true); }
	void DrawWorld() { DrawList(false); }
	int Count() { return static_cast<int>(g_list.size()); }
	bool Ready() { return g_air.ok && g_ground.ok; }

	void Shutdown()
	{
		for (Prog* p : { &g_air, &g_ground })
		{
			if (p->ok) UnloadShader(p->sh);
			*p = Prog{};
		}
		g_tried = false;
		g_list.clear();
	}

	// ── 램프(DESIGN_SYSTEM 10장) ──
	Ramp FireRamp()   { return { util::Rgb(0xFFF4D6), util::Rgb(0xFF9A3C), util::Rgb(0x7A2412) }; }
	Ramp GoldRamp()   { return { util::Rgb(0xFFF7E4), util::Rgb(0xF2C47C), util::Rgb(0x684D2A) }; }
	Ramp TealRamp()   { return { util::Rgb(0xE6FFF9), util::Rgb(0x7FD0C4), util::Rgb(0x1E4A48) }; }
	Ramp FrostRamp()  { return { util::Rgb(0xFFFFFF), util::Rgb(0xBFE6FF), util::Rgb(0x3A6F96) }; }
	Ramp BoltRamp()   { return { util::Rgb(0xFFFFFF), util::Rgb(0xB8FFF2), util::Rgb(0x3AA8A0) }; }
	Ramp MoonRamp()   { return { util::Rgb(0xFFFFFF), util::Rgb(0xD8E2EC), util::Rgb(0x5A6878) }; }
	Ramp PoisonRamp() { return { util::Rgb(0xEAFFC8), util::Rgb(0x8DCE5A), util::Rgb(0x2E4A1E) }; }
	Ramp InkRamp()    { return { util::Rgb(0x4A5260), util::Rgb(0x1C2028), util::Rgb(0x07080B) }; }
	Ramp EmberRamp()  { return { util::Rgb(0xFFE2B0), util::Rgb(0xE0604E), util::Rgb(0x5A1A12) }; }
	Ramp DustRamp()   { return { util::Rgb(0xE8D8B8), util::Rgb(0xB49C78), util::Rgb(0x6E5E48) }; }
	Ramp SmokeRamp()  { return { util::Rgb(0xB0AAA0), util::Rgb(0x6A6660), util::Rgb(0x2C2A28) }; }
	void SetRamp(Spec& s, const Ramp& r) { s.hot = r.hot; s.mid = r.mid; s.cool = r.cool; }
	Spec Make(Kind k, Vector2 iso, float z, float size, float life, const Ramp& r)
	{
		Spec s;
		s.kind = k;
		s.iso = iso;
		s.z = z;
		s.w = s.h = size;
		s.life = life;
		SetRamp(s, r);
		return s;
	}
}
