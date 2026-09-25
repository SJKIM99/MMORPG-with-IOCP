#include "render/ActorSprites.h"
#include "render/Sprites.h"
#include "render/Draw.h"
#include "core/Util.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

using util::Rgb;

namespace actorspr
{
	namespace
	{
		using sprites::Sprite;
		constexpr float kPi = util::kPi;

		// 그림 크기 보정(절차적 그림의 머리 끝 높이 → 그린 그림의 서 있는 키). 그린 그림은 조금 더 크게 보여야 읽힌다.
		constexpr float kPcScale = 1.16f;
		constexpr float kMonScale = 1.14f;
		constexpr float kNpcScale = 1.10f;

		// ── 동작 집합 ────────────────────────────────────────────────────────────
		enum PcPose { PcIdle, PcWalk1, PcWalk2, PcWindup, PcStrike, PcCast, PcGuard, PcHurt, PcDead, PcBIdle, PcBWalk1, PcBWalk2, PcBStrike, PcCount };
		constexpr const char* kPcNames[PcCount] = { "idle", "walk1", "walk2", "windup", "strike", "cast", "guard", "hurt", "dead", "bidle", "bwalk1", "bwalk2", "bstrike" };
		constexpr const char* kClassKey[3] = { "knight", "engraver", "hunter" };

		enum MonPose { MIdle, MMove, MAttack, MHurt, MCount };
		constexpr const char* kMonNames[MCount] = { "idle", "move", "attack", "hurt" };

		enum NpcPose { NIdle, NWalk1, NWalk2, NTalk, NCount };
		constexpr const char* kNpcNames[NCount] = { "idle", "walk1", "walk2", "talk" };

		struct PcSet { const Sprite* s[PcCount]{}; bool ok = false; };
		struct MonSet { const Sprite* s[MCount]{}; bool ok = false; };
		struct NpcSet { const Sprite* s[NCount]{}; bool ok = false; };

		PcSet g_pc[3];
		std::array<MonSet, 64> g_mon{};
		std::array<NpcSet, 32> g_npc{};
		bool g_loaded = false;

		const char* NpcKey(uint16_t id)
		{
			switch (id)
			{
			case 1: return "npc_lada";
			case 2: return "npc_dobran";
			case 3: return "npc_mateo";
			case 4: return "npc_yaro";
			case 5: case 14: case 15: case 16: return "npc_guard";
			case 6: return "npc_goran";
			case 7: return "npc_milena";
			case 8: return "npc_vesna";
			case 9: return "npc_bojan";
			case 10: return "npc_misha";
			case 11: return "npc_anka";
			case 12: return "npc_marfa";
			case 13: return "npc_danilo";
			case 17: return "npc_dog";
			case 18: return "npc_cat";
			case 19: case 20: return "npc_chicken";
			default: return nullptr;
			}
		}

		void EnsureLoaded()
		{
			if (g_loaded || !sprites::Ready()) return;
			g_loaded = true;
			for (int c = 0; c < 3; ++c)
			{
				PcSet& set = g_pc[c];
				for (int i = 0; i < PcCount; ++i)
					set.s[i] = sprites::Find(std::string("pc_") + kClassKey[c] + "_" + kPcNames[i]);
				// 없는 동작은 가까운 것으로(서기는 반드시 있어야 한다)
				if (!set.s[PcIdle]) continue;
				auto fb = [&](int i, int j) { if (!set.s[i]) set.s[i] = set.s[j]; };
				fb(PcWalk1, PcIdle); fb(PcWalk2, PcWalk1); fb(PcWindup, PcIdle); fb(PcStrike, PcWindup);
				fb(PcCast, PcWindup); fb(PcGuard, PcIdle); fb(PcHurt, PcIdle); fb(PcDead, PcHurt);
				set.ok = true;
			}
			for (size_t id = 1; id < g_mon.size(); ++id)
			{
				char key[16];
				std::snprintf(key, sizeof(key), "mon_%02d_", static_cast<int>(id));
				MonSet& set = g_mon[id];
				for (int i = 0; i < MCount; ++i)
					set.s[i] = sprites::Find(std::string(key) + kMonNames[i]);
				if (!set.s[MIdle]) continue;
				for (int i = 1; i < MCount; ++i) if (!set.s[i]) set.s[i] = set.s[MIdle];
				set.ok = true;
			}
			for (size_t id = 1; id < g_npc.size(); ++id)
			{
				const char* k = NpcKey(static_cast<uint16_t>(id));
				if (!k) continue;
				NpcSet& set = g_npc[id];
				for (int i = 0; i < NCount; ++i)
					set.s[i] = sprites::Find(std::string(k) + "_" + kNpcNames[i]);
				if (!set.s[NIdle]) continue;
				for (int i = 1; i < NCount; ++i) if (!set.s[i]) set.s[i] = set.s[NIdle];
				set.ok = true;
			}
		}

		// ── 그리기 한 번 분량 ─────────────────────────────────────────────────────
		struct Anim
		{
			const Sprite* s = nullptr;
			gfx::SpriteXform xf;
			// 잔상(smear): 휘두르는 순간 앞 동작을 뒤쪽에 옅게 한 장 — 눈이 "빨랐다"고 느낀다
			const Sprite* ghost = nullptr;
			Vector2 ghostOff{};
			float ghostAlpha = 0.f;
			// 몸을 감싸는 빛(시전 · 힘 모으기 · 강화 무기) — 더하기 혼합으로 그림을 한 겹 더
			float glow = 0.f;
			Color glowCol{ 255, 255, 255, 255 };
		};

		float Breath(const actors::Pose& p) { return std::sin(p.time * 2.2f + (p.seed % 997) * 0.37f); }

		// 한 번 부딪히는 흔들림(0→1→0)
		float Kick(float u) { return std::sin(util::Clamp01(u) * kPi); }

		// 두 걸음 그림 + 서기 그림으로 걷는 발 위상을 나눈다. 반환: 0 서기(교차), 1 첫 걸음, 2 둘째 걸음
		int StepFrame(float walkPhase)
		{
			const float s = std::sin(walkPhase);
			if (s > 0.38f) return 1;
			if (s < -0.38f) return 2;
			return 0;
		}

		void ApplyWalk(Anim& a, const actors::Pose& p, float bobPx, float leanPx)
		{
			const float m = util::Clamp01(p.moving);
			a.xf.offset.y -= std::fabs(std::cos(p.walk)) * bobPx * m;          // 발이 교차할 때 몸이 올라간다
			a.xf.lean += leanPx * m;                                             // 가는 쪽으로 쏠림
			a.xf.squash *= 1.f - 0.03f * std::fabs(std::sin(p.walk)) * m;        // 디딜 때 살짝 눌림
		}

		void ApplyBreath(Anim& a, const actors::Pose& p, float amt)
		{
			const float b = Breath(p) * amt * (1.f - util::Clamp01(p.moving));
			a.xf.squash *= 1.f + b;
			a.xf.stretch *= 1.f - b * 0.5f;
		}

		// 공통 휘두르기: 준비(젖힘) → 휘두름(튀어 나감 + 잔상) → 되돌아옴
		void Swing(Anim& a, float t, const Sprite* windup, const Sprite* strike, const Sprite* rest, float power, float lungePx)
		{
			constexpr float kWind = 0.30f, kHit = 0.70f;
			if (t < kWind)
			{
				const float u = util::EaseOutCubic(t / kWind);
				a.s = windup;
				a.xf.lean -= 6.f * u * power;
				a.xf.squash *= 1.f - 0.06f * u * power;
				a.xf.stretch *= 1.f + 0.035f * u * power;
				a.xf.offset.x -= 3.f * u * power;
			}
			else if (t < kHit)
			{
				const float u = (t - kWind) / (kHit - kWind);
				const float k = 1.f - util::EaseOutCubic(u);
				a.s = strike;
				a.xf.offset.x += lungePx * (0.35f + 0.65f * k) * power;
				a.xf.lean += 7.f * k * power;
				a.xf.stretch *= 1.f + 0.09f * k * power;
				a.xf.squash *= 1.f - 0.05f * k * power;
				if (u < 0.4f)
				{
					a.ghost = windup;
					a.ghostOff = Vector2{ -lungePx * 0.9f, 0.f };
					a.ghostAlpha = 0.38f * (1.f - u / 0.4f);
				}
			}
			else
			{
				const float u = (t - kHit) / (1.f - kHit);
				a.s = u < 0.55f ? strike : rest;
				a.xf.offset.x += lungePx * 0.35f * (1.f - util::EaseOutCubic(u)) * power;
				a.xf.squash *= 1.f + 0.03f * Kick(u) * power;   // 제자리로 돌아오며 한 번 튀어 오름
			}
		}

		// 가로 배율로 도는 척 — 1 → -1 → 1(한 바퀴). 그림 한 장으로 회전 베기를 만든다.
		void Spin(Anim& a, float u, float turns)
		{
			const float c = std::cos(util::Clamp01(u) * 2.f * kPi * turns);
			a.xf.stretch *= (std::fabs(c) < 0.18f ? (c < 0.f ? -0.18f : 0.18f) : c);
		}

		void Tremble(Anim& a, const actors::Pose& p, float amt)
		{
			a.xf.offset.x += std::sin(p.time * 61.f) * 1.4f * amt;
			a.xf.offset.y += std::cos(p.time * 47.f) * 0.6f * amt;
		}

		// ── 플레이어 ──────────────────────────────────────────────────────────────
		Anim AnimatePlayer(const actors::Pose& p, const PcSet& set, uint8_t cls)
		{
			Anim a;
			const bool back = p.faceBack && set.s[PcBIdle];
			const Sprite* idle = back ? set.s[PcBIdle] : set.s[PcIdle];
			const Sprite* walk1 = back ? (set.s[PcBWalk1] ? set.s[PcBWalk1] : idle) : set.s[PcWalk1];
			const Sprite* walk2 = back ? (set.s[PcBWalk2] ? set.s[PcBWalk2] : walk1) : set.s[PcWalk2];
			const Sprite* windup = back ? idle : set.s[PcWindup];
			const Sprite* strike = back ? (set.s[PcBStrike] ? set.s[PcBStrike] : idle) : set.s[PcStrike];
			a.s = idle;

			// 걷기 · 숨쉬기
			if (p.moving > 0.05f)
			{
				const int f = StepFrame(p.walk);
				a.s = f == 1 ? walk1 : (f == 2 ? walk2 : idle);
				ApplyWalk(a, p, 2.4f, cls == 2 ? 4.f : 2.5f);
			}
			else
				ApplyBreath(a, p, 0.013f);

			// 기술 · 평타
			uint16_t sk = 0;
			float t = -1.f;
			if (p.skillT >= 0.f && p.skill != 0) { sk = p.skill; t = p.skillT; }
			else if (p.attack >= 0.f) { sk = static_cast<uint16_t>(cls == 1 ? 7 : (cls == 2 ? 13 : 1)); t = p.attack; }

			if (sk != 0 && t >= 0.f)
			{
				const float hold = p.skillT >= 0.f ? p.skillHold : 0.f;
				// 힘 모으기 구간(단죄 · 낙성 등): 시전 자세로 떨며 빛이 차오른다
				if (hold > 0.f && t < hold)
				{
					const float u = t / hold;
					a.s = back ? idle : set.s[PcCast];
					a.xf.squash *= 1.f - 0.05f * u;
					a.xf.lean -= 3.f * u;
					Tremble(a, p, 0.4f + u);
					a.glow = 0.25f + 0.55f * u;
					a.glowCol = cls == 1 ? Rgb(0xFFC870) : (cls == 2 ? Rgb(0xC08AFF) : Rgb(0x7FE8DA));
				}
				else
				{
					const float tt = hold > 0.f ? (t - hold) / std::max(0.05f, 1.f - hold) : t;
					switch (sk)
					{
					case 2:   // 방패 강타: 막기 자세로 앞으로 내지른다
					case 5:   // 철벽 태세: 막기 자세를 버틴다
						a.s = back ? idle : set.s[PcGuard];
						if (sk == 2)
						{
							const float k = tt < 0.35f ? -util::EaseOutCubic(tt / 0.35f) : 1.f - util::EaseOutCubic((tt - 0.35f) / 0.65f);
							a.xf.offset.x += 11.f * k;
							a.xf.lean += 5.f * k;
							a.xf.stretch *= 1.f + 0.07f * std::max(0.f, k);
						}
						else
						{
							a.xf.squash *= 0.97f;
							Tremble(a, p, 0.25f);
							a.glow = 0.35f * (0.6f + 0.4f * std::sin(p.time * 6.f));
							a.glowCol = Rgb(0xFFD870);
						}
						break;
					case 3:   // 회전 베기: 휘두름 그림으로 한 바퀴
					case 18:  // 그믐 베기: 한 바퀴 반
						a.s = strike;
						Spin(a, tt, sk == 18 ? 1.5f : 1.f);
						a.xf.squash *= 0.95f;
						a.ghost = strike;
						a.ghostOff = Vector2{ 0.f, 0.f };
						a.ghostAlpha = 0.22f * Kick(tt);
						break;
					case 4:   // 돌진: 휘두름 자세로 몸을 앞으로 던진다
						a.s = strike;
						a.xf.lean += 10.f * (1.f - tt);
						a.xf.stretch *= 1.12f - 0.12f * tt;
						a.xf.squash *= 0.94f + 0.06f * tt;
						a.ghost = set.s[PcWalk1];
						a.ghostOff = Vector2{ -14.f, 0.f };
						a.ghostAlpha = 0.35f * (1.f - tt);
						break;
					case 8: case 9: case 10:   // 서리 각인 · 화염 고리 · 옛길 도약: 시전 자세
						a.s = back ? idle : set.s[PcCast];
						a.xf.squash *= 1.f + 0.04f * Kick(tt);
						a.glow = 0.5f * Kick(tt);
						a.glowCol = sk == 9 ? Rgb(0xFF8A40) : Rgb(0x8AE8FF);
						break;
					case 14:  // 그늘 걸음: 웅크렸다가 튀어 나간다
						if (tt < 0.4f) { a.s = back ? idle : set.s[PcCast]; a.xf.squash *= 0.94f; }
						else Swing(a, (tt - 0.4f) / 0.6f * 0.7f + 0.3f, windup, strike, idle, 1.1f, 10.f);
						break;
					case 16:  // 잔상 회피: 피하는 자세 + 잔상 두 장
						a.s = back ? idle : set.s[PcGuard];
						a.xf.offset.x -= 6.f * Kick(tt);
						a.ghost = idle;
						a.ghostOff = Vector2{ 10.f, 0.f };
						a.ghostAlpha = 0.4f * (1.f - tt);
						break;
					case 13:  // 연속 찌르기: 두 번
						Swing(a, std::fmod(tt * 2.f, 1.f), windup, strike, idle, 0.9f, 8.f);
						break;
					case 6: case 12:   // 단죄 · 낙성(힘을 모은 뒤): 크게 내려친다
						Swing(a, 0.3f + tt * 0.7f, windup, strike, idle, 1.5f, 10.f);
						a.xf.squash *= 1.f - 0.08f * Kick(tt * 2.f);
						break;
					case 17:  // 후방 습격: 크게 파고든다
						Swing(a, tt, windup, strike, idle, 1.3f, 12.f);
						break;
					default:  // 검격 · 각인탄 · 독 칼날 · 벼락 사슬 · 평타
						Swing(a, tt, windup, strike, idle, 1.f, cls == 1 ? 6.f : 9.f);
						break;
					}
				}
			}
			else if (p.cast > 0.f)
			{
				// 예전 시전(레거시 십자베기 등)
				a.s = back ? idle : set.s[PcCast];
				a.glow = 0.4f * Kick(p.cast);
				a.glowCol = Rgb(0xFFD890);
			}

			// 맞음 · 넉백(동작보다 우선하지 않는다 — 공격 중엔 몸만 흔들린다)
			if (p.hurt > 0.f || p.knock > 0.f)
			{
				const float h = std::max(p.hurt, p.knock);
				if (sk == 0 && h > 0.35f && !back) a.s = set.s[PcHurt];
				a.xf.offset.x -= 5.f * p.hurt + 9.f * p.knock;
				a.xf.lean -= 7.f * h;
				a.xf.squash *= 1.f - 0.06f * h;
			}
			// 쓰러짐: 맞은 자세로 젖혀지다가 누운 그림으로
			if (p.dead > 0.f)
			{
				const float u = util::Clamp01(p.dead * 1.4f);
				a.ghost = nullptr;
				a.glow = 0.f;
				a.xf.stretch = 1.f;
				if (u < 0.45f)
				{
					a.s = set.s[PcHurt];
					a.xf.rot = -0.9f * util::EaseInCubic(u / 0.45f);
					a.xf.lean = -4.f;
				}
				else
				{
					a.s = set.s[PcDead];
					a.xf.rot = 0.f;
					a.xf.lean = 0.f;
					const float v = (u - 0.45f) / 0.55f;
					a.xf.squash = 1.f + 0.12f * (1.f - util::EaseOutBounce(v));   // 쿵 하고 바닥에 닿는다
					a.xf.offset.y += -4.f * (1.f - util::EaseOutBounce(v));
				}
			}
			return a;
		}

		// ── 몬스터 ────────────────────────────────────────────────────────────────
		bool FlattensOnDeath(const MonsterTableRow& row)
		{
			return row.archetype == MonsterArchetype::eBlob || row.archetype == MonsterArchetype::eInsect
				|| row.archetype == MonsterArchetype::eCrab || row.archetype == MonsterArchetype::eGhost || row.archetype == MonsterArchetype::eMushroom;
		}

		Anim AnimateMonster(const actors::Pose& p, const MonSet& set, const MonsterTableRow& row)
		{
			Anim a;
			a.s = set.s[MIdle];
			const bool blob = row.archetype == MonsterArchetype::eBlob;
			const bool ghost = row.archetype == MonsterArchetype::eGhost;
			const float size = row.scale / 100.f;
			if (p.moving > 0.05f)
			{
				// 걷는 그림 한 장 + 서기 그림 — 발 위상의 반쪽씩
				a.s = std::sin(p.walk) > 0.f ? set.s[MMove] : set.s[MIdle];
				if (blob)
				{
					// 덩이는 통통 튄다: 떠오르며 늘어나고 닿으며 눌린다
					const float hop = std::fabs(std::sin(p.walk));
					a.xf.offset.y -= hop * 7.f * p.moving;
					a.xf.squash *= 1.f + 0.12f * hop - 0.1f * (1.f - hop);
					a.xf.stretch *= 1.f - 0.08f * hop + 0.08f * (1.f - hop);
				}
				else
					ApplyWalk(a, p, ghost ? 0.f : 1.8f, 2.f);
			}
			else
				ApplyBreath(a, p, blob ? 0.05f : 0.018f);
			if (ghost)
				a.xf.offset.y -= 4.f + std::sin(p.time * 2.f + p.seed) * 2.5f;   // 떠 있다

			// 강공격 예고: 웅크리고 뒤로 젖힌 채 떤다
			if (p.windup > 0.f)
			{
				a.s = set.s[MIdle];
				a.xf.squash *= 1.f - 0.1f * p.windup;
				a.xf.stretch *= 1.f + 0.06f * p.windup;
				a.xf.lean -= 6.f * p.windup;
				a.xf.offset.x -= 3.f * p.windup;
				Tremble(a, p, p.windup);
			}
			if (p.attack >= 0.f)
			{
				const float t = p.attack;
				if (t < 0.28f)
				{
					const float u = util::EaseOutCubic(t / 0.28f);
					a.s = set.s[MIdle];
					a.xf.squash *= 1.f - 0.08f * u;
					a.xf.lean -= 5.f * u;
					a.xf.offset.x -= 3.f * u;
				}
				else if (t < 0.78f)
				{
					const float u = (t - 0.28f) / 0.5f;
					const float k = 1.f - util::EaseOutCubic(u);
					a.s = set.s[MAttack];
					a.xf.offset.x += (6.f + 4.f * size) * (0.4f + 0.6f * k);
					a.xf.stretch *= 1.f + 0.1f * k;
					a.xf.squash *= 1.f - 0.05f * k;
					a.xf.lean += 5.f * k;
					if (u < 0.35f)
					{
						a.ghost = set.s[MIdle];
						a.ghostOff = Vector2{ -8.f, 0.f };
						a.ghostAlpha = 0.3f * (1.f - u / 0.35f);
					}
				}
				else
				{
					const float u = (t - 0.78f) / 0.22f;
					a.s = u < 0.5f ? set.s[MAttack] : set.s[MIdle];
					a.xf.squash *= 1.f + 0.04f * Kick(u);
				}
			}
			if (p.hurt > 0.f || p.knock > 0.f)
			{
				const float h = std::max(p.hurt, p.knock);
				if (p.attack < 0.f && h > 0.3f) a.s = set.s[MHurt];
				a.xf.offset.x -= 5.f * p.hurt + 10.f * p.knock;
				a.xf.lean -= 7.f * h;
				a.xf.squash *= 1.f - 0.07f * h;
				a.xf.stretch *= 1.f + 0.05f * h;
			}
			if (p.dead > 0.f)
			{
				const float u = util::Clamp01(p.dead * 1.3f);
				a.s = set.s[MHurt];
				a.ghost = nullptr;
				if (FlattensOnDeath(row))
				{
					a.xf.squash = 1.f - 0.62f * util::EaseOutCubic(u);
					a.xf.stretch = 1.f + 0.25f * util::EaseOutCubic(u);
					a.xf.lean = 0.f;
				}
				else
				{
					// 뒤로 젖혀지며 무너진다(큰 몸은 넘어뜨리지 않고 주저앉힌다)
					a.xf.rot = -0.55f * util::EaseOutBounce(u);
					a.xf.squash = 1.f - 0.35f * util::EaseOutCubic(u);
					a.xf.lean = -5.f;
				}
			}
			return a;
		}

		// ── NPC ───────────────────────────────────────────────────────────────────
		Anim AnimateNpc(const actors::Pose& p, const NpcSet& set, bool animal)
		{
			Anim a;
			a.s = set.s[NIdle];
			if (p.moving > 0.05f)
			{
				const int f = StepFrame(p.walk);
				a.s = f == 1 ? set.s[NWalk1] : (f == 2 ? set.s[NWalk2] : set.s[NIdle]);
				ApplyWalk(a, p, animal ? 2.5f : 1.8f, animal ? 1.f : 1.5f);
			}
			else
			{
				ApplyBreath(a, p, 0.012f);
				if (p.cast > 0.f)
				{
					// 말하는 중: 손짓과 멈춤을 번갈아(말의 박자), 손짓할 때 살짝 앞으로
					const float beat = std::sin(p.time * 2.6f) + 0.35f * std::sin(p.time * 5.3f + 1.f);
					if (beat > -0.25f)
					{
						a.s = set.s[NTalk];
						a.xf.lean += 1.5f;
						a.xf.squash *= 1.f + 0.012f * std::sin(p.time * 7.f);
					}
				}
			}
			return a;
		}

		// ── 그리기 ────────────────────────────────────────────────────────────────
		// 몸(Phase 1-C): 좌우 돌기(가로 배율) · 앞기울기 · 도는 쪽 기울기 · 멈춤 흔들림을 그림 변환에 얹는다
		void ApplyBody(Anim& a, const actors::Pose& p)
		{
			const float t = actors::TurnScale(p);
			const float k = std::max(actors::kTurnMinWidth, std::fabs(t));
			a.xf.stretch *= k;
			a.xf.rot += (p.lean * 0.55f + p.bank * 0.35f) * (kPi / 180.f);
			a.xf.lean += p.settle * 2.2f;
			a.xf.squash *= 1.f - 0.035f * p.settle;
		}

		void DrawAnim(const Anim& a, Vector2 base, float worldH, bool flip)
		{
			if (!a.s) return;
			gfx::SpriteXform xf = a.xf;
			xf.flip = flip;
			const float alpha0 = gfx::Alpha();
			const bool depthWrite = gfx::DepthWriteOn();
			// 잔상 · 몸빛은 깊이를 쓰지 않는다(뒤에 그려지는 것을 가리지 않게)
			if ((a.ghost && a.ghostAlpha > 0.01f) || a.glow > 0.01f)
			{
				if (depthWrite) gfx::DepthWrite(false);
				if (a.ghost && a.ghostAlpha > 0.01f)
				{
					gfx::SpriteXform gx = xf;
					gx.cutout = false;   // 깊이를 쓰지 않으니 가장자리까지 부드럽게
					gx.offset.x += a.ghostOff.x * (flip ? -1.f : 1.f);
					gx.offset.y += a.ghostOff.y;
					gfx::SetAlpha(alpha0 * a.ghostAlpha);
					sprites::Draw(*a.ghost, base, worldH, gx);
					gfx::SetAlpha(alpha0);
				}
				if (a.glow > 0.01f)
				{
					// 같은 그림을 조금 크게, 한 색으로 칠해 더한다 — 윤곽을 따라 빛이 번진다
					const float f0 = gfx::Flash();
					const Color fc0 = gfx::FlashColor();
					gfx::Additive(true);
					gfx::SetFlashColor(a.glowCol);
					gfx::SetFlash(1.f);
					gfx::SetAlpha(alpha0 * std::min(1.f, a.glow));
					gfx::SpriteXform gx = xf;
					gx.cutout = false;
					gx.squash *= 1.05f;
					gx.stretch *= 1.08f;
					sprites::Draw(*a.s, base, worldH, gx);
					gfx::Additive(false);
					gfx::SetFlash(f0);
					gfx::SetFlashColor(fc0);
					gfx::SetAlpha(alpha0);
				}
				if (depthWrite) gfx::DepthWrite(true);
			}
			sprites::Draw(*a.s, base, worldH, xf);
		}

		void RarityAura(const actors::Pose& p, uint8_t rarity, float r)
		{
			if (rarity < static_cast<uint8_t>(Rarity::Heroic) || p.dead > 0.f) return;
			const Color rc = Rgb(ItemRules::RarityColor(static_cast<Rarity>(rarity)));
			const float pulse = 0.6f + 0.4f * std::sin(p.time * 2.2f);
			gfx::Additive(true);
			gfx::EllipseGrad(p.base, r, r * 0.34f, util::WithA(rc, 0.26f * pulse), util::WithA(rc, 0.f));
			gfx::Additive(false);
		}
	}

	bool DrawPlayer(const actors::Pose& p, const actors::PlayerLook& look)
	{
		EnsureLoaded();
		const PcSet& set = g_pc[look.cls < 3 ? look.cls : 0];
		if (!set.ok) return false;
		const float worldH = actors::PlayerHeight() * kPcScale * p.scale;
		RarityAura(p, look.auraRarity, 21.f * p.scale);
		Anim a = AnimatePlayer(p, set, look.cls);
		ApplyBody(a, p);
		// 강화 · 희귀 무기: 몸을 감싸는 빛(+7부터, 영웅 무기부터)
		if (a.glow <= 0.f && p.dead <= 0.f)
		{
			Color gc{};
			float g = 0.f;
			if (look.weaponRarity >= static_cast<uint8_t>(Rarity::Heroic))
			{
				gc = Rgb(ItemRules::RarityColor(static_cast<Rarity>(look.weaponRarity)));
				g = look.weaponRarity >= static_cast<uint8_t>(Rarity::Legendary) ? 0.26f : 0.16f;
			}
			if (look.enchant >= 7)
			{
				gc = look.enchant >= 9 ? Rgb(0xFF8A4A) : Rgb(0x8AC8FF);
				g = std::max(g, 0.12f + 0.03f * (look.enchant - 7));
			}
			if (g > 0.f)
			{
				a.glow = g * (0.7f + 0.3f * std::sin(p.time * 3.f));
				a.glowCol = gc;
			}
		}
		DrawAnim(a, p.base, worldH, actors::TurnScale(p) < 0.f);
		return true;
	}

	bool DrawMonster(const actors::Pose& p, const MonsterTableRow& row)
	{
		EnsureLoaded();
		if (row.id >= g_mon.size() || !g_mon[row.id].ok) return false;
		const float worldH = actors::MonsterHeight(row) * kMonScale * p.scale;
		Anim a = AnimateMonster(p, g_mon[row.id], row);
		ApplyBody(a, p);
		if (row.boss && p.dead <= 0.f && a.glow <= 0.f)
		{
			// 보스: 몸 둘레에 옅은 불씨빛이 숨 쉰다
			a.glow = 0.10f + 0.06f * std::sin(p.time * 2.f);
			a.glowCol = Rgb(0xFF6A3A);
		}
		DrawAnim(a, p.base, worldH, actors::TurnScale(p) < 0.f);
		return true;
	}

	bool DrawNpc(const actors::Pose& p, const WorldData::NpcRow& npc)
	{
		EnsureLoaded();
		if (npc.id >= g_npc.size() || !g_npc[npc.id].ok) return false;
		const float worldH = actors::NpcHeight(npc) * kNpcScale * p.scale;
		Anim a = AnimateNpc(p, g_npc[npc.id], WorldData::IsAnimal(npc.kind));
		ApplyBody(a, p);
		DrawAnim(a, p.base, worldH, actors::TurnScale(p) < 0.f);
		return true;
	}

	bool HasPlayer(uint8_t cls) { EnsureLoaded(); return cls < 3 && g_pc[cls].ok; }
	bool HasMonster(uint16_t id) { EnsureLoaded(); return id < g_mon.size() && g_mon[id].ok; }
	bool HasNpc(uint16_t id) { EnsureLoaded(); return id < g_npc.size() && g_npc[id].ok; }
}
