#include "pch.h"
#include "Route.h"
#include "GameSession.h"
#include "User.h"
#include "DBThread.h"
#include "WorldHelper.h"
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
			if (session->_state != SOCKET_STATE::ST_ALLOC)
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
			if (session->_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->GetStat()->IsDead())
				break;

			auto* p = reinterpret_cast<const CS_MOVE_PACKET*>(packet);
			if (p->direction > 3)
				break;

			const uint32 now = WorldHelper::GetNowTime();
			if (now > client->_lastMoveTime + 1000)
			{
				client->_lastMoveTime = now;

				short x = client->GetX();
				short y = client->GetY();
				WorldHelper::MovePositionByDirection(x, y, p->direction);

				ObjID clientId = client->GetObjID();
				WorldHelper::UpdateObjectPosition(clientId, x, y);
				UserHelper::SendMovePacket(client, clientId);
				WorldHelper::UpdatePlayerViewList(clientId);
			}
			break;
		}
		case PacketType::CS_ATTACK:
		{
			if (session->_state != SOCKET_STATE::ST_INGAME)
				break;
			auto client = session->GetOwner();
			if (client == nullptr || client->GetStat()->IsDead())
				break;

			auto* p = reinterpret_cast<const CS_ATTACK_PACKET*>(packet);
			const uint32 now = WorldHelper::GetNowTime();
			if (now > client->_lastAttackTime + 1000)
			{
				client->_lastAttackTime = p->attack_time;

				ObjID clientId = client->GetObjID();
				GSector->ForEachNeighborObject(client->GetSectorX(), client->GetSectorY(),
					[&](const shared_ptr<Subject>& object)
				{
					ObjID id = object->GetObjID();
					if (id.GetCategory<EnumCategory>() != EnumCategory::eMonster)
						return;
					if (WorldHelper::CanAttack(clientId, id))
						WorldHelper::AttackNpc(id, clientId);
				});
			}
			break;
		}
		}
	}
}
