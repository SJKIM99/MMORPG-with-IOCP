#include "pch.h"
#include "Content/ChatHelper.h"
#include "Route.h"
#include "GameSession.h"
#include "User.h"
#include "DBThread.h"
#include "SubjectHelper.h"
#include "SectorHelper.h"
#include "UserHelper.h"
#include "Sector.h"
#include "Zone/ZoneLayout.h"
#include "Item/ItemHelper.h"
#include "Item/ItemTable.h"
#include "Content/ContentHelper.h"
#include "Combat/CombatHelper.h"
#include "Combat/SkillHelper.h"
#include "Combat/SkillEx.h"
#include "Combat/SkillGrow.h"
#include "Npc/NpcHelper.h"
#include "Quest/QuestHelper.h"
#include "Data/LevelTable.h"
#include "Collision.h"

namespace Route
{
	void Dispatch(const shared_ptr<GameSession>& session, const char* packet)
	{
		// 타입은 1바이트. char로 읽으면 128 이상에서 음수가 되므로 부호 없이 읽는다.
		switch (static_cast<PacketType>(static_cast<uint8_t>(packet[2])))
		{
		case PacketType::USER_LOGIN_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_ALLOC)
				break;

			auto* p = reinterpret_cast<const USER_LOGIN_REQ_PACKET*>(packet);
			char name[NAME_SIZE + 1]{};
			char password[PASSWORD_SIZE + 1]{};
			::strncpy_s(name, p->name, NAME_SIZE);
			::strncpy_s(password, p->password, PASSWORD_SIZE);

			GDBThread->RequestLogin(session, name, password);
			break;
		}
		case PacketType::USER_LOGIN_EX_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_ALLOC)
				break;

			// 확장 로그인 — 이후 이 세션에만 리니지풍 컨텐츠 패킷을 보낸다.
			// 로그인 처리(DB 조회 → Zone 입장) 경로는 레거시 로그인과 완전히 같다.
			auto* p = reinterpret_cast<const USER_LOGIN_EX_REQ_PACKET*>(packet);
			char name[NAME_SIZE + 1]{};
			char password[PASSWORD_SIZE + 1]{};
			::strncpy_s(name, p->name, NAME_SIZE);
			::strncpy_s(password, p->password, PASSWORD_SIZE);

			session->SetExtended(true);
			session->SetNewClassId(ClassTable::IsValid(p->classId) ? p->classId : 0);
			GDBThread->RequestLogin(session, name, password);
			break;
		}
		case PacketType::USER_MOVE_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->GetStat()->IsDead())
				break;
			if (client->IsTransferring())  // Zone Transfer 완료 전 — 이동 패킷 무시
				break;

			auto* p = reinterpret_cast<const USER_MOVE_REQ_PACKET*>(packet);
			if (p->direction > 7)
				break;

			// 이동 쿨타임: 기본 500ms, 바람걸음 물약(가속) 중에는 더 짧다.
			const uint32_t now = GetNowTime();
			if (now > client->m_lastMoveTime + client->GetMoveCooldown(now))
			{
				const uint32_t sinceLast = now - client->m_lastMoveTime;
				client->SetLastStepMs(sinceLast < 1000u ? sinceLast : 1000u);
				client->m_lastMoveTime = now;

				short x = client->GetX();
				short y = client->GetY();
				SubjectHelper::MovePositionByDirection(x, y, p->direction);
				GuardTownMove("step", client->GetX(), client->GetY(), x, y);   // Phase 4-A 층 규칙 덫(마을 밖이면 바로 통과)

				// Update horizontal facing: left-component dirs=2,4,6 → left; right=3,5,7 → right
				const int dir = p->direction;
				if (dir == 2 || dir == 4 || dir == 6) client->SetFacingLeft(true);
				else if (dir == 3 || dir == 5 || dir == 7) client->SetFacingLeft(false);
				// dir 0(up) / 1(down): no change to horizontal facing

				SectorHelper::HandlePlayerMove(client, x, y);
			}
			break;
		}
		case PacketType::USER_MOVE_EX_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			auto* p = reinterpret_cast<const USER_MOVE_EX_REQ_PACKET*>(packet);
			UserHelper::HandleMoveEx(client, p->dir, p->seq);
			break;
		}
		case PacketType::SKILL_CAST_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			SkillHelper::HandleCast(client, *reinterpret_cast<const SKILL_CAST_REQ_PACKET*>(packet));
			break;
		}
		case PacketType::SKILL_EX_REQ:
		{
			// Phase 2-B 기술 구조 V3 — 확장 세션만(봇 · 레거시 경로에는 없다)
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			SkillEx::HandleRequest(client, *reinterpret_cast<const SKILL_EX_REQ_PACKET*>(packet));
			break;
		}
		case PacketType::SKILL_GROW_REQ:
		{
			// Phase 2-D 기술 성장 — 확장 세션만
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			SkillGrow::HandleRequest(client, *reinterpret_cast<const SKILL_GROW_REQ_PACKET*>(packet));
			break;
		}
		case PacketType::STAT_ALLOC_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			auto* p = reinterpret_cast<const STAT_ALLOC_REQ_PACKET*>(packet);
			CombatHelper::HandleStatAlloc(client, p->stat, p->amount);
			break;
		}
		case PacketType::USER_ATTACK_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->GetStat()->IsDead())
				break;
			if (client->IsTransferring())  // Zone Transfer 완료 전 — 공격 패킷 무시
				break;

			auto* p = reinterpret_cast<const USER_ATTACK_REQ_PACKET*>(packet);
			const uint32_t now = GetNowTime();
			if (now > client->m_lastAttackTime + 500)
			{
				client->m_lastAttackTime = now;

				// Sync facing from client so CanAttack uses the correct direction
				client->SetFacingLeft(p->facing == 1);

				// 단일 순회: 주변 플레이어에게 공격 애니메이션 알림 + 몬스터 공격 동시 처리
				UserHelper::HandleAttack(client, p->facing);
			}
			break;
		}
		case PacketType::USER_SKILL_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->GetStat()->IsDead())
				break;
			if (client->IsTransferring())  // Zone Transfer 완료 전 — 스킬 패킷 무시
				break;

			const uint32_t now = GetNowTime();
			if (now > client->m_lastSkillTime + 5000)
			{
				client->m_lastSkillTime = now;
				ObjID clientId = client->GetObjID();
				UserHelper::SkillAttack(clientId);
			}
			break;
		}
		case PacketType::USER_TELEPORT_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->GetStat()->IsDead())
				break;
			if (client->IsTransferring())
				break;

			auto* p = reinterpret_cast<const USER_TELEPORT_REQ_PACKET*>(packet);
			if (!ZoneLayout::IsValidWorldPosition(p->x, p->y))
				break;

			SectorHelper::HandlePlayerMove(client, p->x, p->y);
			break;
		}
		case PacketType::ITEM_EQUIP_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			if (client->IsTransferring())  // Zone Transfer 완료 전 — 인벤토리 조작 패킷 무시
				break;

			auto* p = reinterpret_cast<const ITEM_EQUIP_REQ_PACKET*>(packet);
			// MAX_INVENTORY_SLOTS는 절대 유효한 슬롯 인덱스가 될 수 없으므로
			// "이전에 장착 중이던 슬롯 없음"의 sentinel로 쓴다.
			uint16_t previousSlot = MAX_INVENTORY_SLOTS;
			const bool success = client->GetInventory()->TryEquip(p->slotIndex, &previousSlot);
			UserHelper::SendITEM_EQUIP_ACK(client, p->slotIndex, success);
			if (success)
			{
				UserHelper::BroadcastEquipChange(client);
				// 이전에 장착 중이던 다른 장비가 자동으로 탈착됐다면, 그 슬롯도
				// 바뀌었다고 알려야 클라이언트 인벤토리 UI의 장착 테두리가 갱신된다.
				if (previousSlot < MAX_INVENTORY_SLOTS)
					UserHelper::SendITEM_ACQUIRE_INF(client, previousSlot);
			}
			break;
		}
		case PacketType::ITEM_UNEQUIP_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			if (client->IsTransferring())
				break;

			auto* p = reinterpret_cast<const ITEM_UNEQUIP_REQ_PACKET*>(packet);
			const bool success = client->GetInventory()->TryUnequip(p->slotIndex);
			UserHelper::SendITEM_UNEQUIP_ACK(client, p->slotIndex, success);
			if (success)
				UserHelper::BroadcastEquipChange(client);
			break;
		}
		case PacketType::ITEM_SWAP_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			if (client->IsTransferring())
				break;

			auto* p = reinterpret_cast<const ITEM_SWAP_REQ_PACKET*>(packet);
			const bool success = client->GetInventory()->TrySwapSlots(p->slotIndexA, p->slotIndexB);
			UserHelper::SendITEM_SWAP_ACK(client, p->slotIndexA, p->slotIndexB, success);
			break;
		}
		case PacketType::ITEM_DISCARD_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			if (client->IsTransferring())  // Zone Transfer 완료 전 — 위치가 불안정한 동안 필드에 놓지 않는다
				break;

			auto* p = reinterpret_cast<const ITEM_DISCARD_REQ_PACKET*>(packet);
			Item::SharedPtr extracted = client->GetInventory()->TryExtractItem(p->slotIndex, p->count);
			if (extracted != nullptr)
			{
				ItemHelper::SpawnFieldItem(extracted, client->GetX(), client->GetY());
				// 방금 버린 게 장착 중이던 아이템이었을 수 있다(TryExtractItem이 이미
				// 탈착까지 해뒀다) - 항상 현재 상태를 다시 계산해서 알린다.
				UserHelper::BroadcastEquipChange(client);
			}

			UserHelper::SendITEM_DISCARD_ACK(client, p->slotIndex, extracted != nullptr);
			break;
		}
		case PacketType::ITEM_PICKUP_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			if (client->IsTransferring())
				break;

			ItemHelper::TryPickupAt(client);
			break;
		}
		case PacketType::ITEM_USE_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->GetStat()->IsDead())
				break;
			if (client->IsTransferring())
				break;

			auto* p = reinterpret_cast<const ITEM_USE_REQ_PACKET*>(packet);
			const uint32_t now = GetNowTime();

			// 회복 물약이 아닌 소비 아이템(두루마리/바람걸음 물약)은 효과가 인벤토리 밖에서
			// 일어나므로 별도 경로로 보낸다. 쿨타임도 물약(5초)과 따로 잰다.
			ItemEffect effect = ItemEffect::eNone;
			{
				const auto& slots = client->GetInventory()->GetSlots();
				if (const auto it = slots.find(p->slotIndex); it != slots.end())
					if (const ItemTableRow* row = ItemTable::Find(it->second->GetItemTableId()))
						effect = row->effect;
			}
			if (effect == ItemEffect::eReturnTown || effect == ItemEffect::eTeleportRandom || effect == ItemEffect::eHaste)
			{
				if (now > client->m_lastScrollUseTime + CombatRule::kScrollCooldownMs)
				{
					client->m_lastScrollUseTime = now;
					ContentHelper::UseEffectItem(client, p->slotIndex);
				}
				else
				{
					UserHelper::SendSystemEx(client, SystemMessageCode::ItemCooldown,
						static_cast<int32_t>(client->m_lastScrollUseTime + CombatRule::kScrollCooldownMs - now));
				}
				break;
			}

			// 회복 물약이 아니면(빈 슬롯/장비/각인 두루마리 등) 쿨타임을 소모하지 않고 바로 실패
			if (effect != ItemEffect::eHeal)
			{
				UserHelper::SendITEM_USE_ACK(client, p->slotIndex, false);
				break;
			}

			if (now > client->m_lastPotionUseTime + CombatRule::kPotionCooldownMs)
			{
				client->m_lastPotionUseTime = now;

				const uint16_t before = client->GetStat()->GetHp();
				const bool success = client->GetInventory()->TryUseItem(p->slotIndex);
				if (success)
				{
					UserHelper::SendUSER_HEAL_INF(client);
					UserHelper::BroadcastEffect(client, client->GetObjID(), EffectId::PotionHeal, client->GetX(), client->GetY(),
						static_cast<int32_t>(client->GetStat()->GetHp() - before));
				}

				UserHelper::SendITEM_USE_ACK(client, p->slotIndex, success);
			}
			else
			{
				UserHelper::SendSystemEx(client, SystemMessageCode::ItemCooldown,
					static_cast<int32_t>(client->m_lastPotionUseTime + CombatRule::kPotionCooldownMs - now));
			}
			break;
		}
		// ── 확장 프로토콜 요청(USER_LOGIN_EX_REQ로 로그인한 세션만) ─────────────────
		case PacketType::USER_TARGET_ATTACK_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->GetStat()->IsDead() || client->IsTransferring())
				break;

			// 쿨타임은 레거시 평타와 같은 값/같은 타임스탬프를 공유한다(둘을 섞어 써서
			// 공격 속도를 두 배로 만드는 것을 막는다).
			auto* p = reinterpret_cast<const USER_TARGET_ATTACK_REQ_PACKET*>(packet);
			const uint32_t now = GetNowTime();
			if (now > client->m_lastAttackTime + CombatRule::kAttackCooldownMs)
			{
				client->m_lastAttackTime = now;
				ContentHelper::HandleTargetAttack(client, p->targetId);
			}
			break;
		}
		case PacketType::USER_RESTART_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->IsTransferring())
				break;
			ContentHelper::HandleRestartRequest(client);
			break;
		}
		case PacketType::ITEM_ENCHANT_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->GetStat()->IsDead() || client->IsTransferring())
				break;

			auto* p = reinterpret_cast<const ITEM_ENCHANT_REQ_PACKET*>(packet);
			const uint32_t now = GetNowTime();
			if (now > client->m_lastScrollUseTime + CombatRule::kScrollCooldownMs)
			{
				client->m_lastScrollUseTime = now;
				ContentHelper::HandleEnchant(client, p->scrollSlot, p->targetSlot);
			}
			break;
		}
		case PacketType::SHOP_BUY_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			auto* p = reinterpret_cast<const SHOP_BUY_REQ_PACKET*>(packet);
			ContentHelper::HandleShopBuy(client, p->npcId, p->itemId, p->count, p->seq);
			break;
		}
		case PacketType::SHOP_SELL_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			auto* p = reinterpret_cast<const SHOP_SELL_REQ_PACKET*>(packet);
			ContentHelper::HandleShopSell(client, p->npcId, p->slotIndex, p->count, p->seq);
			break;
		}
		case PacketType::NPC_TELEPORT_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			auto* p = reinterpret_cast<const NPC_TELEPORT_REQ_PACKET*>(packet);
			ContentHelper::HandleNpcTeleport(client, p->npcId, p->destIndex, p->seq);
			break;
		}
		case PacketType::NPC_TALK_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			NpcHelper::HandleTalk(client, reinterpret_cast<const NPC_TALK_REQ_PACKET*>(packet)->npcId);
			break;
		}
		case PacketType::NPC_SERVICE_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			auto* p = reinterpret_cast<const NPC_SERVICE_REQ_PACKET*>(packet);
			NpcHelper::HandleService(client, p->npcId, p->service);
			break;
		}
		case PacketType::ITEM_SORT_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->IsTransferring())
				break;
			UserHelper::HandleItemSort(client);
			break;
		}
		case PacketType::QUEST_ACCEPT_REQ:
		case PacketType::QUEST_COMPLETE_REQ:
		case PacketType::QUEST_ABANDON_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr)
				break;
			// 세 요청의 본문은 같은 모양(questId 하나)이다.
			const uint16_t questId = reinterpret_cast<const QUEST_ACCEPT_REQ_PACKET*>(packet)->questId;
			switch (static_cast<PacketType>(static_cast<uint8_t>(packet[2])))
			{
			case PacketType::QUEST_ACCEPT_REQ:   QuestHelper::HandleAccept(client, questId); break;
			case PacketType::QUEST_COMPLETE_REQ: QuestHelper::HandleComplete(client, questId); break;
			default:                             QuestHelper::HandleAbandon(client, questId); break;
			}
			break;
		}
		case PacketType::CS_CHAT:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr) break;

			auto* p = reinterpret_cast<const CS_CHAT_PACKET*>(packet);
			char mess[CHAT_SIZE + 1]{};
			::strncpy_s(mess, p->mess, CHAT_SIZE);
			UserHelper::BroadcastChat(client, mess);
			break;
		}
		case PacketType::CS_CHAT_EX:
		{
			// Phase 3-B 채널 채팅 — 확장 세션만. 검사 · 범위는 Content/ChatHelper
			if (session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				break;
			auto client = session->GetOwner();
			if (client == nullptr) break;
			auto* p = reinterpret_cast<const CS_CHAT_EX_PACKET*>(packet);
			ChatHelper::HandleChatEx(client, p->channel, p->targetName, p->mess);
			break;
		}
		}
	}
}
