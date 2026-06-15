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
#include "Collision.h"
#include "CoreTLS.h"

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

	bool IsValidWorldPosition(int x, int y)
	{
		if (x < 0 || x >= W_WIDTH || y < 0 || y >= W_HEIGHT)
			return false;

		return !isCollision(static_cast<short>(x), static_cast<short>(y));
	}

	pair<short, short> FindRandomValidPosition()
	{
		std::uniform_int_distribution<short> distX(0, W_WIDTH - 1);
		std::uniform_int_distribution<short> distY(0, W_HEIGHT - 1);
		while (true)
		{
			const short x = distX(LRng);
			const short y = distY(LRng);
			if (isCollision(x, y))
				continue;

			return { x, y };
		}
	}

	void QueueUserSave(const shared_ptr<User>& user, short saveX, short saveY)
	{
		if (user == nullptr)
			return;

		DB_USER_INFO info{};
		info._name  = user->GetName();
		info._x     = saveX;
		info._y     = saveY;
		info._level = user->GetStat()->GetLevel();
		info._exp   = user->GetStat()->GetExp();

		GDBThread->RequestSaveUser(user->GetObjID(), info);
	}
}

namespace UserHelper
{
	void SendSUBJECT_MOVE_NFY(Subject::SharedPtr sender, const ObjID& targetId)
	{
		const auto target = ::GetGameObject<Subject>(targetId);
		if (target == nullptr)
			return;

		SendSUBJECT_MOVE_NFY(sender, target);
	}

	void SendSUBJECT_MOVE_NFY(Subject::SharedPtr sender, Subject::SharedPtr target)
	{
		if (target == nullptr)
			return;

		auto session = GetSession(sender);
		if (!session)
			return;

		SUBJECT_MOVE_NFY_PACKET packet;
		InitializePacket(packet, PacketType::SUBJECT_MOVE_NFY);
		packet.id = target->GetObjID();
		packet.x = target->GetX();
		packet.y = target->GetY();

		session->PostSend(packet);
	}

	void SendSUBJECT_ADD_NFY(Subject::SharedPtr sender, const ObjID& targetId)
	{
		const auto target = ::GetGameObject<Subject>(targetId);
		if (target == nullptr)
			return;

		SendSUBJECT_ADD_NFY(sender, target);
	}

	void SendSUBJECT_ADD_NFY(Subject::SharedPtr sender, Subject::SharedPtr target)
	{
		if (target == nullptr)
			return;

		auto session = GetSession(sender);
		if (!session)
			return;

		SUBJECT_ADD_NFY_PACKET packet;
		InitializePacket(packet, PacketType::SUBJECT_ADD_NFY);
		if (target->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eMonster)
		{
			const auto monster = static_pointer_cast<Monster>(target);
			packet.monster_type = static_cast<char>(monster->GetType());
		}
		packet.id = target->GetObjID();
		packet.x = target->GetX();
		packet.y = target->GetY();
		::strncpy_s(packet.name, NAME_SIZE, target->GetName().c_str(), _TRUNCATE);

		session->PostSend(packet);
	}

	void SendSUBJECT_REMOVE_NFY(Subject::SharedPtr sender, const ObjID& targetId)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		SUBJECT_REMOVE_NFY_PACKET packet;
		InitializePacket(packet, PacketType::SUBJECT_REMOVE_NFY);
		packet.id = targetId;

		session->PostSend(packet);
	}

	void SendUSER_LOGIN_ACK(Subject::SharedPtr sender)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		USER_LOGIN_ACK_PACKET packet;
		InitializePacket(packet, PacketType::USER_LOGIN_ACK);
		packet.id    = sender->GetObjID();
		packet.x     = sender->GetX();
		packet.y     = sender->GetY();
		packet.maxhp = sender->GetStat()->GetMaxHp();
		packet.hp    = sender->GetStat()->GetHp();
		packet.level = sender->GetStat()->GetLevel();
		packet.exp   = sender->GetStat()->GetExp();

		session->PostSend(packet);
	}

	void SendUSER_LOGIN_FAIL_ACK(Subject::SharedPtr sender)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		USER_LOGIN_FAIL_ACK_PACKET packet;
		InitializePacket(packet, PacketType::USER_LOGIN_FAIL_ACK);

		session->PostSend(packet);
	}

	void SendUSER_ATTACK_ACK(Subject::SharedPtr sender, const ObjID& targetId, int32_t damage)
	{
		const auto target = ::GetGameObject<Subject>(targetId);
		SendUSER_ATTACK_ACK(sender, target, damage);
	}

	void SendUSER_ATTACK_ACK(Subject::SharedPtr sender, Subject::SharedPtr target, int32_t damage)
	{
		if (target == nullptr)
			return;

		auto session = GetSession(sender);
		if (!session)
			return;

		USER_ATTACK_ACK_PACKET packet;
		InitializePacket(packet, PacketType::USER_ATTACK_ACK);
		packet.id     = target->GetObjID();
		packet.hp     = target->GetStat()->GetHp();
		packet.damage = damage;

		session->PostSend(packet);
	}

	void SendSUBJECT_DIE_NFY(Subject::SharedPtr sender, const ObjID& targetId)
	{
		const auto target = ::GetGameObject<Subject>(targetId);
		SendSUBJECT_DIE_NFY(sender, target);
	}

	void SendSUBJECT_DIE_NFY(Subject::SharedPtr sender, Subject::SharedPtr target)
	{
		if (target == nullptr)
			return;

		auto session = GetSession(sender);
		if (!session)
			return;

		SUBJECT_DIE_NFY_PACKET packet;
		InitializePacket(packet, PacketType::SUBJECT_DIE_NFY);
		packet.id = target->GetObjID();
		packet.hp = target->GetStat()->GetHp();

		session->PostSend(packet);
	}

	void SendSUBJECT_RESPAWN_NFY(Subject::SharedPtr sender, const ObjID& targetId)
	{
		const auto target = ::GetGameObject<Subject>(targetId);
		SendSUBJECT_RESPAWN_NFY(sender, target);
	}

	void SendSUBJECT_RESPAWN_NFY(Subject::SharedPtr sender, Subject::SharedPtr target)
	{
		if (target == nullptr)
			return;

		auto session = GetSession(sender);
		if (!session)
			return;

		SUBJECT_RESPAWN_NFY_PACKET packet;
		InitializePacket(packet, PacketType::SUBJECT_RESPAWN_NFY);
		if (target->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eMonster)
		{
			const auto monster = static_pointer_cast<Monster>(target);
			packet.monster_type = static_cast<char>(monster->GetType());
		}
		packet.id = target->GetObjID();
		packet.x = target->GetX();
		packet.y = target->GetY();
		packet.hp = target->GetStat()->GetHp();
		::strncpy_s(packet.name, NAME_SIZE, target->GetName().c_str(), _TRUNCATE);

		session->PostSend(packet);
	}

	void SendSUBJECT_ATTACK_NFY(Subject::SharedPtr viewer, const ObjID& victimId, const ObjID& attackerId, int32_t victimHp)
	{
		auto session = GetSession(viewer);
		if (!session)
			return;

		SUBJECT_ATTACK_NFY_PACKET packet;
		InitializePacket(packet, PacketType::SUBJECT_ATTACK_NFY);
		packet.victim_id   = victimId;
		packet.attacker_id = attackerId;
		packet.hp          = victimHp;

		session->PostSend(packet);
	}

	void SendPLAYER_ATTACK_NFY(Subject::SharedPtr viewer, const ObjID& attackerId, uint8_t facing)
	{
		auto session = GetSession(viewer);
		if (!session)
			return;

		PLAYER_ATTACK_NFY_PACKET packet;
		InitializePacket(packet, PacketType::PLAYER_ATTACK_NFY);
		packet.attacker_id = attackerId;
		packet.facing      = facing;

		session->PostSend(packet);
	}

	void SendSC_CHAT(Subject::SharedPtr viewer, const ObjID& senderId, const char mess[])
	{
		auto session = GetSession(viewer);
		if (!session)
			return;

		SC_CHAT_PACKET packet;
		InitializePacket(packet, PacketType::SC_CHAT);
		packet.sender_id = senderId;
		::strncpy_s(packet.mess, CHAT_SIZE, mess, _TRUNCATE);

		session->PostSend(packet);
	}

	void SendUSER_HEAL_INF(Subject::SharedPtr sender)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		USER_HEAL_INF_PACKET packet;
		InitializePacket(packet, PacketType::USER_HEAL_INF);
		packet.hp = sender->GetStat()->GetHp();

		session->PostSend(packet);
	}

	void SendUSER_STAT_CHANGE_INF(Subject::SharedPtr sender)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		USER_STAT_CHANGE_INF_PACKET packet;
		InitializePacket(packet, PacketType::USER_STAT_CHANGE_INF);
		packet.level = sender->GetStat()->GetLevel();
		packet.hp    = sender->GetStat()->GetHp();
		packet.maxhp = sender->GetStat()->GetMaxHp();
		packet.exp   = sender->GetStat()->GetExp();

		session->PostSend(packet);
	}

	bool SaveUserInfo(const ObjID& targetId)
	{
		const auto target = ::GetGameObject<User>(targetId);
		if (target == nullptr)
			return false;

		short saveX = target->GetX();
		short saveY = target->GetY();
		if (!IsValidWorldPosition(saveX, saveY))
		{
			const auto [fallbackX, fallbackY] = FindRandomValidPosition();
			saveX = fallbackX;
			saveY = fallbackY;

			cout << "Recovered invalid logout position for [" << target->GetName()
				<< "] from (" << target->GetX() << ", " << target->GetY()
				<< ") to (" << saveX << ", " << saveY << ")\n";
		}

		QueueUserSave(target, saveX, saveY);
		return true;
	}

	void AttackMonster(ObjID& monsterId, ObjID& playerId, int damage)
	{
		auto monster = ::GetGameObject<Monster>(monsterId);
		auto attacker = ::GetGameObject<User>(playerId);
		if (monster == nullptr || attacker == nullptr)
			return;
		if (monster->GetStat()->IsDead())
			return;

		const uint16_t remaining = monster->GetStat()->TakeDamage(static_cast<uint16_t>(damage));
		if (remaining > 0)
		{
			SendUSER_ATTACK_ACK(attacker, monster, damage);
			return;
		}

		GSector->ForEachNeighborObject(monster->GetSectorX(), monster->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;

			auto viewer = static_pointer_cast<User>(object);
			auto session = viewer->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;

			if (SubjectHelper::CanSee(object, monster))
				SendSUBJECT_DIE_NFY(viewer, monster);
		});

		GSector->RemoveObject(monsterId, monster->RefSectorX(), monster->RefSectorY());
		monster->SetActive(false);
		monster->SetAttack(false);
		monster->ClearViewList(); // 다음 리스폰 시 oldList가 빈 상태로 시작하도록 초기화

		const uint32_t expGain = (monster->GetType() == MONSTER_TYPE::PASSIVE) ? 3 : 5;
		attacker->GetStat()->AddExp(expGain);
		SendUSER_STAT_CHANGE_INF(attacker);

		GTimerThread->ScheduleAfter(monsterId, 10s, TIMER_EVENT_TYPE::EV_MONSTER_RESPAWN);
	}

	void SkillAttack(ObjID& playerId)
	{
		auto player = ::GetGameObject<User>(playerId);
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

	void HandleAttack(Subject::SharedPtr attacker, uint8_t facing)
	{
		ObjID attackerId = attacker->GetObjID();

		std::vector<ObjID> monsterTargets;

		GSector->ForEachNeighborObject(attacker->GetSectorX(), attacker->GetSectorY(),
			[&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			const auto cat = id.GetCategory<EnumCategory>();

			if (cat == EnumCategory::eUser)
			{
				if (id == attackerId) return;
				auto viewer = static_pointer_cast<User>(object);
				auto session = viewer->GetGameSession();
				if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
				if (SubjectHelper::CanSee(object, attacker))
					SendPLAYER_ATTACK_NFY(viewer, attackerId, facing);
			}
			else if (cat == EnumCategory::eMonster)
			{
				if (SubjectHelper::CanAttack(attacker, object))
					monsterTargets.push_back(id);
			}
		});

		for (ObjID& monsterId : monsterTargets)
			AttackMonster(monsterId, attackerId);
	}

	void BroadcastChat(Subject::SharedPtr sender, const char mess[])
	{
		const ObjID senderId = sender->GetObjID();
		SendSC_CHAT(sender, senderId, mess);

		GSector->ForEachNeighborObject(sender->GetSectorX(), sender->GetSectorY(),
			[&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id == senderId) return;
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;

			auto viewer = static_pointer_cast<User>(object);
			auto session = viewer->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;

			if (SubjectHelper::CanSee(object, sender))
				SendSC_CHAT(viewer, senderId, mess);
		});
	}

	void HandleHeal(const ObjID& playerId)
	{
		auto player = ::GetGameObject<User>(playerId);
		if (player == nullptr)
			return;

		auto session = player->GetGameSession();
		if (!session || session->m_state != SOCKET_STATE::ST_INGAME)
			return;

		if (player->GetStat()->IsDead())
			return;

		player->GetStat()->HealHp(HEAL_SIZE, PLAYER_MAX_HP);
		SendUSER_HEAL_INF(player);
		GTimerThread->ScheduleAfter(playerId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
	}

	void HandleRespawn(const ObjID& playerId)
	{
		auto player = ::GetGameObject<User>(playerId);
		if (player == nullptr)
			return;

		SectorHelper::GetRandomPosition(const_cast<ObjID&>(playerId));
		player->GetStat()->SetDead(false);
		player->GetStat()->SetHp(PLAYER_MAX_HP);

		// Tell the respawning player their own new position/HP
		SendSUBJECT_RESPAWN_NFY(player, player);

		SectorHelper::NotifyPlayerEnteredWorld(const_cast<ObjID&>(playerId), true);
		GTimerThread->ScheduleAfter(playerId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
	}

	void HandleLoginFail(const shared_ptr<GameSession>& session)
	{
		if (session == nullptr)
			return;

		USER_LOGIN_FAIL_ACK_PACKET packet;
		InitializePacket(packet, PacketType::USER_LOGIN_FAIL_ACK);
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
		if (IsValidWorldPosition(userInfo._x, userInfo._y))
		{
			SectorHelper::UpdatePosition(objId, static_cast<short>(userInfo._x), static_cast<short>(userInfo._y));
		}
		else
		{
			const auto [fallbackX, fallbackY] = FindRandomValidPosition();
			SectorHelper::UpdatePosition(objId, fallbackX, fallbackY);
			QueueUserSave(player, fallbackX, fallbackY);

			cout << "Recovered invalid login position for [" << player->GetName()
				<< "] from (" << userInfo._x << ", " << userInfo._y
				<< ") to (" << fallbackX << ", " << fallbackY << ")\n";
		}

		SendUSER_LOGIN_ACK(player);
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
		SectorHelper::GetRandomPosition(objId);

		SendUSER_LOGIN_ACK(player);

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
