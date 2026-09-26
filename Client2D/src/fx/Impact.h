#pragma once

#include "raylib.h"
#include "fx/FxGrade.h"
#include "fx/Vfx.h"

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 타격 조립 도구(Phase 2-A) — 등급표(fx/FxGrade) 예산 안에서 "한 타격 = 5겹 이상"을 한 줄로.
//
//   겹: ① 중심 섬광(셰이더 Flare) ② 충격 고리(바닥 고리 · T3부터 화면 충격파) ③ 튀는 불꽃 · 파편(입자)
//       ④ 연기 · 먼지 ⑤ 순간 조명 (+ T2부터 바닥 자국, T3부터 줌 펀치 · 열 아지랑이)
//   원소(Element)가 색 램프와 자국 · 파편 모양을 고른다. 세기는 fxscale(남의 기술 50% 등)을 따른다.
//
// 기술 연출(fx/SkillFx)은 "예고 → 발동 → 타격 → 여운" 4박자를 이 도구들로 짠다.
// ─────────────────────────────────────────────────────────────────────────────
namespace impact
{
	enum class Element : uint8_t { Steel, Gold, Fire, Frost, Bolt, Teal, Ink, Moon, Poison, Earth, Ember };

	struct Ctx
	{
		Vector2 iso{};                 // 바닥 위치
		float   z = 30.f;              // 타격 높이(px)
		Vector2 dir{ 1.f, 0.f };       // 화면 방향(시전자 → 대상), 정규화
		fxgrade::Tier tier = fxgrade::Tier::T2;
		Element elem = Element::Steel;
		float   scale = 1.f;           // 크기 배율(기술마다)
		int     targets = 1;           // 한 번에 맞힌 수(많이 맞힐수록 보상)
		bool    crit = false;
		bool    heavy = false;         // 강타(파편 · 먼지 더)
	};

	[[nodiscard]] vfx::Ramp RampOf(Element e);
	[[nodiscard]] Color LightOf(Element e);

	// 한 타격(5겹) — 대상 몸에서
	void Strike(const Ctx& c);
	// 폭발(운석 · 증발 폭발 · 불꽃 각인) — radius = 바닥 가로 반지름(px)
	void Explosion(const Ctx& c, float radius);
	// 내려찍기 · 땅 가르기 — 균열 · 흙먼지 · 튀는 돌
	void Slam(const Ctx& c, float radius);
	// 얼음 폭발(빙결 · 산산조각)
	void Shatter(const Ctx& c, float radius);
	// 먹물 폭발(밤사냥꾼)
	void InkBurst(const Ctx& c, float radius);
	// 하늘에서 벼락(아랫변이 대상 발)
	void LightningStrike(const Ctx& c, float height);
	// 빛기둥(각성기 끝 · 대지 가르기 끝)
	void Pillar(const Ctx& c, float height);
	// 칼 궤적 섬광(Slash 셰이더) — dir 방향으로
	void SlashFlash(const Ctx& c, float size);

	// 등급 예산의 체감(히트스톱 · 흔들림 · 줌 펀치) — 치명 · 연계 · 처치 · 많이 맞힘을 반영(상한 = 등급 최대치)
	struct Feel { float hitstop = 0.f, trauma = 0.f, kick = 0.f; };
	[[nodiscard]] Feel FeelOf(fxgrade::Tier t, bool crit, bool combo, bool killed, int targets);

	// 원소 · 등급에 맞는 타격음 겹(예산의 soundLayers까지). skipBody = 몸통 겹은 빼고(부른 쪽이 기술 고유 소리를 냈다)
	void HitSound(Element e, fxgrade::Tier t, float volume, float pan, int targets, bool skipBody = false);
	// 기술의 원소(타격음 · 색) — 기술 id → 원소
	[[nodiscard]] Element ElementOfSkill(uint16_t skillId);
}
