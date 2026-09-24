#include "pch.h"
#include "ContentHelper.h"

#include "User.h"
#include "Monster.h"
#include "UserHelper.h"
#include "SubjectHelper.h"
#include "SectorHelper.h"
#include "MonsterHelper.h"
#include "Sector.h"
#include "TimerThread.h"
#include "Thread/DBThread.h"
#include "CoreTLS.h"
#include "Collision.h"
#include "Item/ItemHelper.h"
#include "Item/ItemTable.h"
#include "Item/ItemTableData.h"
#include "Item/EquipmentItem.h"
#include "Zone/ZoneLayout.h"
#include "Zone/ZoneManager.h"
#include "Data/LevelTable.h"
#include "Data/WorldData.h"
#include "Data/MonsterTable.h"

namespace
{
	// me와 같은 Zone의 이웃 섹터에서 id를 찾는다. 전역 GameObjectManager가 아니라
	// 섹터 격자로 찾는 이유: 섹터 격자는 Zone마다 따로 있으므로, 여기서 찾은 대상은
	// "지금 이 Zone 스레드가 소유한 오브젝트"임이 구조적으로 보장된다(다른 Zone의
	// 오브젝트를 락 없이 건드리는 데이터 레이스가 원천적으로 불가능).
	shared_ptr<Subject> FindNeighbor(const shared_ptr<User>& me, const ObjID& id)
	{
		shared_ptr<Subject> found;
		if (me->GetSectorX() < 0)
			return found;
		GSector->ForEachNeighborObject(me->GetSectorX(), me->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (found == nullptr && object->GetObjID() == id)
				found = object;
		});
		return found;
	}

	void BroadcastSwing(const shared_ptr<User>& attacker, uint8_t facing)
	{
		const ObjID attackerId = attacker->GetObjID();
		GSector->ForEachNeighborObject(attacker->GetSectorX(), attacker->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (object->GetObjID() == attackerId || object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
				return;
			auto viewer = static_pointer_cast<User>(object);
			auto session = viewer->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME)
				return;
			if (SubjectHelper::CanSee(object, attacker))
				UserHelper::SendPLAYER_ATTACK_NFY(viewer, attackerId, facing);
		});
	}

	void FinishRespawn(const shared_ptr<User>& player)
	{
		ObjID playerId = player->GetObjID();
		UserHelper::SendSUBJECT_RESPAWN_NFY(player, player);
		SectorHelper::NotifyPlayerEnteredWorld(playerId, true);
		UserHelper::BroadcastEffect(player, playerId, EffectId::Restart, player->GetX(), player->GetY(), 0);
		UserHelper::SendUSER_INFO_EX_INF(player);
		GTimerThread->ScheduleAfter(playerId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
	}

	const WorldData::Point16& PickTownSpawn()
	{
		return WorldData::kTownSpawnPoints[LRng() % WorldData::kTownSpawnPointCount];
	}

	// PvP: attacker가 victim을 때린다. 두 사람 모두 같은 Zone(=이 스레드 소유)이다.
	void AttackPlayer(const shared_ptr<User>& attacker, const shared_ptr<User>& victim)
	{
		if (victim->GetStat()->IsDead() || victim->IsTransferring())
			return;
		if (WorldData::IsSafeZone(victim->GetX(), victim->GetY()))
		{
			UserHelper::SendSystemEx(attacker, SystemMessageCode::SafeZoneNoCombat);
			return;
		}

		const uint32_t now = GetNowTime();
		// 무고한 대상 = 보라돌이도 아니고 카오틱도 아닌 플레이어. 이런 대상을 먼저 치면
		// 공격자가 보라돌이가 되어, 상대가 반격해도 상대에게 페널티가 없다(정당방위).
		const bool victimInnocent = !victim->IsPurple(now) && victim->GetAlignment() >= 0;
		if (victimInnocent)
		{
			const bool wasPurple = attacker->IsPurple(now);
			attacker->m_purpleUntil = now + AlignmentRule::kPurpleDurationMs;
			GTimerThread->ScheduleAfter(attacker->GetObjID(), std::chrono::milliseconds(AlignmentRule::kPurpleDurationMs + 50), TIMER_EVENT_TYPE::EV_PURPLE_EXPIRE);
			if (!wasPurple)
			{
				UserHelper::SendSystemEx(attacker, SystemMessageCode::BecamePurple, static_cast<int32_t>(AlignmentRule::kPurpleDurationMs));
				UserHelper::BroadcastInfoEx(attacker);
			}
		}

		// PvP는 평타의 70% — 한두 대에 끝나지 않게.
		bool crit = false;
		const uint32_t raw = CombatRule::RollDamage(attacker->GetStat()->GetOffensive(), LRng() % 1001, LRng() % 100, crit);
		const uint16_t damage = static_cast<uint16_t>(std::clamp<uint32_t>(raw * 7 / 10, 1, 65535));
		victim->GetStat()->TakeDamage(damage);

		const uint8_t flags = static_cast<uint8_t>(DAMAGE_PVP | (crit ? DAMAGE_CRIT : 0) | (victim->GetStat()->IsDead() ? DAMAGE_KILLED : 0));

		// 레거시 세션(봇)도 자기 HP가 깎인 걸 알 수 있게 기존 SUBJECT_ATTACK_NFY도 보낸다.
		const ObjID victimId = victim->GetObjID();
		const ObjID attackerId = attacker->GetObjID();
		const int32_t victimHp = static_cast<int32_t>(victim->GetStat()->GetHp());
		GSector->ForEachNeighborObject(victim->GetSectorX(), victim->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser) return;
			if (!SubjectHelper::CanSee(victim, object)) return;
			auto viewer = static_pointer_cast<User>(object);
			auto session = viewer->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
			UserHelper::SendSUBJECT_ATTACK_NFY(viewer, victimId, attackerId, victimHp);
		});
		UserHelper::BroadcastDamage(victim, attackerId, damage, flags);

		if (!victim->GetStat()->IsDead())
			return;

		// 무고한 플레이어를 죽이면 PK — 성향치가 크게 떨어지고 카오틱(빨간 이름)이 된다.
		if (victimInnocent)
		{
			const int16_t before = attacker->GetAlignment();
			const int16_t after = AlignmentRule::Clamp(static_cast<int32_t>(std::min<int16_t>(before, 0)) - AlignmentRule::kPkPenalty);
			attacker->SetAlignment(after);
			attacker->SetPkCount(static_cast<uint16_t>(std::min<uint32_t>(attacker->GetPkCount() + 1u, 65535u)));
			ContentHelper::SaveAlignment(attacker, true);
			UserHelper::SendSystemEx(attacker, SystemMessageCode::BecameChaotic, after);
			UserHelper::BroadcastEffect(attacker, attackerId, EffectId::PlayerKill, attacker->GetX(), attacker->GetY(), 0);
			UserHelper::BroadcastInfoEx(attacker);
			UserHelper::SendUSER_INFO_EX_INF(attacker);
			UserHelper::BroadcastWorldNotice(WorldNoticeCode::PlayerKilled, attacker->GetZoneId(), 0, attacker->GetName());
		}

		ContentHelper::HandlePlayerDeath(victim, attackerId, true);
	}
}

namespace ContentHelper
{
	void HandleTargetAttack(const shared_ptr<User>& attacker, const ObjID& targetId)
	{
		if (attacker == nullptr || attacker->GetStat()->IsDead())
			return;

		auto target = FindNeighbor(attacker, targetId);
		if (target == nullptr || target->GetObjID() == attacker->GetObjID())
		{
			UserHelper::SendSystemEx(attacker, SystemMessageCode::TargetInvalid);
			return;
		}

		const int dx = target->GetX() - attacker->GetX();
		const int dy = target->GetY() - attacker->GetY();
		if (abs(dx) > 1 || abs(dy) > 1)
		{
			UserHelper::SendSystemEx(attacker, SystemMessageCode::TargetTooFar);
			return;
		}

		// 방향은 서버가 대상 위치로 정한다(클라이언트가 보낸 방향을 믿지 않는다).
		attacker->UpdateFacing(dx);
		BroadcastSwing(attacker, attacker->IsFacingLeft() ? 1 : 0);

		if (WorldData::IsSafeZone(attacker->GetX(), attacker->GetY()))
		{
			UserHelper::SendSystemEx(attacker, SystemMessageCode::SafeZoneNoCombat);
			return;
		}

		const EnumCategory category = target->GetObjID().GetCategory<EnumCategory>();
		if (category == EnumCategory::eMonster)
		{
			if (target->GetStat()->IsDead())
				return;
			bool crit = false;
			const uint32_t damage = CombatRule::RollDamage(attacker->GetStat()->GetOffensive(), LRng() % 1001, LRng() % 100, crit);
			ObjID monsterId = target->GetObjID();
			ObjID attackerId = attacker->GetObjID();
			UserHelper::AttackMonster(monsterId, attackerId, static_cast<int>(damage), crit ? DAMAGE_CRIT : 0);
		}
		else if (category == EnumCategory::eUser)
		{
			AttackPlayer(attacker, static_pointer_cast<User>(target));
		}
		else
		{
			UserHelper::SendSystemEx(attacker, SystemMessageCode::TargetInvalid);
		}
	}

	void HandlePlayerDeath(const shared_ptr<User>& victim, const ObjID& killerId, bool killedByPlayer)
	{
		if (victim == nullptr || victim->GetSectorX() < 0)
			return;  // 이미 섹터에서 빠진 상태(중복 처리 방지)

		const ObjID victimId = victim->GetObjID();
		const short x = victim->GetX();
		const short y = victim->GetY();

		// 사망 알림(본인 포함 — 본인도 CanSee(victim, victim)이 참이다)
		GSector->ForEachNeighborObject(victim->GetSectorX(), victim->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;
			if (!SubjectHelper::CanSee(victim, object)) return;
			auto viewer = static_pointer_cast<User>(object);
			auto session = viewer->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
			UserHelper::SendSUBJECT_DIE_NFY(viewer, victim);
		});

		// 경험치 페널티 — 몬스터에게 죽었을 때만(리니지처럼 PvP 사망은 경험치를 잃지 않는다)
		if (!killedByPlayer)
		{
			const uint32_t lost = victim->GetStat()->LoseExp(
				LevelTable::DeathExpPenalty(victim->GetStat()->GetLevel(), victim->GetStat()->GetExp()));
			if (lost > 0)
			{
				UserHelper::SendUSER_STAT_CHANGE_INF(victim);
				UserHelper::SendSystemEx(victim, SystemMessageCode::ExpLost, static_cast<int32_t>(lost));
			}
		}

		// 카오틱(성향치 음수)은 죽을 때 소지품을 떨어뜨릴 수 있다 — 장착 중인 장비도 예외가 아니다.
		if (victim->GetAlignment() < 0 && (LRng() % 100) < AlignmentRule::kChaoticDropPercent)
		{
			const uint32_t roll = static_cast<uint32_t>(LRng());
			if (Item::SharedPtr dropped = victim->GetInventory()->TryTakeRandomItem(roll))
			{
				const auto equipment = dynamic_pointer_cast<EquipmentItem>(dropped);
				const int32_t enchant = equipment != nullptr ? equipment->GetEnchant() : 0;
				const int32_t itemId = dropped->GetItemTableId();
				ItemHelper::SpawnFieldItem(dropped, x, y);
				UserHelper::SendSystemEx(victim, SystemMessageCode::ItemDroppedOnDeath, itemId, enchant);
				UserHelper::BroadcastEquipChange(victim);
			}
		}

		ObjID mutableId = victimId;
		GSector->RemoveObject(mutableId, victim->RefSectorX(), victim->RefSectorY());

		// 부활 타이머 — 확장 세션은 보통 먼저 USER_RESTART_REQ가 온다(타이머는 안전장치).
		// 타이머에 사망 번호를 실어 보내, 그 사이 리스타트/재사망했으면 무시되게 한다.
		++victim->m_deathSeq;
		GTimerThread->ScheduleAfter(victimId, 30s, TIMER_EVENT_TYPE::EV_USER_RESPAWN,
			ObjID(EnumCategory::eNone, static_cast<uint64_t>(victim->m_deathSeq)));

		UserHelper::SendUSER_INFO_EX_INF(victim);
		(void)killerId;
	}

	void OnMonsterKilled(const shared_ptr<User>& killer, const shared_ptr<Monster>& monster)
	{
		if (killer == nullptr || monster == nullptr)
			return;

		const MonsterTableRow* species = monster->GetSpecies();
		const uint8_t level = species != nullptr ? species->level : 1;

		// 사냥으로 성향치가 오른다(라우풀 회복 — 카오틱도 사냥으로 속죄할 수 있다).
		const int16_t before = killer->GetAlignment();
		if (before < AlignmentRule::kMax)
		{
			const int16_t after = AlignmentRule::Clamp(static_cast<int32_t>(before) + AlignmentRule::GainForMonster(level));
			killer->SetAlignment(after);
			if (before < 0 && after >= 0)
				UserHelper::SendSystemEx(killer, SystemMessageCode::AlignmentRecovered, after);
			if (AlignmentRule::ColorOf(before, false) != AlignmentRule::ColorOf(after, false))
				UserHelper::BroadcastInfoEx(killer);
			SaveAlignment(killer, false);
		}

		if (species != nullptr && species->boss)
			UserHelper::BroadcastWorldNotice(WorldNoticeCode::BossKilled, species->id, monster->GetZoneId(), killer->GetName());
	}

	void HandleRestartRequest(const shared_ptr<User>& player)
	{
		if (player == nullptr || !player->GetStat()->IsDead())
			return;
		RestartInTown(player);
	}

	void RestartInTown(const shared_ptr<User>& player)
	{
		if (player == nullptr || !player->GetStat()->IsDead())
			return;

		const auto& spawn = PickTownSpawn();
		++player->m_deathSeq;  // 대기 중인 부활 타이머 무효화
		player->GetStat()->SetDead(false);
		player->GetStat()->SetHp(player->GetStat()->GetMaxHp());

		const ObjID playerId = player->GetObjID();
		const ZoneId townZone = ZoneLayout::GetZoneIdByWorld(spawn.x, spawn.y);
		if (townZone == player->GetZoneId())
		{
			ObjID mutableId = playerId;
			SectorHelper::UpdatePosition(mutableId, spawn.x, spawn.y);
			FinishRespawn(player);
			return;
		}

		// 다른 Zone(마을)으로 — 기존 Zone Transfer(SectorHelper::HandleZoneTransfer)와 같은
		// 순서로 소유권을 넘긴다. 죽은 플레이어는 이미 섹터에 없으므로 A Zone 쪽 정리는 없다.
		// 1) Transferring 표시 → 2) 좌표/Zone 매핑/라우팅 갱신 → 3) B Zone 큐에 입장 위임(FIFO)
		player->SetTransferring(true);
		player->SetPosition(spawn.x, spawn.y);
		player->SetZoneId(townZone);
		GZoneManager->UpdateObjectZone(playerId, townZone);
		if (const auto session = player->GetGameSession())
			session->SetRoutingZoneId(townZone);

		GZoneManager->EnqueueByZone(townZone, [playerId, x = spawn.x, y = spawn.y]()
		{
			auto p = ::GetGameObject<User>(playerId);
			if (p == nullptr)
				return;  // 이동 중 접속 종료

			ObjID mutableId = playerId;
			const bool valid = GSector->UpdateObjectSectorAndPosition(mutableId, x, y,
				p->RefSectorX(), p->RefSectorY(), p->RefX(), p->RefY());
			if (!valid)
				SectorHelper::GetRandomPosition(mutableId);

			FinishRespawn(p);
			p->SetTransferring(false);
		});
	}

	void TeleportPlayer(const shared_ptr<User>& player, short x, short y)
	{
		if (player == nullptr || player->GetStat()->IsDead() || player->IsTransferring())
			return;

		const ObjID playerId = player->GetObjID();
		UserHelper::BroadcastEffect(player, playerId, EffectId::TeleportOut, player->GetX(), player->GetY(), 0);

		const ZoneId destZone = ZoneLayout::GetZoneIdByWorld(x, y);
		const bool crossZone = destZone != player->GetZoneId();

		// 같은 Zone이면 시야 갱신(REMOVE/ADD)까지, 다른 Zone이면 Zone Transfer까지
		// 기존 이동 처리 함수가 그대로 해 준다. 걸음 시간 0 = 보간 없이 바로 옮김.
		player->SetLastStepMs(0);
		player->m_moveClock = 0;
		SectorHelper::HandlePlayerMove(player, x, y);

		if (crossZone)
		{
			// B Zone 큐는 FIFO라 HandleEnterZone보다 반드시 뒤에 실행된다.
			GZoneManager->EnqueueByZone(destZone, [playerId]()
			{
				auto p = ::GetGameObject<User>(playerId);
				if (p != nullptr && !p->IsTransferring())
					UserHelper::BroadcastEffect(p, playerId, EffectId::TeleportIn, p->GetX(), p->GetY(), 0);
			});
		}
		else
		{
			UserHelper::BroadcastEffect(player, playerId, EffectId::TeleportIn, player->GetX(), player->GetY(), 0);
		}
	}

	void UseEffectItem(const shared_ptr<User>& player, uint16_t slotIndex)
	{
		auto inventory = player->GetInventory();
		const auto& slots = inventory->GetSlots();
		const auto it = slots.find(slotIndex);
		if (it == slots.end())
		{
			UserHelper::SendITEM_USE_ACK(player, slotIndex, false);
			return;
		}

		const ItemTableRow* row = ItemTable::Find(it->second->GetItemTableId());
		ASSERT_CRASH(row != nullptr);

		bool success = false;
		switch (row->effect)
		{
		case ItemEffect::eReturnTown:
		{
			if (!inventory->TryConsumeOne(slotIndex, ItemEffect::eReturnTown))
				break;
			success = true;
			UserHelper::SendITEM_USE_ACK(player, slotIndex, true);
			const auto& spawn = PickTownSpawn();
			TeleportPlayer(player, spawn.x, spawn.y);
			UserHelper::SendSystemEx(player, SystemMessageCode::ReturnedToTown);
			return;
		}
		case ItemEffect::eTeleportRandom:
		{
			// 지금 서 있는 지역(Zone) 안의 무작위 칸. 막힌 칸/마을은 피한다.
			const ZoneCoord zone = ZoneLayout::GetZoneCoordById(player->GetZoneId());
			const int minX = zone.x * WorldData::kRegionSize + ZONE_BOUNDARY_MARGIN + 1;
			const int minY = zone.y * WorldData::kRegionSize + ZONE_BOUNDARY_MARGIN + 1;
			const int span = WorldData::kRegionSize - (ZONE_BOUNDARY_MARGIN + 1) * 2;
			short destX = -1, destY = -1;
			for (int attempt = 0; attempt < 64; ++attempt)
			{
				const int x = minX + static_cast<int>(LRng() % static_cast<uint32_t>(span));
				const int y = minY + static_cast<int>(LRng() % static_cast<uint32_t>(span));
				if (isCollision(static_cast<short>(x), static_cast<short>(y)) || WorldData::IsSafeZone(x, y))
					continue;
				destX = static_cast<short>(x);
				destY = static_cast<short>(y);
				break;
			}
			if (destX < 0 || !inventory->TryConsumeOne(slotIndex, ItemEffect::eTeleportRandom))
				break;
			success = true;
			UserHelper::SendITEM_USE_ACK(player, slotIndex, true);
			TeleportPlayer(player, destX, destY);
			UserHelper::SendSystemEx(player, SystemMessageCode::Teleported, player->GetZoneId());
			return;
		}
		case ItemEffect::eHaste:
		{
			if (!inventory->TryConsumeOne(slotIndex, ItemEffect::eHaste))
				break;
			success = true;
			const uint32_t now = GetNowTime();
			const uint32_t durationMs = static_cast<uint32_t>(row->effectValue) * 1000u;
			player->m_hasteUntil = now + durationMs;
			GTimerThread->ScheduleAfter(player->GetObjID(), std::chrono::milliseconds(durationMs + 50), TIMER_EVENT_TYPE::EV_HASTE_EXPIRE);
			UserHelper::BroadcastEffect(player, player->GetObjID(), EffectId::HasteOn, player->GetX(), player->GetY(), static_cast<int32_t>(row->effectValue));
			UserHelper::BroadcastInfoEx(player);
			UserHelper::SendSystemEx(player, SystemMessageCode::HasteStart, row->effectValue);
			UserHelper::SendUSER_INFO_EX_INF(player);
			break;
		}
		default:
			break;
		}

		UserHelper::SendITEM_USE_ACK(player, slotIndex, success);
	}

	void HandleEnchant(const shared_ptr<User>& player, uint16_t scrollSlot, uint16_t targetSlot)
	{
		if (player == nullptr || player->GetStat()->IsDead())
			return;

		const Inventory::EnchantOutcome outcome =
			player->GetInventory()->TryEnchant(scrollSlot, targetSlot, static_cast<uint32_t>(LRng() % 100), static_cast<uint32_t>(LRng() % 3));

		UserHelper::SendENCHANT_RESULT_ACK(player, outcome.result, targetSlot, outcome.itemId, outcome.before, outcome.after);
		if (outcome.result == EnchantResult::Invalid)
		{
			UserHelper::SendSystemEx(player, SystemMessageCode::TargetInvalid);
			return;
		}

		const bool success = outcome.result == EnchantResult::Success;
		if (outcome.wasEquipped)
		{
			// 장착 중이던 무기가 증발했거나(맨손이 됨) 빛이 바뀌었다 — 주변에 외형을 다시 알린다.
			UserHelper::BroadcastEquipChange(player);
			UserHelper::BroadcastInfoEx(player);
		}
		UserHelper::SendUSER_INFO_EX_INF(player);
		UserHelper::BroadcastEffect(player, player->GetObjID(), success ? EffectId::EnchantSuccess : EffectId::EnchantFail,
			player->GetX(), player->GetY(), success ? outcome.after : outcome.before);

		if (success && outcome.after >= EnchantRule::kWorldNoticeLevel)
			UserHelper::BroadcastWorldNotice(WorldNoticeCode::EnchantSuccess, outcome.itemId, outcome.after, player->GetName());
	}

	void GiveStarterKit(const shared_ptr<User>& player)
	{
		using namespace ItemTableData::Id;
		auto inventory = player->GetInventory();

		std::vector<uint16_t> touched;
		if (inventory->TryAddItem(WoodenSword, 1, &touched) && !touched.empty())
			(void)inventory->TryEquip(touched.front());

		(void)inventory->TryAddItem(HealthPotion, 20);
		(void)inventory->TryAddItem(ReturnScroll, 3);
		(void)inventory->TryAddItem(TeleportScroll, 2);
		(void)inventory->TryAddItem(HastePotion, 2);
		(void)inventory->TryAddItem(EnchantScroll, 1);
		(void)inventory->AddAdena(1000, AdenaReason::Init);
	}

	namespace
	{
		// 멱등성 검사: 이미 처리한 seq면 true(중복 — 무시해야 함). 아니면 기록하고 false.
		bool IsDuplicateShopRequest(const shared_ptr<User>& player, uint32_t seq)
		{
			if (seq <= player->m_lastShopSeq)
				return true;
			player->m_lastShopSeq = seq;
			return false;
		}
	}

	void HandleShopBuy(const shared_ptr<User>& player, uint16_t npcId, uint16_t itemId, uint16_t count, uint32_t seq)
	{
		auto reply = [&](ShopResult result, int32_t adenaDelta)
		{
			UserHelper::SendSHOP_RESULT_ACK(player, ShopKind::Buy, result, seq, itemId, count, adenaDelta);
		};

		if (IsDuplicateShopRequest(player, seq))
			return reply(ShopResult::Duplicate, 0);
		if (player->GetStat()->IsDead() || player->IsTransferring())
			return reply(ShopResult::InvalidState, 0);

		const WorldData::NpcRow* npc = WorldData::FindNpc(npcId);
		if (npc == nullptr)
			return reply(ShopResult::InvalidNpc, 0);
		if (!WorldData::IsNearNpc(*npc, player->GetX(), player->GetY()))
			return reply(ShopResult::TooFar, 0);
		if (!WorldData::ShopSells(npcId, itemId))
			return reply(ShopResult::InvalidItem, 0);

		const ItemTableRow* row = ItemTable::Find(itemId);
		if (row == nullptr || row->buyPrice == 0)
			return reply(ShopResult::InvalidItem, 0);
		if (count == 0 || count > row->maxStack)
			return reply(ShopResult::InvalidCount, 0);

		const uint64_t total64 = static_cast<uint64_t>(row->buyPrice) * count;
		if (total64 > 2'000'000'000ull)
			return reply(ShopResult::InvalidCount, 0);
		const uint32_t total = static_cast<uint32_t>(total64);

		uint16_t slot = MAX_INVENTORY_SLOTS;
		const ShopResult result = player->GetInventory()->TryBuy(itemId, count, total, &slot);
		if (result == ShopResult::NotEnoughAdena)
			UserHelper::SendSystemEx(player, SystemMessageCode::NotEnoughAdena, static_cast<int32_t>(total), static_cast<int32_t>(player->GetInventory()->GetAdena()));
		else if (result == ShopResult::InventoryFull)
			UserHelper::SendSYSTEM_MESSAGE_INF(player, SystemMessageCode::InventoryFull, itemId, count);

		reply(result, result == ShopResult::Success ? -static_cast<int32_t>(total) : 0);
	}

	void HandleShopSell(const shared_ptr<User>& player, uint16_t npcId, uint16_t slotIndex, uint16_t count, uint32_t seq)
	{
		uint16_t itemId = 0;
		{
			const auto& slots = player->GetInventory()->GetSlots();
			const auto it = slots.find(slotIndex);
			if (it != slots.end())
				itemId = it->second->GetItemTableId();
		}

		auto reply = [&](ShopResult result, int32_t adenaDelta)
		{
			UserHelper::SendSHOP_RESULT_ACK(player, ShopKind::Sell, result, seq, itemId, count, adenaDelta);
		};

		if (IsDuplicateShopRequest(player, seq))
			return reply(ShopResult::Duplicate, 0);
		if (player->GetStat()->IsDead() || player->IsTransferring())
			return reply(ShopResult::InvalidState, 0);

		const WorldData::NpcRow* npc = WorldData::FindNpc(npcId);
		if (npc == nullptr || !WorldData::NpcBuysItems(*npc))
			return reply(ShopResult::InvalidNpc, 0);
		if (!WorldData::IsNearNpc(*npc, player->GetX(), player->GetY()))
			return reply(ShopResult::TooFar, 0);

		uint32_t gain = 0;
		const ShopResult result = player->GetInventory()->TrySell(slotIndex, count, &gain);
		reply(result, result == ShopResult::Success ? static_cast<int32_t>(gain) : 0);
	}

	void HandleNpcTeleport(const shared_ptr<User>& player, uint16_t npcId, uint16_t destZone, uint32_t seq)
	{
		auto reply = [&](ShopResult result, int32_t adenaDelta)
		{
			UserHelper::SendSHOP_RESULT_ACK(player, ShopKind::Teleport, result, seq, destZone, 0, adenaDelta);
		};

		if (IsDuplicateShopRequest(player, seq))
			return reply(ShopResult::Duplicate, 0);
		if (player->GetStat()->IsDead() || player->IsTransferring())
			return reply(ShopResult::InvalidState, 0);

		const WorldData::NpcRow* npc = WorldData::FindNpc(npcId);
		if (npc == nullptr || npc->kind != WorldData::NpcKind::eTeleporter)
			return reply(ShopResult::InvalidNpc, 0);
		if (!WorldData::IsNearNpc(*npc, player->GetX(), player->GetY()))
			return reply(ShopResult::TooFar, 0);

		const WorldData::RegionRow* region = WorldData::FindRegion(destZone);
		if (region == nullptr || region->teleportFee == 0)
			return reply(ShopResult::InvalidItem, 0);

		if (!player->GetInventory()->TrySpendAdena(region->teleportFee, AdenaReason::Teleport))
		{
			UserHelper::SendSystemEx(player, SystemMessageCode::NotEnoughAdena, static_cast<int32_t>(region->teleportFee), static_cast<int32_t>(player->GetInventory()->GetAdena()));
			return reply(ShopResult::NotEnoughAdena, 0);
		}

		reply(ShopResult::Success, -static_cast<int32_t>(region->teleportFee));
		TeleportPlayer(player, region->landX, region->landY);
		UserHelper::SendSystemEx(player, SystemMessageCode::Teleported, destZone);
	}

	void HandlePurpleExpire(const ObjID& playerId)
	{
		auto player = ::GetGameObject<User>(playerId);
		if (player == nullptr || player->IsPurple(GetNowTime()))
			return;  // 그 사이 다시 공격해서 연장됐다면 다음 타이머가 처리한다
		UserHelper::BroadcastInfoEx(player);
		UserHelper::SendUSER_INFO_EX_INF(player);
	}

	void HandleHasteExpire(const ObjID& playerId)
	{
		auto player = ::GetGameObject<User>(playerId);
		if (player == nullptr || player->m_hasteUntil == 0 || player->IsHasted(GetNowTime()))
			return;
		player->m_hasteUntil = 0;
		UserHelper::SendSystemEx(player, SystemMessageCode::HasteEnd);
		UserHelper::BroadcastInfoEx(player);
		UserHelper::SendUSER_INFO_EX_INF(player);
	}

	void SaveAlignment(const shared_ptr<User>& player, bool force)
	{
		if (player == nullptr)
			return;
		const int16_t current = player->GetAlignment();
		const int diff = abs(static_cast<int>(current) - static_cast<int>(player->m_savedAlignment));
		const bool signChanged = (current < 0) != (player->m_savedAlignment < 0);
		if (!force && diff < 50 && !signChanged)
			return;
		GDBThread->RequestSaveAlignment(player->GetPlayerId(), current, player->GetPkCount());
		player->m_savedAlignment = current;
	}
}
