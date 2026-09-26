#include "audio/Ambience.h"

#include "audio/Sfx.h"
#include "Data/TownMap.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace amb
{
	namespace
	{
		constexpr int kRate = 22050;
		constexpr float kTau = 6.28318530718f;
		constexpr int kBeds = static_cast<int>(Bed::Count);

		// 바탕 소리 음량(효과음보다 한참 낮게 — 들리되 거슬리지 않게)
		constexpr float kBedVolume[kBeds] = { 0.30f, 0.27f, 0.20f, 0.26f, 0.24f };
		constexpr float kOverall = 0.55f;

		std::mt19937 g_rng{ 20260926 };
		float Rand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g_rng); }

		// ── 합성 도구 ─────────────────────────────────────────────────────────────
		struct Biquad
		{
			float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
			// RBJ 대역 통과(가운데 0dB)
			void Band(float f, float q)
			{
				const float w = kTau * std::clamp(f, 20.f, kRate * 0.45f) / kRate;
				const float al = std::sin(w) / (2.f * q), c = std::cos(w), a0 = 1.f + al;
				b0 = al / a0; b1 = 0.f; b2 = -al / a0; a1 = -2.f * c / a0; a2 = (1.f - al) / a0;
			}
			void Low(float f, float q = 0.707f)
			{
				const float w = kTau * std::clamp(f, 20.f, kRate * 0.45f) / kRate;
				const float al = std::sin(w) / (2.f * q), c = std::cos(w), a0 = 1.f + al;
				b0 = (1.f - c) * 0.5f / a0; b1 = (1.f - c) / a0; b2 = b0; a1 = -2.f * c / a0; a2 = (1.f - al) / a0;
			}
			void High(float f, float q = 0.707f)
			{
				const float w = kTau * std::clamp(f, 20.f, kRate * 0.45f) / kRate;
				const float al = std::sin(w) / (2.f * q), c = std::cos(w), a0 = 1.f + al;
				b0 = (1.f + c) * 0.5f / a0; b1 = -(1.f + c) / a0; b2 = b0; a1 = -2.f * c / a0; a2 = (1.f - al) / a0;
			}
			float operator()(float x)
			{
				const float y = b0 * x + z1;
				z1 = b1 * x - a1 * y + z2;
				z2 = b2 * x - a2 * y;
				return y;
			}
		};

		struct Loop
		{
			std::vector<float> s;
			size_t pos = 0;
		};
		std::array<Loop, kBeds> g_beds;
		bool g_built = false;

		// len초 + 이음 fade초를 만들어 끝을 처음에 겹쳐(같은 힘 교차) 끊김 없는 고리로 자른다. RMS를 rms로 맞춘다.
		void Seal(std::vector<float>& x, float len, float fade, float rms)
		{
			const size_t L = static_cast<size_t>(len * kRate), F = static_cast<size_t>(fade * kRate);
			x.resize(L + F, 0.f);
			for (size_t i = 0; i < F; ++i)
			{
				const float a = static_cast<float>(i) / static_cast<float>(F);
				x[i] = x[i] * std::sqrt(a) + x[L + i] * std::sqrt(1.f - a);
			}
			x.resize(L);
			double acc = 0.0;
			for (float v : x) acc += static_cast<double>(v) * v;
			const float cur = static_cast<float>(std::sqrt(acc / std::max<size_t>(1, x.size())));
			const float k = cur > 1e-6f ? rms / cur : 0.f;
			for (float& v : x) v = std::tanh(v * k * 1.2f) / 1.2f;   // 튀는 방울을 부드럽게 눌러 준다
		}

		// 물방울 하나: 공명 주파수가 올라가며 사라지는 사인(거품 터짐)
		void Bubble(std::vector<float>& x, size_t at, float f0, float amp, float dur)
		{
			const size_t n = static_cast<size_t>(dur * kRate);
			float ph = 0.f;
			for (size_t i = 0; i < n && at + i < x.size(); ++i)
			{
				const float u = static_cast<float>(i) / static_cast<float>(n);
				ph += f0 * (1.f + 0.9f * u) / kRate;
				x[at + i] += std::sin(ph * kTau) * amp * std::exp(-u * 5.f) * std::min(1.f, i / 20.f);
			}
		}

		// 분수: 떨어지는 물줄기(넓은 쏴아) + 수면을 때리는 물방울 알갱이 + 거품
		void BuildFountain(std::vector<float>& x)
		{
			const float len = 6.f, fade = 0.6f;
			x.assign(static_cast<size_t>((len + fade) * kRate), 0.f);
			Biquad body, hiss, low;
			body.Band(1500.f, 0.55f); hiss.High(4500.f); low.Low(420.f);
			for (size_t i = 0; i < x.size(); ++i)
			{
				const float t = static_cast<float>(i) / kRate;
				const float w = Rand(-1.f, 1.f);
				const float m = 0.85f + 0.15f * std::sin(t * kTau * 0.37f) * std::sin(t * kTau * 1.9f);
				x[i] += body(w) * 0.55f * m + hiss(w) * 0.05f + low(Rand(-1.f, 1.f)) * 0.4f;
			}
			// 물방울 알갱이: 짧은 노이즈 터짐을 제각각의 대역으로
			const int grains = static_cast<int>((len + fade) * 260.f);
			for (int g = 0; g < grains; ++g)
			{
				Biquad bp; bp.Band(Rand(700.f, 4200.f), Rand(1.5f, 4.f));
				const size_t at = static_cast<size_t>(Rand(0.f, len + fade) * kRate);
				const size_t n = static_cast<size_t>(Rand(0.004f, 0.016f) * kRate);
				const float amp = Rand(0.2f, 1.f) * Rand(0.3f, 1.f);
				for (size_t i = 0; i < n && at + i < x.size(); ++i)
					x[at + i] += bp(Rand(-1.f, 1.f)) * amp * 1.4f * std::exp(-static_cast<float>(i) / (n * 0.35f));
			}
			for (int b = 0; b < static_cast<int>((len + fade) * 14.f); ++b)
				Bubble(x, static_cast<size_t>(Rand(0.f, len + fade) * kRate), Rand(380.f, 1300.f), Rand(0.05f, 0.18f), Rand(0.02f, 0.05f));
			Seal(x, len, fade, 0.22f);
		}

		// 강물: 낮게 흐르는 물살(느린 부풂) + 물결 찰랑임 + 드문 꼴깍
		void BuildRiver(std::vector<float>& x)
		{
			const float len = 7.f, fade = 0.8f;
			x.assign(static_cast<size_t>((len + fade) * kRate), 0.f);
			Biquad lo, mid, lo2;
			lo.Low(520.f); lo2.Low(520.f); mid.Band(1250.f, 0.6f);
			for (size_t i = 0; i < x.size(); ++i)
			{
				const float t = static_cast<float>(i) / kRate;
				const float swell = 0.75f + 0.12f * std::sin(t * kTau * 0.13f + 0.4f) + 0.08f * std::sin(t * kTau * 0.31f + 1.1f);
				const float chop = 0.6f + 0.4f * std::sin(t * kTau * 1.7f) * std::sin(t * kTau * 2.9f + 0.7f);
				x[i] += lo2(lo(Rand(-1.f, 1.f))) * 1.4f * swell + mid(Rand(-1.f, 1.f)) * 0.28f * chop;
			}
			for (int b = 0; b < static_cast<int>((len + fade) * 4.f); ++b)
				Bubble(x, static_cast<size_t>(Rand(0.f, len + fade) * kRate), Rand(240.f, 620.f), Rand(0.04f, 0.12f), Rand(0.03f, 0.08f));
			Seal(x, len, fade, 0.2f);
		}

		// 바람: 가운데 주파수가 천천히 떠도는 대역 노이즈 + 부는 결(세기는 Update가 바람 세기로 곱한다)
		void BuildWind(std::vector<float>& x)
		{
			const float len = 8.f, fade = 1.f;
			x.assign(static_cast<size_t>((len + fade) * kRate), 0.f);
			Biquad bp, bp2, whistle;
			for (size_t i = 0; i < x.size(); ++i)
			{
				const float t = static_cast<float>(i) / kRate;
				if ((i & 31) == 0)
				{
					const float fc = 430.f + 220.f * std::sin(t * kTau * 0.07f) + 110.f * std::sin(t * kTau * 0.19f + 1.f);
					bp.Band(fc, 1.1f); bp2.Band(fc * 2.1f, 1.6f);
					whistle.Band(fc * 3.2f, 9.f);
				}
				const float gust = 0.55f + 0.45f * (0.5f + 0.5f * std::sin(t * kTau * 0.11f + 0.3f)) * (0.6f + 0.4f * std::sin(t * kTau * 0.29f));
				const float w = Rand(-1.f, 1.f);
				x[i] += (bp(w) * 0.9f + bp2(w) * 0.25f + whistle(w) * 0.12f) * gust;
			}
			Seal(x, len, fade, 0.2f);
		}

		// 시장 웅성임: 알아들을 수 없는 여러 목소리(모음 공명 두 개를 거친 톱니 성대음) + 나무 상자 달그락
		void BuildMarket(std::vector<float>& x)
		{
			const float len = 9.f, fade = 1.f;
			const size_t N = static_cast<size_t>((len + fade) * kRate);
			x.assign(N, 0.f);
			struct Vowel { float f1, f2; };
			constexpr Vowel kV[] = { { 730, 1100 }, { 530, 1840 }, { 300, 2250 }, { 480, 820 }, { 330, 780 }, { 620, 1550 } };
			for (int v = 0; v < 9; ++v)
			{
				const bool high = (v % 3) == 0;
				const float f0Base = high ? Rand(185.f, 240.f) : Rand(95.f, 140.f);
				const float gain = Rand(0.35f, 1.f);
				Biquad r1, r2, fric;
				fric.Band(Rand(3200.f, 5200.f), 2.f);
				float ph = Rand(0.f, 1.f);
				size_t i = static_cast<size_t>(Rand(0.f, 1.2f) * kRate);
				Vowel cur = kV[0];
				while (i < N)
				{
					// 한 마디: 음절 3~9개, 끝으로 갈수록 음높이가 조금 내려간다
					const int syl = static_cast<int>(Rand(3.f, 9.99f));
					for (int s = 0; s < syl && i < N; ++s)
					{
						const Vowel next = kV[static_cast<int>(Rand(0.f, 5.99f))];
						const size_t dur = static_cast<size_t>(Rand(0.08f, 0.22f) * kRate);
						const float amp = Rand(0.6f, 1.f) * gain;
						const float f0 = f0Base * (1.f - 0.1f * s / std::max(1, syl)) * Rand(0.94f, 1.06f);
						// 자음(마찰음) 조금
						if (Rand(0.f, 1.f) < 0.45f)
						{
							const size_t cn = static_cast<size_t>(Rand(0.015f, 0.035f) * kRate);
							for (size_t k = 0; k < cn && i < N; ++k, ++i)
								x[i] += fric(Rand(-1.f, 1.f)) * 0.18f * amp;
						}
						for (size_t k = 0; k < dur && i < N; ++k, ++i)
						{
							const float u = static_cast<float>(k) / static_cast<float>(dur);
							if ((k & 31) == 0)
							{
								const float m = std::min(1.f, u * 4.f);   // 앞 25% 동안 모음이 옮겨 간다
								r1.Band(cur.f1 + (next.f1 - cur.f1) * m, 5.f);
								r2.Band(cur.f2 + (next.f2 - cur.f2) * m, 7.f);
							}
							ph += f0 * (1.f + 0.015f * std::sin(u * kTau * 1.3f)) / kRate;
							const float p = ph - std::floor(ph);
							const float src = 2.f * p - 1.f;                      // 톱니 성대음
							const float env = std::min(1.f, u / 0.12f) * std::min(1.f, (1.f - u) / 0.2f);
							x[i] += (r1(src) * 1.f + r2(src) * 0.6f) * env * amp;
						}
						cur = next;
					}
					i += static_cast<size_t>(Rand(0.25f, 1.1f) * kRate);   // 숨
				}
			}
			// 거리감: 고역을 깎고 짧은 울림 셋
			Biquad lp; lp.Low(2300.f);
			for (float& v : x) v = lp(v);
			std::vector<float> wet(N, 0.f);
			constexpr float kDelay[] = { 0.0297f, 0.0371f, 0.0411f };
			for (float d : kDelay)
			{
				const size_t D = static_cast<size_t>(d * kRate);
				std::vector<float> c(N, 0.f);
				for (size_t i = 0; i < N; ++i) c[i] = x[i] + (i >= D ? c[i - D] * 0.55f : 0.f);
				for (size_t i = 0; i < N; ++i) wet[i] += c[i] / 3.f;
			}
			for (size_t i = 0; i < N; ++i) x[i] = x[i] * 0.8f + wet[i] * 0.3f;
			// 나무 상자 · 수레 달그락
			for (int c = 0; c < static_cast<int>((len + fade) * 0.4f); ++c)
			{
				size_t at = static_cast<size_t>(Rand(0.f, len + fade - 0.3f) * kRate);
				Biquad bp; bp.Band(Rand(700.f, 1800.f), 3.f);
				for (int k = 0; k < 3; ++k)
				{
					const size_t n = static_cast<size_t>(0.018f * kRate);
					for (size_t i = 0; i < n && at + i < N; ++i)
						x[at + i] += bp(Rand(-1.f, 1.f)) * 1.1f * std::exp(-static_cast<float>(i) / (n * 0.3f));
					at += static_cast<size_t>(Rand(0.05f, 0.11f) * kRate);
				}
			}
			Seal(x, len, fade, 0.18f);
		}

		// 대장간 불길: 풀무가 숨 쉬듯 부푸는 낮은 불길 + 타닥 튀는 불똥
		void BuildForge(std::vector<float>& x)
		{
			const float len = 6.f, fade = 0.7f;
			x.assign(static_cast<size_t>((len + fade) * kRate), 0.f);
			Biquad lo, lo2, flutter;
			lo.Low(170.f); lo2.Low(210.f); flutter.Band(620.f, 0.8f);
			float slow = 0.f;
			for (size_t i = 0; i < x.size(); ++i)
			{
				const float t = static_cast<float>(i) / kRate;
				const float s = std::max(0.f, std::sin(t * kTau * 0.33f));
				const float bellows = 0.55f + 0.45f * s * s;
				slow += (Rand(-1.f, 1.f) - slow) * 0.0008f;
				x[i] += lo2(lo(Rand(-1.f, 1.f))) * 2.2f * bellows + flutter(Rand(-1.f, 1.f)) * (0.10f + std::fabs(slow) * 2.f) * bellows;
			}
			Biquad hp; hp.High(1800.f);
			for (int c = 0; c < static_cast<int>((len + fade) * 22.f); ++c)
			{
				const size_t at = static_cast<size_t>(Rand(0.f, len + fade) * kRate);
				const size_t n = static_cast<size_t>(Rand(0.001f, 0.004f) * kRate);
				const float amp = Rand(0.1f, 0.8f);
				for (size_t i = 0; i < n && at + i < x.size(); ++i)
					x[at + i] += hp(Rand(-1.f, 1.f)) * amp;
			}
			Seal(x, len, fade, 0.2f);
		}

		void Build()
		{
			if (g_built) return;
			BuildFountain(g_beds[static_cast<int>(Bed::Fountain)].s);
			BuildRiver(g_beds[static_cast<int>(Bed::River)].s);
			BuildWind(g_beds[static_cast<int>(Bed::Wind)].s);
			BuildMarket(g_beds[static_cast<int>(Bed::Market)].s);
			BuildForge(g_beds[static_cast<int>(Bed::Forge)].s);
			g_built = true;
		}

		// ── 섞기(오디오 스레드) ────────────────────────────────────────────────────
		std::array<std::atomic<float>, kBeds> g_target{};
		std::array<std::atomic<float>, kBeds> g_targetPan{};
		std::array<float, kBeds> g_cur{};
		std::array<float, kBeds> g_curPan{};
		AudioStream g_stream{};
		bool g_streamOn = false;

		void Render(float* out, unsigned frames, std::array<Loop, kBeds>& beds, std::array<float, kBeds>& cur, std::array<float, kBeds>& curPan,
			const float* target, const float* targetPan)
		{
			const float k = 1.f - std::exp(-1.f / (0.35f * kRate));   // 목표 음량을 0.35초 시간 상수로 따라간다
			for (unsigned f = 0; f < frames; ++f)
			{
				float l = 0.f, r = 0.f;
				for (int b = 0; b < kBeds; ++b)
				{
					cur[b] += (target[b] - cur[b]) * k;
					curPan[b] += (targetPan[b] - curPan[b]) * k;
					Loop& lp = beds[b];
					if (lp.s.empty() || cur[b] < 1e-4f) { if (!lp.s.empty()) lp.pos = (lp.pos + 1) % lp.s.size(); continue; }
					const float v = lp.s[lp.pos] * cur[b] * kBedVolume[b];
					lp.pos = (lp.pos + 1) % lp.s.size();
					const float a = std::clamp(curPan[b], 0.f, 1.f) * 1.5707963f;
					l += v * std::cos(a);
					r += v * std::sin(a);
				}
				out[f * 2] = std::clamp(l * kOverall * 1.41f, -1.f, 1.f);
				out[f * 2 + 1] = std::clamp(r * kOverall * 1.41f, -1.f, 1.f);
			}
		}

		void Callback(void* buffer, unsigned int frames)
		{
			float target[kBeds], pan[kBeds];
			for (int b = 0; b < kBeds; ++b)
			{
				target[b] = g_target[b].load(std::memory_order_relaxed);
				pan[b] = g_targetPan[b].load(std::memory_order_relaxed);
			}
			Render(static_cast<float*>(buffer), frames, g_beds, g_cur, g_curPan, target, pan);
		}

		// ── 박자(메인 스레드) ──────────────────────────────────────────────────────
		float g_hammerAt = 0.f;       // 다음 망치질까지(초)
		int g_hammerLeft = 0;         // 이번 마디에 남은 망치질
		int g_lastHour = -1;
		float g_clock = 0.f;

		float Falloff(float d, float r)
		{
			const float u = d / std::max(0.5f, r);
			if (u >= 1.f) return 0.f;
			if (u <= 0.25f) return 1.f;
			const float t = (u - 0.25f) / 0.75f;
			return 1.f - t * t * (3.f - 2.f * t);
		}

		int BedOf(TownMap::AmbientKind k)
		{
			switch (k)
			{
			case TownMap::AmbientKind::eFountain: return static_cast<int>(Bed::Fountain);
			case TownMap::AmbientKind::eRiver: return static_cast<int>(Bed::River);
			case TownMap::AmbientKind::eWind: return static_cast<int>(Bed::Wind);
			case TownMap::AmbientKind::eMarket: return static_cast<int>(Bed::Market);
			case TownMap::AmbientKind::eSmithy: return static_cast<int>(Bed::Forge);
			default: return -1;
			}
		}
	}

	Mix MixAt(Vector2 tile, float night, float wind)
	{
		Mix m{};
		float best[kBeds]{};
		for (int b = 0; b < kBeds; ++b) m.pan[b] = 0.5f;
		for (const auto& row : TownMap::kAmbient)
		{
			const float dx = row.x - tile.x, dy = row.y - tile.y;
			const float g = Falloff(std::sqrt(dx * dx + dy * dy), row.radius);
			if (row.kind == TownMap::AmbientKind::eBell) { m.bell = std::max(m.bell, g); continue; }
			const int b = BedOf(row.kind);
			if (b < 0 || g <= best[b]) continue;
			best[b] = g;
			// 화면 가로(X − Y)로 좌우 — 12칸이면 끝까지 치우친다(가까우면 가운데)
			const float sx = (dx - dy) / 12.f;
			m.pan[b] = std::clamp(0.5f + sx * 0.4f * std::min(1.f, std::sqrt(dx * dx + dy * dy) / 3.f), 0.1f, 0.9f);
		}
		const float day = 1.f - std::clamp(night, 0.f, 1.f);
		m.gain[static_cast<int>(Bed::Fountain)] = best[static_cast<int>(Bed::Fountain)];
		m.gain[static_cast<int>(Bed::River)] = best[static_cast<int>(Bed::River)];
		m.gain[static_cast<int>(Bed::Wind)] = best[static_cast<int>(Bed::Wind)] * (0.35f + 0.9f * std::clamp(wind, 0.f, 1.f));
		m.gain[static_cast<int>(Bed::Market)] = best[static_cast<int>(Bed::Market)] * (0.15f + 0.85f * day);   // 밤이면 파장
		m.gain[static_cast<int>(Bed::Forge)] = best[static_cast<int>(Bed::Forge)] * (0.3f + 0.7f * day);       // 밤에는 불씨만
		m.bell *= day > 0.4f ? 1.f : 0.f;
		return m;
	}

	void Init()
	{
		if (g_streamOn || !IsAudioDeviceReady()) return;
		Build();
		for (int b = 0; b < kBeds; ++b) { g_target[b].store(0.f); g_targetPan[b].store(0.5f); g_cur[b] = 0.f; g_curPan[b] = 0.5f; }
		SetAudioStreamBufferSizeDefault(2048);
		g_stream = LoadAudioStream(kRate, 32, 2);
		SetAudioStreamCallback(g_stream, Callback);
		PlayAudioStream(g_stream);
		g_streamOn = true;
	}

	void Shutdown()
	{
		if (!g_streamOn) return;
		StopAudioStream(g_stream);
		UnloadAudioStream(g_stream);
		g_streamOn = false;
	}

	void Update(float dt, Vector2 listenerTile, float night, float hour, float wind, bool enabled)
	{
		g_clock += dt;
		const Mix m = enabled ? MixAt(listenerTile, night, wind) : Mix{};
		for (int b = 0; b < kBeds; ++b)
		{
			g_target[b].store(m.gain[b], std::memory_order_relaxed);
			g_targetPan[b].store(m.pan[b], std::memory_order_relaxed);
		}
		if (!sfx::Ready() || !enabled) { g_lastHour = -1; return; }

		// 대장간 망치: 3~6번 두드리고(0.48초 안팎) 숨을 고른다. 밤에는 쉰다.
		const float forge = m.gain[static_cast<int>(Bed::Forge)];
		g_hammerAt -= dt;
		if (g_hammerAt <= 0.f)
		{
			if (g_hammerLeft <= 0)
			{
				g_hammerLeft = static_cast<int>(Rand(3.f, 6.99f));
				g_hammerAt = Rand(1.4f, 3.6f);
			}
			else
			{
				--g_hammerLeft;
				g_hammerAt = Rand(0.44f, 0.54f);
				if (forge > 0.06f && night < 0.6f)
				{
					const sfx::Layer l[] = { { sfx::Id::Anvil, 0.f, 1.f, Rand(0.97f, 1.03f) } };
					sfx::Layered(l, 1, 0.55f * forge, m.pan[static_cast<int>(Bed::Forge)]);
				}
			}
		}

		// 예배당 종: 낮(6~19시)의 정시마다 세 번, 종에서 멀수록 작게
		const int h = static_cast<int>(std::floor(hour));
		if (g_lastHour >= 0 && h != g_lastHour && h >= 6 && h <= 19 && m.bell > 0.03f)
		{
			const sfx::Layer tolls[] = {
				{ sfx::Id::BellToll, 0.f, 1.f, 0.72f }, { sfx::Id::BellToll, 2.6f, 0.9f, 0.72f }, { sfx::Id::BellToll, 5.2f, 0.85f, 0.72f },
			};
			sfx::Layered(tolls, 3, 0.5f * m.bell, 0.5f);
		}
		g_lastHour = h;
	}

	bool DumpWav(const char* path, Vector2 tile, float night, float wind, float seconds)
	{
		Build();
		const Mix m = MixAt(tile, night, wind);
		std::array<Loop, kBeds> beds = g_beds;
		std::array<float, kBeds> cur{}, curPan{};
		for (int b = 0; b < kBeds; ++b) { cur[b] = m.gain[b]; curPan[b] = m.pan[b]; }
		const unsigned frames = static_cast<unsigned>(seconds * kRate);
		std::vector<float> buf(static_cast<size_t>(frames) * 2);
		Render(buf.data(), frames, beds, cur, curPan, m.gain, m.pan);
		std::vector<int16_t> pcm(buf.size());
		for (size_t i = 0; i < buf.size(); ++i) pcm[i] = static_cast<int16_t>(std::clamp(buf[i], -1.f, 1.f) * 32000.f);
		::Wave w{};
		w.frameCount = frames;
		w.sampleRate = kRate;
		w.sampleSize = 16;
		w.channels = 2;
		w.data = pcm.data();
		return ExportWave(w, path);
	}

	void DumpBeds(const char* dir)
	{
		Build();
		constexpr const char* kName[kBeds] = { "fountain", "river", "wind", "market", "forge" };
		for (int b = 0; b < kBeds; ++b)
		{
			const auto& x = g_beds[b].s;
			std::vector<int16_t> pcm(x.size() * 2);   // 고리 두 번(이음새 확인)
			for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<int16_t>(std::clamp(x[i % x.size()], -1.f, 1.f) * 32000.f);
			::Wave w{};
			w.frameCount = static_cast<unsigned int>(pcm.size());
			w.sampleRate = kRate;
			w.sampleSize = 16;
			w.channels = 1;
			w.data = pcm.data();
			char path[512];
			std::snprintf(path, sizeof(path), "%s/bed_%s.wav", dir, kName[b]);
			ExportWave(w, path);
		}
	}
}
