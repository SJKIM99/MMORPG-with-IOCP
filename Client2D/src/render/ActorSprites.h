#pragma once

#include "render/Actors.h"

// ─────────────────────────────────────────────────────────────────────────────
// 캐릭터 · 몬스터 · NPC를 그린 스프라이트로 움직인다(Higgsfield 동작판 → tools/assetgen).
//
// 한 캐릭터에 동작 그림이 몇 장(서기 · 걸음 두 장 · 준비 · 휘두름 · 시전 · 막기 · 맞음 · 쓰러짐 · 뒷모습)뿐이라,
// 그 사이를 "절차적 연기"로 잇는다 — 이것이 손맛의 대부분이다.
//   · 준비(anticipation): 뒤로 젖히며 웅크림 → 휘두름 순간 앞으로 튀어 나가며 늘어남(smear 잔상 한 장)
//   · 맞음: 뒤로 밀리며 젖힘 + 흰 번쩍임(셰이더) · 넉백
//   · 걷기: 두 걸음 그림을 발 위상에 맞춰 바꾸고, 위아래 흔들림 · 앞으로 쏠림
//   · 숨쉬기 · 기술별 자세(힘 모으기 떨림, 돌기 = 가로 배율 뒤집기)
// 그림이 없으면 false — actors::Draw*가 예전 절차적 그림으로 그린다.
// ─────────────────────────────────────────────────────────────────────────────
namespace actorspr
{
	bool DrawPlayer(const actors::Pose& p, const actors::PlayerLook& look);
	bool DrawMonster(const actors::Pose& p, const MonsterTableRow& row);
	bool DrawNpc(const actors::Pose& p, const WorldData::NpcRow& npc);

	// 스프라이트가 있으면 true(이름표 높이 등을 맞출 때)
	[[nodiscard]] bool HasPlayer(uint8_t cls);
	[[nodiscard]] bool HasMonster(uint16_t id);
	[[nodiscard]] bool HasNpc(uint16_t id);
}
