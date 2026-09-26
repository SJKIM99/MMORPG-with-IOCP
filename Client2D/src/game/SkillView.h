#pragma once

#include "net/Shared.h"
#include "Data/SkillGrowth.h"

// ─────────────────────────────────────────────────────────────────────────────
// 갈래(Phase 2-D)를 입힌 기술 표 — 클라 예측 · 연출 · 미리보기가 서버와 같은 복사본(SkillGrowth::ApplyDef)을 쓴다.
//
//  · flags = SKILL_EX_NFY.flags(비트 1~4 = 켜진 갈래). 내 기술은 Game::MyVarBits가 같은 비트를 만든다.
//  · 기술 id × 갈래 조합(3 × 3)마다 한 번 계산해 둔다 — 돌려준 포인터는 프로그램이 끝날 때까지 유효하다.
//  · 레벨은 연출과 상관없다(피해만 — 서버). 그래서 조합에는 갈래만 들어간다.
// ─────────────────────────────────────────────────────────────────────────────
namespace skillview
{
	[[nodiscard]] const SkillDef* Def(uint16_t id, uint8_t flags);
	[[nodiscard]] const SkillRow* Row(uint16_t id, uint8_t flags);
	// 켜진 갈래 중 이 변형(없으면 nullptr)
	[[nodiscard]] const SkillGrowth::Variant* Mod(uint16_t id, uint8_t flags, SkillGrowth::Mod m);
	// 켜진 갈래의 연출 색(없으면 0)
	[[nodiscard]] uint32_t VariantColor(uint16_t id, uint8_t flags);
}
