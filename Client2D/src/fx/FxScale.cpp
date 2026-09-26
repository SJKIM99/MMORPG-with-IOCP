#include "fx/FxScale.h"
#include "core/Util.h"

#include <algorithm>

namespace fxscale
{
	namespace
	{
		Quality g_quality = Quality::High;
		bool g_photoSafe = false;
		float g_other = 0.5f;
		float g_shake = 1.f;
		bool g_mine = true;
		float g_dim = 1.f;
		bool g_stage = false;
	}

	void SetQuality(Quality q) { g_quality = q; }
	Quality GetQuality() { return g_quality; }
	void SetPhotoSafe(bool on) { g_photoSafe = on; }
	bool PhotoSafe() { return g_photoSafe; }
	void SetOther(float k) { g_other = std::clamp(k, 0.f, 1.f); }
	float Other() { return g_other; }
	void SetShake(float k) { g_shake = std::clamp(k, 0.f, 1.f); }

	Scope::Scope(bool mine) : m_prev(g_mine) { g_mine = mine; }
	Scope::~Scope() { g_mine = m_prev; }

	DimScope::DimScope(float k) : m_prev(g_dim) { g_dim = std::clamp(g_dim * k, 0.f, 1.f); }
	DimScope::~DimScope() { g_dim = m_prev; }
	StageScope::StageScope() : m_prev(g_stage) { g_stage = true; }
	StageScope::~StageScope() { g_stage = m_prev; }
	bool Stage() { return g_stage; }

	// 시연 무대(기술 창)는 "내 것이 아닌 것"으로 친다 — 암전 · 컷인 · 화면 효과 · 흔들림이 게임 화면에 새지 않게
	bool Mine() { return g_mine && !g_stage; }
	float Alpha() { return (g_stage || g_mine ? 1.f : g_other) * g_dim; }

	float Count()
	{
		const float q = g_quality == Quality::Low ? 0.5f : (g_quality == Quality::Medium ? 0.75f : 1.f);
		// 남의 연출은 투명도만 줄이지 않고 수도 조금 줄인다(화면이 덜 붐비게)
		return q * (g_stage || g_mine ? 1.f : (0.4f + 0.6f * g_other)) * (0.4f + 0.6f * g_dim);
	}

	float Flash() { return Mine() ? (g_photoSafe ? 0.5f : 1.f) : 0.f; }
	float Shake() { return g_shake * (g_photoSafe ? 0.5f : 1.f); }

	bool Keep()
	{
		const float c = Count();
		return c >= 0.999f || util::RandF() < c;
	}
}
