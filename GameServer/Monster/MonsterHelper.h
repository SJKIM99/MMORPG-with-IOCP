#pragma once
#include "Monster.h"
#include "Zone/ZoneTypes.h"

class User;

namespace MonsterHelper
{
	void RandomMove(ObjID& monsterId);
	void AStarMove(ObjID& monsterId, short nextX, short nextY);

	void WakeUpMonster(ObjID& monsterId, ObjID& wakerId, bool forceWake = false);

	void HandleRandomMove(const ObjID& monsterId);
	void HandleRespawn(const ObjID& monsterId);
	void HandleAggroMove(const ObjID& monsterId, const ObjID& targetId);
	void HandleAttackToPlayer(const ObjID& monsterId, const ObjID& playerId);

	// Zone 전용 스레드에서 GSector 설정 이후 호출.
	// 지역(Data/WorldData.h)의 스폰 가중치대로 종족을 정하고, 필드 보스를 배치한다.
	void InitForZone(ZoneId zoneId);

	// ── 리니지풍 컨텐츠 확장 ──
	// 맞은 몬스터가 때린 플레이어를 쫓게 한다(선공하지 않는 몬스터의 반격 포함).
	// 몬스터와 공격자가 같은 Zone 스레드 위에 있을 때만 호출한다(공격 처리 경로).
	void Provoke(const shared_ptr<Monster>& monster, const shared_ptr<User>& attacker);

	// 보스 광역기: 예고(시전) → 1.2초 뒤 적중. TimerThread가 보스의 Zone 스레드로 넘겨준다.
	void HandleBossSkillCast(const ObjID& bossId);
	void HandleBossSkillImpact(const ObjID& bossId);
};
