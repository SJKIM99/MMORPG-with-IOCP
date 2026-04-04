#include "pch.h"
#include "UserHelper.h"
#include "Subject.h"
#include "User.h"
#include "DBThread.h"

namespace
{
	template<typename Packet>
	void InitializePacket(Packet& packet, PacketType type)
	{
		packet = {};
		packet.size = sizeof(Packet);
		packet.type = static_cast<char>(type);
	}
}

namespace UserHelper
{
	void SendMovePacket(Subject& receiver, uint32 clientId)
	{
		const Subject& target = *(*GObjectManager)[clientId];

		SC_MOVE_OBJECT_PACKET packet;
		InitializePacket(packet, PacketType::SC_MOVE_OBJECT);
		packet.id        = clientId;
		packet.x         = target._transform.GetX();
		packet.y         = target._transform.GetY();
		packet.move_time = target._lastMoveTime;

		receiver.PostSend(packet);
	}

	void SendAddPlayerPacket(Subject& receiver, uint32 clientId)
	{
		receiver._viewList.insert(clientId);

		const Subject& target = *(*GObjectManager)[clientId];

		SC_ADD_OBJECT_PACKET packet;
		InitializePacket(packet, PacketType::SC_ADD_OBJECT);
		if (IsNPC(clientId))
			packet.monster_type = static_cast<char>(AsMonster(clientId)->GetType());
		packet.id = clientId;
		packet.x  = target._transform.GetX();
		packet.y  = target._transform.GetY();
		::strcpy_s(packet.name, target._name);

		receiver.PostSend(packet);
	}

	void SendRemovePlayerPacket(Subject& receiver, uint32 clientId)
	{
		if (receiver._viewList.erase(clientId) == 0)
			return;

		SC_REMOVE_OBJECT_PACKET packet;
		InitializePacket(packet, PacketType::SC_REMOVE_OBJECT);
		packet.id = clientId;

		receiver.PostSend(packet);
	}

	void SendLoginSuccessPacket(Subject& receiver)
	{
		SC_LOGIN_SUCCESS_PACKET packet;
		InitializePacket(packet, PacketType::SC_LOGIN_SUCCESS);
		packet.id    = receiver._id;
		packet.x     = receiver._transform.GetX();
		packet.y     = receiver._transform.GetY();
		packet.maxhp = receiver._stat.GetMaxHp();
		packet.hp    = receiver._stat.GetHp();

		receiver.PostSend(packet);
	}

	void SendLoginFailPacket(Subject& receiver)
	{
		SC_LOGIN_FAIL_PACKET packet;
		InitializePacket(packet, PacketType::SC_LOGIN_FAIL);

		receiver.PostSend(packet);
	}

	void SendPlayerAttackToNpcPacket(Subject& receiver, uint32 clientId)
	{
		SC_PLAYER_ATTACK_NPC_PACKET packet;
		InitializePacket(packet, PacketType::SC_PLAYER_ATTACK_NPC);
		packet.id = clientId;
		packet.hp = (*GObjectManager)[clientId]->_stat.GetHp();

		receiver.PostSend(packet);
	}

	void SendNpcDiePacket(Subject& receiver, uint32 clientId)
	{
		SC_NPC_DIE_PACKET packet;
		InitializePacket(packet, PacketType::SC_NPC_DIE);
		packet.npc_id = clientId;

		receiver.PostSend(packet);
	}

	void SendRespawnNpcPacket(Subject& receiver, uint32 clientId)
	{
		const Subject& target = *(*GObjectManager)[clientId];

		SC_NPC_RESPAWN_PACKET packet;
		InitializePacket(packet, PacketType::SC_NPC_RESPAWN);
		packet.npc_id = clientId;
		packet.x      = target._transform.GetX();
		packet.y      = target._transform.GetY();

		receiver.PostSend(packet);
	}

	void SendNpcAttackToPlayerPacket(Subject& receiver)
	{
		SC_NPC_ATTACK_PLAYER_PACKET packet;
		InitializePacket(packet, PacketType::SC_NPC_ATTACK_PLAYER);
		packet.hp = receiver._stat.GetHp();

		receiver.PostSend(packet);
	}

	void SendHealPacket(Subject& receiver)
	{
		SC_HEAL_PACKET packet;
		InitializePacket(packet, PacketType::SC_HEAL);
		packet.hp = receiver._stat.GetHp();

		receiver.PostSend(packet);
	}

	void SendPlayerDiePacket(Subject& receiver, uint32 clientId)
	{
		if (receiver._viewList.erase(clientId) == 0)
			return;

		SC_PLAYER_DIE_PACKET packet;
		InitializePacket(packet, PacketType::SC_PLAYER_DIE);
		packet.id = clientId;
		packet.hp = (*GObjectManager)[clientId]->_stat.GetHp();

		receiver.PostSend(packet);
	}

	void SendRespawnPlayerPacket(Subject& receiver, uint32 clientId)
	{
		receiver._viewList.insert(clientId);

		const Subject& target = *(*GObjectManager)[clientId];

		SC_PLAYER_RESPAWN_PACKET packet;
		InitializePacket(packet, PacketType::SC_PLAYER_RESPAWN);
		packet.id = clientId;
		packet.x  = target._transform.GetX();
		packet.y  = target._transform.GetY();
		packet.hp = target._stat.GetHp();

		receiver.PostSend(packet);
	}

	bool FlushPlayerSave(uint32 clientId)
	{
		if (!IsPc(clientId))
			return false;

		DB_PLAYER_INFO info{};
		if (AsUser(clientId)->TryBuildSaveInfo(info) == false)
			return false;

		GDBThread->RequestSavePlayer(clientId, info);
		return true;
	}
}
