#include "pch.h"
#include "UserHelper.h"
#include "GameObjectManager.h"
#include "Subject.h"
#include "User.h"
#include "Monster.h"
#include "DBThread.h"
#include "Sector.h"
#include "TimerThread.h"
#include "WorldHelper.h"

namespace
{
	template<typename Packet>
	void InitializePacket(Packet& packet, PacketType type)
	{
		packet = {};
		packet.size = sizeof(Packet);
		packet.type = static_cast<char>(type);
	}

	shared_ptr<GameSession> GetSession(const Subject::SharedPtr& sender)
	{
		auto user = static_pointer_cast<User>(sender);
		if (!user)
			return nullptr;
		return user->GetGameSession();
	}
}

namespace UserHelper
{
	void SendMovePacket(Subject::SharedPtr sender, const ObjID& targetId)
	{
		const auto target = GGameObjectManager->Seek<Subject>(targetId);
		if (target == nullptr)
			return;

		auto session = GetSession(sender);
		if (!session)
			return;

		SC_MOVE_OBJECT_PACKET packet;
		InitializePacket(packet, PacketType::SC_MOVE_OBJECT);
		packet.id = targetId;
		packet.x = target->GetX();
		packet.y = target->GetY();

		session->PostSend(packet);
	}

	void SendAddPlayerPacket(Subject::SharedPtr sender, const ObjID& targetId)
	{
		const auto target = GGameObjectManager->Seek<Subject>(targetId);
		if (target == nullptr)
			return;

		auto session = GetSession(sender);
		if (!session)
			return;

		SC_ADD_OBJECT_PACKET packet;
		InitializePacket(packet, PacketType::SC_ADD_OBJECT);
		if (const auto monster = GGameObjectManager->Seek<Monster>(targetId); monster != nullptr)
			packet.monster_type = static_cast<char>(monster->GetType());
		packet.id = targetId;
		packet.x = target->GetX();
		packet.y = target->GetY();

		session->PostSend(packet);
	}

	void SendRemovePlayerPacket(Subject::SharedPtr sender, const ObjID& targetId)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		SC_REMOVE_OBJECT_PACKET packet;
		InitializePacket(packet, PacketType::SC_REMOVE_OBJECT);
		packet.id = targetId;

		session->PostSend(packet);
	}

	void SendLoginSuccessPacket(Subject::SharedPtr sender)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		SC_LOGIN_SUCCESS_PACKET packet;
		InitializePacket(packet, PacketType::SC_LOGIN_SUCCESS);
		packet.id    = sender->GetObjID();
		packet.x     = sender->GetX();
		packet.y     = sender->GetY();
		packet.maxhp = sender->GetStat()->GetMaxHp();
		packet.hp    = sender->GetStat()->GetHp();
		packet.level = sender->GetStat()->GetLevel();
		packet.exp   = sender->GetStat()->GetExp();

		session->PostSend(packet);
	}

	void SendLoginFailPacket(Subject::SharedPtr sender)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		SC_LOGIN_FAIL_PACKET packet;
		InitializePacket(packet, PacketType::SC_LOGIN_FAIL);

		session->PostSend(packet);
	}

	void SendPlayerAttackToNpcPacket(Subject::SharedPtr sender, const ObjID& targetId)
	{
		const auto target = GGameObjectManager->Seek<User>(targetId);
		if (target == nullptr)
			return;

		auto session = GetSession(sender);
		if (!session)
			return;

		SC_PLAYER_ATTACK_NPC_PACKET packet;
		InitializePacket(packet, PacketType::SC_PLAYER_ATTACK_NPC);
		packet.id = targetId;
		packet.hp = target->GetStat()->GetHp();

		session->PostSend(packet);
	}

	void SendNpcDiePacket(Subject::SharedPtr sender, const ObjID& targetId)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		SC_NPC_DIE_PACKET packet;
		InitializePacket(packet, PacketType::SC_NPC_DIE);
		packet.npc_id = targetId;

		session->PostSend(packet);
	}

	void SendRespawnNpcPacket(Subject::SharedPtr sender, const ObjID& targetId)
	{
		const auto target = GGameObjectManager->Seek<User>(targetId);
		if (target == nullptr)
			return;

		auto session = GetSession(sender);
		if (!session)
			return;

		SC_NPC_RESPAWN_PACKET packet;
		InitializePacket(packet, PacketType::SC_NPC_RESPAWN);
		packet.npc_id = targetId;
		packet.x = target->GetX();
		packet.y = target->GetY();

		session->PostSend(packet);
	}

	void SendNpcAttackToPlayerPacket(Subject::SharedPtr sender)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		SC_NPC_ATTACK_PLAYER_PACKET packet;
		InitializePacket(packet, PacketType::SC_NPC_ATTACK_PLAYER);
		packet.hp = sender->GetStat()->GetHp();

		session->PostSend(packet);
	}

	void SendHealPacket(Subject::SharedPtr sender)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		SC_HEAL_PACKET packet;
		InitializePacket(packet, PacketType::SC_HEAL);
		packet.hp = sender->GetStat()->GetHp();

		session->PostSend(packet);
	}

	void SendPlayerDiePacket(Subject::SharedPtr sender, const ObjID& targetId)
	{
		const auto target = GGameObjectManager->Seek<User>(targetId);
		if (target == nullptr)
			return;

		auto session = GetSession(sender);
		if (!session)
			return;

		SC_PLAYER_DIE_PACKET packet;
		InitializePacket(packet, PacketType::SC_PLAYER_DIE);
		packet.id = targetId;
		packet.hp = target->GetStat()->GetHp();

		session->PostSend(packet);
	}

	void SendStatChangePacket(Subject::SharedPtr sender)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		SC_STAT_CHANGE_PACKET packet;
		InitializePacket(packet, PacketType::SC_STAT_CHANGE);
		packet.level = sender->GetStat()->GetLevel();
		packet.hp    = sender->GetStat()->GetHp();
		packet.maxhp = sender->GetStat()->GetMaxHp();
		packet.exp   = sender->GetStat()->GetExp();

		session->PostSend(packet);
	}

	void SendRespawnPlayerPacket(Subject::SharedPtr sender, const ObjID& targetId)
	{
		const auto target = GGameObjectManager->Seek<User>(targetId);
		if (target == nullptr)
			return;

		auto session = GetSession(sender);
		if (!session)
			return;

		SC_PLAYER_RESPAWN_PACKET packet;
		InitializePacket(packet, PacketType::SC_PLAYER_RESPAWN);
		packet.id = targetId;
		packet.x = target->GetX();
		packet.y = target->GetY();
		packet.hp = target->GetStat()->GetHp();

		session->PostSend(packet);
	}

	bool FlushPlayerSave(const ObjID& targetId)
	{
		const auto target = GGameObjectManager->Seek<User>(targetId);
		if (target == nullptr)
			return false;

		DB_USER_INFO info{};
		info._name  = target->GetName();
		info._x     = target->GetX();
		info._y     = target->GetY();
		info._level = target->GetStat()->GetLevel();
		info._exp   = target->GetStat()->GetExp();

		GDBThread->RequestSaveUser(targetId, info);
		return true;
	}

	void HandleHeal(const ObjID& playerId)
	{
		auto player = GGameObjectManager->Seek<User>(playerId);
		if (player == nullptr)
			return;

		auto session = player->GetGameSession();
		if (!session || session->_state != SOCKET_STATE::ST_INGAME)
			return;

		if (player->GetStat()->IsDead())
			return;

		player->GetStat()->HealHp(HEAL_SIZE, PLAYER_MAX_HP);
		SendHealPacket(player);
		GTimerThread->ScheduleAfter(playerId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
	}

	void HandleRespawn(const ObjID& playerId)
	{
		auto player = GGameObjectManager->Seek<User>(playerId);
		if (player == nullptr)
			return;

		WorldHelper::PlaceObjectAtRandomWalkablePosition(const_cast<ObjID&>(playerId));
		player->GetStat()->SetDead(false);
		player->GetStat()->SetHp(PLAYER_MAX_HP);

		WorldHelper::NotifyPlayerEnteredWorld(const_cast<ObjID&>(playerId), true);
		GTimerThread->ScheduleAfter(playerId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
	}

	void HandleLoginFail(const shared_ptr<GameSession>& session)
	{
		if (session == nullptr)
			return;

		SC_LOGIN_FAIL_PACKET packet;
		InitializePacket(packet, PacketType::SC_LOGIN_FAIL);
		session->PostSend(packet);
	}

	void HandleGetUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo)
	{
		if (session == nullptr || session->_state != SOCKET_STATE::ST_ALLOC)
			return;

		auto player = std::make_shared<User>();
		player->SetObjID(EnumCategory::eUser, static_cast<uint64_t>(session->_objectId));
		player->InitInstance();
		player->SetName(userInfo._name);
		player->GetStat()->SetLevel(userInfo._level);
		player->GetStat()->SetExp(userInfo._exp);

		if (!GGameObjectManager->Insert(player->GetObjID(), player))
			return;

		session->BindOwner(player);
		player->SetGameSession(session);
		session->_state = SOCKET_STATE::ST_INGAME;

		ObjID objId = player->GetObjID();
		WorldHelper::UpdateObjectPosition(objId, static_cast<short>(userInfo._x), static_cast<short>(userInfo._y));

		SendLoginSuccessPacket(player);
		GTimerThread->ScheduleAfter(objId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
		WorldHelper::NotifyPlayerEnteredWorld(objId, false);
	}

	void HandleAddUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo)
	{
		if (session == nullptr || session->_state != SOCKET_STATE::ST_ALLOC)
			return;

		auto player = std::make_shared<User>();
		player->SetObjID(EnumCategory::eUser, static_cast<uint64_t>(session->_objectId));
		player->InitInstance();
		player->SetName(userInfo._name);

		if (!GGameObjectManager->Insert(player->GetObjID(), player))
			return;

		session->BindOwner(player);
		player->SetGameSession(session);
		session->_state = SOCKET_STATE::ST_INGAME;

		ObjID objId = player->GetObjID();
		WorldHelper::PlaceObjectAtRandomWalkablePosition(objId);

		SendLoginSuccessPacket(player);

		DB_USER_INFO save{};
		save._name     = player->GetName();
		save._password = userInfo._password;
		save._x        = player->GetX();
		save._y        = player->GetY();
		GDBThread->RequestAddUser(player->GetObjID(), save);

		GTimerThread->ScheduleAfter(objId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
		WorldHelper::NotifyPlayerEnteredWorld(objId, false);
	}
}
