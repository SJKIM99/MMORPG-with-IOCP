#include "audio/Sfx.h"

#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace sfx
{
	namespace
	{
		constexpr int kRate = 22050;
		constexpr float kTau = 6.28318530718f;
		constexpr int kVoices = 4;

		struct Slot
		{
			Sound base{};
			std::array<Sound, kVoices> alias{};
			int next = 0;
			bool loaded = false;
		};

		std::array<Slot, static_cast<size_t>(Id::Count)> g_slots;
		bool g_ready = false;
		float g_master = 0.8f;
		std::mt19937 g_rng{ 1234 };

		float Rand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g_rng); }

		// ── 합성 도구 ─────────────────────────────────────────────────────────────
		struct Buf
		{
			std::vector<float> s;
			explicit Buf(float seconds) : s(static_cast<size_t>(seconds * kRate) + 1, 0.f) {}
			[[nodiscard]] int N() const { return static_cast<int>(s.size()); }
		};

		enum class Osc { Sine, Tri, Saw, Square, Noise };

		float OscValue(Osc o, float phase)
		{
			const float p = phase - std::floor(phase);
			switch (o)
			{
			case Osc::Sine:   return std::sin(p * kTau);
			case Osc::Tri:    return 4.f * std::fabs(p - 0.5f) - 1.f;
			case Osc::Saw:    return 2.f * p - 1.f;
			case Osc::Square: return p < 0.5f ? 1.f : -1.f;
			case Osc::Noise:  return Rand(-1.f, 1.f);
			}
			return 0.f;
		}

		// attack(초) 후 지수 감쇠(decay: 초당 감쇠 계수)
		float Env(float t, float attack, float decay)
		{
			if (t < attack) return attack > 0.f ? t / attack : 1.f;
			return std::exp(-(t - attack) * decay);
		}

		// start초부터 dur초 동안, 시간 비율 u(0~1)에 따른 주파수 f(u)로 톤을 더한다.
		template <typename FreqFn>
		void Tone(Buf& b, float start, float dur, Osc osc, FreqFn f, float amp, float attack, float decay, float vibratoHz = 0.f, float vibratoDepth = 0.f)
		{
			const int i0 = static_cast<int>(start * kRate);
			const int n = static_cast<int>(dur * kRate);
			float phase = Rand(0.f, 1.f);
			for (int i = 0; i < n && i0 + i < b.N(); ++i)
			{
				const float t = static_cast<float>(i) / kRate;
				const float u = static_cast<float>(i) / std::max(1, n - 1);
				float freq = f(u);
				if (vibratoHz > 0.f) freq *= 1.f + std::sin(t * kTau * vibratoHz) * vibratoDepth;
				phase += freq / kRate;
				// 끝 3ms 페이드아웃(클릭 방지)
				const float tail = std::min(1.f, static_cast<float>(n - i) / (0.003f * kRate));
				b.s[i0 + i] += OscValue(osc, phase) * amp * Env(t, attack, decay) * tail;
			}
		}

		// 필터 걸린 노이즈. cutoff(u) Hz 저역통과, hp > 0이면 그 아래를 깎는다.
		template <typename CutFn>
		void Noise(Buf& b, float start, float dur, CutFn cutoff, float amp, float attack, float decay, float hp = 0.f)
		{
			const int i0 = static_cast<int>(start * kRate);
			const int n = static_cast<int>(dur * kRate);
			float lp = 0.f, lp2 = 0.f, hpState = 0.f;
			for (int i = 0; i < n && i0 + i < b.N(); ++i)
			{
				const float t = static_cast<float>(i) / kRate;
				const float u = static_cast<float>(i) / std::max(1, n - 1);
				const float a = 1.f - std::exp(-kTau * cutoff(u) / kRate);
				const float x = Rand(-1.f, 1.f);
				lp += (x - lp) * a;
				lp2 += (lp - lp2) * a;
				float y = lp2;
				if (hp > 0.f)
				{
					const float ah = 1.f - std::exp(-kTau * hp / kRate);
					hpState += (y - hpState) * ah;
					y -= hpState;
				}
				const float tail = std::min(1.f, static_cast<float>(n - i) / (0.003f * kRate));
				b.s[i0 + i] += y * amp * Env(t, attack, decay) * tail;
			}
		}

		auto Const(float v) { return [v](float) { return v; }; }
		auto Sweep(float a, float b) { return [a, b](float u) { return a + (b - a) * u; }; }
		auto ExpSweep(float a, float b) { return [a, b](float u) { return a * std::pow(b / a, u); }; }

		void Load(Id id, Buf& b, float peak)
		{
			// 정규화 + 16비트 변환
			float mx = 0.0001f;
			for (float v : b.s) mx = std::max(mx, std::fabs(v));
			const float k = peak / mx;
			std::vector<int16_t> pcm(b.s.size());
			for (size_t i = 0; i < b.s.size(); ++i)
			{
				float v = b.s[i] * k;
				// 부드러운 클리핑
				v = std::tanh(v * 1.1f) / std::tanh(1.1f);
				pcm[i] = static_cast<int16_t>(std::clamp(v, -1.f, 1.f) * 32000.f);
			}
			::Wave w{};
			w.frameCount = static_cast<unsigned int>(pcm.size());
			w.sampleRate = kRate;
			w.sampleSize = 16;
			w.channels = 1;
			w.data = pcm.data();
			// 개발용: CLIENT_SFX_DUMP=폴더 를 주면 합성한 소리를 WAV로 저장(파형 점검용)
			if (const char* dump = std::getenv("CLIENT_SFX_DUMP"))
			{
				char path[512];
				std::snprintf(path, sizeof(path), "%s/sfx_%02d.wav", dump, static_cast<int>(id));
				ExportWave(w, path);
			}
			Slot& slot = g_slots[static_cast<size_t>(id)];
			slot.base = LoadSoundFromWave(w);
			for (auto& a : slot.alias)
				a = LoadSoundAlias(slot.base);
			slot.loaded = true;
		}

		void Build()
		{
			// ── UI ──
			{ Buf b(0.06f); Tone(b, 0, 0.05f, Osc::Sine, Const(1250), 1.f, 0.001f, 70.f); Noise(b, 0, 0.01f, Const(6000), 0.3f, 0.f, 300.f); Load(Id::Click, b, 0.35f); }
			{ Buf b(0.03f); Tone(b, 0, 0.025f, Osc::Sine, Const(1900), 1.f, 0.001f, 150.f); Load(Id::Hover, b, 0.12f); }
			{ Buf b(0.14f); Tone(b, 0, 0.06f, Osc::Tri, Const(520), 1.f, 0.002f, 30.f); Tone(b, 0.05f, 0.08f, Osc::Tri, Const(780), 1.f, 0.002f, 30.f); Load(Id::Open, b, 0.35f); }
			{ Buf b(0.14f); Tone(b, 0, 0.06f, Osc::Tri, Const(700), 1.f, 0.002f, 30.f); Tone(b, 0.05f, 0.08f, Osc::Tri, Const(470), 1.f, 0.002f, 30.f); Load(Id::Close, b, 0.3f); }
			{ Buf b(0.2f); Tone(b, 0, 0.18f, Osc::Square, Const(150), 0.6f, 0.005f, 12.f, 18.f, 0.05f); Tone(b, 0, 0.18f, Osc::Square, Const(113), 0.5f, 0.005f, 12.f); Load(Id::Error, b, 0.3f); }

			// ── 전투 ──
			{ Buf b(0.22f); Noise(b, 0, 0.2f, [](float u) { return 400.f + 3500.f * std::sin(u * 3.14159f); }, 1.f, 0.03f, 9.f, 300.f); Load(Id::Swing, b, 0.45f); }
			{
				Buf b(0.18f);
				Tone(b, 0, 0.14f, Osc::Sine, ExpSweep(140, 55), 1.f, 0.001f, 22.f);
				Noise(b, 0, 0.05f, Const(2600), 0.9f, 0.f, 60.f);
				Tone(b, 0, 0.04f, Osc::Square, ExpSweep(900, 300), 0.15f, 0.f, 70.f);
				Load(Id::Hit, b, 0.7f);
			}
			{
				Buf b(0.35f);
				Tone(b, 0, 0.3f, Osc::Sine, ExpSweep(120, 38), 1.f, 0.001f, 11.f);
				Noise(b, 0, 0.1f, Sweep(5000, 800), 1.1f, 0.f, 28.f);
				Tone(b, 0.005f, 0.25f, Osc::Sine, Const(2450), 0.25f, 0.f, 14.f);
				Tone(b, 0.005f, 0.25f, Osc::Sine, Const(3710), 0.15f, 0.f, 18.f);
				Load(Id::HitHeavy, b, 0.85f);
			}
			{
				Buf b(0.4f);
				Noise(b, 0, 0.08f, Const(3000), 1.f, 0.f, 40.f);
				Tone(b, 0.02f, 0.3f, Osc::Square, ExpSweep(620, 140), 0.35f, 0.002f, 9.f);
				Tone(b, 0.02f, 0.3f, Osc::Sine, ExpSweep(310, 70), 0.6f, 0.002f, 8.f);
				Load(Id::Kill, b, 0.55f);
			}
			{
				Buf b(0.2f);
				Tone(b, 0, 0.16f, Osc::Saw, ExpSweep(190, 120), 0.7f, 0.004f, 16.f);
				Noise(b, 0, 0.08f, Const(1500), 0.6f, 0.f, 40.f);
				Load(Id::PlayerHurt, b, 0.55f);
			}
			{
				Buf b(1.4f);
				const float notes[] = { 392.f, 311.f, 262.f, 196.f };
				for (int i = 0; i < 4; ++i)
					Tone(b, i * 0.22f, 0.6f, Osc::Tri, Const(notes[i]), 0.6f, 0.01f, 4.f);
				Tone(b, 0, 1.3f, Osc::Sine, Const(98), 0.5f, 0.2f, 1.8f);
				Load(Id::Death, b, 0.55f);
			}
			{ Buf b(0.16f); Noise(b, 0, 0.14f, Sweep(1200, 4500), 1.f, 0.02f, 16.f, 600.f); Load(Id::MonsterSwipe, b, 0.4f); }
			{
				Buf b(0.5f);
				Noise(b, 0, 0.3f, [](float u) { return 300.f + 5000.f * std::sin(u * 3.14159f); }, 1.f, 0.05f, 6.f, 200.f);
				Tone(b, 0.12f, 0.35f, Osc::Sine, Const(1560), 0.35f, 0.f, 8.f);
				Tone(b, 0.12f, 0.35f, Osc::Sine, Const(2340), 0.2f, 0.f, 10.f);
				Tone(b, 0.1f, 0.2f, Osc::Sine, ExpSweep(110, 45), 0.8f, 0.001f, 14.f);
				Load(Id::Skill, b, 0.7f);
			}

			// ── 아이템 ──
			{
				Buf b(0.2f);
				Tone(b, 0, 0.08f, Osc::Sine, Const(1320), 1.f, 0.001f, 30.f);
				Tone(b, 0, 0.08f, Osc::Sine, Const(3960), 0.3f, 0.001f, 50.f);
				Tone(b, 0.06f, 0.13f, Osc::Sine, Const(1760), 1.f, 0.001f, 22.f);
				Tone(b, 0.06f, 0.13f, Osc::Sine, Const(5280), 0.25f, 0.001f, 40.f);
				Load(Id::Coin, b, 0.4f);
			}
			{ Buf b(0.1f); Tone(b, 0, 0.09f, Osc::Tri, ExpSweep(600, 1300), 1.f, 0.002f, 20.f); Load(Id::Pickup, b, 0.4f); }
			{ Buf b(0.14f); Tone(b, 0, 0.12f, Osc::Sine, ExpSweep(220, 90), 1.f, 0.001f, 25.f); Noise(b, 0, 0.04f, Const(1200), 0.5f, 0.f, 60.f); Load(Id::Drop, b, 0.45f); }
			{
				Buf b(0.3f);
				Noise(b, 0, 0.02f, Const(8000), 0.8f, 0.f, 150.f, 2000.f);
				Tone(b, 0, 0.28f, Osc::Sine, Const(812), 0.6f, 0.001f, 14.f);
				Tone(b, 0, 0.28f, Osc::Sine, Const(1333), 0.5f, 0.001f, 18.f);
				Tone(b, 0, 0.2f, Osc::Sine, Const(2231), 0.3f, 0.001f, 25.f);
				Load(Id::Equip, b, 0.45f);
			}
			{
				Buf b(0.45f);
				const float n[] = { 1320.f, 1760.f, 2640.f };
				for (int i = 0; i < 3; ++i)
				{
					Tone(b, i * 0.07f, 0.2f, Osc::Sine, Const(n[i]), 1.f, 0.001f, 16.f);
					Tone(b, i * 0.07f, 0.2f, Osc::Sine, Const(n[i] * 3.f), 0.25f, 0.001f, 30.f);
				}
				Load(Id::Buy, b, 0.4f);
			}
			{
				Buf b(0.45f);
				for (int i = 0; i < 6; ++i)
				{
					const float f = Rand(280.f, 620.f);
					Tone(b, i * 0.055f, 0.06f, Osc::Sine, ExpSweep(f, f * 1.6f), 0.8f, 0.004f, 30.f);
				}
				Noise(b, 0.25f, 0.18f, Const(700), 0.5f, 0.02f, 14.f);
				Load(Id::Potion, b, 0.4f);
			}
			{
				Buf b(0.6f);
				Tone(b, 0, 0.55f, Osc::Sine, Const(880), 0.8f, 0.01f, 6.f);
				Tone(b, 0.05f, 0.5f, Osc::Sine, Const(1320), 0.6f, 0.01f, 7.f);
				Tone(b, 0.1f, 0.45f, Osc::Sine, Const(1760), 0.4f, 0.01f, 8.f);
				Load(Id::Heal, b, 0.35f);
			}
			{
				Buf b(1.5f);
				const float n[] = { 523.25f, 659.25f, 783.99f, 1046.5f };
				for (int i = 0; i < 4; ++i)
				{
					Tone(b, i * 0.09f, 0.9f, Osc::Tri, Const(n[i]), 0.7f, 0.005f, 3.5f);
					Tone(b, i * 0.09f, 0.9f, Osc::Sine, Const(n[i] * 2.f), 0.3f, 0.005f, 4.5f);
				}
				for (int i = 0; i < 10; ++i)
					Tone(b, 0.4f + i * 0.08f, 0.25f, Osc::Sine, Const(Rand(2000.f, 4200.f)), 0.18f, 0.002f, 14.f);
				Tone(b, 0.36f, 1.0f, Osc::Sine, Const(261.6f), 0.5f, 0.05f, 2.5f);
				Load(Id::LevelUp, b, 0.6f);
			}
			{
				Buf b(0.7f);
				Tone(b, 0, 0.65f, Osc::Sine, ExpSweep(300, 2200), 0.8f, 0.05f, 3.f, 11.f, 0.08f);
				Noise(b, 0, 0.6f, Sweep(1500, 9000), 0.5f, 0.1f, 4.f, 1500.f);
				Load(Id::Teleport, b, 0.5f);
			}
			{
				Buf b(0.5f);
				Noise(b, 0, 0.45f, ExpSweep(500, 6000), 1.f, 0.08f, 6.f, 400.f);
				Tone(b, 0.05f, 0.35f, Osc::Sine, ExpSweep(400, 1200), 0.4f, 0.02f, 7.f);
				Load(Id::Haste, b, 0.45f);
			}
			{
				Buf b(0.9f);
				Tone(b, 0, 0.85f, Osc::Sine, ExpSweep(220, 880), 0.8f, 0.4f, 1.f, 7.f, 0.04f);
				Tone(b, 0, 0.85f, Osc::Sine, ExpSweep(330, 1320), 0.4f, 0.4f, 1.f, 9.f, 0.05f);
				Noise(b, 0.3f, 0.55f, Sweep(2000, 8000), 0.25f, 0.3f, 2.f, 2000.f);
				Load(Id::EnchantCharge, b, 0.4f);
			}
			{
				Buf b(1.4f);
				const float n[] = { 1046.5f, 1318.5f, 1568.f, 2093.f };
				for (int i = 0; i < 4; ++i)
				{
					Tone(b, i * 0.03f, 1.3f, Osc::Sine, Const(n[i]), 0.6f, 0.002f, 2.6f);
					Tone(b, i * 0.03f, 0.8f, Osc::Sine, Const(n[i] * 2.76f), 0.15f, 0.002f, 5.f);
				}
				for (int i = 0; i < 12; ++i)
					Tone(b, 0.1f + i * 0.06f, 0.2f, Osc::Sine, Const(Rand(3000.f, 6000.f)), 0.12f, 0.002f, 18.f);
				Load(Id::EnchantSuccess, b, 0.6f);
			}
			{
				Buf b(0.9f);
				for (int i = 0; i < 5; ++i)
					Noise(b, i * 0.035f, 0.12f, Const(9000), 0.8f, 0.f, 40.f, 3000.f);
				const float n[] = { 2211.f, 3517.f, 4870.f, 1609.f };
				for (int i = 0; i < 4; ++i)
					Tone(b, i * 0.02f, 0.5f, Osc::Sine, ExpSweep(n[i], n[i] * 0.7f), 0.25f, 0.f, 9.f);
				Tone(b, 0.1f, 0.7f, Osc::Saw, ExpSweep(300, 60), 0.3f, 0.02f, 4.f);
				Load(Id::EnchantFail, b, 0.55f);
			}

			// ── 보스 ──
			{
				Buf b(1.5f);
				Tone(b, 0, 1.4f, Osc::Saw, [](float u) { return 70.f + 30.f * std::sin(u * 3.14159f); }, 0.8f, 0.15f, 1.6f, 6.f, 0.06f);
				Tone(b, 0, 1.4f, Osc::Saw, [](float u) { return 105.f + 40.f * std::sin(u * 3.14159f); }, 0.5f, 0.15f, 1.6f, 7.f, 0.07f);
				Noise(b, 0, 1.3f, Const(900), 0.6f, 0.2f, 1.8f);
				Load(Id::BossRoar, b, 0.7f);
			}
			{
				Buf b(1.2f);
				Tone(b, 0, 1.1f, Osc::Sine, ExpSweep(75, 28), 1.f, 0.002f, 3.5f);
				Noise(b, 0, 0.6f, Sweep(1800, 200), 1.f, 0.f, 6.f);
				Noise(b, 0.05f, 0.9f, Const(250), 0.8f, 0.05f, 3.f);
				Load(Id::Slam, b, 0.95f);
			}
			{ Buf b(1.3f); Tone(b, 0, 1.25f, Osc::Saw, ExpSweep(70, 170), 0.6f, 0.9f, 0.5f, 9.f, 0.05f); Noise(b, 0, 1.2f, Sweep(300, 1500), 0.3f, 0.9f, 0.5f); Load(Id::SlamWarn, b, 0.45f); }

			// ── 기타 ──
			{ Buf b(0.05f); Noise(b, 0, 0.045f, Const(900), 1.f, 0.002f, 70.f, 120.f); Load(Id::Step, b, 0.18f); }
			{
				Buf b(0.7f);
				Tone(b, 0, 0.18f, Osc::Square, Const(784), 0.4f, 0.005f, 8.f);
				Tone(b, 0, 0.18f, Osc::Sine, Const(784), 0.5f, 0.005f, 8.f);
				Tone(b, 0.16f, 0.5f, Osc::Square, Const(1046.5f), 0.4f, 0.005f, 4.f);
				Tone(b, 0.16f, 0.5f, Osc::Sine, Const(1046.5f), 0.5f, 0.005f, 4.f);
				Load(Id::Notice, b, 0.45f);
			}
			{ Buf b(0.06f); Tone(b, 0, 0.05f, Osc::Sine, ExpSweep(900, 1300), 1.f, 0.002f, 60.f); Load(Id::Chat, b, 0.2f); }

			// ── 환경음(가끔 한 번씩) ──
			{
				Buf b(0.5f);
				for (int k = 0; k < 3; ++k)
					Tone(b, k * 0.12f, 0.09f, Osc::Sine, [](float u) { return 2600.f + 1400.f * std::sin(u * 3.14159f) - 600.f * u; }, 0.7f, 0.005f, 20.f);
				Load(Id::AmbBird, b, 0.18f);
			}
			{
				Buf b(0.45f);
				const int n = b.N();
				for (int i = 0; i < n; ++i)
				{
					const float t = static_cast<float>(i) / kRate;
					const float gate = (std::fmod(t, 0.05f) < 0.025f) ? 1.f : 0.f;
					b.s[i] = std::sin(t * kTau * 4400.f) * gate * std::sin(t / 0.45f * 3.14159f);
				}
				Load(Id::AmbCricket, b, 0.06f);
			}
			{ Buf b(2.5f); Noise(b, 0, 2.4f, [](float u) { return 300.f + 500.f * std::sin(u * 3.14159f); }, 1.f, 1.0f, 1.2f); Load(Id::AmbWind, b, 0.2f); }
			{ Buf b(0.08f); Tone(b, 0, 0.07f, Osc::Sine, ExpSweep(250, 900), 1.f, 0.002f, 40.f); Load(Id::AmbBubble, b, 0.15f); }
			{
				Buf b(0.9f);
				for (int i = 0; i < 14; ++i)
					Noise(b, Rand(0.f, 0.8f), 0.02f, Const(7000), Rand(0.3f, 1.f), 0.f, 200.f, 1500.f);
				Load(Id::AmbCrackle, b, 0.2f);
			}
			{ Buf b(3.f); Noise(b, 0, 2.9f, [](float u) { return 250.f + 900.f * std::sin(u * 3.14159f); }, 1.f, 1.2f, 1.1f); Load(Id::AmbWave, b, 0.22f); }
			{
				Buf b(0.8f);
				for (int k = 0; k < 2; ++k)
				{
					Tone(b, k * 0.35f, 0.22f, Osc::Saw, ExpSweep(560, 420), 0.7f, 0.01f, 9.f, 30.f, 0.03f);
					Noise(b, k * 0.35f, 0.2f, Const(1800), 0.3f, 0.01f, 10.f);
				}
				Load(Id::AmbCrow, b, 0.2f);
			}
		}
	}

	void Init()
	{
		InitAudioDevice();
		if (!IsAudioDeviceReady())
		{
			TraceLog(LOG_WARNING, "Audio device not available - sound disabled");
			return;
		}
		Build();
		SetMasterVolume(g_master);
		g_ready = true;
	}

	void Shutdown()
	{
		if (!g_ready)
			return;
		for (auto& s : g_slots)
		{
			if (!s.loaded) continue;
			for (auto& a : s.alias) UnloadSoundAlias(a);
			UnloadSound(s.base);
			s.loaded = false;
		}
		CloseAudioDevice();
		g_ready = false;
	}

	bool Ready() { return g_ready; }

	void Play(Id id, float volume, float pitchVar, float pan)
	{
		if (!g_ready)
			return;
		Slot& slot = g_slots[static_cast<size_t>(id)];
		if (!slot.loaded)
			return;
		Sound& s = slot.alias[slot.next];
		slot.next = (slot.next + 1) % kVoices;
		SetSoundVolume(s, std::clamp(volume, 0.f, 1.5f));
		SetSoundPitch(s, 1.f + Rand(-pitchVar, pitchVar));
		SetSoundPan(s, std::clamp(pan, 0.f, 1.f));
		PlaySound(s);
	}

	void SetMasterVolume(float v)
	{
		g_master = std::clamp(v, 0.f, 1.f);
		if (IsAudioDeviceReady())
			::SetMasterVolume(g_master);
	}

	float MasterVolume() { return g_master; }
}
