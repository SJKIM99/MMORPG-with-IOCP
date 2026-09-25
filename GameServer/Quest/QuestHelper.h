#pragma once

#include <memory>
#include <cstdint>

class User;
class Monster;
struct MonsterTableRow;

// ─────────────────────────────────────────────────────────────────────────────
// 퀘스트 · 메인 이야기(Data/QuestData.h) — 서버 권위.
//
// 받기/보고는 NPC 곁에서만(NpcHelper::IsNear — 마을 Zone이면 배회 중인 지금 위치),
// 진행은 서버가 본 사건(처치 · 대화 · 이동 · 장비 변경)으로만 오른다. 클라이언트는
// 표를 읽어 보여 줄 뿐이다.
//
// 전제(기존 서버와 같다): 모든 함수는 그 사용자를 소유한 Zone 스레드에서 불린다
// (패킷 → Route, 처치 → UserHelper::AttackMonster, 대화 → NpcHelper, 이동 → HandleMoveEx).
// 상태는 User::m_quests/m_questDone/m_questCooldowns(진행 데이터 v3)에 있다.
// 확장 세션만 퀘스트를 가진다 — 레거시 세션/봇에는 어떤 패킷도 가지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
namespace QuestHelper
{
	// 로그인 직후 전체 상태(QUEST_LIST_INF)
	void SendList(const std::shared_ptr<User>& user);

	// QUEST_ACCEPT_REQ / QUEST_COMPLETE_REQ / QUEST_ABANDON_REQ
	void HandleAccept(const std::shared_ptr<User>& user, uint16_t questId);
	void HandleComplete(const std::shared_ptr<User>& user, uint16_t questId);
	void HandleAbandon(const std::shared_ptr<User>& user, uint16_t questId);

	// ── 진행 훅 ──
	// 몬스터를 쓰러뜨림(막타) — 처치 · 지역 처치 · 수집. (x, y) = 쓰러진 칸
	void OnKill(const std::shared_ptr<User>& killer, const MonsterTableRow* species, short x, short y);
	// 보스가 쓰러짐 — 가까이(QuestData::kBossShareRange) 있던 모두가 함께 인정받는다(막타 포함)
	void OnBossKilled(uint16_t bossSpecies, short x, short y);
	// NPC에게 말을 걸었다(동물이면 쓰다듬기)
	void OnTalk(const std::shared_ptr<User>& user, uint16_t npcId);
	// 한 걸음 옮긴 뒤(존 이동 중이 아닐 때) — 탐험 목표
	void OnMoved(const std::shared_ptr<User>& user);
	// 장비가 바뀜 — "N부위 착용" 목표(지금 상태라 줄어들 수도 있다)
	void OnEquipChanged(const std::shared_ptr<User>& user);
}
