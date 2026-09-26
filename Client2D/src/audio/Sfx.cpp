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
			// 휘두름: 칼바람이 귀를 스치듯(대역이 올라갔다 내려오고, 끝에 짧은 쉿)
			{
				Buf b(0.24f);
				Noise(b, 0, 0.22f, [](float u) { return 500.f + 4200.f * std::sin(u * 3.14159f); }, 1.f, 0.035f, 8.f, 350.f);
				Noise(b, 0.06f, 0.1f, Sweep(7000, 3000), 0.35f, 0.01f, 30.f, 2500.f);
				Load(Id::Swing, b, 0.45f);
			}
			// 타격 — 네 겹: ①딱(3ms 고역 딸깍) ②퍽(낮은 몸통 쿵) ③살·갑옷 부딪는 바삭함 ④칼날의 짧은 쇳소리
			{
				Buf b(0.24f);
				Noise(b, 0, 0.006f, Const(9500), 1.2f, 0.f, 380.f, 2200.f);
				Tone(b, 0, 0.18f, Osc::Sine, ExpSweep(185, 50), 1.15f, 0.001f, 19.f);
				Noise(b, 0.002f, 0.07f, Sweep(3400, 900), 0.85f, 0.001f, 42.f, 280.f);
				Tone(b, 0.003f, 0.09f, Osc::Tri, ExpSweep(1500, 700), 0.13f, 0.f, 45.f);
				Tone(b, 0.004f, 0.12f, Osc::Sine, Const(2830), 0.07f, 0.f, 32.f);
				Load(Id::Hit, b, 0.78f);
			}
			// 강타 · 치명타: 위에 땅울림(서브 베이스) · 긴 꼬리 · 금속 배음 셋
			{
				Buf b(0.45f);
				Noise(b, 0, 0.008f, Const(10000), 1.4f, 0.f, 300.f, 1800.f);
				Tone(b, 0, 0.38f, Osc::Sine, ExpSweep(150, 38), 1.2f, 0.001f, 9.5f);
				Tone(b, 0.002f, 0.4f, Osc::Sine, Const(47), 0.7f, 0.004f, 7.f);
				Noise(b, 0.002f, 0.14f, Sweep(5200, 600), 1.2f, 0.f, 22.f, 180.f);
				Tone(b, 0.005f, 0.3f, Osc::Sine, Const(2450), 0.22f, 0.f, 13.f);
				Tone(b, 0.005f, 0.3f, Osc::Sine, Const(3710), 0.14f, 0.f, 16.f);
				Tone(b, 0.005f, 0.22f, Osc::Sine, Const(5230), 0.08f, 0.f, 22.f);
				Load(Id::HitHeavy, b, 0.92f);
			}
			// 처치: 부서짐 + 낮은 쿵 + 내려가는 음(쓰러짐이 귀로 들린다)
			{
				Buf b(0.5f);
				Noise(b, 0, 0.01f, Const(9000), 1.2f, 0.f, 250.f, 1500.f);
				Noise(b, 0, 0.12f, Sweep(4200, 700), 1.f, 0.f, 26.f, 200.f);
				Tone(b, 0, 0.35f, Osc::Sine, ExpSweep(120, 36), 0.9f, 0.002f, 8.f);
				Tone(b, 0.02f, 0.3f, Osc::Square, ExpSweep(620, 140), 0.3f, 0.002f, 9.f);
				Tone(b, 0.02f, 0.34f, Osc::Sine, ExpSweep(310, 70), 0.55f, 0.002f, 7.f);
				Load(Id::Kill, b, 0.62f);
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
			// 바닥 종류별 발소리(Phase 1-C) — 짧고 작게, 종류마다 결이 다르게
			{   // 풀: 스치는 소리(낮은 잡음 + 가는 쉬익)
				Buf b(0.10f);
				Noise(b, 0, 0.08f, Const(1100), 1.f, 0.004f, 34.f, 180.f);
				Noise(b, 0.004f, 0.06f, Const(4200), 0.35f, 0.002f, 55.f, 2500.f);
				Load(Id::StepGrass, b, 0.16f);
			}
			{   // 돌: 딱 부딪힘 + 짧은 쿵
				Buf b(0.08f);
				Noise(b, 0, 0.03f, Const(2600), 1.f, 0.001f, 110.f, 400.f);
				Tone(b, 0, 0.06f, Osc::Sine, ExpSweep(170, 90), 0.8f, 0.001f, 55.f);
				Load(Id::StepStone, b, 0.18f);
			}
			{   // 나무: 속이 빈 두드림
				Buf b(0.10f);
				Tone(b, 0, 0.09f, Osc::Sine, ExpSweep(260, 190), 0.9f, 0.001f, 32.f);
				Noise(b, 0, 0.04f, Const(900), 0.5f, 0.001f, 60.f, 200.f);
				Load(Id::StepWood, b, 0.17f);
			}
			{   // 눈: 뽀드득(짧은 알갱이 여러 개)
				Buf b(0.13f);
				for (int i = 0; i < 5; ++i)
					Noise(b, i * 0.018f, 0.03f, Const(3000.f + 500.f * (i % 3)), 0.8f - i * 0.1f, 0.001f, 90.f, 1200.f);
				Load(Id::StepSnow, b, 0.16f);
			}
			{   // 모래: 부드럽게 쓸림
				Buf b(0.11f);
				Noise(b, 0, 0.1f, Sweep(2400, 1400), 1.f, 0.01f, 26.f, 500.f);
				Load(Id::StepSand, b, 0.13f);
			}
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

			// ── V2 전투: 직업 기술 ─────────────────────────────────────────────
			// 기사 — 둔탁한 금속·무게감
			{
				Buf b(0.45f);
				Tone(b, 0, 0.4f, Osc::Sine, ExpSweep(95, 36), 1.f, 0.001f, 9.f);
				Noise(b, 0, 0.09f, Sweep(3500, 900), 1.f, 0.f, 30.f);
				const float clang[] = { 523.f, 811.f, 1247.f, 1733.f };
				for (int i = 0; i < 4; ++i)
					Tone(b, 0.004f, 0.35f, Osc::Sine, Const(clang[i]), 0.22f - i * 0.03f, 0.f, 12.f + i * 4.f);
				Load(Id::ShieldBash, b, 0.85f);
			}
			{
				Buf b(0.5f);
				Noise(b, 0, 0.45f, [](float u) { return 500.f + 3200.f * std::sin(u * 3.14159f); }, 1.f, 0.06f, 5.f, 250.f);
				Noise(b, 0.12f, 0.3f, [](float u) { return 700.f + 2600.f * std::sin(u * 3.14159f); }, 0.6f, 0.04f, 7.f, 300.f);
				Tone(b, 0, 0.4f, Osc::Sine, ExpSweep(140, 90), 0.35f, 0.05f, 6.f);
				Load(Id::Whirl, b, 0.6f);
			}
			{
				Buf b(0.4f);
				Noise(b, 0, 0.35f, ExpSweep(300, 2800), 1.f, 0.03f, 7.f, 200.f);
				Tone(b, 0, 0.3f, Osc::Sine, ExpSweep(70, 45), 0.7f, 0.01f, 8.f);
				Load(Id::Dash, b, 0.6f);
			}
			{
				Buf b(0.8f);
				const float n[] = { 659.f, 988.f, 1319.f };
				for (int i = 0; i < 3; ++i)
					Tone(b, i * 0.03f, 0.7f, Osc::Sine, Const(n[i]), 0.5f, 0.06f, 3.5f, 5.f, 0.01f);
				Noise(b, 0, 0.3f, Const(6000), 0.25f, 0.05f, 9.f, 2500.f);
				Tone(b, 0, 0.25f, Osc::Sine, ExpSweep(110, 70), 0.6f, 0.002f, 12.f);
				Load(Id::GuardUp, b, 0.5f);
			}
			{
				Buf b(0.5f);
				Tone(b, 0, 0.48f, Osc::Saw, ExpSweep(70, 240), 0.5f, 0.3f, 0.6f, 8.f, 0.04f);
				Tone(b, 0, 0.48f, Osc::Sine, ExpSweep(160, 520), 0.5f, 0.3f, 0.6f, 11.f, 0.03f);
				Noise(b, 0, 0.45f, ExpSweep(400, 3000), 0.3f, 0.35f, 0.8f, 300.f);
				Load(Id::ChargeUp, b, 0.45f);
			}
			{
				Buf b(1.1f);
				Tone(b, 0, 1.0f, Osc::Sine, ExpSweep(80, 26), 1.f, 0.001f, 4.f);
				Noise(b, 0, 0.35f, Sweep(6500, 300), 1.2f, 0.f, 9.f);
				Noise(b, 0, 0.04f, Const(9000), 0.8f, 0.f, 80.f, 2500.f);
				Tone(b, 0.01f, 0.6f, Osc::Sine, Const(1840), 0.2f, 0.f, 7.f);
				Tone(b, 0.01f, 0.6f, Osc::Sine, Const(2710), 0.12f, 0.f, 9.f);
				Noise(b, 0.05f, 0.9f, Const(260), 0.7f, 0.05f, 3.f);
				Load(Id::Execute, b, 0.95f);
			}
			// 각인술사 — 마력·원소
			{
				Buf b(0.2f);
				Tone(b, 0, 0.16f, Osc::Sine, ExpSweep(1500, 620), 0.8f, 0.002f, 16.f);
				Tone(b, 0, 0.12f, Osc::Tri, ExpSweep(2300, 900), 0.4f, 0.002f, 20.f);
				Noise(b, 0, 0.06f, Const(7000), 0.25f, 0.f, 50.f, 2000.f);
				Load(Id::RuneBolt, b, 0.45f);
			}
			{
				Buf b(0.8f);
				for (int i = 0; i < 12; ++i)
					Tone(b, Rand(0.f, 0.45f), 0.25f, Osc::Sine, Const(Rand(1900.f, 5200.f)), Rand(0.15f, 0.35f), 0.001f, 14.f);
				Noise(b, 0, 0.6f, Sweep(4000, 9000), 0.4f, 0.02f, 4.f, 3000.f);
				Tone(b, 0, 0.3f, Osc::Sine, ExpSweep(300, 140), 0.4f, 0.002f, 9.f);
				Load(Id::FrostSeal, b, 0.5f);
			}
			{
				Buf b(0.8f);
				Noise(b, 0, 0.7f, [](float u) { return 300.f + 2600.f * std::sin(std::min(1.f, u * 1.6f) * 3.14159f); }, 1.f, 0.04f, 3.5f, 120.f);
				for (int i = 0; i < 16; ++i)
					Noise(b, Rand(0.05f, 0.7f), 0.015f, Const(6500), Rand(0.2f, 0.6f), 0.f, 200.f, 1800.f);
				Tone(b, 0, 0.5f, Osc::Sine, ExpSweep(120, 60), 0.6f, 0.01f, 5.f);
				Load(Id::FireRing, b, 0.65f);
			}
			{
				Buf b(0.45f);
				Tone(b, 0, 0.2f, Osc::Sine, ExpSweep(320, 2600), 0.7f, 0.01f, 8.f);
				Tone(b, 0.12f, 0.25f, Osc::Sine, ExpSweep(2400, 380), 0.6f, 0.005f, 9.f);
				Noise(b, 0, 0.4f, Sweep(3000, 8000), 0.25f, 0.05f, 6.f, 2500.f);
				Load(Id::Blink, b, 0.45f);
			}
			{
				Buf b(1.0f);
				Noise(b, 0, 0.18f, Const(9000), 1.2f, 0.f, 22.f, 1400.f);
				Noise(b, 0.02f, 0.9f, Const(320), 0.9f, 0.04f, 3.5f);
				Tone(b, 0, 0.7f, Osc::Sine, ExpSweep(60, 35), 0.5f, 0.02f, 4.f);
				for (int i = 0; i < 5; ++i)
					Noise(b, 0.02f + i * 0.025f, 0.02f, Const(8000), 0.7f, 0.f, 120.f, 2000.f);
				Load(Id::Thunder, b, 0.7f);
			}
			{
				Buf b(0.9f);
				Tone(b, 0, 0.85f, Osc::Sine, ExpSweep(1900, 260), 0.5f, 0.2f, 1.2f, 9.f, 0.02f);
				Noise(b, 0, 0.85f, ExpSweep(1200, 300), 0.6f, 0.4f, 0.8f);
				Load(Id::MeteorFall, b, 0.5f);
			}
			{
				Buf b(1.4f);
				Tone(b, 0, 1.3f, Osc::Sine, ExpSweep(70, 22), 1.f, 0.001f, 3.f);
				Noise(b, 0, 0.5f, Sweep(7000, 250), 1.2f, 0.f, 6.f);
				Noise(b, 0.04f, 1.2f, Const(220), 0.8f, 0.05f, 2.2f);
				for (int i = 0; i < 18; ++i)
					Noise(b, Rand(0.1f, 1.1f), 0.02f, Const(5000), Rand(0.15f, 0.45f), 0.f, 150.f, 1200.f);
				Load(Id::MeteorImpact, b, 1.f);
			}
			// 밤사냥꾼 — 날카롭고 가볍게
			{
				Buf b(0.24f);
				Noise(b, 0, 0.07f, Sweep(2000, 6000), 1.f, 0.005f, 30.f, 1200.f);
				Noise(b, 0.1f, 0.07f, Sweep(2400, 7000), 0.9f, 0.005f, 30.f, 1200.f);
				Tone(b, 0.02f, 0.05f, Osc::Sine, Const(2900), 0.2f, 0.f, 60.f);
				Tone(b, 0.12f, 0.05f, Osc::Sine, Const(3300), 0.2f, 0.f, 60.f);
				Load(Id::Stab, b, 0.45f);
			}
			{
				Buf b(0.5f);
				Noise(b, 0, 0.45f, ExpSweep(900, 180), 1.f, 0.15f, 6.f);
				Tone(b, 0, 0.4f, Osc::Sine, ExpSweep(160, 55), 0.7f, 0.1f, 6.f, 6.f, 0.05f);
				Load(Id::ShadowStep, b, 0.55f);
			}
			{
				Buf b(0.4f);
				Noise(b, 0, 0.18f, [](float u) { return 600.f + 4000.f * std::sin(u * 3.14159f); }, 1.f, 0.02f, 10.f, 400.f);
				for (int i = 0; i < 6; ++i)
				{
					const float f = Rand(300.f, 700.f);
					Tone(b, 0.12f + i * 0.035f, 0.05f, Osc::Sine, ExpSweep(f, f * 1.7f), 0.35f, 0.003f, 35.f);
				}
				Load(Id::PoisonSlash, b, 0.5f);
			}
			{
				Buf b(0.3f);
				Noise(b, 0, 0.26f, ExpSweep(900, 5500), 1.f, 0.03f, 9.f, 700.f);
				Tone(b, 0, 0.2f, Osc::Sine, ExpSweep(500, 1200), 0.2f, 0.02f, 12.f);
				Load(Id::Evade, b, 0.4f);
			}
			{
				Buf b(0.4f);
				Noise(b, 0, 0.04f, Const(9000), 1.f, 0.f, 70.f, 2500.f);
				Tone(b, 0.005f, 0.3f, Osc::Sine, Const(2637), 0.35f, 0.f, 12.f);
				Tone(b, 0.005f, 0.3f, Osc::Sine, Const(3951), 0.2f, 0.f, 16.f);
				Tone(b, 0, 0.2f, Osc::Sine, ExpSweep(160, 60), 0.9f, 0.001f, 16.f);
				Load(Id::Backstab, b, 0.7f);
			}
			{
				Buf b(1.0f);
				for (int i = 0; i < 5; ++i)
					Noise(b, i * 0.14f, 0.12f, [](float u) { return 900.f + 5000.f * std::sin(u * 3.14159f); }, 0.8f, 0.01f, 16.f, 800.f);
				const float n[] = { 1047.f, 1319.f, 1568.f };
				for (int i = 0; i < 3; ++i)
					Tone(b, 0.02f, 0.9f, Osc::Sine, Const(n[i]), 0.18f, 0.1f, 2.5f, 6.f, 0.01f);
				Load(Id::MoonDance, b, 0.55f);
			}
			// 연계·피드백
			{
				Buf b(0.7f);
				const float n[] = { 523.25f, 659.25f, 783.99f, 1046.5f };
				for (int i = 0; i < 4; ++i)
				{
					Tone(b, i * 0.045f, 0.5f, Osc::Tri, Const(n[i]), 0.6f, 0.003f, 5.f);
					Tone(b, i * 0.045f, 0.4f, Osc::Sine, Const(n[i] * 2.f), 0.25f, 0.003f, 7.f);
				}
				for (int i = 0; i < 8; ++i)
					Tone(b, 0.15f + i * 0.04f, 0.15f, Osc::Sine, Const(Rand(2500.f, 5000.f)), 0.12f, 0.002f, 20.f);
				Load(Id::ComboSting, b, 0.55f);
			}
			{
				Buf b(0.25f);
				Tone(b, 0, 0.22f, Osc::Sine, Const(1568), 0.6f, 0.002f, 14.f);
				Tone(b, 0.03f, 0.2f, Osc::Sine, Const(2349), 0.35f, 0.002f, 16.f);
				Load(Id::SkillReady, b, 0.22f);
			}
			{
				Buf b(0.22f);
				Tone(b, 0, 0.2f, Osc::Sine, ExpSweep(240, 170), 0.8f, 0.004f, 12.f);
				Tone(b, 0, 0.2f, Osc::Square, ExpSweep(120, 85), 0.2f, 0.004f, 12.f);
				Load(Id::NoMana, b, 0.35f);
			}
			{
				Buf b(0.5f);
				const float n[] = { 1109.f, 1663.f, 2489.f, 3322.f };
				for (int i = 0; i < 4; ++i)
					Tone(b, 0, 0.45f, Osc::Sine, Const(n[i]), 0.35f - i * 0.06f, 0.f, 8.f + i * 3.f);
				Noise(b, 0, 0.03f, Const(9000), 0.8f, 0.f, 90.f, 2500.f);
				Load(Id::Counter, b, 0.55f);
			}
			{
				Buf b(0.5f);
				Tone(b, 0, 0.45f, Osc::Sine, ExpSweep(85, 32), 1.f, 0.001f, 7.f);
				Noise(b, 0, 0.35f, Sweep(1500, 200), 1.f, 0.f, 8.f);
				for (int i = 0; i < 8; ++i)
					Noise(b, Rand(0.03f, 0.35f), 0.02f, Const(4000), 0.4f, 0.f, 150.f, 1000.f);
				Load(Id::WallSlam, b, 0.8f);
			}
			{
				Buf b(0.6f);
				for (int i = 0; i < 12; ++i)
				{
					const float f = Rand(250.f, 900.f);
					Tone(b, Rand(0.f, 0.35f), 0.06f, Osc::Sine, ExpSweep(f, f * 1.8f), 0.5f, 0.003f, 30.f);
				}
				Noise(b, 0, 0.2f, Const(1500), 0.6f, 0.01f, 12.f);
				Load(Id::PoisonBurst, b, 0.5f);
			}
			{
				Buf b(0.9f);
				Noise(b, 0, 0.85f, Sweep(9000, 3000), 1.f, 0.01f, 3.5f, 2000.f);
				Tone(b, 0, 0.2f, Osc::Sine, ExpSweep(200, 70), 0.7f, 0.001f, 14.f);
				Load(Id::SteamBurst, b, 0.5f);
			}
			{
				Buf b(0.4f);
				Tone(b, 0, 0.38f, Osc::Saw, ExpSweep(180, 330), 0.45f, 0.08f, 3.f, 14.f, 0.08f);
				Tone(b, 0, 0.38f, Osc::Sine, ExpSweep(360, 660), 0.3f, 0.08f, 3.f);
				Load(Id::TeleWarn, b, 0.32f);
			}
			{
				Buf b(0.35f);
				Tone(b, 0, 0.3f, Osc::Square, ExpSweep(1100, 260), 0.35f, 0.001f, 10.f);
				Noise(b, 0, 0.05f, Const(6000), 0.7f, 0.f, 60.f, 1500.f);
				Tone(b, 0, 0.25f, Osc::Sine, Const(1760), 0.3f, 0.f, 14.f);
				Load(Id::Interrupt, b, 0.5f);
			}
			{
				Buf b(0.45f);
				const float n[] = { 880.f, 1174.7f, 1568.f };
				for (int i = 0; i < 3; ++i)
					Tone(b, i * 0.06f, 0.3f, Osc::Sine, Const(n[i]), 0.6f, 0.002f, 9.f);
				Load(Id::StatUp, b, 0.4f);
			}
			{ Buf b(0.08f); Noise(b, 0, 0.07f, Const(5000), 1.f, 0.002f, 40.f, 2500.f); Load(Id::DotTick, b, 0.18f); }
			{ Buf b(0.18f); Noise(b, 0, 0.15f, Sweep(800, 3500), 1.f, 0.02f, 14.f, 500.f); Load(Id::Miss, b, 0.3f); }
			{
				Buf b(0.5f);
				Noise(b, 0, 0.06f, Const(9000), 1.f, 0.f, 50.f, 3000.f);
				for (int i = 0; i < 7; ++i)
					Tone(b, Rand(0.f, 0.15f), 0.3f, Osc::Sine, Const(Rand(2600.f, 6000.f)), 0.25f, 0.001f, 12.f);
				Load(Id::Freeze, b, 0.45f);
			}
			{
				Buf b(0.7f);
				Tone(b, 0, 0.65f, Osc::Sine, Const(1244), 0.5f, 0.002f, 5.f, 7.f, 0.01f);
				Tone(b, 0, 0.65f, Osc::Sine, Const(1866), 0.3f, 0.002f, 6.f);
				Tone(b, 0, 0.15f, Osc::Sine, ExpSweep(200, 90), 0.6f, 0.001f, 20.f);
				Load(Id::Stun, b, 0.4f);
			}

			// ── 살아 있는 마을 ─────────────────────────────────────────────────
			{
				// 알아들을 수 없는 웅얼거림(동물의 숲 풍) — 음절마다 음높이가 오르내린다
				Buf b(0.62f);
				float t = 0.f;
				for (int i = 0; i < 6; ++i)
				{
					const float f = Rand(210.f, 330.f);
					const float d = Rand(0.05f, 0.08f);
					Tone(b, t, d, Osc::Tri, ExpSweep(f, f * Rand(0.85f, 1.2f)), 0.8f, 0.006f, 22.f, 30.f, 0.03f);
					Tone(b, t, d, Osc::Sine, ExpSweep(f * 2.5f, f * 2.2f), 0.25f, 0.006f, 30.f);
					Noise(b, t, d * 0.4f, Const(2500), 0.12f, 0.002f, 60.f, 800.f);
					t += d + Rand(0.015f, 0.04f);
				}
				Load(Id::Voice, b, 0.35f);
			}
			{
				Buf b(0.45f);
				for (int k = 0; k < 2; ++k)
				{
					const float s0 = k * 0.2f;
					Noise(b, s0, 0.09f, Sweep(2200, 700), 0.8f, 0.003f, 22.f, 300.f);
					Tone(b, s0, 0.1f, Osc::Saw, ExpSweep(520, 320), 0.5f, 0.004f, 18.f);
					Tone(b, s0, 0.1f, Osc::Sine, ExpSweep(1040, 620), 0.3f, 0.004f, 20.f);
				}
				Load(Id::DogBark, b, 0.5f);
			}
			{
				Buf b(0.6f);
				Tone(b, 0, 0.55f, Osc::Tri, [](float u) { return 650.f + 380.f * std::sin(u * 3.14159f) - 120.f * u; }, 0.8f, 0.05f, 3.5f, 7.f, 0.03f);
				Tone(b, 0, 0.55f, Osc::Sine, [](float u) { return 1300.f + 700.f * std::sin(u * 3.14159f); }, 0.25f, 0.05f, 4.f);
				Load(Id::CatMeow, b, 0.35f);
			}
			{
				Buf b(0.5f);
				for (int k = 0; k < 3; ++k)
				{
					Tone(b, k * 0.12f, 0.07f, Osc::Square, ExpSweep(780, 620), 0.35f, 0.003f, 30.f);
					Noise(b, k * 0.12f, 0.05f, Const(3000), 0.3f, 0.002f, 60.f, 900.f);
				}
				Load(Id::Cluck, b, 0.3f);
			}
			{
				// 류트: 뜯는 소리(빠르게 사라지는 배음) 아르페지오
				Buf b(1.6f);
				const float n[] = { 220.f, 261.6f, 329.6f, 392.f, 329.6f, 261.6f };
				for (int i = 0; i < 6; ++i)
				{
					Tone(b, i * 0.16f, 0.9f, Osc::Tri, Const(n[i]), 0.6f, 0.002f, 5.f);
					Tone(b, i * 0.16f, 0.5f, Osc::Sine, Const(n[i] * 2.f), 0.3f, 0.002f, 8.f);
					Tone(b, i * 0.16f, 0.3f, Osc::Sine, Const(n[i] * 3.f), 0.15f, 0.001f, 14.f);
				}
				Load(Id::Lute, b, 0.3f);
			}
			{
				Buf b(0.5f);
				const float n[] = { 1318.5f, 1568.f, 2093.f };
				for (int i = 0; i < 3; ++i)
					Tone(b, i * 0.07f, 0.3f, Osc::Sine, Const(n[i]), 0.5f, 0.003f, 9.f);
				Load(Id::Pet, b, 0.3f);
			}

			// ── 희귀도별 획득음(높을수록 길고 넓게) ──
			{
				// 희귀: 맑은 종 두 번
				Buf b(0.8f);
				const float n[] = { 1318.5f, 1975.5f };
				for (int i = 0; i < 2; ++i)
				{
					Tone(b, i * 0.09f, 0.6f, Osc::Sine, Const(n[i]), 0.6f, 0.002f, 5.f);
					Tone(b, i * 0.09f, 0.4f, Osc::Sine, Const(n[i] * 2.76f), 0.14f, 0.002f, 9.f);
				}
				for (int i = 0; i < 5; ++i)
					Tone(b, 0.15f + i * 0.05f, 0.15f, Osc::Sine, Const(Rand(3500.f, 6000.f)), 0.08f, 0.002f, 20.f);
				Load(Id::LootRare, b, 0.45f);
			}
			{
				// 영웅: 올라가는 아르페지오 + 반짝임
				Buf b(1.2f);
				const float n[] = { 1046.5f, 1318.5f, 1568.f, 2093.f };
				for (int i = 0; i < 4; ++i)
				{
					Tone(b, i * 0.075f, 0.9f, Osc::Tri, Const(n[i]), 0.5f, 0.003f, 4.f);
					Tone(b, i * 0.075f, 0.6f, Osc::Sine, Const(n[i] * 2.f), 0.2f, 0.003f, 6.f);
				}
				for (int i = 0; i < 9; ++i)
					Tone(b, 0.3f + i * 0.055f, 0.2f, Osc::Sine, Const(Rand(3000.f, 6500.f)), 0.1f, 0.002f, 16.f);
				Load(Id::LootHeroic, b, 0.5f);
			}
			{
				// 전설: 낮게 부풀었다가 큰 화음이 울리고 반짝임이 쏟아진다
				Buf b(2.0f);
				Tone(b, 0, 0.45f, Osc::Sine, ExpSweep(55, 110), 0.7f, 0.3f, 3.f);
				Noise(b, 0, 0.45f, ExpSweep(300, 4000), 0.25f, 0.35f, 4.f, 300.f);
				const float n[] = { 523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f };
				for (int i = 0; i < 5; ++i)
				{
					Tone(b, 0.38f + i * 0.02f, 1.5f, Osc::Sine, Const(n[i]), 0.5f, 0.004f, 1.8f, 5.f, 0.004f);
					Tone(b, 0.38f + i * 0.02f, 1.0f, Osc::Sine, Const(n[i] * 2.76f), 0.1f, 0.004f, 4.f);
				}
				Tone(b, 0.38f, 1.4f, Osc::Tri, Const(130.8f), 0.5f, 0.01f, 2.f);
				for (int i = 0; i < 16; ++i)
					Tone(b, 0.45f + i * 0.06f, 0.22f, Osc::Sine, Const(Rand(2800.f, 7000.f)), 0.1f, 0.002f, 14.f);
				Load(Id::LootLegendary, b, 0.65f);
			}
			{
				// 신화: 합창 같은 부풂(떨림 있는 사인 여러 겹) + 리디안 종 화음 + 깊은 울림
				Buf b(2.6f);
				const float pad[] = { 261.6f, 329.6f, 370.0f, 493.9f };
				for (int i = 0; i < 4; ++i)
				{
					Tone(b, 0, 2.4f, Osc::Sine, Const(pad[i]), 0.35f, 0.7f, 1.1f, 5.5f, 0.006f);
					Tone(b, 0.02f, 2.4f, Osc::Sine, Const(pad[i] * 1.004f), 0.25f, 0.7f, 1.1f, 6.1f, 0.006f);
				}
				Tone(b, 0.65f, 1.8f, Osc::Sine, ExpSweep(70, 40), 0.8f, 0.004f, 2.2f);
				Noise(b, 0.62f, 0.5f, Sweep(4000, 600), 0.35f, 0.f, 6.f, 200.f);
				const float bell[] = { 1046.5f, 1318.5f, 1480.f, 1975.5f, 2093.f };
				for (int i = 0; i < 5; ++i)
				{
					Tone(b, 0.66f + i * 0.05f, 1.6f, Osc::Sine, Const(bell[i]), 0.4f, 0.003f, 2.f);
					Tone(b, 0.66f + i * 0.05f, 1.0f, Osc::Sine, Const(bell[i] * 2.76f), 0.1f, 0.003f, 5.f);
				}
				for (int i = 0; i < 22; ++i)
					Tone(b, 0.7f + i * 0.07f, 0.25f, Osc::Sine, Const(Rand(3000.f, 8000.f)), 0.09f, 0.002f, 12.f);
				Load(Id::LootMythic, b, 0.7f);
			}

			// ── 퀘스트 · 메인 이야기 ──
			{
				// 받기: 양피지 스치는 소리 + 부드러운 두 음(솔 → 도)
				Buf b(0.9f);
				Noise(b, 0, 0.22f, Sweep(1800, 5200), 0.35f, 0.04f, 9.f, 700.f);
				const float n[] = { 783.99f, 1046.5f };
				for (int i = 0; i < 2; ++i)
				{
					Tone(b, 0.1f + i * 0.12f, 0.7f, Osc::Sine, Const(n[i]), 0.6f, 0.004f, 4.5f);
					Tone(b, 0.1f + i * 0.12f, 0.4f, Osc::Sine, Const(n[i] * 2.76f), 0.12f, 0.003f, 9.f);
				}
				Load(Id::QuestAccept, b, 0.4f);
			}
			{
				// 진행: 작게 튕기는 소리
				Buf b(0.25f);
				Tone(b, 0, 0.22f, Osc::Tri, Const(880.f), 0.8f, 0.002f, 16.f);
				Tone(b, 0, 0.12f, Osc::Sine, Const(1760.f), 0.3f, 0.002f, 28.f);
				Load(Id::QuestTick, b, 0.28f);
			}
			{
				// 목표 하나 완료: 맑은 두 음이 위로
				Buf b(0.7f);
				const float n[] = { 1318.5f, 1760.f };
				for (int i = 0; i < 2; ++i)
				{
					Tone(b, i * 0.08f, 0.55f, Osc::Sine, Const(n[i]), 0.6f, 0.003f, 6.f);
					Tone(b, i * 0.08f, 0.3f, Osc::Sine, Const(n[i] * 2.76f), 0.12f, 0.002f, 12.f);
				}
				Load(Id::QuestObjDone, b, 0.38f);
			}
			{
				// 모든 목표 완료: 도 · 미 · 솔 · 도 아르페지오 + 반짝임
				Buf b(1.1f);
				const float n[] = { 1046.5f, 1318.5f, 1568.f, 2093.f };
				for (int i = 0; i < 4; ++i)
				{
					Tone(b, i * 0.07f, 0.8f, Osc::Sine, Const(n[i]), 0.5f, 0.003f, 4.f);
					Tone(b, i * 0.07f, 0.5f, Osc::Tri, Const(n[i] * 0.5f), 0.2f, 0.003f, 6.f);
				}
				for (int i = 0; i < 6; ++i)
					Tone(b, 0.3f + i * 0.06f, 0.18f, Osc::Sine, Const(Rand(3200.f, 6400.f)), 0.08f, 0.002f, 16.f);
				Load(Id::QuestReady, b, 0.45f);
			}
			{
				// 퀘스트 완료: 짧은 금관 팡파르(솔 → 도·미·솔 화음) + 종 + 반짝임
				Buf b(2.0f);
				Tone(b, 0, 0.16f, Osc::Saw, Const(392.f), 0.25f, 0.01f, 10.f);
				Tone(b, 0, 0.16f, Osc::Tri, Const(392.f), 0.4f, 0.01f, 10.f);
				const float chord[] = { 523.25f, 659.25f, 783.99f, 1046.5f };
				for (int i = 0; i < 4; ++i)
				{
					Tone(b, 0.18f, 1.6f, Osc::Saw, Const(chord[i]), 0.12f, 0.02f, 1.7f, 5.f, 0.004f);
					Tone(b, 0.18f, 1.6f, Osc::Tri, Const(chord[i]), 0.35f, 0.02f, 1.7f, 5.f, 0.004f);
				}
				Tone(b, 0.18f, 1.5f, Osc::Sine, Const(130.8f), 0.5f, 0.02f, 2.2f);
				Tone(b, 0.2f, 1.4f, Osc::Sine, Const(2093.f), 0.25f, 0.003f, 2.5f);
				Tone(b, 0.2f, 1.0f, Osc::Sine, Const(2093.f * 2.76f), 0.06f, 0.003f, 5.f);
				for (int i = 0; i < 12; ++i)
					Tone(b, 0.3f + i * 0.07f, 0.2f, Osc::Sine, Const(Rand(3000.f, 7000.f)), 0.07f, 0.002f, 14.f);
				Load(Id::QuestComplete, b, 0.6f);
			}
			{
				// 장 제목: 깊은 종(비조화 배음) + 낮은 울림이 부풀어 오른다
				Buf b(3.4f);
				const float f0 = 98.f;
				const float partial[] = { 1.f, 2.f, 2.76f, 5.4f, 8.93f };
				const float amp[] = { 0.7f, 0.45f, 0.35f, 0.18f, 0.08f };
				for (int i = 0; i < 5; ++i)
					Tone(b, 0.05f, 3.3f, Osc::Sine, Const(f0 * partial[i]), amp[i], 0.004f, 0.9f + i * 0.5f);
				Tone(b, 0, 3.3f, Osc::Sine, Const(49.f), 0.5f, 1.2f, 0.8f);
				Noise(b, 0.05f, 0.25f, Sweep(2500, 400), 0.25f, 0.f, 12.f, 150.f);
				Load(Id::ChapterBell, b, 0.6f);
			}
			{
				// 발견: 하프가 위로 쓸어 올라간다(오음 음계) + 반짝임
				Buf b(1.6f);
				const float scale[] = { 523.25f, 587.33f, 659.25f, 783.99f, 880.f, 1046.5f, 1174.7f, 1318.5f, 1568.f, 1760.f };
				for (int i = 0; i < 10; ++i)
				{
					Tone(b, i * 0.045f, 0.9f, Osc::Tri, Const(scale[i]), 0.35f, 0.002f, 3.5f);
					Tone(b, i * 0.045f, 0.5f, Osc::Sine, Const(scale[i] * 2.f), 0.12f, 0.002f, 6.f);
				}
				for (int i = 0; i < 10; ++i)
					Tone(b, 0.45f + i * 0.07f, 0.25f, Osc::Sine, Const(Rand(3500.f, 7500.f)), 0.06f, 0.002f, 12.f);
				Load(Id::Discover, b, 0.42f);
			}
			{
				// 심장 박동: 쿵-쿵(두 번째가 조금 약하다)
				Buf b(1.0f);
				for (int k = 0; k < 2; ++k)
				{
					const float t0 = k * 0.3f;
					const float a = k == 0 ? 1.f : 0.75f;
					Tone(b, t0, 0.45f, Osc::Sine, ExpSweep(78, 38), a, 0.004f, 9.f);
					Noise(b, t0, 0.06f, Const(500), 0.25f * a, 0.001f, 40.f);
				}
				Load(Id::Heartbeat, b, 0.85f);
			}
			// ── Phase 2-A: 기술 소리 겹 ──
			{
				// 큰 종: 어긋난 배음(1 · 2 · 2.4 · 3 · 4.2 · 5.4) + 때리는 소리. 낮은 음은 길게, 높은 음은 짧게
				Buf b(2.8f);
				const float f0 = 196.f;
				const float part[6] = { 1.f, 2.f, 2.4f, 3.f, 4.2f, 5.4f };
				const float amp[6] = { 1.f, 0.6f, 0.45f, 0.35f, 0.25f, 0.18f };
				for (int i = 0; i < 6; ++i)
					Tone(b, 0, 2.7f, Osc::Sine, Const(f0 * part[i]), amp[i], 0.002f, 1.1f + i * 0.9f, 0.6f + i * 0.3f, 0.0025f);
				Tone(b, 0, 2.6f, Osc::Sine, Const(f0 * 0.5f), 0.5f, 0.01f, 0.9f);
				Noise(b, 0, 0.03f, Const(4000), 0.9f, 0.f, 120.f, 800.f);
				Load(Id::BellToll, b, 0.85f);
			}
			{
				Buf b(1.2f);
				const float f0 = 587.f;
				const float part[4] = { 1.f, 2.1f, 2.9f, 4.3f };
				for (int i = 0; i < 4; ++i)
					Tone(b, 0, 1.1f, Osc::Sine, Const(f0 * part[i]), 1.f / (1.f + i), 0.001f, 2.5f + i * 2.f, 1.f, 0.002f);
				Noise(b, 0, 0.012f, Const(8000), 0.6f, 0.f, 250.f, 2000.f);
				Load(Id::BellSmall, b, 0.6f);
			}
			{
				// 글자 새기기: 올라가는 쉬익 + 반짝이는 높은 톤 몇 개
				Buf b(0.55f);
				Noise(b, 0, 0.5f, ExpSweep(1500, 7000), 0.45f, 0.15f, 5.f, 900.f);
				for (int i = 0; i < 6; ++i)
					Tone(b, 0.05f + i * 0.06f, 0.2f, Osc::Sine, Const(1800.f + i * 330.f), 0.18f, 0.002f, 14.f);
				Tone(b, 0, 0.5f, Osc::Tri, ExpSweep(420, 880), 0.2f, 0.1f, 4.f);
				Load(Id::Scribe, b, 0.4f);
			}
			{
				Buf b(0.32f);
				Noise(b, 0, 0.3f, [](float u) { return 900.f + 5200.f * std::sin(u * 3.14159f); }, 1.f, 0.06f, 7.f, 500.f);
				Load(Id::Whoosh, b, 0.5f);
			}
			{
				Buf b(1.1f);
				Tone(b, 0, 1.05f, Osc::Sine, ExpSweep(62, 28), 1.f, 0.003f, 3.2f);
				Tone(b, 0, 0.6f, Osc::Sine, ExpSweep(124, 50), 0.4f, 0.002f, 6.f);
				Noise(b, 0, 0.25f, Const(300), 0.5f, 0.005f, 9.f);
				Load(Id::BoomLow, b, 0.95f);
			}
			{
				Buf b(0.8f);
				Noise(b, 0, 0.012f, Const(11000), 1.3f, 0.f, 260.f, 1500.f);
				Noise(b, 0, 0.5f, ExpSweep(6000, 350), 1.1f, 0.f, 6.5f, 90.f);
				Tone(b, 0, 0.5f, Osc::Sine, ExpSweep(110, 34), 0.8f, 0.002f, 6.f);
				for (int i = 0; i < 14; ++i)
					Noise(b, Rand(0.08f, 0.7f), 0.015f, Const(6000), Rand(0.1f, 0.35f), 0.f, 170.f, 1500.f);
				Load(Id::Burst, b, 0.85f);
			}
			{
				Buf b(1.0f);
				Noise(b, 0, 0.95f, [](float u) { return 400.f + 900.f * std::sin(u * 3.14159f); }, 1.f, 0.18f, 2.2f, 60.f);
				Tone(b, 0, 0.9f, Osc::Saw, ExpSweep(55, 80), 0.18f, 0.2f, 2.5f, 7.f, 0.04f);
				for (int i = 0; i < 10; ++i)
					Noise(b, Rand(0.1f, 0.85f), 0.01f, Const(5000), Rand(0.08f, 0.2f), 0.f, 200.f, 1800.f);
				Load(Id::FireRoar, b, 0.6f);
			}
			{
				Buf b(0.7f);
				for (int i = 0; i < 5; ++i)
					Noise(b, i * 0.035f + Rand(0.f, 0.02f), 0.012f, Const(9000), 1.f - i * 0.12f, 0.f, 300.f, 2500.f);
				const float glass[4] = { 2350.f, 3520.f, 4710.f, 6150.f };
				for (int i = 0; i < 4; ++i)
					Tone(b, 0.01f, 0.6f, Osc::Sine, Const(glass[i]), 0.22f - i * 0.04f, 0.001f, 7.f + i * 2.f);
				Tone(b, 0, 0.25f, Osc::Sine, ExpSweep(300, 120), 0.4f, 0.001f, 14.f);
				Load(Id::IceCrack, b, 0.6f);
			}
			{
				Buf b(1.6f);
				Noise(b, 0, 0.02f, Const(12000), 1.5f, 0.f, 200.f, 2000.f);
				Noise(b, 0.005f, 0.12f, ExpSweep(8000, 1500), 1.2f, 0.f, 22.f, 600.f);
				Noise(b, 0.05f, 1.5f, [](float u) { return 180.f + 120.f * std::sin(u * 9.f); }, 1.f, 0.12f, 1.8f);
				Tone(b, 0.03f, 1.2f, Osc::Sine, ExpSweep(58, 30), 0.7f, 0.05f, 2.2f);
				Load(Id::ThunderClap, b, 0.9f);
			}
			{
				Buf b(0.5f);
				Noise(b, 0, 0.3f, ExpSweep(2200, 500), 1.f, 0.004f, 11.f, 150.f);
				for (int i = 0; i < 7; ++i)
					Tone(b, Rand(0.02f, 0.3f), 0.06f, Osc::Sine, ExpSweep(Rand(300.f, 700.f), Rand(900.f, 1500.f)), 0.25f, 0.002f, 40.f);
				Load(Id::InkSplash, b, 0.55f);
			}
			{
				Buf b(1.0f);
				Noise(b, 0, 0.008f, Const(10000), 1.2f, 0.f, 350.f, 2500.f);
				const float fr[3] = { 1870.f, 2980.f, 4410.f };
				for (int i = 0; i < 3; ++i)
					Tone(b, 0.002f, 0.95f, Osc::Sine, Const(fr[i]), 0.5f - i * 0.12f, 0.f, 5.f + i * 2.f, 5.f, 0.004f);
				Tone(b, 0, 0.2f, Osc::Sine, ExpSweep(240, 90), 0.5f, 0.001f, 18.f);
				Load(Id::MetalRing, b, 0.6f);
			}
			{
				Buf b(1.8f);
				Noise(b, 0, 1.75f, ExpSweep(2600, 500), 0.6f, 0.05f, 1.6f, 200.f);
				Load(Id::TailVerb, b, 0.35f);
			}
			{
				Buf b(0.95f);
				Tone(b, 0, 0.92f, Osc::Saw, ExpSweep(110, 440), 0.35f, 0.8f, 0.2f, 6.f, 0.02f);
				Tone(b, 0, 0.92f, Osc::Sine, ExpSweep(220, 880), 0.5f, 0.8f, 0.2f);
				Noise(b, 0, 0.92f, ExpSweep(500, 5000), 0.3f, 0.8f, 0.2f, 300.f);
				Load(Id::ChargeLoop, b, 0.4f);
			}
			{
				Buf b(0.3f);
				Tone(b, 0, 0.28f, Osc::Sine, Const(1320), 1.f, 0.001f, 14.f);
				Tone(b, 0, 0.28f, Osc::Sine, Const(1980), 0.5f, 0.001f, 18.f);
				Load(Id::ChargeStage, b, 0.4f);
			}
			{
				Buf b(0.9f);
				const float ch[3] = { 1046.5f, 1318.5f, 1568.f };
				for (int i = 0; i < 3; ++i)
					Tone(b, i * 0.03f, 0.85f, Osc::Sine, Const(ch[i]), 0.6f, 0.002f, 4.f);
				Noise(b, 0, 0.3f, ExpSweep(3000, 9000), 0.3f, 0.01f, 10.f, 2000.f);
				Load(Id::Perfect, b, 0.55f);
			}
			{
				Buf b(0.4f);
				Noise(b, 0, 0.3f, ExpSweep(1800, 400), 0.8f, 0.03f, 10.f, 150.f);
				Tone(b, 0.2f, 0.18f, Osc::Sine, ExpSweep(140, 60), 0.9f, 0.002f, 20.f);
				Load(Id::Roll, b, 0.55f);
			}
			{
				Buf b(0.9f);
				Tone(b, 0, 0.85f, Osc::Saw, [](float u) { return 196.f * (1.f + 0.06f * std::sin(u * 3.14159f)); }, 0.5f, 0.06f, 2.2f, 5.5f, 0.012f);
				Tone(b, 0, 0.85f, Osc::Saw, [](float u) { return 294.f * (1.f + 0.06f * std::sin(u * 3.14159f)); }, 0.35f, 0.08f, 2.2f, 5.f, 0.012f);
				Noise(b, 0, 0.8f, Const(1400), 0.15f, 0.05f, 3.f, 600.f);
				Load(Id::Rally, b, 0.5f);
			}
			{
				Buf b(0.7f);
				for (int i = 0; i < 9; ++i)
					Noise(b, i * 0.07f, 0.09f, [](float u) { return 1500.f + 4500.f * std::sin(u * 3.14159f); }, 0.8f, 0.02f, 18.f, 700.f);
				Load(Id::BladeHum, b, 0.45f);
			}
			{
				Buf b(1.0f);
				Noise(b, 0, 0.45f, ExpSweep(600, 7000), 0.9f, 0.3f, 3.f, 300.f);
				Noise(b, 0.42f, 0.015f, Const(9000), 1.3f, 0.f, 260.f, 1500.f);
				Tone(b, 0.42f, 0.55f, Osc::Sine, ExpSweep(90, 35), 1.f, 0.002f, 5.f);
				Load(Id::Cutin, b, 0.85f);
			}
			{
				Buf b(1.4f);
				Tone(b, 0, 1.35f, Osc::Saw, Const(55), 0.3f, 0.35f, 1.4f, 3.f, 0.01f);
				Tone(b, 0, 1.35f, Osc::Sine, Const(41), 0.8f, 0.3f, 1.3f);
				Noise(b, 0, 1.3f, Const(260), 0.35f, 0.3f, 1.6f);
				Load(Id::Drone, b, 0.6f);
			}
			{
				// 퀘스트 물건이 날아온다: 쉭 + 높은 반짝임
				Buf b(0.45f);
				Noise(b, 0, 0.35f, ExpSweep(800, 6000), 0.4f, 0.12f, 8.f, 500.f);
				for (int i = 0; i < 4; ++i)
					Tone(b, 0.15f + i * 0.05f, 0.15f, Osc::Sine, Const(2400.f + i * 500.f), 0.2f, 0.002f, 18.f);
				Load(Id::QuestFly, b, 0.3f);
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

	namespace
	{
		struct Pending { Id id; float at; float volume; float pitch; float pan; bool fixedPitch; };
		std::vector<Pending> g_pending;
		float g_clock = 0.f;
		float g_dedupe = 0.03f;
		std::array<double, static_cast<size_t>(Id::Count)> g_lastAt{};

		// 같은 소리를 아주 짧은 간격으로 겹치지 않는다
		bool Dedupe(Id id)
		{
			const double now = GetTime();
			double& last = g_lastAt[static_cast<size_t>(id)];
			if (now - last < g_dedupe) return true;
			last = now;
			return false;
		}
	}

	void SetDedupe(float seconds) { g_dedupe = std::max(0.f, seconds); }

	void PlayAfter(Id id, float delay, float volume, float pitchVar, float pan)
	{
		if (!g_ready) return;
		if (delay <= 0.f) { Play(id, volume, pitchVar, pan); return; }
		if (g_pending.size() > 64) g_pending.erase(g_pending.begin());
		g_pending.push_back(Pending{ id, g_clock + delay, volume, 1.f + Rand(-pitchVar, pitchVar), pan, false });
	}

	void Layered(const Layer* layers, int count, float volume, float pan)
	{
		if (!g_ready || !layers) return;
		for (int i = 0; i < count; ++i)
		{
			const Layer& l = layers[i];
			if (l.delay <= 0.f) PlayPitched(l.id, l.volume * volume, l.pitch * (1.f + Rand(-0.03f, 0.03f)), pan);
			else g_pending.push_back(Pending{ l.id, g_clock + l.delay, l.volume * volume, l.pitch * (1.f + Rand(-0.03f, 0.03f)), pan, true });
		}
	}

	void Update(float dt)
	{
		g_clock += dt;
		if (g_pending.empty()) return;
		for (size_t i = 0; i < g_pending.size();)
		{
			if (g_pending[i].at <= g_clock)
			{
				const Pending p = g_pending[i];
				g_pending.erase(g_pending.begin() + static_cast<std::ptrdiff_t>(i));
				PlayPitched(p.id, p.volume, p.pitch, p.pan);
			}
			else
				++i;
		}
	}

	void Play(Id id, float volume, float pitchVar, float pan)
	{
		if (!g_ready)
			return;
		Slot& slot = g_slots[static_cast<size_t>(id)];
		if (!slot.loaded || Dedupe(id))
			return;
		Sound& s = slot.alias[slot.next];
		slot.next = (slot.next + 1) % kVoices;
		SetSoundVolume(s, std::clamp(volume, 0.f, 1.5f));
		SetSoundPitch(s, 1.f + Rand(-pitchVar, pitchVar));
		SetSoundPan(s, std::clamp(pan, 0.f, 1.f));
		PlaySound(s);
	}

	void PlayPitched(Id id, float volume, float pitch, float pan)
	{
		if (!g_ready)
			return;
		Slot& slot = g_slots[static_cast<size_t>(id)];
		if (!slot.loaded || Dedupe(id))
			return;
		Sound& s = slot.alias[slot.next];
		slot.next = (slot.next + 1) % kVoices;
		SetSoundVolume(s, std::clamp(volume, 0.f, 1.5f));
		SetSoundPitch(s, std::clamp(pitch, 0.25f, 4.f));
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
