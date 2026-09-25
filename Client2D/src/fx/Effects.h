#pragma once

#include "ui/Font.h"

#include "raylib.h"
#include "net/Shared.h"

#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// "손맛" 이펙트 모음 — 데미지 숫자, 충격파, 빛기둥, 보스 경고 장판, 칼 궤적,
// 화면 번쩍임, 히트스톱, 그리고 파티클 연출(피격/사망/레벨업/순간이동...).
// 서버는 "무슨 일이 일어났다"만 알려 주고(EFFECT_NFY 등) 모양/소리/흔들림은
// 전부 여기서 정한다.
// ─────────────────────────────────────────────────────────────────────────────
namespace fx
{
	enum class DamageStyle { Normal, Crit, Skill, Taken, TakenCrit, Heal, Other, Exp, DotBurn, DotPoison, Guarded };

	class Effects
	{
	public:
		void Update(float dt);
		void Clear();

		// 그리기 단계
		void DrawGround();                          // 바닥 데칼(깊이 테스트 O, 쓰기 X)
		void DrawWorld();                           // 월드 반투명(빛기둥/칼 궤적)
		void DrawOverlay(const Camera2D& cam);      // 화면 공간(데미지 숫자/떠오르는 글자)
		void DrawScreen(int w, int h);              // 화면 전체(번쩍임)

		// ── 기본 요소 ──
		// stackKey != 0: 같은 열쇠의 숫자가 막 떠 있으면 새로 띄우지 않고 합쳐서 키운다
		// (연타·다단 히트가 숫자 비로 번지지 않고 "쌓이는" 맛이 나게).
		void Damage(Vector2 iso, float height, int amount, DamageStyle style, uint64_t stackKey = 0);
		// 떠오르는 글자(상태 · 알림). face: 기본 Strong, 영문 연출은 Latin.
		void FloatText(Vector2 iso, float height, const std::string& text, Color color, float size, float life = 1.4f, ui::Face face = ui::Face::Strong);
		void Ring(Vector2 iso, float r0, float r1, float life, Color color, float thickness);
		void Beam(Vector2 iso, float width, float height, float life, Color color);
		void Telegraph(Vector2 iso, int radiusTiles, float duration, Color color);
		void Slash(Vector2 iso, float height, float dirSign, bool back, Color color, float scale);
		void Flash(Color color, float strength, float duration);
		void HitStop(float seconds);
		[[nodiscard]] float TimeScale() const { return m_hitStop > 0.f ? 0.06f : 1.f; }

		// ── 연출 묶음(파티클 포함) ──
		void HitSpark(Vector2 iso, float height, Color color, bool crit, float dirSign);
		void DeathBurst(Vector2 iso, float height, Color c1, Color c2, float scale, bool boss);
		void LevelUp(Vector2 iso);
		void Heal(Vector2 iso, float height);
		void TeleportOut(Vector2 iso);
		void TeleportIn(Vector2 iso);
		void Haste(Vector2 iso);
		void HasteTrail(Vector2 iso);
		void EnchantResult(Vector2 iso, bool success);
		void SkillCross(Vector2 iso);
		void BossSlamImpact(Vector2 iso, int radiusTiles);
		void BossSpawn(Vector2 iso);
		void PlayerKill(Vector2 iso);
		void Restart(Vector2 iso);
		void Dust(Vector2 iso, float amount);
		void CoinBurst(Vector2 iso, int count);
		void LootSparkle(Vector2 iso, Color color);
		void ClickMarker(Vector2 iso, Color color);

		// 지역 분위기 파티클(눈/재/반딧불...) — 매 프레임 카메라 주변에 조금씩
		void Ambient(WorldData::Biome biome, Vector2 isoCenter, float viewW, float viewH, float dt, float night, bool inTown);

	private:
		struct DamageNum { Vector2 iso; float h; std::string text; DamageStyle style; float t, life; float vx; uint64_t key = 0; int total = 0; int hits = 1; float punch = 0.f; };
		struct Floating { Vector2 iso; float h; std::string text; Color color; float size, t, life; ui::Face face = ui::Face::Strong; };
		struct RingFx { Vector2 iso; float r0, r1, t, life, thick; Color color; };
		struct BeamFx { Vector2 iso; float w, h, t, life; Color color; };
		struct TeleFx { Vector2 iso; int radius; float t, life; Color color; };
		struct SlashFx { Vector2 iso; float h, dir; bool back; float t, life, scale; Color color; };
		struct ClickFx { Vector2 iso; float t; Color color; };

		std::vector<DamageNum> m_damage;
		std::vector<Floating> m_floats;
		std::vector<RingFx> m_rings;
		std::vector<BeamFx> m_beams;
		std::vector<TeleFx> m_teles;
		std::vector<SlashFx> m_slashes;
		std::vector<ClickFx> m_clicks;
		Color m_flashColor{ 255, 255, 255, 0 };
		float m_flash = 0.f, m_flashLife = 0.f;
		float m_hitStop = 0.f;
		float m_time = 0.f;
		float m_ambientAcc = 0.f;
	};

	Effects& Fx();
}
