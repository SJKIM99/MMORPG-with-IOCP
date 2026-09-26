#include "fx/SkillFx.h"
#include "fx/SkillFxKit.h"
#include "fx/Impact.h"
#include "fx/Vfx.h"
#include "fx/Trail.h"
#include "fx/FxScale.h"
#include "fx/FxGrade.h"
#include "fx/Effects.h"
#include "render/Post.h"
#include "render/FlashLight.h"
#include "render/Draw.h"
#include "audio/Sfx.h"
#include "core/Util.h"
#include "game/SkillView.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 기술 구조 V3 연출(Phase 2-B · 2-C) — Data/SkillData.h의 유형 · 타격 시각표를 따라 4박자로 짠다.
//
//   예고(OnExPhase: 시작 · 차지 · 홀딩 · 캐스팅) → 발동 → 타격(OnExBeat: 표의 hits[i].atMs에, 모양 가운데에서)
//   → 여운(바닥 자국 · 식어 가는 빛 · 남은 불길). 한 대상의 확정 피해는 OnExVictim(작은 겹)만.
//
//  · 예전 18기술 중 판정이 예전 그대로인 것(Legacy · 돌진 · 도약)은 예전 연출(OnCast/OnHit)을 그대로 쓴다.
//  · 모든 효과는 fxscale 범위 안에서 만든다 — 남의 기술은 설정 세기(기본 50%), 화면 효과는 내 것만.
//  · 색은 「새김」 팔레트(DESIGN_SYSTEM 10장): 서약기사 새벽빛 금 · 강철 백 / 각인술사 옛길 청록 · 서리 청백 ·
//    불씨 주황 · 벼락 흰청록 / 밤사냥꾼 먹빛 · 달빛 은백 · 독 녹색. 보라 · 네온은 쓰지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
namespace skillfx
{
	namespace
	{
		using util::Rgb;
		using util::RandF;
		using util::kPi;
		using namespace kit;

		constexpr float kTilePx = 56.f;   // 칸 반경 → 바닥 원 가로 반지름(px)

		// ── 팔레트 ──
		const Color kGold = Rgb(0xF2C47C), kGoldHot = Rgb(0xFFF6DC), kSteel = Rgb(0xE8ECF0);
		const Color kTeal = Rgb(0x7FD0C4), kTealHot = Rgb(0xE0FFF8), kFrost = Rgb(0xBFE6FF), kEmber = Rgb(0xFF9A3C), kBolt = Rgb(0xB8FFF2);
		const Color kMoon = Rgb(0xD8E2EC), kMoonHot = Rgb(0xF6F9FF), kInk = Rgb(0x23232C), kPoison = Rgb(0x8DCE5A);

		Vector2 g_listener{};
		bool g_hasListener = false;
		float g_t = 0.f;

		// ── 늦게 할 일(입자 · 소리처럼 지연이 없는 것) ──
		struct Deferred { float at; bool mine; bool stage; std::function<void()> fn; };
		std::vector<Deferred> g_later;
		void Later(float delay, std::function<void()> fn)
		{
			// 주인(내 것 · 남의 것)과 시연 무대 여부를 그대로 들고 간다 — 풀 때 같은 범위로 다시 세운다
			g_later.push_back(Deferred{ g_t + std::max(0.f, delay), fxscale::Stage() ? true : fxscale::Mine(), fxscale::Stage(), std::move(fn) });
		}

		// ── 투사체(방패 · 창 · 비수 · 약병 · 각인탄) ──
		enum class MKind : uint8_t { Orb, Spear, Dagger, Shield, Vial };
		struct Missile
		{
			MKind   kind = MKind::Orb;
			Vector2 a{}, b{};
			float   za = 40.f, zb = 30.f;
			float   t = 0.f, dur = 0.2f, delay = 0.f;
			float   arc = 0.f;        // 포물선 높이(px)
			float   size = 1.f;
			Color   c0{}, c1{};
			int     trail = -1;
			bool    mine = true;
			float   alpha = 1.f;
			std::function<void()> onArrive;
		};
		std::vector<Missile> g_missiles;

		// ── 심판의 종 ──
		struct Bell { Vector2 iso{}; float t = 0.f, life = 2.1f, drop = 0.6f; float swing = 0.f; float alpha = 1.f; };
		std::vector<Bell> g_bells;

		// ── 남는 장판(맹독 안개 · 불꽃 각인의 불길) ──
		enum class ZKind : uint8_t { Poison, Fire, Glint };   // Glint = 갈래 「장판」(서리 · 먹물 · 빛 — 색만 다른 반짝임)
		struct Zone { ZKind kind; Vector2 iso{}; float r = 100.f; float t = 0.f, life = 4.f; float acc = 0.f; bool mine = true; Color col{}; float pulse = 0.f; bool linger = false; };
		std::vector<Zone> g_zones;

		// ── 바닥 그림(부채꼴 · 빛 균열 선 · 도는 글자 고리) ──
		enum class GKind : uint8_t { Sector, GlowLine, RingSpin };
		struct GroundFx
		{
			GKind   kind;
			Vector2 a{}, b{};
			float   r = 60.f, ang = 0.f, half = 0.5f;
			float   t = 0.f, life = 1.f;
			Color   col{};
			float   alpha = 1.f;
		};
		std::vector<GroundFx> g_ground;

		// ── 떨어지는 것(단죄의 금빛 칼날) ──
		struct Faller { Vector2 iso{}; float t = 0.f, dur = 0.14f, h0 = 420.f; float alpha = 1.f; };
		std::vector<Faller> g_fallers;

		// ── 하늘 각인(낙성: 여러 겹 원이 획 순서대로) ──
		struct SkyRune { Vector2 iso{}; float z = 200.f, r = 90.f; float t = 0.f, life = 1.7f, draw = 1.0f; float alpha = 1.f; };
		std::vector<SkyRune> g_skyRunes;

		// ── 이번 프레임만: 모으는 창 · 칼날 빛(OnExHeld) ──
		struct Held { uint16_t skill; Vector2 iso{}; Vector2 dir{}; float amount = 0.f; bool perfect = false; float alpha = 1.f; };
		std::vector<Held> g_held;

		// ── 도구 ──
		float GroundAng(Vector2 dirScreen) { return std::atan2(dirScreen.y * 2.f, dirScreen.x); }
		Vector2 IsoDir(Vector2 dirTile) { return util::Norm(Vector2{ (dirTile.x - dirTile.y) * gfx::kHalfW, (dirTile.x + dirTile.y) * gfx::kHalfH }); }
		Vector2 Along(Vector2 iso, Vector2 dirTile, float tiles) { return util::Add(iso, Vector2{ (dirTile.x - dirTile.y) * gfx::kHalfW * tiles, (dirTile.x + dirTile.y) * gfx::kHalfH * tiles }); }
		Vector2 RotTile(Vector2 dirTile, float rad) { return util::Rot(dirTile, rad); }
		Vector2 OnEllipse(Vector2 c, float r, float a) { return Vector2{ c.x + std::cos(a) * r, c.y + std::sin(a) * r * 0.5f }; }

		void Snd(sfx::Id id, Vector2 iso, float vol, float pitchVar = 0.05f, float delay = 0.f)
		{
			if (fxscale::Stage()) return;   // 기술 창 시연은 소리 없이
			float v = vol, pan = 0.5f;
			if (g_hasListener)
			{
				const float dx = iso.x - g_listener.x, dy = (iso.y - g_listener.y) * 2.f;
				const float d = std::sqrt(dx * dx + dy * dy);
				v *= std::clamp(1.25f - d / 900.f, 0.f, 1.f);
				pan = 0.5f + std::clamp(dx / 1400.f, -0.35f, 0.35f);
			}
			if (!fxscale::Mine()) v *= 0.7f;
			if (v < 0.02f) return;
			if (delay > 0.f) sfx::PlayAfter(id, delay, v, pitchVar, pan);
			else sfx::Play(id, v, pitchVar, pan);
		}

		void Swing(Vector2 iso, float ang, float from, float to, float radius, float blade, float tilt, float sweep, Color edge, Color body,
			float delay = 0.f, float z = 34.f, bool additive = true)
		{
			trail::ArcSpec a;
			a.iso = iso; a.z = z; a.radius = radius; a.blade = blade;
			a.a0 = ang + from; a.a1 = ang + to;
			a.tilt = tilt; a.sweep = sweep; a.lag = sweep * 0.75f; a.delay = delay;
			a.edge = edge; a.body = body; a.additive = additive;
			trail::Arc(a);
		}

		void Spawn(vfx::Kind k, Vector2 iso, float z, float size, float life, const vfx::Ramp& r, float delay = 0.f, float rot = 0.f, float intensity = 1.f, float grow = 1.f)
		{
			vfx::Spec s = vfx::Make(k, iso, z, size, life, r);
			s.delay = delay; s.rot = rot; s.intensity = intensity; s.grow = grow;
			vfx::Spawn(s);
		}
		void SpawnWH(vfx::Kind k, Vector2 iso, float z, float w, float h, float life, const vfx::Ramp& r, float delay = 0.f, float rot = 0.f, float intensity = 1.f)
		{
			vfx::Spec s = vfx::Make(k, iso, z, w, life, r);
			s.w = w; s.h = h; s.delay = delay; s.rot = rot; s.intensity = intensity;
			vfx::Spawn(s);
		}
		vfx::Ramp RampOf(Color hot, Color mid, Color cool) { return vfx::Ramp{ hot, mid, cool }; }

		void Light(Vector2 iso, fxgrade::Tier t, Color c, float k = 1.f)
		{
			const auto& b = fxgrade::Get(t);
			flashlight::Add(iso, b.lightRadius * k, c, 1.f, b.lightSec * k);
		}

		Missile& Shoot(MKind kind, Vector2 a, float za, Vector2 b, float zb, float dur, Color c0, Color c1, float delay = 0.f, float arc = 0.f, float size = 1.f)
		{
			Missile m;
			m.kind = kind; m.a = a; m.b = b; m.za = za; m.zb = zb; m.dur = std::max(0.03f, dur); m.delay = delay; m.arc = arc; m.size = size;
			m.c0 = c0; m.c1 = c1; m.mine = fxscale::Mine(); m.alpha = fxscale::Alpha();
			g_missiles.push_back(std::move(m));
			return g_missiles.back();
		}

		// 예고(4박자의 첫 박): 힘이 모이는 빛 — 크게 시작해 작게 모이는 빛 + 가장자리에서 가운데로 빨려 드는 빛 알갱이
		void Gather(Vector2 iso, float z, Color col, float life = 0.1f, float radius = 36.f)
		{
			Glow(iso, z, radius * 1.5f, 8.f, util::WithA(col, 0.7f), life + 0.04f);
			for (int i = 0; i < 7; ++i)
			{
				const float a = i * kPi * 2.f / 7.f + RandF(-0.3f, 0.3f);
				const Vector2 from = OnEllipse(iso, radius * RandF(0.8f, 1.2f), a);
				fx::Particle& p = P();
				p.pos = from; p.z = z + RandF(-8.f, 8.f);
				p.vel = { (iso.x - from.x) / life, (iso.y - from.y) / life };
				p.life = p.maxLife = life;
				p.size0 = RandF(4.f, 6.5f); p.size1 = 1.5f;
				p.c0 = util::Mix(col, WHITE, 0.55f); p.c1 = util::WithA(col, 0.f);
				p.shape = fx::Shape::Glow;
				p.additive = true;
			}
		}
		// 여운(4박자의 끝 박): 천천히 떠오르며 식는 빛 알갱이
		void Motes(Vector2 iso, float z, Color col, int n, float life = 0.7f, float spread = 24.f)
		{
			for (int i = 0; i < n; ++i) Ember(iso, z, util::Mix(col, WHITE, 0.4f), util::WithA(col, 0.f), spread, RandF(life * 0.7f, life * 1.2f));
		}

		void Dusts(Vector2 iso, float radius, int n, float life = 0.8f)
		{
			for (int i = 0; i < n; ++i)
			{
				const float a = i * kPi * 2.f / n + RandF(-0.2f, 0.2f);
				Smoke(OnEllipse(iso, radius * RandF(0.6f, 1.f), a), 4.f, Rgb(0xB8A078, 150), RandF(18.f, 28.f), life, 14.f);
			}
		}

		CastInfo ToCast(const ExCtx& c)
		{
			CastInfo ci;
			ci.casterIso = c.casterIso;
			ci.targetIso = c.targetIso;
			ci.landIso = c.landIso;
			ci.casterHeight = c.casterHeight;
			ci.targetHeight = c.targetHeight;
			ci.faceRight = c.faceRight;
			ci.castSec = c.castSec;
			ci.self = fxscale::Mine();
			ci.classId = c.classId;
			return ci;
		}


		// ── 직업별 기본 공격 궤적 ──────────────────────────────────────────
		void KnightBasic(const ExCtx& c)
		{
			const float ang = GroundAng(c.dir);
			// 예고: 칼끝에 빛이 모인다(0.05초) → 휘두름 → 여운: 궤적 끝의 불씨
			Gather(Along(c.casterIso, c.dirTile, 0.35f), 40.f, kGold, 0.05f, 20.f);
			switch (c.stage)
			{
			case 2:   // 올려 베기(세워서 반대로)
				Swing(c.casterIso, ang, 1.25f, -1.3f, 62.f, 36.f, 0.95f, 0.12f, kGoldHot, kGold, 0.04f);
				break;
			case 3:   // 방패 밀치기 — 짧은 앞 궤적 + 방패 빛
				Swing(c.casterIso, ang, -0.75f, 0.75f, 48.f, 22.f, 0.1f, 0.09f, kGoldHot, kGold, 0.06f);
				Spawn(vfx::Kind::Flare, Along(c.casterIso, c.dirTile, 0.7f), 32.f, 74.f, 0.24f, vfx::GoldRamp(), 0.1f);
				break;
			default:  // 베기
				Swing(c.casterIso, ang, -1.35f, 1.05f, 60.f, 34.f, 0.25f, 0.11f, kGoldHot, kGold, 0.04f);
				break;
			}
			Later(0.16f, [at = Along(c.casterIso, c.dirTile, 0.9f)]() { Motes(at, 30.f, kGold, 3, 0.5f); });
		}

		void HunterBasic(const ExCtx& c)
		{
			const float ang = GroundAng(c.dir);
			const Color edge = kMoonHot, body = kMoon;
			// 예고: 단검 끝이 번뜩(0.03초 — 타격이 0.06초라 짧게) → 찌르기 → 여운: 달빛 먼지
			Gather(Along(c.casterIso, c.dirTile, 0.3f), 38.f, kMoon, 0.03f, 16.f);
			switch (c.stage)
			{
			case 2: Swing(c.casterIso, ang, 0.9f, -0.6f, 50.f, 24.f, 0.5f, 0.07f, edge, body, 0.02f); break;
			case 3: Swing(c.casterIso, ang, -0.9f, 0.6f, 52.f, 24.f, 0.3f, 0.07f, edge, body, 0.02f); break;
			case 4:   // X자 베기(두 궤적이 엇갈린다)
				Swing(c.casterIso, ang, -1.1f, 1.0f, 58.f, 30.f, 0.9f, 0.08f, edge, body, 0.03f);
				Swing(c.casterIso, ang, 1.1f, -1.0f, 58.f, 30.f, 0.9f, 0.08f, edge, Rgb(0xB8C4D4), 0.1f);
				break;
			default: Swing(c.casterIso, ang, -0.6f, 0.9f, 50.f, 22.f, 0.35f, 0.07f, edge, body, 0.02f); break;
			}
			Later(0.12f, [at = Along(c.casterIso, c.dirTile, 0.8f)]() { Motes(at, 30.f, kMoon, 2, 0.45f); });
		}

		float EngraverBasic(const ExCtx& c)
		{
			// 각인탄: 손끝에 작은 글자 → 청록 탄(3발째는 관통하며 끝까지)
			const bool pierce = c.stage == 3;
			const float at = pierce ? 0.17f : 0.14f;
			const Vector2 hand = util::Add(c.casterIso, Vector2{ c.dir.x * 14.f, c.dir.y * 14.f });
			Spawn(vfx::Kind::Glyph, hand, 50.f, pierce ? 46.f : 34.f, 0.3f, vfx::TealRamp());
			Vector2 to = Along(c.casterIso, c.dirTile, pierce ? 7.f : 6.f);
			float zb = 30.f;
			if (!pierce && c.nPts > 0) { to = c.pts[0]; zb = c.ptsZ[0]; }
			const float flight = at - 0.03f;
			Missile& m = Shoot(MKind::Orb, hand, 48.f, to, zb, pierce ? flight + 0.08f : flight, kTealHot, kTeal, 0.03f, 0.f, pierce ? 1.35f : 1.f);
			(void)m;
			return at;
		}

		// ── 각성기 ────────────────────────────────────────────────────────
		void AwakenKnight(const ExCtx& c)
		{
			// 0.6 대상 바닥에 거대한 새김 원이 획 순서대로 → 1.0 하늘에 빛이 모인다(종 세 번은 타격 시각에)
			Spawn(vfx::Kind::Seal, c.aimIso, 0.f, 2.f * kTilePx * 3.4f, 2.4f, vfx::GoldRamp(), 0.55f, 0.f, 1.2f);
			Spawn(vfx::Kind::Rune, c.aimIso, 0.f, 2.f * kTilePx * 2.4f, 2.2f, vfx::GoldRamp(), 0.65f, 0.f, 0.8f);
			for (int i = 0; i < 3; ++i)
				Spawn(vfx::Kind::Flare, c.aimIso, 260.f + i * 20.f, 90.f + i * 30.f, 0.5f, vfx::GoldRamp(), 0.95f + i * 0.12f, 0.f, 0.8f);
			Later(0.6f, [iso = c.casterIso]()
			{
				for (int i = 0; i < 14; ++i) Smoke(iso, 2.f, Rgb(0xB8A078, 150), RandF(20.f, 30.f), 0.8f, 20.f);
			});
			// 하늘빛이 모인다
			Later(1.0f, [iso = c.aimIso]()
			{
				for (int i = 0; i < 24; ++i)
				{
					fx::Particle& p = P();
					const float a = RandF(0.f, kPi * 2.f), r = RandF(60.f, 120.f);
					p.pos = { iso.x + std::cos(a) * r, iso.y + std::sin(a) * r * 0.5f };
					p.z = RandF(240.f, 320.f);
					p.vel = { -std::cos(a) * r * 2.2f, -std::sin(a) * r * 1.1f };
					p.life = p.maxLife = 0.4f;
					p.size0 = 9.f; p.size1 = 2.f;
					p.c0 = Rgb(0xFFF0C8); p.c1 = Rgb(0xF2A040, 0);
					p.shape = fx::Shape::Glow;
					p.additive = true;
				}
			});
		}

		void AwakenEngraver(const ExCtx& c)
		{
			// 대상 위 하늘에 여러 겹 원형 각인이 획 순서대로 — 1.1 운석(예전 낙하) → 1.6 충돌
			for (int i = 0; i < 3; ++i)
			{
				SkyRune r;
				r.iso = c.aimIso; r.z = 200.f + i * 34.f; r.r = 120.f - i * 28.f;
				r.life = std::max(0.8f, c.castSec + 0.1f) - i * 0.1f; r.draw = 0.7f + i * 0.15f; r.alpha = fxscale::Alpha();
				g_skyRunes.push_back(r);
			}
			Spawn(vfx::Kind::Rune, c.aimIso, 0.f, 2.f * kTilePx * 3.5f, std::max(1.f, c.castSec + 0.2f), RampOf(Rgb(0xFFE8C0), kEmber, Rgb(0x6A2A10)), 0.1f, 0.f, 0.9f);
			Snd(sfx::Id::Scribe, c.aimIso, 0.8f, 0.02f, 0.15f);
			Snd(sfx::Id::MeteorFall, c.aimIso, 0.8f, 0.02f, 1.05f);
		}

		void AwakenHunter(const ExCtx& c)
		{
			// 하늘에 가는 초승달 → 0.4~1.2 최대 7명 사이를 순간이동하며 은빛 선 → 1.4 한꺼번에(타격 시각)
			Spawn(vfx::Kind::Slash, util::Add(c.casterIso, Vector2{ 40.f, 0.f }), 300.f, 120.f, 1.6f, vfx::MoonRamp(), 0.1f, -kPi * 0.5f, 0.8f);
			const int n = std::max(1, c.nPts);
			Vector2 prev = c.casterIso;
			for (int i = 0; i < n; ++i)
			{
				const Vector2 to = c.nPts > 0 ? c.pts[i] : OnEllipse(c.casterIso, 120.f, i * 2.4f);
				const float at = 0.4f + 0.8f * static_cast<float>(i) / n;
				Later(at, [prev, to]()
				{
					trail::FollowSpec f;
					f.width = 10.f; f.fade = 1.1f; f.edge = kMoonHot; f.body = kMoon;
					trail::Streak(prev, 34.f, to, 34.f, f);
					trail::FollowSpec ink;
					ink.width = 16.f; ink.fade = 0.9f; ink.edge = Rgb(0x3A3A46); ink.body = kInk; ink.additive = false; ink.alpha = 0.6f;
					trail::Streak(prev, 20.f, to, 20.f, ink);
					Spawn(vfx::Kind::InkStain, to, 0.f, 70.f, 4.f, vfx::InkRamp());
					Spawn(vfx::Kind::Slash, to, 34.f, 70.f, 0.2f, vfx::MoonRamp(), 0.f, RandF(0.f, kPi));
					Snd(sfx::Id::ShadowStep, to, 0.5f, 0.1f);
				});
				prev = to;
			}
		}

		// ── 서약기사 새 기술 ──────────────────────────────────────────────
		void ShieldPath(const ExCtx& c, bool back)
		{
			// 방패 투척: 1단 = 대상 → 가까운 적(최대 3), 2단 = 되돌아오며 끌어당김
			std::vector<Vector2> pts;
			std::vector<float> zs;
			if (!back)
			{
				pts.push_back(util::Add(c.casterIso, Vector2{ c.dir.x * 16.f, c.dir.y * 16.f }));
				zs.push_back(40.f);
				for (int i = 0; i < c.nPts && i < 3; ++i) { pts.push_back(c.pts[i]); zs.push_back(c.ptsZ[i]); }
				if (pts.size() == 1) { pts.push_back(Along(c.casterIso, c.dirTile, 5.f)); zs.push_back(30.f); }
			}
			else
			{
				for (int i = c.nPts - 1; i >= 0; --i) { pts.push_back(c.pts[i]); zs.push_back(c.ptsZ[i]); }
				if (pts.empty()) { pts.push_back(Along(c.casterIso, c.dirTile, 4.f)); zs.push_back(30.f); }
				pts.push_back(c.casterIso);
				zs.push_back(40.f);
			}
			// 던지기 전 0.08초 = 예고(방패가 달아오른다). 서버 첫 타 0.15초는 그대로 — 첫 구간을 0.07초로 빠르게
			float t = back ? 0.f : 0.08f;
			if (!back)
			{
				Gather(pts[0], 40.f, kGold, 0.08f, 24.f);
				Spawn(vfx::Kind::Flare, pts[0], 40.f, 46.f, 0.12f, vfx::GoldRamp(), 0.03f);
			}
			for (size_t i = 0; i + 1 < pts.size(); ++i)
			{
				const float leg = i == 0 ? (back ? 0.2f : 0.07f) : 0.11f;
				Missile& m = Shoot(MKind::Shield, pts[i], zs[i], pts[i + 1], zs[i + 1], leg, kGoldHot, kGold, t, 0.f, 1.f);
				const Vector2 hit = pts[i + 1];
				const bool last = i + 2 == pts.size();
				m.onArrive = [hit, back, last]()
				{
					if (back && last) { Snd(sfx::Id::ShieldBash, hit, 0.4f, 0.05f); return; }
					Spawn(vfx::Kind::Flare, hit, 30.f, 80.f, 0.2f, vfx::GoldRamp());
					for (int k = 0; k < 8; ++k) Spark(hit, 30.f, RandF(120.f, 240.f), RandF(0.f, 6.28f), kGoldHot, Rgb(0xE08A30, 0), 0.25f, 6.f);
					Motes(hit, 30.f, kGold, 3, 0.6f);   // 여운
					Snd(sfx::Id::MetalRing, hit, 0.55f, 0.08f);
				};
				t += leg;
			}
			if (back)
				for (int i = 0; i < c.nPts; ++i)
				{
					vfx::Spec s = vfx::Make(vfx::Kind::Beam, c.pts[i], 30.f, 12.f, 0.3f, vfx::GoldRamp());
					s.delay = 0.24f + i * 0.06f;
					vfx::BeamBetween(c.pts[i], c.ptsZ[i], c.casterIso, 36.f, 12.f, 0.3f, s);
				}
		}

		void Bell22(const ExCtx& c)
		{
			Bell b;
			b.iso = c.aimIso;
			b.drop = std::max(0.3f, c.castSec);
			b.alpha = fxscale::Alpha();
			g_bells.push_back(b);
			Spawn(vfx::Kind::Seal, c.aimIso, 0.f, 2.f * kTilePx * 2.4f, 2.0f, vfx::GoldRamp(), 0.f, 0.f, 0.7f);
			Snd(sfx::Id::BellSmall, c.aimIso, 0.4f, 0.02f);
		}

		// ── 각인술사 새 기술 ──────────────────────────────────────────────
		void FireRune26(const ExCtx& c)
		{
			const vfx::Ramp fire = RampOf(Rgb(0xFFEEC8), kEmber, Rgb(0x6A2410));
			Spawn(vfx::Kind::Rune, c.aimIso, 0.f, 2.f * kTilePx * 3.2f, 2.0f, fire, 0.f, 0.f, 1.1f);
			Spawn(vfx::Kind::Glyph, util::Add(c.casterIso, Vector2{ c.dir.x * 14.f, c.dir.y * 14.f }), 52.f, 50.f, 0.6f, vfx::TealRamp());
			Snd(sfx::Id::Scribe, c.casterIso, 0.8f);
		}

		void Barrier27(const ExCtx& c)
		{
			const float r = kTilePx * 2.f;
			for (int i = 0; i < 12; ++i)
			{
				const float a = i * kPi * 2.f / 12.f;
				Spawn(vfx::Kind::Glyph, OnEllipse(c.aimIso, r, a), 18.f + 6.f * std::sin(a * 3.f), 34.f, 1.7f, vfx::TealRamp(), i * 0.025f);
			}
			GroundFx g;
			g.kind = GKind::RingSpin; g.a = c.aimIso; g.r = r; g.life = 1.8f; g.col = kTeal; g.alpha = fxscale::Alpha();
			g_ground.push_back(g);
			Spawn(vfx::Kind::GroundRing, c.aimIso, 0.f, r * 2.2f, 0.6f, vfx::TealRamp());
			Snd(sfx::Id::Scribe, c.aimIso, 0.8f);
		}

		void Thunder28(const ExCtx& c)
		{
			// 대상이 있으면 그 머리 위(targetIso), 없으면 지점 — 타격은 타격 시각의 대상 자리에 떨어진다
			const Vector2 at = c.targetIso;
			Spawn(vfx::Kind::Glyph, at, 190.f, 96.f, 1.35f, RampOf(Rgb(0xF4FFFF), kBolt, Rgb(0x2A6A70)));
			Spawn(vfx::Kind::Rune, at, 0.f, 2.f * kTilePx * 1.4f, 1.3f, RampOf(Rgb(0xF4FFFF), kBolt, Rgb(0x2A6A70)), 0.f, 0.f, 0.8f);
			Snd(sfx::Id::Scribe, at, 0.8f);
			Snd(sfx::Id::Thunder, at, 0.25f, 0.1f, 0.6f);
		}

		// ── 밤사냥꾼 새 기술 ──────────────────────────────────────────────
		void Daggers33(const ExCtx& c)
		{
			const bool pierce = c.stage >= 3;
			const Vector2 hand = util::Add(c.casterIso, Vector2{ c.dir.x * 12.f, c.dir.y * 12.f });
			// 갈래 「세 줄기」: 부채꼴이 넓어지고 셋째 투척이 세 줄기 · 「독 비수」: 녹빛
			const bool twin = skillview::Mod(33, c.flags, SkillGrowth::Mod::Twin) != nullptr;
			const bool venom = skillview::Mod(33, c.flags, SkillGrowth::Mod::Element) != nullptr;
			const Color hot = venom ? Rgb(0xE8FFD0) : kMoonHot, body = venom ? kPoison : kMoon;
			// 예고: 손끝에 달빛이 모인다(0.08초) → 던진다(서버 타격 0.12 · 0.15초에 닿게 비행을 줄인다)
			Gather(hand, 40.f, body, 0.08f, 22.f);
			if (pierce)
			{
				for (int i = twin ? -1 : 0; i <= (twin ? 1 : 0); ++i)
					Shoot(MKind::Dagger, hand, 40.f, Along(c.casterIso, RotTile(c.dirTile, i * 0.35f), 6.5f), 30.f, 0.08f, hot, body, 0.08f, 0.f, i == 0 ? 1.6f : 1.2f);
				Snd(sfx::Id::Whoosh, c.casterIso, 0.8f, 0.05f, 0.05f);
				Snd(sfx::Id::MetalRing, c.casterIso, 0.3f, 0.1f);
				return;
			}
			for (int i = -2; i <= 2; ++i)
			{
				const Vector2 d = RotTile(c.dirTile, i * (twin ? 0.36f : 0.26f));
				Shoot(MKind::Dagger, hand, 40.f, Along(c.casterIso, d, 5.2f), 26.f, 0.05f + std::abs(i) * 0.01f, hot, body, 0.08f, 0.f, 1.f);
			}
			Snd(sfx::Id::Whoosh, c.casterIso, 0.7f, 0.1f, 0.05f);
			Snd(sfx::Id::MetalRing, c.casterIso, 0.25f, 0.1f);
		}

		void Vial34(const ExCtx& c)
		{
			// 예고 0.08초(손에 독빛) → 던진다(착탄 0.2초 그대로)
			const Vector2 hand = util::Add(c.casterIso, Vector2{ c.dir.x * 10.f, c.dir.y * 10.f });
			Gather(hand, 44.f, kPoison, 0.08f, 22.f);
			Shoot(MKind::Vial, hand, 44.f, c.aimIso, 6.f, 0.12f, Rgb(0xD8FFB0), kPoison, 0.08f, 46.f);
			Snd(sfx::Id::Whoosh, c.casterIso, 0.5f, 0.1f, 0.06f);
		}

		void Clones32(const ExCtx& c)
		{
			// 예고 0.08초(먹물이 모인다) → 분신 둘
			Gather(c.casterIso, 40.f, kMoon, 0.08f, 50.f);
			for (int s = -1; s <= 1; s += 2)
			{
				const Vector2 p = util::Add(c.casterIso, Vector2{ s * 36.f, -6.f });
				Spawn(vfx::Kind::InkBurst, p, 30.f, 80.f, 0.5f, vfx::InkRamp(), 0.08f);
				Spawn(vfx::Kind::InkStain, p, 0.f, 60.f, 3.f, vfx::InkRamp(), 0.08f);
				Later(0.08f, [p]() { for (int i = 0; i < 6; ++i) Smoke(p, RandF(10.f, 50.f), Rgb(0x2A2A34, 170), 18.f, 0.6f, 24.f); });
			}
			Snd(sfx::Id::InkSplash, c.casterIso, 0.8f, 0.05f, 0.08f);
			Snd(sfx::Id::ShadowStep, c.casterIso, 0.4f, 0.05f);
		}

		void Veil36(const ExCtx& c)
		{
			// 예고 0.1초(먹물이 모인다 · 낮은 울림) → 터짐(T3: 충격파 · 조명) → 여운: 먹물 자국 5초
			Gather(c.casterIso, 40.f, kMoon, 0.1f, 60.f);
			Snd(sfx::Id::Drone, c.casterIso, 0.35f, 0.02f);
			Later(0.1f, [at = c.casterIso]()
			{
				for (int i = 0; i < 8; ++i)
				{
					const float a = i * kPi / 4.f;
					Smoke(OnEllipse(at, 26.f, a), RandF(10.f, 50.f), Rgb(0x1E1E26, 190), 26.f, 0.9f, 30.f);
				}
				post::Shockwave(at, 16.f, 170.f, 26.f, 0.55f, 0.4f);
				Light(at, fxgrade::Tier::T3, kMoon, 0.8f);
			});
			Spawn(vfx::Kind::InkBurst, c.casterIso, 34.f, 110.f, 0.6f, vfx::InkRamp(), 0.1f);
			Spawn(vfx::Kind::InkStain, c.casterIso, 0.f, 110.f, 5.f, vfx::InkRamp(), 0.1f);
			Spawn(vfx::Kind::Flare, c.casterIso, 56.f, 60.f, 0.25f, vfx::MoonRamp(), 0.2f);
			Snd(sfx::Id::InkSplash, c.casterIso, 0.8f, 0.05f, 0.1f);
			Snd(sfx::Id::ShadowStep, c.casterIso, 0.7f, 0.05f, 0.1f);
			Snd(sfx::Id::BoomLow, c.casterIso, 0.5f, 0.05f, 0.1f);
		}

		// ── Phase 2-D 갈래 연출 — 기술별 연출 위에 한 겹(표의 복사본이 바꾼 모양 · 성질이 눈에 보이게) ──
		Color ClassCol(uint8_t cls) { return cls == 0 ? kGold : (cls == 1 ? kTeal : kMoon); }
		Color VarCol(const ExCtx& c, const SkillGrowth::Variant* v)
		{
			if (v && v->color != 0) return Rgb(v->color);
			return ClassCol(c.classId);
		}
		vfx::Ramp RampFrom(Color col) { return RampOf(util::Mix(col, WHITE, 0.55f), col, util::Shade(col, 0.35f)); }

		// 화염 고리의 "타격" 박자(판정 0.12초 · 메아리) — 갈래 「푸른 불꽃」이면 불 자체가 푸르다(덧칠이 아니라 색이 바뀐다).
		// 주황으로 칠해진 그림 조각(fxart::FireRing · Flame)은 색을 입혀도 주황이 남아 푸른 불에는 셰이더 효과(색 램프)만 쓴다
		void FireRing9(const ExCtx& c)
		{
			const Vector2 at = c.casterIso;
			if (const auto* blue = skillview::Mod(9, c.flags, SkillGrowth::Mod::Element))
			{
				const Color hot = Rgb(0xE8F4FF), mid = Rgb(blue->color), deep = Rgb(0x1E3E78);
				const vfx::Ramp ramp = RampOf(hot, mid, deep);
				fx::Fx().Ring(at, kTilePx * 0.4f, kTilePx * 2.3f, 0.45f, util::WithA(hot, 0.9f), 4.f);
				for (int i = 0; i < 12; ++i)
				{
					const float a = i * kPi / 6.f + RandF(-0.15f, 0.15f);
					Spawn(vfx::Kind::Fire, OnEllipse(at, kTilePx * RandF(1.6f, 2.3f), a), 0.f, RandF(40.f, 58.f), RandF(0.55f, 0.8f), ramp, RandF(0.f, 0.12f));
				}
				for (int i = 0; i < 26; ++i)
				{
					const float a = RandF(0.f, kPi * 2.f);
					Ember(util::Add(at, Vector2{ std::cos(a) * RandF(40.f, 110.f), std::sin(a) * RandF(20.f, 55.f) }), RandF(0.f, 10.f), Rgb(0xD8F0FF), util::WithA(mid, 0), 6.f, RandF(0.4f, 0.8f));
				}
				Spawn(vfx::Kind::GroundRing, at, 0.f, 2.f * kTilePx * 2.4f, 0.5f, ramp, 0.f, 0.f, 1.2f);
				Spawn(vfx::Kind::Scorch, at, 0.f, 2.f * kTilePx * 2.2f, 3.f, RampOf(Rgb(0xB8D8FF), Rgb(0x3A5A8A), Rgb(0x10182A)), 0.1f);
				for (int i = 0; i < 6; ++i) Smoke(OnEllipse(at, kTilePx * 2.f, i * kPi / 3.f), 6.f, Rgb(0x9AB0C8, 120), 22.f, 0.8f, 18.f);
				post::HeatHaze(at, kTilePx * 2.4f, 0.3f, 1.0f);
				Light(at, fxgrade::Tier::T2, mid, 1.2f);
				Snd(sfx::Id::FireRoar, at, 0.5f);
				Snd(sfx::Id::IceCrack, at, 0.3f, 0.05f, 0.05f);
				return;
			}
			OnCast(9, ToCast(c));   // 불고리 그림 · 불꽃 여섯 · 불씨 · 바닥 고리 자국
			Spawn(vfx::Kind::GroundRing, at, 0.f, 2.f * kTilePx * 2.4f, 0.5f, vfx::FireRamp(), 0.f, 0.f, 1.2f);
			Spawn(vfx::Kind::Scorch, at, 0.f, 2.f * kTilePx * 2.2f, 3.f, vfx::FireRamp(), 0.1f);
			for (int i = 0; i < 6; ++i) Smoke(OnEllipse(at, kTilePx * 2.f, i * kPi / 3.f), 6.f, Rgb(0x5A4A3A, 130), 22.f, 0.8f, 18.f);
			post::HeatHaze(at, kTilePx * 2.4f, 0.4f, 1.0f);
			Light(at, fxgrade::Tier::T2, kEmber, 1.2f);
			Snd(sfx::Id::FireRoar, at, 0.6f);
		}

		// 넓은 범위: 넓어진 모양의 테(바닥)
		void WiderEdge(Vector2 center, float rPx, Color col)
		{
			Spawn(vfx::Kind::GroundRing, center, 0.f, rPx * 2.f, 0.45f, RampFrom(col), 0.f, 0.f, 0.8f);
			for (int i = 0; i < 10; ++i)
				Glow(OnEllipse(center, rPx, i * kPi / 5.f + RandF(-0.1f, 0.1f)), 6.f, 10.f, 26.f, util::WithA(col, 0.55f), 0.3f);
		}

		// 끌어당김: 가장자리에서 가운데로 빨려 드는 먼지 · 빛 줄
		void PullSwirl(Vector2 center, float rPx, Color col)
		{
			for (int i = 0; i < 14; ++i)
			{
				const float a = i * kPi * 2.f / 14.f + RandF(-0.15f, 0.15f);
				const Vector2 from = OnEllipse(center, rPx * RandF(0.85f, 1.15f), a);
				fx::Particle& p = P();
				p.pos = from;
				p.z = RandF(4.f, 26.f);
				const Vector2 to = util::Sub(center, from);
				p.vel = { to.x * 2.6f, to.y * 2.6f };
				p.drag = 0.8f;
				p.life = p.maxLife = 0.38f;
				p.size0 = RandF(4.f, 7.f); p.size1 = 1.f;
				p.c0 = util::Mix(col, WHITE, 0.4f); p.c1 = util::WithA(col, 0.f);
				p.shape = fx::Shape::Glow;
				p.additive = true;
			}
			for (int i = 0; i < 5; ++i)
				Smoke(OnEllipse(center, rPx * 0.9f, RandF(0.f, 6.28f)), 4.f, Rgb(0xB8A078, 110), 16.f, 0.45f, 8.f);
			Spawn(vfx::Kind::Vortex, center, 6.f, rPx * 1.6f, 0.45f, RampFrom(col), 0.f, 0.f, 0.8f);
		}

		// 속성 바꿈: 상태이상의 모양으로 한 번 더 터진다
		void ElementBurst(Vector2 center, float z, float rPx, StatusId st, Color col, bool small)
		{
			const int n = small ? 5 : 12;
			switch (st)
			{
			case StatusId::Burn:
				for (int i = 0; i < n; ++i) Ember(OnEllipse(center, rPx * RandF(0.f, 0.8f), RandF(0.f, 6.28f)), z, Rgb(0xFFE0A0), Rgb(0xFF4A10, 0), 30.f, RandF(0.5f, 0.9f));
				if (!small) Spawn(vfx::Kind::Fire, center, 0.f, rPx * 0.9f, 0.7f, vfx::FireRamp());
				break;
			case StatusId::Poison:
				for (int i = 0; i < n; ++i) Ember(OnEllipse(center, rPx * RandF(0.f, 0.8f), RandF(0.f, 6.28f)), z, Rgb(0xC8FF8A), Rgb(0x2A6A18, 0), 12.f, RandF(0.5f, 0.9f));
				if (!small) Spawn(vfx::Kind::InkBurst, center, z, rPx * 1.1f, 0.45f, vfx::PoisonRamp());
				break;
			case StatusId::Stun:
				for (int i = 0; i < n; ++i) Spark(center, z, RandF(120.f, 260.f), RandF(0.f, 6.28f), Rgb(0xFFFFFF), util::WithA(col, 0.f), 0.25f, 6.f);
				if (!small) Spawn(vfx::Kind::Flare, center, z, rPx * 1.2f, 0.22f, RampFrom(col));
				break;
			default:   // 둔화 · 빙결: 서리 결정
				for (int i = 0; i < n; ++i) Shard(center, z, util::Mix(col, WHITE, 0.5f), RandF(60.f, 150.f), RandF(3.f, 6.f), RandF(0.4f, 0.7f));
				if (!small) Spawn(vfx::Kind::Frost, center, 0.f, rPx * 1.3f, 2.4f, RampFrom(col));
				break;
			}
		}

		// 묵직함: 주 타격에 먼지 고리 · 낮은 울림 · 빛 한 겹
		void HeavyThud(Vector2 center, Color col)
		{
			Spawn(vfx::Kind::Dust, center, 0.f, 150.f, 0.9f, vfx::DustRamp());
			Dusts(center, 60.f, 10, 0.9f);
			Glow(center, 20.f, 30.f, 110.f, util::WithA(col, 0.6f), 0.25f);
			post::Shockwave(center, 10.f, 150.f, 24.f, 0.45f, 0.35f);
			Light(center, fxgrade::Tier::T3, col, 0.7f);
			Snd(sfx::Id::BoomLow, center, 0.55f, 0.05f);
		}

		// 재빠름: 몸 뒤로 짧은 흐름선 셋
		void SwiftStreaks(const ExCtx& c)
		{
			const Color col = ClassCol(c.classId);
			for (int i = -1; i <= 1; ++i)
			{
				trail::FollowSpec f;
				f.width = 6.f; f.fade = 0.22f; f.edge = util::Mix(col, WHITE, 0.6f); f.body = col;
				const Vector2 side{ -c.dir.y * i * 14.f, c.dir.x * i * 7.f };
				const Vector2 a = util::Add(util::Sub(c.casterIso, util::Mul(c.dir, 46.f)), side);
				const Vector2 b = util::Add(util::Add(c.casterIso, util::Mul(c.dir, 6.f)), side);
				trail::Streak(a, 26.f + i * 6.f, b, 30.f + i * 6.f, f);
			}
		}

		// 굳건함: 몸을 감싸는 새김 막이 잠깐
		void SteadyShell(const ExCtx& c)
		{
			const Color col = ClassCol(c.classId);
			Spawn(vfx::Kind::Seal, c.casterIso, 0.f, 90.f, 0.6f, RampFrom(col), 0.f, 0.f, 0.8f);
			Glow(c.casterIso, 40.f, 50.f, 90.f, util::WithA(col, 0.45f), 0.4f);
			fx::Fx().Ring(c.casterIso, 20.f, 60.f, 0.35f, util::WithA(col, 0.8f), 3.f);
		}

		// ── 차지 · 홀딩 단계 도착 표시 ──
		bool Crossed(float held, float dt, float step)
		{
			if (step <= 0.f) return false;
			return std::floor(held / step) != std::floor((held - dt) / step) && held - dt >= 0.f;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// 공개
	// ─────────────────────────────────────────────────────────────────────────
	int TierOf(uint16_t skillId)
	{
		const SkillDef* d = SkillDefTable::Find(skillId);
		return d ? d->tier : 2;
	}

	void SetListener(Vector2 iso) { g_listener = iso; g_hasListener = true; }

	void Linger(Vector2 iso, float radiusTiles, float seconds, uint32_t color, StatusId status)
	{
		const Color col = color != 0 ? Rgb(color) : kGold;
		Zone z;
		z.kind = status == StatusId::Burn ? ZKind::Fire : (status == StatusId::Poison ? ZKind::Poison : ZKind::Glint);
		z.iso = iso;
		z.r = kTilePx * std::max(1.f, radiusTiles);
		z.life = std::max(0.5f, seconds);
		z.mine = fxscale::Mine();
		z.col = col;
		z.linger = true;
		g_zones.push_back(z);
		GroundFx g;
		g.kind = GKind::RingSpin; g.a = iso; g.r = z.r; g.life = z.life; g.col = col; g.alpha = fxscale::Alpha() * 0.8f;
		g_ground.push_back(g);
		Spawn(vfx::Kind::GroundRing, iso, 0.f, z.r * 2.2f, 0.45f, RampFrom(col));
		if (status == StatusId::Slow) Spawn(vfx::Kind::Frost, iso, 0.f, z.r * 2.f, z.life, RampFrom(col), 0.f, 0.f, 0.8f);
		else if (status == StatusId::Burn) Spawn(vfx::Kind::Scorch, iso, 0.f, z.r * 2.f, z.life + 1.f, vfx::FireRamp());
		else if (status == StatusId::None) Spawn(vfx::Kind::Crack, iso, 0.f, z.r * 2.f, z.life, RampFrom(col), 0.f, RandF(0.f, 6.f));
		else Spawn(vfx::Kind::InkStain, iso, 0.f, z.r * 2.f, z.life, RampFrom(col));
	}

	int ExCount()
	{
		return static_cast<int>(g_missiles.size() + g_bells.size() + g_zones.size() + g_ground.size() + g_fallers.size() + g_skyRunes.size() + g_later.size());
	}

	float OnExPhase(const ExCtx& c)
	{
		fxscale::Scope scope(c.mine);
		const auto ph = static_cast<SkillExPhase>(c.phase);
		const float ang = GroundAng(c.dir);
		// ── Phase 2-D 갈래: 시작하는 순간의 한 겹 ──
		if ((c.flags & SKILLEX_VARIANT_MASK) && (ph == SkillExPhase::Start || ph == SkillExPhase::ChargeRelease || ph == SkillExPhase::HoldBegin || ph == SkillExPhase::ChainStep || ph == SkillExPhase::ToggleOn))
		{
			if (skillview::Mod(c.skillId, c.flags, SkillGrowth::Mod::Swift)) SwiftStreaks(c);
			if (skillview::Mod(c.skillId, c.flags, SkillGrowth::Mod::Steady)) SteadyShell(c);
			const SkillDef* d = skillview::Def(c.skillId, c.flags);
			// 예전 판정 기술의 메아리: 서버가 같은 판정을 한 번 더 — 시전 연출을 작게 다시
			if (const auto* echo = skillview::Mod(c.skillId, c.flags, SkillGrowth::Mod::Echo); echo && d && d->special == SkillSpecial::Legacy)
			{
				// 서버: 첫 판정 atMs 뒤 + value×10ms → 시전 연출도 그만큼 뒤에 한 번 더(판정까지의 간격이 같다)
				const Color col = VarCol(c, echo);
				// 화염 고리는 불고리가 판정 순간(OnExBeat)에 퍼진다 → 메아리도 판정 시각 + 간격
				const float lead = c.skillId == 9 && d->hitCount > 0 ? d->hits[0].atMs / 1000.f : 0.f;
				Later(lead + echo->value * 0.01f, [c, col]()
				{
					fxscale::DimScope dim(0.62f);
					if (c.skillId == 9) FireRing9(c);
					else OnCast(c.skillId, ToCast(c));
					const Vector2 at = c.skillId == 8 ? c.targetIso : (c.skillId == 2 || c.skillId == 17 ? c.targetIso : c.casterIso);
					Spawn(vfx::Kind::GroundRing, at, 0.f, kTilePx * 3.f, 0.5f, RampFrom(col), 0.f, 0.f, 0.9f);
				});
			}
			// 돌진 · 그늘 걸음의 장판: 도착한 자리에
			if (const auto* lv = skillview::Mod(c.skillId, c.flags, SkillGrowth::Mod::Lasting); lv && (c.skillId == 4 || c.skillId == 14))
			{
				const Vector2 at = c.landIso;
				const float sec = static_cast<float>(lv->value);
				const StatusId st = lv->status;
				const uint32_t col = lv->color;
				Later(0.25f, [at, sec, st, col]() { Linger(at, 1.f, sec, col, st); });
			}
		}
		switch (c.skillId)
		{
		// ── 기본 공격 ──
		case 1: KnightBasic(c); return 0.f;
		case 7: return EngraverBasic(c);
		case 13: HunterBasic(c); return 0.f;

		// ── 서약기사 ──
		case 3:   // 회전 베기(홀딩)
			if (ph == SkillExPhase::HoldBegin)
			{
				// 예고: 몸 둘레로 금빛이 모인다(0.1초 — 첫 틱 0.12초) → 첫 바퀴
				Gather(c.casterIso, 34.f, kGold, 0.1f, 48.f);
				Swing(c.casterIso, ang, 0.f, kPi * 2.f, 62.f, 32.f, 0.15f, 0.16f, kGoldHot, kGold, 0.1f);
				Spawn(vfx::Kind::GroundRing, c.casterIso, 0.f, 150.f, 0.4f, vfx::GoldRamp(), 0.1f);
				Dusts(c.casterIso, 50.f, 8);
				Snd(sfx::Id::Whirl, c.casterIso, 0.7f, 0.05f, 0.08f);
			}
			return 0.f;
		case 5:   // 철벽 태세(토글)
			if (ph == SkillExPhase::ToggleOff)
			{
				fx::Fx().Ring(c.casterIso, 30.f, 10.f, 0.25f, Rgb(0xFFE8B0, 180), 3.f);
				Snd(sfx::Id::GuardUp, c.casterIso, 0.35f, 0.02f);
				return 0.f;
			}
			Gather(c.casterIso, 40.f, kGold, 0.1f, 44.f);
			return OnCast(5, ToCast(c));
		case 6:
			OnCast(6, ToCast(c));
			AwakenKnight(c);
			return 0.f;
		case 19:  // 땅 가르기(차지)
			if (ph == SkillExPhase::ChargeBegin)
			{
				Spawn(vfx::Kind::Crack, c.casterIso, 0.f, 90.f, 1.6f, vfx::GoldRamp(), 0.f, RandF(0.f, 6.f), 0.6f);
				Snd(sfx::Id::ChargeLoop, c.casterIso, 0.6f, 0.02f);
			}
			else if (ph == SkillExPhase::ChargeRelease)
			{
				// 내리친다: 세운 궤적 + 발밑 충격
				Swing(c.casterIso, ang, -0.2f, 0.2f, 70.f, 44.f, kPi * 0.5f, 0.1f, kGoldHot, kGold);
				impact::Ctx k; k.iso = Along(c.casterIso, c.dirTile, 0.6f); k.z = 10.f; k.tier = fxgrade::Tier::T2; k.elem = impact::Element::Earth; k.dir = c.dir;
				impact::Slam(k, 40.f + c.stage * 8.f);
				Snd(sfx::Id::BoomLow, c.casterIso, 0.7f);
				if (c.flags & SKILLEX_PERFECT) { Snd(sfx::Id::Perfect, c.casterIso, 0.8f, 0.01f); Spawn(vfx::Kind::Flare, c.casterIso, 60.f, 120.f, 0.3f, vfx::GoldRamp()); }
			}
			return 0.f;
		case 20:  // 방패 투척(체인)
			ShieldPath(c, c.stage >= 2);
			Snd(sfx::Id::Whoosh, c.casterIso, 0.6f, 0.08f);
			return 0.f;
		case 21:  // 맹세 함성: 예고(가슴에 금빛이 모인다 · 새김 원 0.1초) → 음파 고리 · 빛기둥 → 여운: 떠오르는 금빛
			Gather(c.casterIso, 44.f, kGold, 0.1f, 50.f);
			Spawn(vfx::Kind::Seal, c.casterIso, 0.f, 150.f, 0.9f, vfx::GoldRamp());
			for (int i = 0; i < 3; ++i)
				Spawn(vfx::Kind::GroundRing, c.casterIso, 0.f, 2.f * kTilePx * (1.2f + i * 0.6f), 0.55f, vfx::GoldRamp(), 0.1f + i * 0.07f);
			for (int i = 0; i < 6; ++i)
				SpawnWH(vfx::Kind::Pillar, OnEllipse(c.casterIso, 30.f, i * kPi / 3.f), 0.f, 16.f, 130.f, 0.5f, vfx::GoldRamp(), 0.15f + i * 0.03f);
			Snd(sfx::Id::Rally, c.casterIso, 0.9f, 0.02f, 0.08f);
			Snd(sfx::Id::BellSmall, c.casterIso, 0.5f, 0.02f, 0.14f);
			Later(0.45f, [at = c.casterIso]() { Motes(at, 20.f, kGold, 8, 1.0f, 70.f); });
			return 0.f;
		case 22:  // 심판의 종(캐스팅)
			if (ph == SkillExPhase::Cancel) { g_bells.clear(); return 0.f; }
			Bell22(c);
			return 0.f;
		case 23:  // 몸 굴리기: 박차는 먼지 · 쉭 → 구름 → 착지 먼지 · 쿵
			Dusts(c.casterIso, 20.f, 5, 0.6f);
			Snd(sfx::Id::Whoosh, c.casterIso, 0.45f, 0.08f);
			Snd(sfx::Id::Roll, c.casterIso, 0.8f);
			Later(0.22f, [at = c.landIso]() { Dusts(at, 24.f, 6, 0.7f); Snd(sfx::Id::BoomLow, at, 0.25f, 0.1f); });
			return 0.f;
		case 24:  // 서약 해방(T3): 예고(몸에 금빛이 모이고 새김 원이 먼저 그려진다 0.1초) → 빛기둥 · 충격파(판정 0.15초) → 여운(OnExBeat: 균열 5초)
			Gather(c.casterIso, 50.f, kGold, 0.1f, 60.f);
			Spawn(vfx::Kind::Seal, c.casterIso, 0.f, 230.f, 1.3f, vfx::GoldRamp(), 0.f, 0.f, 1.2f);
			Snd(sfx::Id::Drone, c.casterIso, 0.3f, 0.02f);
			Later(0.1f, [at = c.casterIso]()
			{
				impact::Ctx k; k.iso = at; k.z = 0.f; k.tier = fxgrade::Tier::T3; k.elem = impact::Element::Gold;
				impact::Pillar(k, 300.f);
				Spawn(vfx::Kind::GroundRing, at, 0.f, 260.f, 0.6f, vfx::GoldRamp());
				post::Shockwave(at, 20.f, 180.f, 30.f, 0.6f, 0.5f);
				Light(at, fxgrade::Tier::T3, kGold);
			});
			Snd(sfx::Id::BellToll, c.casterIso, 0.8f, 0.01f, 0.1f);
			Snd(sfx::Id::Rally, c.casterIso, 0.6f, 0.02f, 0.15f);
			return 0.f;

		// ── 각인술사 ──
		case 12:
			OnCast(12, ToCast(c));
			AwakenEngraver(c);
			return 0.f;
		case 25:  // 얼음 창(차지)
			if (ph == SkillExPhase::ChargeBegin)
			{
				Spawn(vfx::Kind::Glyph, util::Add(c.casterIso, Vector2{ c.dir.x * 16.f, c.dir.y * 16.f }), 52.f, 44.f, 0.5f, vfx::FrostRamp());
				Snd(sfx::Id::Scribe, c.casterIso, 0.6f);
			}
			else if (ph == SkillExPhase::ChargeRelease)
			{
				const Vector2 hand = util::Add(c.casterIso, Vector2{ c.dir.x * 20.f, c.dir.y * 20.f });
				// 갈래 「세 줄기」: 늘 세 줄기(3단이면 다섯 — 부채꼴이 넓어진다)
				const bool twin = skillview::Mod(25, c.flags, SkillGrowth::Mod::Twin) != nullptr;
				const int n = c.stage >= 3 ? (twin ? 5 : 3) : (twin ? 3 : 1);
				for (int i = 0; i < n; ++i)
				{
					const float step = n == 5 ? 0.3f : 0.44f;
					const float off = n == 1 ? 0.f : (i - (n - 1) * 0.5f) * step;
					const Vector2 d = RotTile(c.dirTile, off);
					Shoot(MKind::Spear, hand, 44.f, Along(c.casterIso, d, 6.5f), 30.f, 0.14f, Rgb(0xF4FCFF), kFrost, 0.f, 0.f, 0.8f + 0.15f * c.stage);
				}
				Snd(sfx::Id::IceCrack, c.casterIso, 0.7f);
				Snd(sfx::Id::Whoosh, c.casterIso, 0.6f, 0.08f);
				if (c.flags & SKILLEX_PERFECT) { Snd(sfx::Id::Perfect, c.casterIso, 0.8f, 0.01f); Spawn(vfx::Kind::Flare, hand, 50.f, 110.f, 0.3f, vfx::FrostRamp()); }
			}
			return 0.f;
		case 26:
			if (ph == SkillExPhase::Cancel) return 0.f;
			FireRune26(c);
			return 0.f;
		case 27: Barrier27(c); return 0.f;
		case 28: Thunder28(c); return 0.f;
		case 29:  // 눈보라(홀딩)
			if (ph == SkillExPhase::HoldBegin)
			{
				// 예고: 손끝에 서리 빛이 모이며 글자(0.1초 — 첫 틱 0.12초)
				Gather(util::Add(c.casterIso, Vector2{ c.dir.x * 16.f, c.dir.y * 16.f }), 50.f, kFrost, 0.1f, 30.f);
				Spawn(vfx::Kind::Glyph, util::Add(c.casterIso, Vector2{ c.dir.x * 16.f, c.dir.y * 16.f }), 52.f, 44.f, 0.5f, vfx::FrostRamp());
				Snd(sfx::Id::IceCrack, c.casterIso, 0.5f);
			}
			return 0.f;
		case 30:  // 새김 울림
			Spawn(vfx::Kind::GroundRing, c.casterIso, 0.f, 2.f * kTilePx * 8.f, 0.9f, vfx::TealRamp());
			Spawn(vfx::Kind::Rune, c.casterIso, 0.f, 220.f, 1.2f, vfx::TealRamp());
			Spawn(vfx::Kind::Glyph, c.casterIso, 70.f, 70.f, 0.8f, vfx::TealRamp());
			Snd(sfx::Id::BellSmall, c.casterIso, 0.6f, 0.02f);
			Snd(sfx::Id::Scribe, c.casterIso, 0.7f);
			return 0.f;

		// ── 밤사냥꾼 ──
		case 18:
			AwakenHunter(c);
			return 0.f;
		case 31:  // 칼날 폭풍(홀딩)
			if (ph == SkillExPhase::HoldBegin)
			{
				// 예고: 몸 둘레로 달빛 칼날이 모인다(0.1초 — 첫 틱 0.12초)
				Gather(c.casterIso, 34.f, kMoon, 0.1f, 44.f);
				GroundFx g;
				g.kind = GKind::RingSpin; g.a = c.casterIso; g.r = kTilePx * 1.2f; g.life = 1.7f; g.col = kMoon; g.alpha = fxscale::Alpha();
				g_ground.push_back(g);
				Snd(sfx::Id::BladeHum, c.casterIso, 0.6f, 0.05f, 0.06f);
			}
			return 0.f;
		case 32: Clones32(c); return 0.f;
		case 33: Daggers33(c); return 0.f;
		case 34: Vial34(c); return 0.f;
		case 35:  // 달 가르기(차지 → 돌진)
			if (ph == SkillExPhase::ChargeBegin)
			{
				Glow(c.casterIso, 40.f, 20.f, 60.f, util::WithA(kMoon, 0.7f), 0.5f);
				Snd(sfx::Id::ChargeLoop, c.casterIso, 0.5f, 0.02f);
			}
			else if (ph == SkillExPhase::ChargeRelease)
			{
				trail::FollowSpec f;
				f.width = 22.f; f.fade = 0.5f; f.edge = kMoonHot; f.body = kMoon;
				trail::Streak(c.casterIso, 30.f, c.landIso, 30.f, f);
				Spawn(vfx::Kind::InkStain, c.casterIso, 0.f, 70.f, 5.f, vfx::InkRamp());
				Snd(sfx::Id::Dash, c.casterIso, 0.7f);
				if (c.flags & SKILLEX_PERFECT) Snd(sfx::Id::Perfect, c.casterIso, 0.8f, 0.01f);
			}
			return 0.f;
		case 36: Veil36(c); return 0.f;

		// ── 예전 판정 기술: 예전 연출 + 2-A 도구로 한 겹 더(궤적 · 글자 · 여운) ──
		case 2:   // 방패 강타: 방패가 달아오른다 → 짧은 앞 궤적
			OnCast(2, ToCast(c));
			Spawn(vfx::Kind::Flare, util::Add(c.casterIso, Vector2{ c.dir.x * 18.f, c.dir.y * 18.f }), 34.f, 60.f, 0.18f, vfx::GoldRamp());
			Swing(c.casterIso, ang, -0.8f, 0.8f, 50.f, 26.f, 0.2f, 0.09f, kGoldHot, kGold, 0.05f);
			Snd(sfx::Id::BellSmall, c.casterIso, 0.4f, 0.02f, 0.1f);
			return 0.f;
		case 4:   // 돌진: 금빛 흐름선 + 흙먼지 꼬리
		{
			// 예고: 웅크림 먼지 · 모이는 빛(0.08초) → 돌진(방사형 흐림은 T4 예산이라 쓰지 않는다) → 여운: 멈춘 자리 흙먼지
			Gather(c.casterIso, 30.f, kGold, 0.08f, 30.f);
			Dusts(c.casterIso, 22.f, 4, 0.5f);
			OnCast(4, ToCast(c));
			trail::FollowSpec f;
			f.width = 26.f; f.fade = 0.45f; f.edge = kGoldHot; f.body = Rgb(0xD8B070);
			trail::Streak(c.casterIso, 30.f, c.landIso, 30.f, f);
			Snd(sfx::Id::Whoosh, c.casterIso, 0.6f, 0.08f);
			Later(0.35f, [at = c.landIso]() { Dusts(at, 34.f, 6, 0.9f); });
			return 0.f;
		}
		case 8:   // 서리 각인: 손끝 글자 → 지점에 서리 각인진이 획 순서대로
			OnCast(8, ToCast(c));
			Spawn(vfx::Kind::Glyph, util::Add(c.casterIso, Vector2{ c.dir.x * 14.f, c.dir.y * 14.f }), 50.f, 40.f, 0.4f, vfx::FrostRamp());
			Spawn(vfx::Kind::Rune, c.targetIso, 0.f, 2.f * kTilePx * 1.6f, 0.9f, vfx::FrostRamp(), 0.f, 0.f, 0.9f);
			Snd(sfx::Id::Scribe, c.casterIso, 0.6f);
			return 0.f;
		case 9:   // 화염 고리: 예고(손끝에 불 글자 · 불씨가 모인다) → 판정 순간(0.12초) OnExBeat가 불고리(FireRing9)
		{
			const bool blue = skillview::Mod(9, c.flags, SkillGrowth::Mod::Element) != nullptr;
			const Color col = blue ? Rgb(0x7FB8FF) : kEmber;
			const Vector2 hand = util::Add(c.casterIso, Vector2{ c.dir.x * 12.f, c.dir.y * 12.f });
			Gather(hand, 46.f, col, 0.1f, 28.f);
			Spawn(vfx::Kind::Glyph, hand, 50.f, 40.f, 0.3f, blue ? RampOf(Rgb(0xE8F4FF), col, Rgb(0x1E3E78)) : vfx::FireRamp());
			Snd(sfx::Id::Scribe, c.casterIso, 0.45f);
			return 0.f;
		}
		case 10:  // 옛길 도약: 청록 글자 가루 → 잔광선 → 고리 폭발(일그러짐은 기술 이동 알림이)
		{
			OnCast(10, ToCast(c));
			trail::FollowSpec f;
			f.width = 12.f; f.fade = 0.6f; f.edge = kTealHot; f.body = kTeal;
			trail::Streak(c.casterIso, 40.f, c.landIso, 40.f, f);
			Spawn(vfx::Kind::Rune, c.landIso, 0.f, 120.f, 0.6f, vfx::TealRamp());
			return 0.f;
		}
		case 11:  // 벼락 사슬: 손끝에 벼락 글자
			Spawn(vfx::Kind::Glyph, util::Add(c.casterIso, Vector2{ c.dir.x * 14.f, c.dir.y * 14.f }), 52.f, 40.f, 0.3f, RampOf(Rgb(0xF4FFFF), kBolt, Rgb(0x2A6A70)));
			return OnCast(11, ToCast(c));
		case 14:  // 그늘 걸음: 먹물로 녹아 사라지고 등 뒤에서 튄다
			OnCast(14, ToCast(c));
			Spawn(vfx::Kind::InkBurst, c.casterIso, 30.f, 80.f, 0.45f, vfx::InkRamp());
			Spawn(vfx::Kind::InkStain, c.landIso, 0.f, 70.f, 3.f, vfx::InkRamp(), 0.05f);
			Spawn(vfx::Kind::Slash, c.landIso, 34.f, 70.f, 0.2f, vfx::MoonRamp(), 0.08f, RandF(0.f, kPi));
			return 0.f;
		case 15:  // 독 칼날: 예고(칼끝에 독빛이 모인다 0.07초) → 녹빛 궤적 → 여운(OnExBeat: 독 자국)
			Gather(Along(c.casterIso, c.dirTile, 0.35f), 40.f, kPoison, 0.08f, 22.f);
			OnCast(15, ToCast(c));
			Swing(c.casterIso, ang, -1.0f, 0.9f, 52.f, 26.f, 0.3f, 0.07f, Rgb(0xE8FFD0), kPoison, 0.06f);
			Snd(sfx::Id::Swing, c.casterIso, 0.5f, 0.1f, 0.05f);
			return 0.f;
		case 16:  // 잔상 회피: 달빛 잔상 셋 + 흐름선
		{
			OnCast(16, ToCast(c));
			trail::FollowSpec f;
			f.width = 18.f; f.fade = 0.5f; f.edge = kMoonHot; f.body = kMoon;
			trail::Streak(c.casterIso, 34.f, c.landIso, 34.f, f);
			Snd(sfx::Id::Whoosh, c.casterIso, 0.6f, 0.08f);
			Later(0.16f, [at = c.landIso]() { Dusts(at, 22.f, 4, 0.6f); Motes(at, 30.f, kMoon, 3, 0.5f); Snd(sfx::Id::StepStone, at, 0.5f, 0.1f); });
			return 0.f;
		}
		case 17:  // 후방 습격: 예고(쌍단검에 달빛이 모인다 0.09초) → X 궤적(판정 0.14초)
			Gather(Along(c.casterIso, c.dirTile, 0.3f), 42.f, kMoon, 0.09f, 26.f);
			OnCast(17, ToCast(c));
			Swing(c.casterIso, ang, -0.9f, 0.7f, 54.f, 28.f, 0.8f, 0.07f, kMoonHot, kMoon, 0.07f);
			Swing(c.casterIso, ang, 0.9f, -0.7f, 54.f, 28.f, 0.8f, 0.07f, kMoonHot, Rgb(0xB8C4D4), 0.12f);
			return 0.f;

		default:
			break;
		}
		// 예전 판정 그대로인 기술: 예전 연출
		if (ph == SkillExPhase::Cancel || ph == SkillExPhase::ToggleOff || ph == SkillExPhase::HoldEnd)
			return 0.f;
		return OnCast(c.skillId, ToCast(c));
	}

	void OnExBeat(const ExCtx& c, int hitIdx, Vector2 center, int targets)
	{
		fxscale::Scope scope(c.mine);
		const SkillDef* d = skillview::Def(c.skillId, c.flags);   // 갈래를 입힌 표(알림 · 내 상태의 비트 1~4)
		if (!d || hitIdx < 0 || hitIdx >= d->hitCount) return;
		const SkillHit& h = d->hits[hitIdx];
		// 갈래로 덧붙은 타격: 원래 타격의 연출을 다시 — 메아리는 작게 + 파문, 세 줄기는 돌린 방향으로
		if (h.tag != HT_None && h.src < d->hitCount && d->hits[h.src].tag == HT_None)
		{
			ExCtx c2 = c;
			if (h.tag == HT_Twin)
			{
				c2.dirTile = RotTile(c.dirTile, static_cast<float>(h.turn) * kPi / 180.f);
				c2.dir = IsoDir(c2.dirTile);
				OnExBeat(c2, h.src, center, targets);
				return;
			}
			{
				fxscale::DimScope dim(0.62f);
				OnExBeat(c2, h.src, center, targets);
			}
			const Color col = VarCol(c, skillview::Mod(c.skillId, c.flags, SkillGrowth::Mod::Echo));
			const float r = h.shape == HitShape::Target ? kTilePx * 0.8f : kTilePx * std::max<float>(1.f, h.a);
			Spawn(vfx::Kind::GroundRing, center, 0.f, r * 2.3f, 0.5f, RampFrom(col), 0.f, 0.f, 0.9f);
			fx::Fx().Ring(center, r * 0.4f, r * 1.2f, 0.4f, util::WithA(col, 0.7f), 2.f);
			return;
		}
		// 갈래 한 겹(넓은 범위 · 끌어당김 · 속성 · 묵직함) — 기술별 연출은 아래 switch가 그대로
		if (c.flags & SKILLEX_VARIANT_MASK)
		{
			const bool tick = (h.flags & HF_Tick) != 0;
			const float rPx = kTilePx * (h.shape == HitShape::Target ? 0.8f : std::max<float>(1.f, h.a));
			if (const auto* v = skillview::Mod(c.skillId, c.flags, SkillGrowth::Mod::Wider); v && !tick && h.shape != HitShape::Target && h.shape != HitShape::Line)
				WiderEdge(center, rPx, VarCol(c, v));
			if (const auto* v = skillview::Mod(c.skillId, c.flags, SkillGrowth::Mod::Pull); v && (!tick || RandF() < 0.5f))
				PullSwirl(h.anchor == HitAnchor::Caster ? c.casterIso : center, rPx, VarCol(c, v));
			if (const auto* v = skillview::Mod(c.skillId, c.flags, SkillGrowth::Mod::Element))
				ElementBurst(center, 20.f, rPx, v->status, VarCol(c, v), tick);
			if (const auto* v = skillview::Mod(c.skillId, c.flags, SkillGrowth::Mod::Heavy); v && !tick && hitIdx == SkillGrowth::MainHit(*d))
				HeavyThud(center, VarCol(c, v));
		}
		const bool any = targets > 0;
		const bool heavy = (h.flags & HF_Heavy) != 0;
		const float ang = GroundAng(c.dir);
		impact::Ctx k;
		k.iso = center; k.z = 30.f; k.dir = c.dir; k.targets = std::max(1, targets); k.heavy = heavy;
		switch (c.skillId)
		{
		case 1: case 13:   // 기본 공격: 맞힌 게 있을 때만 작은 섬광(예측)
			if (any && heavy) { k.tier = fxgrade::Tier::T2; k.elem = c.skillId == 1 ? impact::Element::Gold : impact::Element::Moon; impact::SlashFlash(k, 70.f); }
			return;
		case 7:
			return;
		case 3:   // 회전 베기
			if (h.flags & HF_Tick)
			{
				Spawn(vfx::Kind::GroundRing, c.casterIso, 0.f, 130.f, 0.3f, vfx::GoldRamp(), 0.f, 0.f, any ? 1.f : 0.6f);
				if (any) Snd(sfx::Id::Swing, c.casterIso, 0.35f, 0.15f);
			}
			else
			{
				Swing(c.casterIso, ang, 0.f, kPi * 2.2f, 96.f, 46.f, 0.1f, 0.18f, kGoldHot, kGold);
				Spawn(vfx::Kind::GroundRing, c.casterIso, 0.f, 2.f * kTilePx * 2.4f, 0.5f, vfx::GoldRamp(), 0.f, 0.f, 1.3f);
				Spawn(vfx::Kind::Crack, c.casterIso, 0.f, 2.f * kTilePx * 1.8f, 2.f, vfx::GoldRamp(), 0.f, RandF(0.f, 6.f), 0.7f);   // T2 자국 2초
				Dusts(c.casterIso, 90.f, 12, 1.0f);
				Light(c.casterIso, fxgrade::Tier::T2, kGold, 1.2f);
				Later(0.2f, [at = c.casterIso]() { Motes(at, 30.f, kGold, 6, 0.8f, 60.f); });
				Snd(sfx::Id::Whirl, c.casterIso, 0.9f);
				Snd(sfx::Id::BoomLow, c.casterIso, 0.6f);
			}
			return;
		case 6:   // 단죄: 종 세 번(약한 타격) → 내리꽂음
			if (!(h.flags & HF_Final))
			{
				Spawn(vfx::Kind::Seal, center, 0.f, 2.f * kTilePx * 3.4f, 0.6f, vfx::GoldRamp(), 0.f, 0.f, 1.4f);
				Spawn(vfx::Kind::GroundRing, center, 0.f, 2.f * kTilePx * 3.2f, 0.5f, vfx::GoldRamp());
				Snd(sfx::Id::BellToll, center, 0.8f, 0.01f);
				if (hitIdx == 2)
				{
					Faller f; f.iso = center; f.dur = 0.134f; f.h0 = 460.f; f.alpha = fxscale::Alpha();
					g_fallers.push_back(f);
				}
				return;
			}
			k.tier = fxgrade::Tier::T4; k.elem = impact::Element::Gold; k.z = 10.f;
			impact::Slam(k, 110.f);
			impact::Pillar(k, 380.f);
			impact::Explosion(k, 90.f);
			Spawn(vfx::Kind::Crack, center, 0.f, 2.f * kTilePx * 3.6f, 9.f, vfx::GoldRamp());
			post::Shockwave(center, 30.f, 420.f, 50.f, 1.f, 0.7f);
			if (fxscale::Mine()) { post::RadialBlur(center, 0.5f, 0.35f); post::Chroma(0.6f, 0.4f); fx::Fx().Flash(Rgb(0xFFF4DC), 0.35f, 0.3f); }
			Light(center, fxgrade::Tier::T4, kGold);
			for (int i = 0; i < 30; ++i) Ember(center, RandF(10.f, 60.f), kGoldHot, Rgb(0xE08A30, 0), 90.f, RandF(1.2f, 2.2f));
			Snd(sfx::Id::MetalRing, center, 1.f, 0.01f);
			Snd(sfx::Id::BoomLow, center, 1.f, 0.01f);
			Snd(sfx::Id::TailVerb, center, 0.8f, 0.01f, 0.2f);
			return;
		case 19:  // 땅 가르기: 균열이 달리며 바위가 솟는다 → 완벽 구간이면 빛기둥
			if (h.flags & HF_Perfect)
			{
				k.tier = fxgrade::Tier::T3; k.elem = impact::Element::Gold; k.z = 0.f;
				impact::Pillar(k, 300.f);
				impact::Explosion(k, 60.f);
				post::Shockwave(center, 20.f, 200.f, 34.f, 0.7f, 0.5f);
				Light(center, fxgrade::Tier::T3, kGold);
				Snd(sfx::Id::Burst, center, 0.9f);
				return;
			}
			{
				GroundFx g;
				g.kind = GKind::GlowLine;
				g.a = hitIdx == 0 ? c.casterIso : Along(c.casterIso, c.dirTile, static_cast<float>(h.along) - 1.f);
				g.b = center; g.life = 1.6f; g.col = kGold; g.alpha = fxscale::Alpha();
				g_ground.push_back(g);
				Spawn(vfx::Kind::Crack, center, 0.f, 96.f, 4.f, vfx::GoldRamp(), 0.f, RandF(0.f, 6.f));
				Spawn(vfx::Kind::Dust, center, 0.f, 90.f + h.along * 6.f, 0.9f, vfx::DustRamp());
				for (int i = 0; i < 6 + h.along; ++i) Shard(center, 8.f, Rgb(0x9A8A70), RandF(80.f, 180.f), RandF(5.f, 9.f), RandF(0.6f, 1.0f));
				Glow(center, 16.f, 20.f, 70.f, util::WithA(kGold, 0.7f), 0.25f);
				Light(center, fxgrade::Tier::T2, kGold, 0.8f);
				Snd(sfx::Id::BoomLow, center, 0.35f + 0.08f * h.along, 0.08f);
				if (heavy) post::Shockwave(center, 10.f, 110.f, 20.f, 0.4f, 0.35f);
			}
			return;
		case 20: return;   // 방패가 닿을 때(투사체 도착)
		case 21:
			Spawn(vfx::Kind::GroundRing, c.casterIso, 0.f, 2.f * kTilePx * 2.4f, 0.5f, vfx::GoldRamp(), 0.f, 0.f, 1.2f);
			Spawn(vfx::Kind::Crack, c.casterIso, 0.f, 2.f * kTilePx * 1.6f, 2.f, vfx::GoldRamp(), 0.f, RandF(0.f, 6.f), 0.6f);   // T2 자국 2초
			Dusts(c.casterIso, kTilePx * 1.6f, 8, 0.8f);
			if (any) Snd(sfx::Id::BellSmall, c.casterIso, 0.5f, 0.05f);
			return;
		case 22:  // 종이 울린다(셋째에 기절)
		{
			for (auto& b : g_bells) if (util::Len(util::Sub(b.iso, center)) < 40.f) b.swing = 1.f;
			const bool last = (h.flags & HF_Final) != 0;
			Spawn(vfx::Kind::GroundRing, center, 0.f, 2.f * kTilePx * (last ? 3.0f : 2.6f), 0.55f, vfx::GoldRamp(), 0.f, 0.f, last ? 1.5f : 1.f);
			Spawn(vfx::Kind::Seal, center, 0.f, 2.f * kTilePx * 2.4f, 0.5f, vfx::GoldRamp(), 0.f, 0.f, last ? 1.5f : 1.f);
			Dusts(center, kTilePx * 2.f, last ? 14 : 8, 0.9f);
			post::Shockwave(center, 20.f, last ? 260.f : 180.f, 30.f, last ? 0.9f : 0.5f, 0.5f);
			Light(center, last ? fxgrade::Tier::T3 : fxgrade::Tier::T2, kGold);
			Snd(sfx::Id::BellToll, center, last ? 1.f : 0.75f, 0.01f);
			if (last)
			{
				k.tier = fxgrade::Tier::T3; k.elem = impact::Element::Gold; k.z = 0.f;
				impact::Slam(k, 70.f);
				Snd(sfx::Id::BoomLow, center, 0.8f);
			}
			return;
		}
		case 24:
			Spawn(vfx::Kind::GroundRing, c.casterIso, 0.f, 2.f * kTilePx * 2.4f, 0.5f, vfx::GoldRamp());
			Spawn(vfx::Kind::Crack, c.casterIso, 0.f, 2.f * kTilePx * 2.2f, 5.f, vfx::GoldRamp(), 0.f, RandF(0.f, 6.f));   // T3 자국 5초
			Dusts(c.casterIso, kTilePx * 1.8f, 10, 0.9f);
			Snd(sfx::Id::BoomLow, c.casterIso, 0.7f, 0.05f);
			Later(0.3f, [at = c.casterIso]() { Motes(at, 20.f, kGold, 10, 1.2f, 80.f); });
			return;
		case 12:  // 낙성 충돌(한 번 — 대상마다는 OnExVictim이 작게)
			k.tier = fxgrade::Tier::T4; k.elem = impact::Element::Fire; k.z = 20.f;
			impact::Explosion(k, 120.f);
			impact::Slam(k, 90.f);
			Spawn(vfx::Kind::Scorch, center, 0.f, 2.f * kTilePx * 3.6f, 9.f, vfx::FireRamp());
			Spawn(vfx::Kind::Crack, center, 0.f, 2.f * kTilePx * 3.0f, 6.f, RampOf(Rgb(0xFFE0A0), kEmber, Rgb(0x5A1A08)));
			post::Shockwave(center, 30.f, 460.f, 56.f, 1.f, 0.8f);
			post::HeatHaze(center, 200.f, 0.6f, 2.5f);
			if (fxscale::Mine()) { post::RadialBlur(center, 0.55f, 0.35f); post::Chroma(0.6f, 0.45f); fx::Fx().Flash(Rgb(0xFFFFFF), 0.4f, 0.3f); }
			Light(center, fxgrade::Tier::T4, kEmber);
			{
				Zone z; z.kind = ZKind::Fire; z.iso = center; z.r = kTilePx * 2.4f; z.life = 3.f; z.mine = fxscale::Mine();
				g_zones.push_back(z);
			}
			for (int i = 0; i < 6; ++i)
				Later(0.3f + i * 0.25f, [center]() { for (int j = 0; j < 6; ++j) Ember(center, RandF(120.f, 260.f), Rgb(0xFFE0A0), Rgb(0xFF4A10, 0), 140.f, RandF(0.8f, 1.4f)); });
			Snd(sfx::Id::MeteorImpact, center, 1.f, 0.01f);
			Snd(sfx::Id::Burst, center, 0.9f, 0.02f, 0.05f);
			Snd(sfx::Id::TailVerb, center, 0.8f, 0.01f, 0.25f);
			return;
		case 25:
			if (h.flags & HF_Perfect)
			{
				k.tier = fxgrade::Tier::T3; k.elem = impact::Element::Frost;
				impact::Shatter(k, 70.f);
				post::Shockwave(center, 10.f, 150.f, 26.f, 0.6f, 0.4f);
				Snd(sfx::Id::IceCrack, center, 0.9f);
				return;
			}
			{
				GroundFx g;
				g.kind = GKind::GlowLine; g.a = c.casterIso; g.b = Along(c.casterIso, hitIdx == 1 ? RotTile(c.dirTile, 0.44f) : c.dirTile, 6.f);
				g.life = 2.f; g.col = kFrost; g.alpha = fxscale::Alpha();
				g_ground.push_back(g);
				if (hitIdx == 1)
				{
					g.b = Along(c.casterIso, RotTile(c.dirTile, -0.44f), 6.f);
					g_ground.push_back(g);
				}
				for (int i = 1; i <= 3; ++i) Spawn(vfx::Kind::Frost, Along(c.casterIso, c.dirTile, i * 1.8f), 0.f, 70.f, 3.f, vfx::FrostRamp());
			}
			return;
		case 26:  // 불꽃 각인
			if (hitIdx == 0)
			{
				const float r = kTilePx * 3.f;
				const vfx::Ramp fire = vfx::FireRamp();
				for (int i = 0; i < 8; ++i)
				{
					const float a = i * kPi / 4.f + 0.2f;
					SpawnWH(vfx::Kind::Fire, OnEllipse(center, r, a), 0.f, 70.f, 190.f, 1.0f, fire, i * 0.04f, 0.f, 1.1f);
					Later(i * 0.04f, [p = OnEllipse(center, r, a)]() { Snd(sfx::Id::FireRoar, p, 0.25f, 0.1f); });
				}
				k.tier = fxgrade::Tier::T3; k.elem = impact::Element::Fire; k.z = 20.f;
				impact::Explosion(k, 90.f);
				Spawn(vfx::Kind::Scorch, center, 0.f, 2.f * r, 6.f, vfx::FireRamp());
				post::HeatHaze(center, r * 1.2f, 0.5f, 2.6f);
				post::Shockwave(center, 20.f, 240.f, 36.f, 0.7f, 0.5f);
				Light(center, fxgrade::Tier::T3, kEmber, 1.3f);
				Zone z; z.kind = ZKind::Fire; z.iso = center; z.r = r * 0.9f; z.life = 2.6f; z.mine = fxscale::Mine();
				g_zones.push_back(z);
				Snd(sfx::Id::Burst, center, 0.9f);
			}
			else
				for (int i = 0; i < 3; ++i)
					Spawn(vfx::Kind::Fire, OnEllipse(center, RandF(0.f, kTilePx * 2.6f), RandF(0.f, 6.28f)), 0.f, RandF(40.f, 60.f), 0.7f, vfx::FireRamp());
			return;
		case 27:  // 결계 고리가 조인다
			Spawn(vfx::Kind::GroundRing, center, 0.f, 2.f * kTilePx * 2.2f, 0.5f, vfx::TealRamp(), 0.f, 0.f, 1.2f);
			for (int i = 0; i < 12; ++i) Glow(OnEllipse(center, kTilePx * 2.f, i * kPi / 6.f), 20.f, 16.f, 40.f, util::WithA(kTeal, 0.8f), 0.3f);
			Snd(sfx::Id::Blink, center, 0.6f);
			return;
		case 28:  // 낙뢰
			k.tier = fxgrade::Tier::T3; k.elem = impact::Element::Bolt; k.z = 0.f;
			impact::LightningStrike(k, 320.f);
			Spawn(vfx::Kind::Scorch, center, 0.f, 150.f, 6.f, vfx::SmokeRamp());
			post::Shockwave(center, 10.f, 200.f, 30.f, 0.7f, 0.45f);
			if (fxscale::Mine()) fx::Fx().Flash(Rgb(0xE8FFFF), 0.3f, 0.18f);
			Light(center, fxgrade::Tier::T3, kBolt, 1.2f);
			Snd(sfx::Id::ThunderClap, center, 1.f, 0.02f);
			return;
		case 29:  // 눈보라
			if (h.flags & HF_Tick)
			{
				if (any) Spawn(vfx::Kind::Flare, Along(c.casterIso, c.dirTile, 1.8f), 30.f, 46.f, 0.18f, vfx::FrostRamp());
			}
			else
			{
				k.tier = fxgrade::Tier::T2; k.elem = impact::Element::Frost; k.iso = Along(c.casterIso, c.dirTile, 2.f);
				impact::Shatter(k, 60.f);
				GroundFx g; g.kind = GKind::Sector; g.a = c.casterIso; g.r = kTilePx * 4.2f; g.ang = ang; g.half = 0.55f; g.life = 0.6f; g.col = kFrost; g.alpha = fxscale::Alpha();
				g_ground.push_back(g);
				Snd(sfx::Id::IceCrack, c.casterIso, 0.9f);
			}
			return;
		case 18:  // 그믐 베기: 모든 선이 한꺼번에
			if (hitIdx == 0)
			{
				Vector2 prev = c.casterIso;
				for (int i = 0; i < c.nPts; ++i)
				{
					vfx::Spec s = vfx::Make(vfx::Kind::Beam, c.pts[i], 30.f, 10.f, 0.35f, vfx::MoonRamp());
					vfx::BeamBetween(prev, 34.f, c.pts[i], 34.f, 10.f, 0.35f, s);
					k.iso = c.pts[i]; k.tier = fxgrade::Tier::T3; k.elem = impact::Element::Ink; k.z = c.ptsZ[i];
					impact::InkBurst(k, 50.f);
					Spawn(vfx::Kind::GroundRing, c.pts[i], 0.f, 110.f, 0.4f, vfx::MoonRamp());
					prev = c.pts[i];
				}
				Spawn(vfx::Kind::InkStain, c.casterIso, 0.f, 2.f * kTilePx * 3.6f, 9.f, vfx::InkRamp());   // T4 자국 9초
				post::Shockwave(c.casterIso, 20.f, 320.f, 40.f, 0.8f, 0.6f);
				if (fxscale::Mine()) { post::RadialBlur(c.casterIso, 0.45f, 0.3f); post::Chroma(0.5f, 0.35f); fx::Fx().Flash(kMoonHot, 0.25f, 0.2f); }
				Light(c.casterIso, fxgrade::Tier::T4, kMoon);
				Snd(sfx::Id::MoonDance, c.casterIso, 1.f, 0.01f);
				Snd(sfx::Id::InkSplash, c.casterIso, 0.9f, 0.02f, 0.03f);
				Snd(sfx::Id::TailVerb, c.casterIso, 0.7f, 0.01f, 0.2f);
			}
			else
			{
				for (int i = 0; i < c.nPts; ++i)
					Spawn(vfx::Kind::Slash, c.pts[i], c.ptsZ[i], 60.f, 0.16f, vfx::MoonRamp(), 0.f, RandF(0.f, kPi * 2.f));
				if (h.flags & HF_Final)
				{
					// 절정: 마지막 한 번이 가운데에서 먹물로 터진다 → 여운: 흩날리는 먹물 조각 · 달빛 먼지
					k.iso = c.casterIso; k.tier = fxgrade::Tier::T4; k.elem = impact::Element::Ink; k.z = 30.f;
					impact::InkBurst(k, 90.f);
					post::Shockwave(c.casterIso, 30.f, 380.f, 48.f, 0.9f, 0.7f);
					Light(c.casterIso, fxgrade::Tier::T4, kMoon);
					Later(0.2f, [at = c.casterIso]() { Motes(at, 30.f, kMoon, 14, 1.4f, 120.f); for (int i = 0; i < 10; ++i) Smoke(OnEllipse(at, RandF(20.f, 110.f), RandF(0.f, 6.28f)), RandF(10.f, 60.f), Rgb(0x1E1E26, 170), RandF(16.f, 28.f), 1.4f, 30.f); });
					Snd(sfx::Id::InkSplash, c.casterIso, 1.f, 0.02f);
					Snd(sfx::Id::BoomLow, c.casterIso, 0.9f, 0.02f);
					Snd(sfx::Id::TailVerb, c.casterIso, 0.8f, 0.01f, 0.2f);
				}
			}
			return;
		case 31:  // 칼날 폭풍
			if (h.flags & HF_Tick)
			{
				if (any) Spawn(vfx::Kind::Slash, OnEllipse(c.casterIso, RandF(10.f, 40.f), RandF(0.f, 6.28f)), 30.f, 54.f, 0.14f, vfx::MoonRamp(), 0.f, RandF(0.f, kPi * 2.f));
			}
			else
			{
				k.tier = fxgrade::Tier::T2; k.elem = impact::Element::Moon; k.iso = c.casterIso;
				impact::SlashFlash(k, 120.f);
				Spawn(vfx::Kind::GroundRing, c.casterIso, 0.f, 2.f * kTilePx * 1.6f, 0.45f, vfx::MoonRamp());
				k.elem = impact::Element::Ink;
				impact::InkBurst(k, 40.f);
				Snd(sfx::Id::Whoosh, c.casterIso, 0.8f);
			}
			return;
		case 9:   // 화염 고리: 불고리가 퍼진다(판정 순간)
			FireRing9(c);
			return;
		case 15:  // 독 칼날: 여운 — 칼끝이 지난 자리에 독 방울 · 옅은 독 자국 2초
			Spawn(vfx::Kind::InkStain, center, 0.f, 70.f, 2.f, vfx::PoisonRamp(), 0.05f);
			Later(0.1f, [center]() { Motes(center, 26.f, kPoison, 4, 0.7f); });
			Snd(sfx::Id::PoisonBurst, center, any ? 0.4f : 0.2f, 0.1f);
			return;
		case 33: return;
		case 34:  // 약병이 깨진다 → 안개
			if (h.flags & HF_Status)
			{
				// 착탄: 섬광 · 고리 · 조명(T2 5겹) → 안개(여운)
				Spawn(vfx::Kind::Flare, center, 16.f, 70.f, 0.18f, vfx::PoisonRamp());
				Spawn(vfx::Kind::GroundRing, center, 0.f, 2.f * kTilePx * 2.f, 0.4f, vfx::PoisonRamp());
				Light(center, fxgrade::Tier::T2, kPoison);
				Spawn(vfx::Kind::InkBurst, center, 20.f, 100.f, 0.5f, vfx::PoisonRamp());
				Spawn(vfx::Kind::InkStain, center, 0.f, 2.f * kTilePx * 2.2f, 4.6f, vfx::PoisonRamp());
				Zone z; z.kind = ZKind::Poison; z.iso = center; z.r = kTilePx * std::max<float>(2.f, h.a); z.life = 4.2f; z.mine = fxscale::Mine();
				if (const auto* longer = skillview::Mod(c.skillId, c.flags, SkillGrowth::Mod::Longer)) z.life = 4.2f * (100.f + longer->value) / 100.f;
				g_zones.push_back(z);
				Snd(sfx::Id::InkSplash, center, 0.7f);
				Snd(sfx::Id::PoisonBurst, center, 0.4f);
			}
			return;
		case 35:  // 달 가르기: 0.3초 늦게 베인 선이 한꺼번에(일섬)
		{
			const Vector2 a = c.casterIso, b = c.landIso;
			vfx::Spec s = vfx::Make(vfx::Kind::Beam, a, 30.f, 14.f, 0.3f, vfx::MoonRamp());
			vfx::BeamBetween(a, 34.f, b, 34.f, hitIdx == 0 ? 14.f : 9.f, 0.3f, s);
			for (int i = 0; i < c.nPts; ++i)
			{
				k.iso = c.pts[i]; k.z = c.ptsZ[i]; k.tier = fxgrade::Tier::T3; k.elem = impact::Element::Moon;
				impact::SlashFlash(k, 110.f);
				Spawn(vfx::Kind::InkBurst, c.pts[i], c.ptsZ[i], 70.f, 0.45f, vfx::InkRamp());
				Spawn(vfx::Kind::GroundRing, c.pts[i], 0.f, 100.f, 0.35f, vfx::MoonRamp());
				Spawn(vfx::Kind::InkStain, c.pts[i], 0.f, 60.f, 5.f, vfx::InkRamp(), 0.05f);
			}
			post::Shockwave(util::LerpV(a, b, 0.5f), 10.f, 160.f, 24.f, 0.6f, 0.4f);
			Snd(sfx::Id::MetalRing, b, 0.7f, 0.05f);
			Snd(sfx::Id::Stab, b, 0.8f, 0.05f);
			return;
		}
		default:
			break;
		}
		// 표에 타격이 있는 나머지(예전 판정 기술 — 방패 강타 · 서리 각인 · 벼락 사슬 · 후방 습격)는 대상 피해의 예전 연출이 맡는다.
		// 예측 히트스톱이 빈 화면에 걸리지 않게, 모양 안에 적이 보이면 판정 자리에 작은 섬광 · 고리를 먼저
		if (any)
		{
			const Color col = ClassCol(c.classId);
			Spawn(vfx::Kind::Flare, center, 30.f, 54.f, 0.12f, RampFrom(col));
			Spawn(vfx::Kind::GroundRing, center, 0.f, 80.f, 0.25f, RampFrom(col), 0.f, 0.f, 0.8f);
		}
	}

	void ComboFx(uint8_t comboId, Vector2 iso, float height, bool crit)
	{
		// 연계는 한 등급 올려 T3 — 결과가 보이는 모양(무너짐 · 증발 · 전도 · 부서짐 · 독 · 반격 · 벽)
		const float z = height * 0.5f;
		impact::Ctx k;
		k.iso = iso; k.z = z; k.tier = fxgrade::Tier::T3; k.heavy = true; k.crit = crit;
		switch (comboId)
		{
		case 1:   // 무너뜨리기: 기절한 적을 내려친다 — 금빛 땅울림
			k.elem = impact::Element::Gold; impact::Strike(k); k.z = 0.f; impact::Slam(k, 46.f);
			break;
		case 2:   // 단죄: 빛기둥
			k.elem = impact::Element::Gold; impact::Strike(k); k.z = 0.f; impact::Pillar(k, 240.f);
			break;
		case 3:   // 증발 폭발: 흰 증기 폭발
			k.elem = impact::Element::Frost; impact::Explosion(k, 56.f);
			Spawn(vfx::Kind::Smoke, iso, z, 110.f, 1.1f, RampOf(Rgb(0xFFFFFF), Rgb(0xE8F4FF), Rgb(0x9AB4C8)), 0.05f, 0.f, 1.f, 1.6f);
			post::HeatHaze(iso, 90.f, 0.5f, 0.8f);
			break;
		case 4:   // 전도: 벼락이 대상으로 꽂힌다
			k.elem = impact::Element::Bolt; impact::LightningStrike(k, 220.f);
			break;
		case 5:   // 산산조각: 얼음이 부서진다
			k.elem = impact::Element::Frost; impact::Shatter(k, 70.f);
			break;
		case 6:   // 기습: 달빛 섬광 + 먹물
			k.elem = impact::Element::Moon; impact::SlashFlash(k, 120.f); k.elem = impact::Element::Ink; impact::InkBurst(k, 44.f);
			break;
		case 7:   // 독 폭발
			k.elem = impact::Element::Poison; impact::InkBurst(k, 60.f);
			Spawn(vfx::Kind::Smoke, iso, z, 100.f, 1.2f, vfx::PoisonRamp(), 0.05f, 0.f, 1.f, 1.5f);
			break;
		case 8:   // 반격: 새김 원 + 금빛 반격 베기
			k.elem = impact::Element::Gold; impact::SlashFlash(k, 110.f);
			Spawn(vfx::Kind::Seal, iso, 0.f, 130.f, 0.6f, vfx::GoldRamp(), 0.f, 0.f, 1.3f);
			break;
		case 9:   // 벽 충돌: 균열 · 흙먼지
			k.elem = impact::Element::Earth; k.z = 0.f; impact::Slam(k, 56.f);
			break;
		case 10:  // 급소: 크게 번쩍 + 먹물
			k.elem = impact::Element::Moon; impact::Strike(k); impact::SlashFlash(k, 150.f);
			Spawn(vfx::Kind::InkStain, iso, 0.f, 90.f, 4.f, vfx::InkRamp());
			break;
		default:
			k.elem = impact::Element::Gold; impact::Strike(k);
			break;
		}
		Light(iso, fxgrade::Tier::T3, comboId == 3 || comboId == 5 ? kFrost : (comboId == 4 ? kBolt : (comboId == 7 ? kPoison : kGold)));
		post::Shockwave(iso, 10.f, 140.f, 24.f, 0.55f, 0.4f);
	}

	void OnExVictim(uint16_t skillId, uint8_t hitIdx, Vector2 iso, float height, bool crit, uint8_t comboId, uint8_t flags2)
	{
		const float z = height * 0.5f;
		impact::Ctx k;
		k.iso = iso; k.z = z; k.crit = crit; k.heavy = (flags2 & DAMAGE2_HEAVY) != 0;
		k.tier = fxgrade::Tier::T1;
		auto& fxs = fx::Fx();
		switch (skillId)
		{
		// 기본 공격(T1): 섬광 · 작은 고리 · 불꽃 · 먼지 한 줌 · 순간 조명(HitSpark는 GameCombat이 이미 — 겹치지 않게 Strike만)
		case 1:  k.tier = k.heavy ? fxgrade::Tier::T2 : fxgrade::Tier::T1; k.elem = impact::Element::Gold; impact::Strike(k); break;
		case 13: k.tier = fxgrade::Tier::T1; k.elem = impact::Element::Moon; impact::Strike(k); break;
		case 7:
			k.tier = fxgrade::Tier::T1; k.elem = impact::Element::Teal; impact::Strike(k);
			Spawn(vfx::Kind::Glyph, iso, z, 26.f, 0.45f, vfx::TealRamp(), 0.08f);   // 여운: 맞은 자리에 흐려지는 글자
			break;
		case 5:  // 철벽 태세 반격: 방패 문장이 번쩍 + 먼지
			k.tier = fxgrade::Tier::T2; k.elem = impact::Element::Gold; k.heavy = true; impact::Strike(k);
			Spawn(vfx::Kind::Seal, iso, 0.f, 90.f, 0.6f, vfx::GoldRamp());
			Dusts(iso, 30.f, 5, 0.6f);
			break;
		case 15: // 독 칼날: 녹빛 타격 + 독 방울
			k.tier = fxgrade::Tier::T2; k.elem = impact::Element::Poison; impact::Strike(k);
			Tx(fxart::Venom, iso, z, 40.f, 80.f, 0.35f, Rgb(0xE0FFC0, 235), Rgb(0x4AB020, 0), true, RandF(0.f, 6.f), 0.f, 0.05f);
			for (int i = 0; i < 4; ++i) Ember(iso, z, Rgb(0xB8FF7A), Rgb(0x2A8A20, 0), 10.f, 0.6f);
			break;
		case 6:  k.tier = fxgrade::Tier::T3; k.elem = impact::Element::Gold; impact::Strike(k); break;
		case 12: k.tier = fxgrade::Tier::T3; k.elem = impact::Element::Fire; impact::Strike(k); if (comboId != 0) impact::Shatter(k, 50.f); break;
		case 18: k.tier = fxgrade::Tier::T2; k.elem = impact::Element::Moon; impact::Strike(k); break;
		case 3:  k.tier = fxgrade::Tier::T1; k.elem = impact::Element::Gold; impact::Strike(k); break;
		case 19: k.tier = fxgrade::Tier::T2; k.elem = impact::Element::Earth; impact::Strike(k); break;
		case 20: case 21: case 22: case 24:
			k.tier = skillId == 24 ? fxgrade::Tier::T3 : fxgrade::Tier::T2; k.elem = impact::Element::Gold; impact::Strike(k);
			if (skillId == 24 && (flags2 & DAMAGE2_PROC))
				Spawn(vfx::Kind::Slash, iso, z, 60.f, 0.2f, vfx::GoldRamp(), 0.04f, RandF(0.f, kPi));   // 금빛 잔향
			break;
		case 25: case 29: k.tier = fxgrade::Tier::T2; k.elem = impact::Element::Frost; impact::Shatter(k, 26.f); break;
		case 26: k.tier = fxgrade::Tier::T2; k.elem = impact::Element::Fire; impact::Strike(k); break;
		case 27: case 30: k.tier = fxgrade::Tier::T2; k.elem = impact::Element::Teal; impact::Strike(k); break;
		case 28: k.tier = fxgrade::Tier::T2; k.elem = impact::Element::Bolt; impact::Strike(k); break;
		case 31: k.tier = fxgrade::Tier::T1; k.elem = impact::Element::Moon; impact::Strike(k); break;
		case 32:   // 분신이 따라 벤다
			Spawn(vfx::Kind::Slash, iso, z, 64.f, 0.18f, vfx::InkRamp(), static_cast<float>(hitIdx & 1) * 0.05f, RandF(0.f, kPi));
			Spawn(vfx::Kind::GroundRing, iso, 0.f, 70.f, 0.3f, vfx::InkRamp(), 0.f, 0.f, 0.8f);
			for (int i = 0; i < 4; ++i) Smoke(iso, z, Rgb(0x2A2A34, 150), 14.f, 0.4f, 20.f);
			Light(iso, fxgrade::Tier::T2, kMoon, 0.7f);
			break;
		case 33: k.tier = fxgrade::Tier::T2; k.elem = impact::Element::Moon; impact::Strike(k); break;
		case 34: for (int i = 0; i < 3; ++i) Ember(iso, z, Rgb(0xB8FF7A), Rgb(0x2A8A20, 0), 10.f, 0.5f); break;
		case 35: k.tier = fxgrade::Tier::T2; k.elem = impact::Element::Ink; impact::InkBurst(k, 36.f); break;
		default: fxs.HitSpark(iso, height, Rgb(0xFFE0B0), crit, 1.f); break;
		}
		if (flags2 & DAMAGE2_WALL)
			for (int i = 0; i < 8; ++i) Smoke(iso, 8.f, Rgb(0x9A8A70, 170), 22.f, 0.8f, 20.f);
		if (comboId != 0)
			ComboFx(comboId, iso, height, crit);
	}

	void OnExHeld(const ExCtx& c, float held, uint8_t stage, bool perfectWindow, float dt)
	{
		fxscale::Scope scope(c.mine);
		const float ang = GroundAng(c.dir);
		const SkillDef* d = skillview::Def(c.skillId, c.flags);   // 재빠름: 모으기 단계가 짧다
		if (!d) return;
		const float full = d->kind == SkillKind::Charge ? static_cast<float>(SkillDefTable::ChargeMaxMs(*d) - d->perfectMs) / 1000.f : d->holdMaxMs / 1000.f;
		const float amount = full > 0.f ? util::Clamp01(held / full) : 1.f;
		// 차지: 단계에 닿을 때마다 발밑이 울리고, 완벽 구간이 열리면 번쩍
		if (d->kind == SkillKind::Charge)
		{
			if (Crossed(held, dt, d->stageMs / 1000.f) && stage <= d->stages)
			{
				const Color col = c.skillId == 19 ? kGold : (c.skillId == 25 ? kFrost : kMoon);
				fx::Fx().Ring(c.casterIso, 8.f, 40.f + stage * 14.f, 0.3f, util::WithA(col, 0.85f), 3.f);
				Glow(c.casterIso, 30.f, 20.f, 50.f + stage * 10.f, util::WithA(col, 0.7f), 0.25f);
				if (fxscale::Mine()) sfx::Play(sfx::Id::ChargeStage, 0.5f + 0.1f * stage, 0.f);
			}
			if (perfectWindow && Crossed(held, dt, static_cast<float>(d->stageMs * d->stages) / 1000.f))
			{
				Spawn(vfx::Kind::Flare, util::Add(c.casterIso, Vector2{ 0.f, -8.f }), 56.f, 90.f, 0.2f, RampOf(Rgb(0xFFFFFF), kGoldHot, kGold));
				if (fxscale::Mine()) sfx::Play(sfx::Id::ChargeStage, 0.9f, 0.f);
			}
			// 모이는 빛
			if (RandF() < dt * (18.f + 20.f * amount))
			{
				fx::Particle& p = P();
				const float a = RandF(0.f, kPi * 2.f), r = RandF(40.f, 70.f);
				p.pos = { c.casterIso.x + std::cos(a) * r, c.casterIso.y + std::sin(a) * r * 0.5f };
				p.z = RandF(10.f, 60.f);
				p.vel = { -std::cos(a) * r * 3.f, -std::sin(a) * r * 1.5f };
				p.life = p.maxLife = 0.3f;
				p.size0 = 6.f; p.size1 = 1.5f;
				const Color col = c.skillId == 19 ? kGoldHot : (c.skillId == 25 ? kFrost : kMoonHot);
				p.c0 = col; p.c1 = util::WithA(col, 0.f);
				p.shape = fx::Shape::Glow;
				p.additive = true;
			}
			if (c.skillId == 25 || c.skillId == 35)
				g_held.push_back(Held{ c.skillId, c.casterIso, c.dir, amount, perfectWindow, fxscale::Alpha() });
			if (c.skillId == 19 && RandF() < dt * 8.f)
				Shard(Along(c.casterIso, c.dirTile, RandF(0.2f, 0.8f)), 2.f, Rgb(0x9A8A70), RandF(20.f, 60.f), RandF(3.f, 5.f), 0.5f);
			return;
		}
		// 홀딩
		switch (c.skillId)
		{
		case 3:
			if (Crossed(held, dt, 0.14f))
				Swing(c.casterIso, ang + held * 9.f, 0.f, kPi * 2.f, RandF(56.f, 66.f), 30.f, RandF(0.05f, 0.3f), 0.14f, kGoldHot, kGold);
			if (RandF() < dt * 14.f)
				Smoke(OnEllipse(c.casterIso, RandF(30.f, 60.f), RandF(0.f, 6.28f)), 4.f, Rgb(0xB8A078, 120), 16.f, 0.5f, 18.f);
			break;
		case 29:
			for (int i = 0; i < 3; ++i)
				if (RandF() < dt * 40.f)
				{
					const float spread = RandF(-0.5f, 0.5f);
					const Vector2 dir = IsoDir(RotTile(c.dirTile, spread));
					fx::Particle& p = P();
					p.pos = util::Add(c.casterIso, Vector2{ dir.x * 14.f, dir.y * 14.f });
					p.z = RandF(30.f, 46.f);
					const float sp = RandF(260.f, 380.f);
					p.vel = { dir.x * sp, dir.y * sp };
					p.vz = RandF(-30.f, 10.f);
					p.drag = 1.2f;
					p.life = p.maxLife = RandF(0.45f, 0.65f);
					p.size0 = RandF(3.f, 6.f); p.size1 = 1.f;
					p.c0 = Rgb(0xF4FCFF, 230); p.c1 = util::WithA(kFrost, 0.f);
					p.shape = RandF() < 0.5f ? fx::Shape::Flake : fx::Shape::Glow;
					p.additive = true;
					p.rot = RandF(0.f, 6.f); p.vrot = RandF(-6.f, 6.f);
				}
			if (Crossed(held, dt, 0.12f))
			{
				vfx::Spec s = vfx::Make(vfx::Kind::Smoke, Along(c.casterIso, RotTile(c.dirTile, RandF(-0.4f, 0.4f)), RandF(1.2f, 3.6f)), 20.f, RandF(60.f, 90.f), 0.8f, vfx::FrostRamp());
				s.alpha = 0.6f;
				vfx::Spawn(s);
			}
			if (Crossed(held, dt, 0.3f))
				Spawn(vfx::Kind::Frost, Along(c.casterIso, RotTile(c.dirTile, RandF(-0.4f, 0.4f)), RandF(1.f, 3.8f)), 0.f, 70.f, 3.f, vfx::FrostRamp());
			if (fxscale::Mine() && Crossed(held, dt, 0.35f)) sfx::Play(sfx::Id::AmbWind, 0.35f, 0.1f);
			break;
		case 31:
			if (Crossed(held, dt, 0.055f))
			{
				const float a0 = RandF(0.f, kPi * 2.f);
				Swing(c.casterIso, a0, 0.f, RandF(1.6f, 2.6f), RandF(38.f, 64.f), RandF(14.f, 22.f), RandF(0.f, 0.7f), 0.1f, kMoonHot, kMoon, 0.f, RandF(22.f, 44.f));
			}
			if (Crossed(held, dt, 0.2f))
			{
				vfx::Spec s = vfx::Make(vfx::Kind::Smoke, OnEllipse(c.casterIso, RandF(10.f, 40.f), RandF(0.f, 6.28f)), 20.f, 50.f, 0.6f, vfx::InkRamp());
				s.alpha = 0.5f;
				vfx::Spawn(s);
			}
			if (fxscale::Mine() && Crossed(held, dt, 0.3f)) sfx::Play(sfx::Id::BladeHum, 0.4f, 0.1f);
			break;
		default:
			break;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// 매 프레임 · 그리기(kit — SkillFx가 부른다)
	// ─────────────────────────────────────────────────────────────────────────
	namespace kit
	{
		void ClearEx()
		{
			g_later.clear();
			for (auto& m : g_missiles) if (m.trail >= 0) trail::End(m.trail);
			g_missiles.clear();
			g_bells.clear();
			g_zones.clear();
			g_ground.clear();
			g_fallers.clear();
			g_skyRunes.clear();
			g_held.clear();
		}

		void UpdateEx(float dt)
		{
			g_t += dt;
			// 늦게 할 일(도중에 새 일이 붙을 수 있다 — 복사해서 돈다)
			if (!g_later.empty())
			{
				std::vector<Deferred> due;
				for (auto it = g_later.begin(); it != g_later.end();)
				{
					if (it->at <= g_t) { due.push_back(std::move(*it)); it = g_later.erase(it); }
					else ++it;
				}
				for (auto& d : due)
				{
					fxscale::Scope s(d.mine);
					if (d.stage) { fxscale::StageScope st; d.fn(); }
					else d.fn();
				}
			}
			// 투사체
			for (size_t i = 0; i < g_missiles.size(); ++i)
			{
				Missile& m = g_missiles[i];
				m.t += dt;
				const float u = util::Clamp01((m.t - m.delay) / m.dur);
				if (m.t < m.delay) continue;
				const Vector2 pos = util::LerpV(m.a, m.b, u);
				const float z = util::Lerp(m.za, m.zb, u) + std::sin(u * kPi) * m.arc;
				if (m.trail < 0)
				{
					fxscale::Scope s(m.mine);
					trail::FollowSpec f;
					f.width = m.kind == MKind::Spear ? 14.f * m.size : (m.kind == MKind::Shield ? 18.f : (m.kind == MKind::Dagger ? 6.f * m.size : 12.f * m.size));
					f.fade = m.kind == MKind::Dagger ? 0.16f : 0.24f;
					f.edge = m.c0; f.body = m.c1;
					m.trail = trail::Begin(f);
				}
				trail::Push(m.trail, pos, z);
				if (m.kind == MKind::Orb && RandF() < dt * 40.f)
					Glow(pos, z, 10.f * m.size, 2.f, util::WithA(m.c1, 0.8f), 0.2f);
				if (m.kind == MKind::Spear && RandF() < dt * 30.f)
					Shard(pos, z, Rgb(0xE8F8FF, 200), 30.f, 3.f, 0.4f);
			}
			for (size_t i = 0; i < g_missiles.size();)
			{
				Missile& m = g_missiles[i];
				if (m.t >= m.delay + m.dur)
				{
					if (m.trail >= 0) trail::End(m.trail);
					auto fn = std::move(m.onArrive);
					const bool mine = m.mine;
					g_missiles.erase(g_missiles.begin() + static_cast<long>(i));
					if (fn) { fxscale::Scope s(mine); fn(); }
					continue;
				}
				++i;
			}
			for (auto& b : g_bells) { b.t += dt; b.swing = std::max(0.f, b.swing - dt * 2.2f); }
			g_bells.erase(std::remove_if(g_bells.begin(), g_bells.end(), [](const Bell& b) { return b.t >= b.life; }), g_bells.end());
			for (auto& z : g_zones)
			{
				z.t += dt;
				z.acc += dt;
				fxscale::Scope s(z.mine);
				const float step = z.kind == ZKind::Poison ? 0.16f : 0.14f;
				while (z.acc >= step && z.t < z.life - 0.4f)
				{
					z.acc -= step;
					const Vector2 p = OnEllipse(z.iso, z.r * std::sqrt(RandF()), RandF(0.f, 6.28f));
					if (z.kind == ZKind::Poison)
					{
						vfx::Spec sp = vfx::Make(vfx::Kind::Smoke, p, 18.f, RandF(60.f, 100.f), RandF(1.0f, 1.4f), vfx::PoisonRamp());
						sp.alpha = 0.55f;
						vfx::Spawn(sp);
						Ember(p, 4.f, Rgb(0xC8FF8A), Rgb(0x2A6A18, 0), 8.f, RandF(0.5f, 0.9f));
					}
					else if (z.kind == ZKind::Fire)
					{
						vfx::Spec sp = vfx::Make(vfx::Kind::Fire, p, 0.f, RandF(34.f, 56.f), RandF(0.6f, 0.9f), vfx::FireRamp());
						sp.h = sp.w * 1.5f;
						vfx::Spawn(sp);
						if (RandF() < 0.5f) Ember(p, 10.f, Rgb(0xFFD060), Rgb(0xFF3A10, 0), 10.f, 0.6f);
					}
					else
					{
						// 서리 · 먹물 · 빛: 떠오르는 반짝임 + 옅은 안개
						Ember(p, 4.f, util::Mix(z.col, WHITE, 0.5f), util::WithA(z.col, 0.f), 18.f, RandF(0.6f, 1.0f));
						if (RandF() < 0.35f)
						{
							vfx::Spec sp = vfx::Make(vfx::Kind::Smoke, p, 10.f, RandF(50.f, 80.f), RandF(0.8f, 1.2f), RampOf(util::Mix(z.col, WHITE, 0.4f), z.col, util::Shade(z.col, 0.4f)));
							sp.alpha = 0.35f;
							vfx::Spawn(sp);
						}
					}
				}
				// 갈래 「장판」: 서버 틱(0.5초)마다 테가 한 번 울린다
				if (z.linger)
				{
					z.pulse += dt;
					if (z.pulse >= 0.5f && z.t < z.life - 0.2f)
					{
						z.pulse -= 0.5f;
						Spawn(vfx::Kind::GroundRing, z.iso, 0.f, z.r * 2.1f, 0.35f, RampOf(util::Mix(z.col, WHITE, 0.5f), z.col, util::Shade(z.col, 0.35f)), 0.f, 0.f, 0.7f);
					}
				}
			}
			g_zones.erase(std::remove_if(g_zones.begin(), g_zones.end(), [](const Zone& z) { return z.t >= z.life; }), g_zones.end());
			for (auto& g : g_ground) g.t += dt;
			g_ground.erase(std::remove_if(g_ground.begin(), g_ground.end(), [](const GroundFx& g) { return g.t >= g.life; }), g_ground.end());
			for (auto& f : g_fallers)
			{
				f.t += dt;
				const float u = util::Clamp01(f.t / f.dur);
				Ember(f.iso, f.h0 * (1.f - u) + 30.f, kGoldHot, Rgb(0xE08A30, 0), 10.f, 0.3f);
			}
			g_fallers.erase(std::remove_if(g_fallers.begin(), g_fallers.end(), [](const Faller& f) { return f.t >= f.dur + 0.12f; }), g_fallers.end());
			for (auto& r : g_skyRunes) r.t += dt;
			g_skyRunes.erase(std::remove_if(g_skyRunes.begin(), g_skyRunes.end(), [](const SkyRune& r) { return r.t >= r.life; }), g_skyRunes.end());
		}

		void DrawExGround()
		{
			for (const auto& g : g_ground)
			{
				const float u = util::Clamp01(g.t / g.life);
				const float fade = (u < 0.15f ? u / 0.15f : 1.f) * (1.f - util::EaseInCubic(u)) * g.alpha;
				gfx::SetDepthRaw(-0.964f);
				gfx::Additive(true);
				switch (g.kind)
				{
				case GKind::Sector:
				{
					// 부채꼴(바닥 평면 각 → 화면 타원)
					constexpr int n = 14;
					Vector2 pts[n + 2];
					pts[0] = g.a;
					for (int i = 0; i <= n; ++i)
					{
						const float a = g.ang - g.half + (2.f * g.half) * i / n;
						pts[i + 1] = OnEllipse(g.a, g.r * (0.6f + 0.4f * util::EaseOutCubic(u * 3.f)), a);
					}
					gfx::PolyGrad(pts, n + 2, g.a, util::MulA(g.col, 0.05f * fade), util::MulA(g.col, 0.55f * fade));
					break;
				}
				case GKind::GlowLine:
				{
					gfx::LineGrad(g.a, g.b, 9.f * fade, 9.f * fade, util::MulA(g.col, 0.35f * fade), util::MulA(g.col, 0.5f * fade));
					gfx::LineGrad(g.a, g.b, 3.f, 3.f, util::MulA(Rgb(0xFFFFFF), 0.7f * fade), util::MulA(g.col, 0.9f * fade));
					break;
				}
				case GKind::RingSpin:
				{
					const float spin = g.t * 2.4f;
					for (int i = 0; i < 6; ++i)
					{
						const float a0 = spin + i * kPi / 3.f;
						gfx::EllipseArc(g.a, g.r, g.r * 0.5f, a0, a0 + 0.55f, 3.5f, util::MulA(g.col, 0.8f * fade));
					}
					gfx::EllipseArc(g.a, g.r * 0.92f, g.r * 0.46f, 0.f, kPi * 2.f, 1.5f, util::MulA(g.col, 0.35f * fade));
					break;
				}
				}
				gfx::Additive(false);
			}
		}

		void DrawExWorld()
		{
			gfx::Additive(true);
			// 투사체 머리
			for (const auto& m : g_missiles)
			{
				if (m.t < m.delay) continue;
				const float u = util::Clamp01((m.t - m.delay) / m.dur);
				const Vector2 pos = util::LerpV(m.a, m.b, u);
				const float z = util::Lerp(m.za, m.zb, u) + std::sin(u * kPi) * m.arc;
				const Vector2 c{ pos.x, pos.y - z };
				const Vector2 dir = util::Norm(util::Sub(Vector2{ m.b.x, m.b.y - m.zb }, Vector2{ m.a.x, m.a.y - m.za }));
				gfx::SetDepth(pos.y / gfx::kHalfH + 0.6f);
				const Color c0 = util::MulA(m.c0, m.alpha), c1 = util::MulA(m.c1, m.alpha);
				switch (m.kind)
				{
				case MKind::Orb:
					gfx::CircleGrad(c, 16.f * m.size, util::MulA(c1, 0.8f), util::WithA(c1, 0.f));
					gfx::Star(c, 2.f, 9.f * m.size, 4, g_t * 9.f, c0);
					gfx::Circle(c, 4.f * m.size, c0);
					break;
				case MKind::Spear:
				{
					const float len = 40.f * m.size, w = 6.f * m.size;
					const Vector2 n{ -dir.y, dir.x };
					const Vector2 tip = util::Add(c, util::Mul(dir, len * 0.6f)), tail = util::Sub(c, util::Mul(dir, len * 0.4f));
					const Vector2 l = util::Add(c, util::Mul(n, w)), r = util::Sub(c, util::Mul(n, w));
					gfx::Tri3(tip, l, r, c0, util::MulA(c1, 0.8f), util::MulA(c1, 0.8f));
					gfx::Tri3(tail, r, l, util::MulA(c1, 0.3f), util::MulA(c1, 0.8f), util::MulA(c1, 0.8f));
					gfx::CircleGrad(c, 18.f * m.size, util::MulA(c1, 0.35f), util::WithA(c1, 0.f));
					break;
				}
				case MKind::Dagger:
				{
					const float len = 16.f * m.size;
					gfx::LineGrad(util::Sub(c, util::Mul(dir, len)), c, 1.f, 4.f * m.size, util::WithA(c1, 0.f), c0);
					gfx::Circle(c, 2.2f * m.size, c0);
					break;
				}
				case MKind::Shield:
				{
					// 도는 방패: 금빛 원판 + 네 갈래 문장
					const float spin = g_t * 22.f;
					gfx::CircleGrad(c, 22.f, util::MulA(c1, 0.5f), util::WithA(c1, 0.f));
					gfx::Ellipse(c, 12.f, 12.f * (0.55f + 0.45f * std::fabs(std::cos(spin))), util::MulA(c1, 0.85f));
					gfx::Star(c, 3.f, 10.f, 4, spin, c0);
					break;
				}
				case MKind::Vial:
					gfx::CircleGrad(c, 10.f, util::MulA(c1, 0.8f), util::WithA(c1, 0.f));
					gfx::Circle(c, 3.5f, c0);
					break;
				}
			}
			// 모으는 창 · 칼날 빛
			for (const auto& h : g_held)
			{
				const Vector2 hand{ h.iso.x + h.dir.x * 22.f, h.iso.y + h.dir.y * 22.f - 46.f };
				gfx::SetDepth(h.iso.y / gfx::kHalfH + 0.62f);
				if (h.skill == 25)
				{
					const float len = 16.f + 46.f * h.amount, w = 3.f + 4.f * h.amount;
					const Vector2 n{ -h.dir.y, h.dir.x };
					const Vector2 tip = util::Add(hand, util::Mul(h.dir, len * 0.7f)), tail = util::Sub(hand, util::Mul(h.dir, len * 0.3f));
					const Color c0 = util::MulA(Rgb(0xF4FCFF), h.alpha), c1 = util::MulA(kFrost, h.alpha * 0.8f);
					gfx::Tri3(tip, util::Add(hand, util::Mul(n, w)), util::Sub(hand, util::Mul(n, w)), c0, c1, c1);
					gfx::Tri3(tail, util::Sub(hand, util::Mul(n, w)), util::Add(hand, util::Mul(n, w)), util::MulA(c1, 0.3f), c1, c1);
					gfx::CircleGrad(hand, 20.f + 20.f * h.amount, util::MulA(kFrost, 0.3f * h.alpha), util::WithA(kFrost, 0.f));
				}
				else
				{
					const float pulse = 0.6f + 0.4f * std::sin(g_t * 20.f);
					gfx::CircleGrad(hand, 10.f + 18.f * h.amount * pulse, util::MulA(kMoonHot, (0.4f + 0.4f * h.amount) * h.alpha), util::WithA(kMoon, 0.f));
					if (h.perfect) gfx::Star(hand, 2.f, 16.f, 4, g_t * 3.f, util::MulA(kMoonHot, h.alpha));
				}
			}
			g_held.clear();
			// 떨어지는 금빛 칼날(단죄)
			for (const auto& f : g_fallers)
			{
				const float u = util::Clamp01(f.t / f.dur);
				const float z = f.h0 * (1.f - util::EaseInCubic(u));
				gfx::SetDepth(f.iso.y / gfx::kHalfH + 0.7f);
				const Vector2 tip{ f.iso.x, f.iso.y - z };
				const float len = 220.f, w = 26.f;
				const Color edge = util::MulA(kGoldHot, f.alpha), body = util::MulA(kGold, 0.85f * f.alpha);
				gfx::Tri3(tip, Vector2{ tip.x - w, tip.y - 40.f }, Vector2{ tip.x + w, tip.y - 40.f }, edge, body, body);
				gfx::Quad4(Vector2{ tip.x - w, tip.y - 40.f }, Vector2{ tip.x + w, tip.y - 40.f }, Vector2{ tip.x + w * 0.6f, tip.y - len }, Vector2{ tip.x - w * 0.6f, tip.y - len },
					body, body, util::WithA(kGold, 0.f), util::WithA(kGold, 0.f));
				gfx::CircleGrad(tip, 40.f, util::MulA(kGoldHot, 0.5f * f.alpha), util::WithA(kGold, 0.f));
			}
			// 하늘 각인(낙성)
			for (const auto& r : g_skyRunes)
			{
				const float u = util::Clamp01(r.t / r.draw);
				const float fade = r.alpha * (r.t > r.life - 0.25f ? (r.life - r.t) / 0.25f : 1.f);
				const Vector2 c{ r.iso.x, r.iso.y - r.z };
				gfx::SetDepth(r.iso.y / gfx::kHalfH + 0.8f);
				const Color col = util::Mix(kTeal, kEmber, util::Clamp01(r.t / r.life));
				const float a1 = kPi * 2.f * util::EaseOutCubic(u);
				gfx::EllipseArc(c, r.r, r.r * 0.5f, -kPi * 0.5f, -kPi * 0.5f + a1, 3.f, util::MulA(col, 0.9f * fade));
				gfx::EllipseArc(c, r.r * 0.8f, r.r * 0.4f, kPi * 0.5f, kPi * 0.5f + a1, 2.f, util::MulA(col, 0.7f * fade));
				const int ticks = static_cast<int>(12 * u);
				for (int i = 0; i < ticks; ++i)
				{
					const float a = i * kPi / 6.f + r.t * 0.8f;
					const Vector2 p0 = OnEllipse(c, r.r * 0.8f, a), p1 = OnEllipse(c, r.r, a + 0.12f);
					gfx::Line(p0, p1, 2.f, util::MulA(col, 0.8f * fade));
				}
				gfx::EllipseGrad(c, r.r * 0.7f, r.r * 0.35f, util::MulA(col, 0.25f * fade * u), util::WithA(col, 0.f));
			}
			// 종(심판의 종): 반투명 금빛 종이 내려앉아 흔들린다
			for (const auto& b : g_bells)
			{
				const float drop = util::EaseOutCubic(util::Clamp01(b.t / b.drop));
				const float fade = b.alpha * util::Clamp01(b.t / 0.2f) * (b.t > b.life - 0.4f ? (b.life - b.t) / 0.4f : 1.f);
				const float z = util::Lerp(240.f, 70.f, drop);
				const float swing = std::sin(b.t * 18.f) * 0.12f * b.swing;
				const Vector2 top{ b.iso.x + std::sin(swing) * 20.f, b.iso.y - z - 70.f };
				const Vector2 mouth{ b.iso.x - std::sin(swing) * 10.f, b.iso.y - z + 40.f };
				gfx::SetDepth(b.iso.y / gfx::kHalfH + 0.8f);
				const Color body = util::MulA(Rgb(0xF2C47C), 0.28f * fade), rim = util::MulA(Rgb(0xFFF0C8), 0.85f * fade);
				// 종 몸통(사다리꼴 + 둥근 머리)
				const Vector2 tl{ top.x - 34.f, top.y + 16.f }, tr{ top.x + 34.f, top.y + 16.f };
				const Vector2 bl{ mouth.x - 66.f, mouth.y }, br{ mouth.x + 66.f, mouth.y };
				gfx::Quad4(tl, tr, br, bl, body, body, util::MulA(body, 1.6f), util::MulA(body, 1.6f));
				gfx::Ellipse(Vector2{ top.x, top.y + 16.f }, 34.f, 22.f, body);
				gfx::Line(tl, bl, 3.f, rim);
				gfx::Line(tr, br, 3.f, rim);
				gfx::EllipseArc(mouth, 66.f, 16.f, 0.f, kPi, 4.f, rim);
				gfx::EllipseArc(Vector2{ top.x, top.y + 16.f }, 34.f, 22.f, kPi, kPi * 2.f, 2.f, rim);
				gfx::EllipseArc(Vector2{ (top.x + mouth.x) * 0.5f, (top.y + mouth.y) * 0.5f + 12.f }, 52.f, 12.f, 0.f, kPi, 2.f, util::MulA(rim, 0.6f));
				if (b.swing > 0.f)
					gfx::CircleGrad(mouth, 90.f * b.swing, util::MulA(kGoldHot, 0.35f * b.swing * fade), util::WithA(kGold, 0.f));
			}
			gfx::Additive(false);
		}
	}
}
