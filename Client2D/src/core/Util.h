#pragma once

#include "raylib.h"
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <random>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 공용 수학/색/이징/난수 도우미
// ─────────────────────────────────────────────────────────────────────────────
namespace util
{
	inline constexpr float kPi = 3.14159265358979f;

	inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
	inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
	inline float InvLerp(float a, float b, float v) { return (b == a) ? 0.f : (v - a) / (b - a); }
	inline float Remap(float v, float a, float b, float c, float d) { return Lerp(c, d, Clamp01(InvLerp(a, b, v))); }
	inline Vector2 V2(float x, float y) { return Vector2{ x, y }; }
	inline Vector2 Add(Vector2 a, Vector2 b) { return { a.x + b.x, a.y + b.y }; }
	inline Vector2 Sub(Vector2 a, Vector2 b) { return { a.x - b.x, a.y - b.y }; }
	inline Vector2 Mul(Vector2 a, float s) { return { a.x * s, a.y * s }; }
	inline Vector2 LerpV(Vector2 a, Vector2 b, float t) { return { Lerp(a.x, b.x, t), Lerp(a.y, b.y, t) }; }
	inline float Len(Vector2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
	inline Vector2 Norm(Vector2 a) { const float l = Len(a); return l > 1e-5f ? Vector2{ a.x / l, a.y / l } : Vector2{ 0, 0 }; }
	inline Vector2 Rot(Vector2 v, float a) { const float c = std::cos(a), s = std::sin(a); return { v.x * c - v.y * s, v.x * s + v.y * c }; }

	// 프레임레이트와 무관한 지수 감쇠 보간(damp)
	inline float Damp(float current, float target, float lambda, float dt)
	{
		return Lerp(current, target, 1.f - std::exp(-lambda * dt));
	}
	inline Vector2 DampV(Vector2 c, Vector2 t, float lambda, float dt)
	{
		const float k = 1.f - std::exp(-lambda * dt);
		return { Lerp(c.x, t.x, k), Lerp(c.y, t.y, k) };
	}

	// ── 이징 ──
	inline float EaseOutCubic(float t) { t = Clamp01(t); const float u = 1 - t; return 1 - u * u * u; }
	inline float EaseInCubic(float t) { t = Clamp01(t); return t * t * t; }
	inline float EaseInOutCubic(float t) { t = Clamp01(t); return t < .5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.f) / 2; }
	inline float EaseOutQuad(float t) { t = Clamp01(t); return 1 - (1 - t) * (1 - t); }
	inline float EaseOutBack(float t, float s = 1.70158f) { t = Clamp01(t) - 1; return t * t * ((s + 1) * t + s) + 1; }
	inline float EaseOutElastic(float t)
	{
		t = Clamp01(t);
		if (t == 0 || t == 1) return t;
		return std::pow(2.f, -10 * t) * std::sin((t * 10 - 0.75f) * (2 * kPi / 3)) + 1;
	}
	inline float EaseOutBounce(float t)
	{
		t = Clamp01(t);
		const float n1 = 7.5625f, d1 = 2.75f;
		if (t < 1 / d1) return n1 * t * t;
		if (t < 2 / d1) { t -= 1.5f / d1; return n1 * t * t + .75f; }
		if (t < 2.5 / d1) { t -= 2.25f / d1; return n1 * t * t + .9375f; }
		t -= 2.625f / d1; return n1 * t * t + .984375f;
	}
	// 0→1→0 한 번 튀는 곡선(펀치 스케일 등)
	inline float Pulse(float t) { t = Clamp01(t); return std::sin(t * kPi); }

	// ── 색 ──
	inline Color Rgb(uint32_t hex, uint8_t a = 255) { return Color{ (uint8_t)((hex >> 16) & 0xFF), (uint8_t)((hex >> 8) & 0xFF), (uint8_t)(hex & 0xFF), a }; }
	inline Color WithA(Color c, float a) { c.a = (uint8_t)std::clamp(a * 255.f, 0.f, 255.f); return c; }
	inline Color MulA(Color c, float a) { c.a = (uint8_t)std::clamp(c.a * a, 0.f, 255.f); return c; }
	inline Color Mix(Color a, Color b, float t)
	{
		t = Clamp01(t);
		return Color{ (uint8_t)Lerp(a.r, b.r, t), (uint8_t)Lerp(a.g, b.g, t), (uint8_t)Lerp(a.b, b.b, t), (uint8_t)Lerp(a.a, b.a, t) };
	}
	inline Color Shade(Color c, float f)  // f<1 어둡게, f>1 밝게(흰색 쪽)
	{
		if (f <= 1.f) return Color{ (uint8_t)(c.r * f), (uint8_t)(c.g * f), (uint8_t)(c.b * f), c.a };
		const float t = std::min(f - 1.f, 1.f);
		return Color{ (uint8_t)Lerp(c.r, 255, t), (uint8_t)Lerp(c.g, 255, t), (uint8_t)Lerp(c.b, 255, t), c.a };
	}

	// ── 난수/해시 ──
	inline uint32_t Hash32(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
		return x;
	}
	inline uint32_t Hash2(int x, int y, uint32_t seed = 0)
	{
		return Hash32((uint32_t)x * 0x9E3779B1u ^ Hash32((uint32_t)y + 0x85EBCA77u + seed * 0xC2B2AE3Du));
	}
	inline float Hash01(int x, int y, uint32_t seed = 0) { return (Hash2(x, y, seed) & 0xFFFFFF) / float(0x1000000); }

	inline std::mt19937& Rng() { static std::mt19937 rng{ std::random_device{}() }; return rng; }
	inline float RandF(float a = 0.f, float b = 1.f) { return std::uniform_real_distribution<float>(a, b)(Rng()); }
	inline int RandI(int a, int b) { return std::uniform_int_distribution<int>(a, b)(Rng()); }

	// 1D 값 노이즈(흔들림/깜빡임용)
	inline float Noise1(float x)
	{
		const int i = (int)std::floor(x);
		const float f = x - i;
		const float a = Hash01(i, 7), b = Hash01(i + 1, 7);
		const float u = f * f * (3 - 2 * f);
		return Lerp(a, b, u) * 2.f - 1.f;
	}

	// ── 문자열 ──
	std::string FormatNumber(long long v);   // 1234567 → "1,234,567"
}
