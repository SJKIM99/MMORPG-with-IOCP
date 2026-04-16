#include "pch.h"
#include "Route.h"
#include "GameSession.h"
#include "User.h"
#include "DBThread.h"
#include "SubjectHelper.h"
#include "SectorHelper.h"
#include "UserHelper.h"
#include "Sector.h"

namespace Route
{
	void Dispatch(const shared_ptr<GameSession>& session, const char* packet)
	{
		switch (static_cast<PacketType>(packet[2]))
		{
		case PacketType::CS_LOGIN:
		{
			if (session->m_state != SOCKET_STATE::ST_ALLOC)
				break;

			auto* p = reinterpret_cast<const CS_LOGIN_PACKET*>(packet);
			char name[NAME_SIZE + 1]{};
			char password[PASSWORD_SIZE + 1]{};
			::strncpy_s(name, p->name, NAME_SIZE);
			::strncpy_s(password, p->password, PASSWORD_SIZE);

			GDBThread->RequestLogin(session, name, password);
			break;
		}
		case PacketType::CS_MOVE:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->GetStat()->IsDead())
				break;

			auto* p = reinterpret_cast<const CS_MOVE_PACKET*>(packet);
			if (p->direction > 7)
				break;

			const uint32_t now = GetNowTime();
			if (now > client->m_lastMoveTime + 250)
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

				ObjID clientId = client->GetObjID();
				SectorHelper::UpdateObjectPosition(clientId, x, y);
				UserHelper::SendMovePacket(client, clientId);
				SectorHelper::UpdatePlayerViewList(clientId);
			}
			break;
		}
		case PacketType::CS_ATTACK:
		{
			if (session->m_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->GetStat()->IsDead())
				break;

			auto* p = reinterpret_cast<const CS_ATTACK_PACKET*>(packet);
			const uint32_t now = GetNowTime();
			if (now > client->m_lastAttackTime + 1000)
			{
				client->m_lastAttackTime = p->attack_time;

				// Sync facing from client so CanAttack uses the correct direction
				client->SetFacingLeft(p->facing == 1);

				ObjID clientId = client->GetObjID();
				GSector->ForEachNeighborObject(client->GetSectorX(), client->GetSectorY(),
					[&](const shared_ptr<Subject>& object)
				{
					ObjID id = object->GetObjID();
					if (id.GetCategory<EnumCategory>() != EnumCategory::eMonster)
						return;
					if (SubjectHelper::CanAttack(clientId, id))
						UserHelper::AttackMonster(id, clientId);
				});
			}
			break;
		}
		case PacketType::CS_SKILL:
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
		}
	}
}
