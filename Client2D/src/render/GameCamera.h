#pragma once

#include "raylib.h"
#include "core/Util.h"

// ─────────────────────────────────────────────────────────────────────────────
// 카메라: 부드러운 추적 + 트라우마 기반 화면 흔들림 + 줌 펀치.
//   트라우마(0~1)를 쌓으면 흔들림 세기는 트라우마²에 비례하고 시간이 지나며 줄어든다
//   (작은 타격은 살짝, 큰 타격은 확실히 — 여러 번 맞으면 누적).
// ─────────────────────────────────────────────────────────────────────────────
class GameCamera
{
public:
	void SnapTo(Vector2 isoTarget) { m_target = isoTarget; m_follow = isoTarget; }
	void Follow(Vector2 isoTarget) { m_follow = isoTarget; }
	void SetLead(Vector2 lead) { m_leadGoal = lead; }

	void AddTrauma(float amount) { m_trauma = std::min(1.f, m_trauma + amount * m_shakeScale); }
	// 타격 방향으로 화면을 살짝 밀었다가 되돌린다(px, 아이소 공간). 흔들림보다 멀미가 적다.
	void Nudge(Vector2 dir, float px)
	{
		const float l = util::Len(dir);
		if (l < 1e-4f) return;
		const float k = std::min(px, 10.f) * m_shakeScale / l;
		m_nudge = util::Add(m_nudge, Vector2{ dir.x * k, dir.y * k });
		const float nl = util::Len(m_nudge);
		if (nl > 12.f) m_nudge = util::Mul(m_nudge, 12.f / nl);
	}
	// 흔들림 세기(설정: 0 끔 ~ 1 기본)
	void SetShakeScale(float s) { m_shakeScale = std::clamp(s, 0.f, 1.f); }
	[[nodiscard]] float ShakeScale() const { return m_shakeScale; }
	void Kick(float zoomPunch) { m_kick = std::max(m_kick, zoomPunch); }
	void AdjustZoom(float delta) { m_zoomGoal = std::clamp(m_zoomGoal + delta, kMinZoom, kMaxZoom); }
	void SetZoom(float z) { m_zoomGoal = std::clamp(z, kMinZoom, kMaxZoom); m_zoom = m_zoomGoal; }

	void Update(float dt)
	{
		m_time += dt;
		m_lead = util::DampV(m_lead, m_leadGoal, 3.f, dt);
		m_target = util::DampV(m_target, util::Add(m_follow, m_lead), 9.f, dt);
		m_zoom = util::Damp(m_zoom, m_zoomGoal, 10.f, dt);
		m_trauma = std::max(0.f, m_trauma - dt * 1.6f);
		m_kick = std::max(0.f, m_kick - dt * 0.35f);
		m_kickAnim = util::Damp(m_kickAnim, m_kick, 30.f, dt);
		m_nudge = util::DampV(m_nudge, Vector2{ 0, 0 }, 14.f, dt);
	}

	Camera2D Get(int screenW, int screenH) const
	{
		const float shake = m_trauma * m_trauma;
		const float sx = util::Noise1(m_time * 23.f) * 14.f * shake;
		const float sy = util::Noise1(m_time * 23.f + 51.f) * 10.f * shake;
		const float rot = util::Noise1(m_time * 17.f + 97.f) * 1.4f * shake;
		Camera2D cam{};
		cam.offset = Vector2{ screenW * 0.5f + sx - m_nudge.x * m_zoom, screenH * 0.5f + sy - m_nudge.y * m_zoom };
		cam.target = m_target;
		cam.rotation = rot;
		cam.zoom = m_zoom * (1.f + m_kickAnim);
		return cam;
	}

	[[nodiscard]] Vector2 Target() const { return m_target; }
	[[nodiscard]] float Zoom() const { return m_zoom; }
	[[nodiscard]] float Trauma() const { return m_trauma; }

	static constexpr float kMinZoom = 0.9f;
	static constexpr float kMaxZoom = 2.2f;

private:
	Vector2 m_target{};
	Vector2 m_follow{};
	Vector2 m_lead{};
	Vector2 m_leadGoal{};
	float m_zoom = 1.45f;
	float m_zoomGoal = 1.45f;
	float m_trauma = 0.f;
	float m_kick = 0.f;
	float m_kickAnim = 0.f;
	float m_time = 0.f;
	Vector2 m_nudge{};
	float m_shakeScale = 1.f;
};
