#pragma once

#include "raylib.h"
#include "core/Util.h"

// ─────────────────────────────────────────────────────────────────────────────
// 카메라: 임계 감쇠 스프링 추적(Phase 1-C) + 트라우마 기반 화면 흔들림 + 줌 펀치.
//   · 추적: 목표(캐릭터 + 진행 방향 앞서기 + 커서 쪽 치우침)를 시간 상수 kTau의 임계 감쇠 스프링으로
//     따라간다 — 1차 감쇠처럼 "끌려가지" 않고, 넘치지도 않는다. 닫힌 식이라 프레임이 길어도 안정하다.
//   · 픽셀 맞춤: 월드는 실수 좌표로 그리고 마지막 화면 이동량만 정수 픽셀에 맞춘다(서 있는 것들이 떨지 않는다).
//   트라우마(0~1)를 쌓으면 흔들림 세기는 트라우마²에 비례하고 시간이 지나며 줄어든다
//   (작은 타격은 살짝, 큰 타격은 확실히 — 여러 번 맞으면 누적).
// ─────────────────────────────────────────────────────────────────────────────
class GameCamera
{
public:
	void SnapTo(Vector2 isoTarget) { m_target = isoTarget; m_follow = isoTarget; m_vel = Vector2{ 0, 0 }; m_lead = m_leadGoal = m_bias = m_biasGoal = Vector2{ 0, 0 }; }
	void Follow(Vector2 isoTarget) { m_follow = isoTarget; }
	void SetLead(Vector2 lead) { m_leadGoal = lead; }
	void SetCursorBias(Vector2 bias) { m_biasGoal = bias; }   // 커서 쪽 치우침(px, 아이소)
	void SetPixelSnap(bool on) { m_pixelSnap = on; }

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
	// 카메라 투어(Phase 4 --script tour): 평소 범위(0.9~2.2)를 벗어난 전경 줌도 허용 — 부드럽게 따라간다
	void SetZoomFree(float z) { m_zoomGoal = std::clamp(z, 0.3f, 3.f); }

	void Update(float dt)
	{
		m_time += dt;
		m_lead = util::DampV(m_lead, m_leadGoal, 4.f, dt);
		m_bias = util::DampV(m_bias, m_biasGoal, 3.f, dt);
		// 임계 감쇠 스프링(축마다 닫힌 식): x' = g + (e + (v + ωe)·dt)·e^(−ωdt), v' = (v − ω(v + ωe)·dt)·e^(−ωdt)
		const Vector2 goal = util::Add(util::Add(m_follow, m_lead), m_bias);
		const float w = 1.f / kTau, ex = std::exp(-w * dt);
		auto axis = [&](float& x, float& v, float g)
		{
			const float e = x - g;
			const float tmp = (v + w * e) * dt;
			x = g + (e + tmp) * ex;
			v = (v - w * tmp) * ex;
		};
		axis(m_target.x, m_vel.x, goal.x);
		axis(m_target.y, m_vel.y, goal.y);
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
		if (m_pixelSnap && rot == 0.f)
		{
			// 화면 이동량(offset − target·zoom)만 정수 픽셀로 — 카메라가 소수점만큼 움직여도 월드가 떨지 않는다
			const float tx = cam.offset.x - cam.target.x * cam.zoom, ty = cam.offset.y - cam.target.y * cam.zoom;
			cam.offset.x += std::round(tx) - tx;
			cam.offset.y += std::round(ty) - ty;
		}
		return cam;
	}

	[[nodiscard]] Vector2 Target() const { return m_target; }
	[[nodiscard]] float Zoom() const { return m_zoom; }
	[[nodiscard]] float ZoomGoal() const { return m_zoomGoal; }
	[[nodiscard]] float Trauma() const { return m_trauma; }

	static constexpr float kMinZoom = 0.9f;
	static constexpr float kMaxZoom = 2.2f;
	static constexpr float kTau = 0.15f;   // 추적 스프링 시간 상수(초)

private:
	Vector2 m_target{};
	Vector2 m_vel{};
	Vector2 m_bias{}, m_biasGoal{};
	bool m_pixelSnap = true;
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
