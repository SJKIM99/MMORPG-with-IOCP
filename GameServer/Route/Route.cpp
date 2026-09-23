#include "pch.h"
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

namespace
{
	// Zone Transfer가 진행 중인 동안에도 처리해도 되는 패킷인가.
	//
	// 이관 중(SectorHelper::HandleZoneTransfer 3번 ~ HandleEnterZone 마지막 줄)에는
	// 플레이어가 어느 Sector 격자에도 속하지 않는다. 그 상태에서 좌표·섹터·인벤토리를
	// 건드리면 격자에 중복 등록되거나, 심하면 이관이 끝나기도 전에 두 번째 이관이
	// 시작되어 "소유 Zone과 등록된 격자가 어긋난" 상태가 남는다.
	//
	// 그래서 판정을 기본 거부로 둔다 — 여기 명시적으로 적힌 것만 통과하고,
	// 새로 추가되는 패킷은 자동으로 막힌다. 반대로(가드를 적어야 막히는 방식) 두면
	// 패킷을 늘릴 때 한 곳만 빠뜨려도 아무도 모르게 뚫린다. 인벤토리 슬롯 범위
	// 검증을 각 핸들러가 아니라 Inventory 안쪽 길목에 모아둔 것과 같은 이유다.
	[[nodiscard]] constexpr bool IsAllowedWhileTransferring(PacketType type) noexcept
	{
		switch (type)
		{
		case PacketType::USER_LOGIN_REQ:
			// 로그인은 아직 월드에 들어오기 전이라 이관 대상 자체가 없다.
			return true;

		case PacketType::CS_CHAT:
			// 채팅은 좌표·섹터·인벤토리를 건드리지 않는다. 이관 중에 막으면
			// 사용자에게는 이유 없이 말이 씹히는 것으로만 보인다.
			return true;

		default:
			return false;
		}
	}
}

namespace Route
{
	void Dispatch(const shared_ptr<GameSession>& session, const char* packet)
	{
		const PacketType packetType = static_cast<PacketType>(packet[2]);

		// 이관 중 차단은 각 case가 아니라 이 길목 한 곳에서만 판정한다(위 주석 참고).
		// 여기서 걸린 요청은 아무 상태도 바꾸지 않고 사라진다 — 부분 처리로 격자가
		// 어긋나는 것보다, 요청 자체가 없던 일이 되는 편이 훨씬 안전하다.
		if (IsAllowedWhileTransferring(packetType) == false)
		{
			const auto owner = session->GetOwner();
			if (owner != nullptr && owner->IsTransferring())
				return;
		}

		switch (packetType)
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
		case PacketType::USER_MOVE_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->GetStat()->IsDead())
				break;
			auto* p = reinterpret_cast<const USER_MOVE_REQ_PACKET*>(packet);
			if (p->direction > 7)
				break;

			const uint32_t now = GetNowTime();
			if (now > client->m_lastMoveTime + 500)
			{
				client->m_lastMoveTime = now;

				float x = client->GetX();
				float z = client->GetZ();
				SubjectHelper::MovePositionByDirection(x, z, p->direction);

				// Update horizontal facing: left-component dirs=2,4,6 → left; right=3,5,7 → right
				const int dir = p->direction;
				if (dir == 2 || dir == 4 || dir == 6) client->SetFacingLeft(true);
				else if (dir == 3 || dir == 5 || dir == 7) client->SetFacingLeft(false);
				// dir 0(up) / 1(down): no change to horizontal facing

				SectorHelper::HandlePlayerMove(client, x, z);
			}
			break;
		}
		case PacketType::USER_ATTACK_REQ:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->GetStat()->IsDead())
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
			auto* p = reinterpret_cast<const ITEM_DISCARD_REQ_PACKET*>(packet);
			Item::SharedPtr extracted = client->GetInventory()->TryExtractItem(p->slotIndex, p->count);
			if (extracted != nullptr)
			{
				ItemHelper::SpawnFieldItem(extracted, ToLegacyTile(client->GetX()), ToLegacyTile(client->GetZ()));
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
			auto* p = reinterpret_cast<const ITEM_USE_REQ_PACKET*>(packet);
			const uint32_t now = GetNowTime();
			if (now > client->m_lastPotionUseTime + 5000)
			{
				client->m_lastPotionUseTime = now;

				const bool success = client->GetInventory()->TryUseItem(p->slotIndex);
				if (success)
					UserHelper::SendUSER_HEAL_INF(client);

				UserHelper::SendITEM_USE_ACK(client, p->slotIndex, success);
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
		}
	}
}
