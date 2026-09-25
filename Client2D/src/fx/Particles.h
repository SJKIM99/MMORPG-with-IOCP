#pragma once

#include "raylib.h"
#include <cstdint>

#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 파티클. 위치는 아이소 px의 "바닥 평면" 좌표(pos) + 높이(z, 화면 위쪽 px)로 나눠
// 들고 있어서, 튀어 오르는 파편이 땅에 떨어져 튕기거나 그림자를 남길 수 있다.
// 깊이는 바닥 위치로 정한다(pos.y / 16 = X + Y).
// ─────────────────────────────────────────────────────────────────────────────
namespace fx
{
	enum class Shape : uint8_t { Glow, Soft, Smoke, Spark, Star, Ring, Chunk, Leaf, Flake, Coin, Tex };

	struct Particle
	{
		Vector2 pos{};        // 바닥 평면(아이소 px)
		float   z = 0.f;      // 높이(px, 위로 +)
		Vector2 vel{};
		float   vz = 0.f;
		float   gravity = 0.f;      // z 가속(px/s², 아래로 +)
		float   drag = 0.f;         // 초당 감속 비율
		float   life = 1.f, maxLife = 1.f;
		float   size0 = 8.f, size1 = 0.f;
		Color   c0 = WHITE, c1 = WHITE;
		float   rot = 0.f, vrot = 0.f;
		Shape   shape = Shape::Soft;
		bool    additive = false;
		bool    bounce = false;     // 땅(z=0)에서 튕김
		bool    stretch = false;    // 속도 방향으로 늘여 그림(불꽃)
		float   wobble = 0.f;       // 좌우 흔들림(잎/눈)
		float   seed = 0.f;
		// Shape::Tex(그린 효과 — render/FxArt)
		const Texture2D* tex = nullptr;
		float   aspect = 1.f;       // 가로 / 세로(바닥에 눕히면 세로를 0.5배)
		float   squash = 1.f;       // 세로 배율(바닥 문양 = 0.5)
		bool    flipX = false;
		bool    ground = false;     // 바닥 문양: 캐릭터 발밑(깊이 맨 뒤)에 그린다
		float   pop = 0.f;          // 처음 pop초 동안 0 → 1로 튀어나오듯 커진다(EaseOutBack)
	};

	class ParticleSystem
	{
	public:
		Particle& Emit();
		void Update(float dt);
		// 월드 패스에서 호출(깊이 테스트 켠 상태, 깊이 쓰기 끔)
		void Draw();
		void Clear() { m_parts.clear(); }
		[[nodiscard]] size_t Count() const { return m_parts.size(); }

	private:
		std::vector<Particle> m_parts;
	};

	ParticleSystem& Particles();

	// 그린 효과 한 장(fxId = fxart::Id). 그림이 없으면 nullptr — 부르는 쪽은 절차적 효과만으로 충분하다.
	Particle* EmitTex(int fxId, Vector2 iso, float z, float size0, float size1, float life, Color c0, Color c1, bool additive);
}
