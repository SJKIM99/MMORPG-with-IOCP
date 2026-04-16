#include "pch.h"
#include "UserHelper.h"
#include "GameObjectManager.h"
#include "Subject.h"
#include "User.h"
#include "Monster.h"
#include "DBThread.h"
#include "Sector.h"
#include "TimerThread.h"
#include "SubjectHelper.h"
#include "SectorHelper.h"

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

	void SendPlayerAttackToMonsterPacket(Subject::SharedPtr sender, const ObjID& targetId, int32_t damage)
	{
		const auto target = GGameObjectManager->Seek<Subject>(targetId);
		if (target == nullptr)
			return;

		auto session = GetSession(sender);
		if (!session)
			return;

		SC_PLAYER_ATTACK_MONSTER_PACKET packet;
		InitializePacket(packet, PacketType::SC_PLAYER_ATTACK_MONSTER);
		packet.id     = targetId;
		packet.hp     = target->GetStat()->GetHp();
		packet.damage = damage;

		session->PostSend(packet);
	}

	void SendMonsterDiePacket(Subject::SharedPtr sender, const ObjID& targetId)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		SC_MONSTER_DIE_PACKET packet;
		InitializePacket(packet, PacketType::SC_MONSTER_DIE);
		packet.monster_id = targetId;

		session->PostSend(packet);
	}

	void SendRespawnMonsterPacket(Subject::SharedPtr sender, const ObjID& targetId)
	{
		const auto target = GGameObjectManager->Seek<Subject>(targetId);
		if (target == nullptr)
			return;

		auto session = GetSession(sender);
		if (!session)
			return;

		SC_MONSTER_RESPAWN_PACKET packet;
		InitializePacket(packet, PacketType::SC_MONSTER_RESPAWN);
		packet.monster_id = targetId;
		packet.x = target->GetX();
		packet.y = target->GetY();

		session->PostSend(packet);
	}

	void SendMonsterAttackToPlayerPacket(Subject::SharedPtr sender, const ObjID& monsterId)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		SC_MONSTER_ATTACK_PLAYER_PACKET packet;
		InitializePacket(packet, PacketType::SC_MONSTER_ATTACK_PLAYER);
		packet.monster_id = monsterId;
		packet.hp         = sender->GetStat()->GetHp();

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

	void AttackMonster(ObjID& monsterId, ObjID& playerId, int damage)
	{
		auto monster = GGameObjectManager->Seek<Monster>(monsterId);
		auto attacker = GGameObjectManager->Seek<User>(playerId);
		if (monster == nullptr || attacker == nullptr)
			return;
		if (monster->GetStat()->IsDead())
			return;

		const uint16_t remaining = monster->GetStat()->TakeDamage(static_cast<uint16_t>(damage));
		if (remaining > 0)
		{
			SendPlayerAttackToMonsterPacket(attacker, monsterId, damage);
			return;
		}

		GSector->ForEachNeighborObject(monster->GetSectorX(), monster->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;

			auto viewer = GGameObjectManager->Seek<User>(id);
			if (viewer == nullptr) return;
			auto session = viewer->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;

			if (SubjectHelper::CanSee(id, monsterId))
				SendMonsterDiePacket(viewer, monsterId);
		});

		GSector->RemoveObject(monsterId, monster->RefSectorX(), monster->RefSectorY());
		monster->SetActive(false);
		monster->SetAttack(false);

		const uint32_t expGain = (monster->GetType() == MONSTER_TYPE::PASSIVE) ? 3 : 5;
		attacker->GetStat()->AddExp(expGain);
		SendStatChangePacket(attacker);

		GTimerThread->ScheduleAfter(monsterId, 10s, TIMER_EVENT_TYPE::EV_MONSTER_RESPAWN);
	}

	void SkillAttack(ObjID& playerId)
	{
		auto player = GGameObjectManager->Seek<User>(playerId);
		if (player == nullptr || player->GetStat()->IsDead())
			return;

		const int px = player->GetX();
		const int py = player->GetY();

		// Collect IDs of monsters on the 4 adjacent cardinal tiles first,
		// then attack them — avoids mutating the sector while iterating.
		std::vector<ObjID> targets;
		GSector->ForEachNeighborObject(player->GetSectorX(), player->GetSectorY(),
			[&](const shared_ptr<Subject>& object)
		{
			if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eMonster)
				return;
			const int dx = object->GetX() - px;
			const int dy = object->GetY() - py;
			if (abs(dx) + abs(dy) == 1)
				targets.push_back(object->GetObjID());
		});

		for (ObjID& monsterId : targets)
			AttackMonster(monsterId, playerId, SKILL_DAMAGE);
	}

	void HandleHeal(const ObjID& playerId)
	{
		auto player = GGameObjectManager->Seek<User>(playerId);
		if (player == nullptr)
			return;

		auto session = player->GetGameSession();
		if (!session || session->m_state != SOCKET_STATE::ST_INGAME)
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

		SectorHelper::PlaceObjectAtRandomWalkablePosition(const_cast<ObjID&>(playerId));
		player->GetStat()->SetDead(false);
		player->GetStat()->SetHp(PLAYER_MAX_HP);

		// Tell the respawning player their own new position/HP
		SendRespawnPlayerPacket(player, playerId);

		SectorHelper::NotifyPlayerEnteredWorld(const_cast<ObjID&>(playerId), true);
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
		if (session == nullptr || session->m_state != SOCKET_STATE::ST_ALLOC)
			return;

		auto player = std::make_shared<User>();
		player->SetObjID(EnumCategory::eUser, static_cast<uint64_t>(session->m_objectId));
		player->InitInstance();
		player->SetName(userInfo._name);
		player->GetStat()->SetLevel(userInfo._level);
		player->GetStat()->SetExp(userInfo._exp);

		if (!GGameObjectManager->Insert(player->GetObjID(), player))
			return;

		session->BindOwner(player);
		player->SetGameSession(session);
		session->m_state = SOCKET_STATE::ST_INGAME;

		ObjID objId = player->GetObjID();
		SectorHelper::UpdateObjectPosition(objId, static_cast<short>(userInfo._x), static_cast<short>(userInfo._y));

		SendLoginSuccessPacket(player);
		GTimerThread->ScheduleAfter(objId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
		SectorHelper::NotifyPlayerEnteredWorld(objId, false);
	}

	void HandleAddUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo)
	{
		if (session == nullptr || session->m_state != SOCKET_STATE::ST_ALLOC)
			return;

		auto player = std::make_shared<User>();
		player->SetObjID(EnumCategory::eUser, static_cast<uint64_t>(session->m_objectId));
		player->InitInstance();
		player->SetName(userInfo._name);

		if (!GGameObjectManager->Insert(player->GetObjID(), player))
			return;

		session->BindOwner(player);
		player->SetGameSession(session);
		session->m_state = SOCKET_STATE::ST_INGAME;

		ObjID objId = player->GetObjID();
		SectorHelper::PlaceObjectAtRandomWalkablePosition(objId);

		SendLoginSuccessPacket(player);

		DB_USER_INFO save{};
		save._name     = player->GetName();
		save._password = userInfo._password;
		save._x        = player->GetX();
		save._y        = player->GetY();
		GDBThread->RequestAddUser(player->GetObjID(), save);

		GTimerThread->ScheduleAfter(objId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
		SectorHelper::NotifyPlayerEnteredWorld(objId, false);
	}
}
