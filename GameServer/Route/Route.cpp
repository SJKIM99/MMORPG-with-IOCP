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

namespace Route
{
	void Dispatch(const shared_ptr<GameSession>& session, const char* packet)
	{
		switch (static_cast<PacketType>(packet[2]))
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
			if (client->IsTransferring())  // Zone Transfer 완료 전 — 이동 패킷 무시
				break;

			auto* p = reinterpret_cast<const USER_MOVE_REQ_PACKET*>(packet);
			if (p->direction > 7)
				break;

			const uint32_t now = GetNowTime();
			if (now > client->m_lastMoveTime + 500)
			{
				client->m_lastMoveTime = now;

				short x = client->GetX();
				short y = client->GetY();
				SubjectHelper::MovePositionByDirection(x, y, p->direction);

				// Update horizontal facing: left-component dirs=2,4,6 → left; right=3,5,7 → right
				const int dir = p->direction;
				if (dir == 2 || dir == 4 || dir == 6) client->SetFacingLeft(true);
				else if (dir == 3 || dir == 5 || dir == 7) client->SetFacingLeft(false);
				// dir 0(up) / 1(down): no change to horizontal facing

				SectorHelper::HandlePlayerMove(client, x, y);
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
			if (client->IsTransferring())  // Zone Transfer 완료 전 — 공격 패킷 무시
				break;

			auto* p = reinterpret_cast<const USER_ATTACK_REQ_PACKET*>(packet);
			const uint32_t now = GetNowTime();
			if (now > client->m_lastAttackTime + 500)
			{
				client->m_lastAttackTime = p->attack_time;

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
