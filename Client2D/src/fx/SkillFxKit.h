#pragma once

#include "raylib.h"
#include "fx/Particles.h"
#include "render/FxArt.h"

// ─────────────────────────────────────────────────────────────────────────────
// 기술 연출 공용 도구 — fx/SkillFx.cpp(예전 18기술)와 fx/SkillFxEx.cpp(기술 구조 V3)가 함께 쓰는 입자 조각.
// 모두 fx::Particles 한 풀에 넣는다(한 번에 그리기 · 상한은 풀이 지킨다).
// ─────────────────────────────────────────────────────────────────────────────
namespace skillfx::kit
{
	[[nodiscard]] fx::Particle& P();
	// 그린 효과(fxart) — 그림이 없으면 nullptr(절차적 효과는 그대로)
	fx::Particle* Tx(fxart::Id id, Vector2 iso, float z, float s0, float s1, float life, Color c0, Color c1, bool additive,
		float rot = 0.f, float vrot = 0.f, float pop = 0.f);
	fx::Particle* TxGround(fxart::Id id, Vector2 iso, float s0, float s1, float life, Color c0, Color c1, bool additive, float rot = 0.f, float vrot = 0.f);
	// 튀는 불꽃(늘어나는 선) · 빛 덩이 · 연기 · 튀는 조각(바닥에 튕김) · 떠오르는 불씨
	void Spark(Vector2 iso, float z, float speed, float ang, Color c0, Color c1, float life, float size);
	void Glow(Vector2 iso, float z, float s0, float s1, Color c, float life);
	void Smoke(Vector2 iso, float z, Color c, float size, float life, float rise);
	void Shard(Vector2 iso, float z, Color c, float speed, float size, float life);
	void Ember(Vector2 iso, float z, Color c0, Color c1, float spread, float life);
	// 아이소 바닥 위 원(가로 반지름 r)
	void GroundRing(Vector2 c, float r, float th, Color col);

	// SkillFxEx(기술 구조 V3)의 매 프레임 · 그리기 — SkillFx의 Update · DrawGround · DrawWorld · Clear가 부른다
	void UpdateEx(float dt);
	void DrawExGround();
	void DrawExWorld();
	void ClearEx();
}
