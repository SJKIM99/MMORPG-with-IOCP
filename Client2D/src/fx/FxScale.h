#pragma once

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 연출 세기 조절(Phase 2-A) — 그래픽 품질 · 광과민 보호 · 다른 플레이어 기술 세기.
//
//  · 내 기술 100%, 다른 플레이어 기술은 설정값(기본 50%). 다른 사람의 시전 · 적중 연출을 만들 때
//    Scope(false)를 세우면 그 안에서 만든 입자 · 셰이더 효과 · 궤적 · 순간 조명 · 화면 효과가 모두 그 비율로 약해진다.
//  · 광과민 보호: 화면 번쩍임 · 흔들림 · 색수차 · 방사형 흐림 · 암전을 절반으로(post::SetPhotoSafe와 함께).
//  · 품질 낮음: 입자 수 절반(Emit이 절반을 버린다), 후처리 끔. 보통: 입자 75%, 블룸 ¼ 해상도.
// ─────────────────────────────────────────────────────────────────────────────
namespace fxscale
{
	enum class Quality : uint8_t { Low, Medium, High };

	void SetQuality(Quality q);
	[[nodiscard]] Quality GetQuality();
	void SetPhotoSafe(bool on);
	[[nodiscard]] bool PhotoSafe();
	void SetOther(float k);          // 다른 플레이어 기술 세기(0~1)
	[[nodiscard]] float Other();
	void SetShake(float k);          // 화면 흔들림 배율(0~1)

	// 지금 만드는 연출의 주인. 중첩 가능(안쪽이 이긴다).
	class Scope
	{
	public:
		explicit Scope(bool mine);
		~Scope();
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
	private:
		bool m_prev;
	};

	// 약하게(갈래 「메아리」 — 같은 연출을 작게 한 번 더). 중첩되면 곱한다
	class DimScope
	{
	public:
		explicit DimScope(float k);
		~DimScope();
		DimScope(const DimScope&) = delete;
		DimScope& operator=(const DimScope&) = delete;
	private:
		float m_prev;
	};

	// 기술 창 시연(Phase 2-D): 연출은 내 것처럼 100%로, 화면 효과(암전 · 번쩍임 · 흔들림)와 소리는 없이
	class StageScope
	{
	public:
		StageScope();
		~StageScope();
		StageScope(const StageScope&) = delete;
		StageScope& operator=(const StageScope&) = delete;
	private:
		bool m_prev;
	};
	[[nodiscard]] bool Stage();

	[[nodiscard]] bool Mine();       // 내 연출인가(암전 · 컷인 · 화면 효과는 내 것만)
	[[nodiscard]] float Alpha();     // 효과 투명도 · 세기 배율(내 것 1, 남의 것 Other())
	[[nodiscard]] float Count();     // 입자 수 배율(품질 × 주인)
	[[nodiscard]] float Flash();     // 화면 번쩍임 배율(광과민 보호 0.5, 남의 것 0)
	[[nodiscard]] float Shake();     // 흔들림 배율(설정 × 광과민)
	[[nodiscard]] bool Keep();       // 입자 하나를 남길까(Count 확률) — Particles::Emit이 부른다
}
