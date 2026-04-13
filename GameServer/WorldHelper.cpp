#include "pch.h"
#include "WorldHelper.h"
#include "User.h"
#include "Monster.h"
#include "UserHelper.h"
#include "Sector.h"
#include "TimerThread.h"
#include "Collision.h"

namespace WorldHelper
{
	uint32 GetNowTime()
	{
		return static_cast<uint32>(chrono::duration_cast<chrono::milliseconds>(
			chrono::steady_clock::now().time_since_epoch()).count());
	}

	void MovePositionByDirection(short& x, short& y, char direction)
	{
		switch (direction)
		{
		case 0: if (y > 0)            { --y; if (isCollision(x, y)) ++y; } break;
		case 1: if (y < W_HEIGHT - 1) { ++y; if (isCollision(x, y)) --y; } break;
		case 2: if (x > 0)            { --x; if (isCollision(x, y)) ++x; } break;
		case 3: if (x < W_WIDTH - 1)  { ++x; if (isCollision(x, y)) --x; } break;
		}
	}

	bool CanSee(const ObjID& a, const ObjID& b)
	{
		const auto aSubject = ::GetGameObject<Subject>(a);
		const auto bSubject = ::GetGameObject<Subject>(b);
		if (aSubject == nullptr || bSubject == nullptr)
			return false;

		if (abs(aSubject->GetX() - bSubject->GetX()) >= VIEW_RANGE)
			return false;

		return abs(aSubject->GetY() - bSubject->GetY()) <= VIEW_RANGE;
	}

	bool CanAttack(const ObjID& a, const ObjID& b)
	{
		const auto aSubject = ::GetGameObject<Subject>(a);
		const auto bSubject = ::GetGameObject<Subject>(b);
		if (aSubject == nullptr || bSubject == nullptr)
			return false;

		if (abs(aSubject->GetX() - bSubject->GetX()) >= ATTACK_RANGE)
			return false;

		return abs(aSubject->GetY() - bSubject->GetY()) <= ATTACK_RANGE;
	}

	void UpdateObjectPosition(ObjID& subjectId, short nextX, short nextY)
	{
		auto object = ::GetGameObject<Subject>(subjectId);
		if (object == nullptr)
			return;

		const bool updated = GSector->UpdateObjectSector(
			subjectId,
			nextX,
			nextY,
			object->RefSectorX(),
			object->RefSectorY());

		ASSERT_CRASH(updated || GSector->GetSectorCoord(nextX, nextY).IsAssigned());
		object->SetPosition(nextX, nextY);
	}

	void PlaceObjectAtRandomWalkablePosition(ObjID& subjectId)
	{
		while (true)
		{
			const short x = static_cast<short>(rand() % W_WIDTH);
			const short y = static_cast<short>(rand() % W_HEIGHT);
			if (isCollision(x, y))
				continue;

			UpdateObjectPosition(subjectId, x, y);
			return;
		}
	}

	std::vector<ObjID> CollectVisiblePlayersAround(ObjID& monsterId)
	{
		std::vector<ObjID> visiblePlayers;
		visiblePlayers.reserve(16);
		const auto monster = ::GetGameObject<Monster>(monsterId);
		if (monster == nullptr)
			return visiblePlayers;

		GSector->ForEachNeighborObject(monster->GetSectorX(), monster->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			auto id = object->GetObjID();
			if (id.GetCategory() != EnumCategory::eUser) return;
			auto user = ::GetGameObject<User>(id);
			if (!user)
				return;

			auto session = user->GetGameSession();
			if (!session)
				return;

			if (session->_state != ST_INGAME) return;
			if (!CanSee(id, monsterId)) return;
			visiblePlayers.push_back(id);
		});

		return visiblePlayers;
	}

	void BroadcastNpcVisibilityDelta(
		ObjID npcId,
		const std::vector<ObjID>& oldList,
		const std::vector<ObjID>& newList)
	{
		const auto inOld = [&](uint32 id) {
			return std::find(oldList.begin(), oldList.end(), id) != oldList.end();
		};
		const auto inNew = [&](uint32 id) {
			return std::find(newList.begin(), newList.end(), id) != newList.end();
		};

		for (const ObjID& id : newList)
		{
			auto viewer = GGameObjectManager->Seek<Subject>(id);
			if (viewer == nullptr)
				continue;

			if (!inOld(id)) UserHelper::SendAddPlayerPacket(viewer, npcId);
			else            UserHelper::SendMovePacket(viewer, npcId);
		}

		for (const ObjID& id : oldList)
		{
			auto viewer = GGameObjectManager->Seek<Subject>(id);
			if (viewer == nullptr)
				continue;

			if (!inNew(id))
				UserHelper::SendRemovePlayerPacket(viewer, npcId);
		}
	}

	void NotifyPlayerEnteredWorld(ObjID& playerId, bool isRespawn)
	{
		const auto player = GGameObjectManager->Seek<User>(playerId);
		if (player == nullptr)
			return;

		GSector->ForEachNeighborObject(player->GetSectorX(), player->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id == playerId) return;

			if (id.GetCategory<EnumCategory>() == EnumCategory::eUser)
			{
				auto user = GGameObjectManager->Seek<User>(id);
				if (!user) return;

				auto session = user->GetGameSession();
				if (!session || session->_state != SOCKET_STATE::ST_INGAME) return;
				if (!CanSee(playerId, id)) return;

				if (isRespawn) UserHelper::SendRespawnPlayerPacket(user, playerId);
				else           UserHelper::SendAddPlayerPacket(user, playerId);

				UserHelper::SendAddPlayerPacket(player, id);
			}
			else if (id.GetCategory<EnumCategory>() == EnumCategory::eMonster)
			{
				if (!CanSee(playerId, id)) return;
				UserHelper::SendAddPlayerPacket(player, id);
				WakeUpNpc(id, playerId);
			}
		});
	}

	void UpdatePlayerViewList(ObjID& clientId)
	{
		auto myPlayer = GGameObjectManager->Seek<User>(clientId);
		if (myPlayer == nullptr)
			return;

		GSector->ForEachNeighborObject(myPlayer->GetSectorX(), myPlayer->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id == clientId) return;
			if (!CanSee(id, clientId)) return;

			if (id.GetCategory<EnumCategory>() == EnumCategory::eUser)
			{
				auto otherPlayer = GGameObjectManager->Seek<User>(id);
				if (otherPlayer == nullptr) return;

				auto otherSession = otherPlayer->GetGameSession();
				if (!otherSession || otherSession->_state != SOCKET_STATE::ST_INGAME) return;

				UserHelper::SendAddPlayerPacket(otherPlayer, myPlayer->GetObjID());
				UserHelper::SendAddPlayerPacket(myPlayer, id);
			}
			else
			{
				UserHelper::SendAddPlayerPacket(myPlayer, id);
				WakeUpNpc(id, clientId);
			}
		});
	}

	void WakeUpNpc(ObjID& npcId, ObjID& wakerId)
	{
		auto npc = GGameObjectManager->Seek<Monster>(npcId);
		auto waker = GGameObjectManager->Seek<User>(wakerId);
		if (npc == nullptr || waker == nullptr)
			return;
		if (npc->GetStat()->IsDead()) return;

		// atomic exchange: 이미 활성화된 경우 중복 스케줄링 방지
		if (!npc->TryActivate()) return;

		switch (npc->GetType())
		{
		case MONSTER_TYPE::PASSIVE:
			GTimerThread->ScheduleAfter(npcId, 1s, TIMER_EVENT_TYPE::EV_RANOM_MOVE);
			return;

		case MONSTER_TYPE::AGGRO:
			GTimerThread->ScheduleAfter(npcId, 1s, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, wakerId);
			return;
		}
	}

	void AttackNpc(ObjID& npcId, ObjID& playerId)
	{
		auto npc = GGameObjectManager->Seek<Monster>(npcId);
		auto attacker = GGameObjectManager->Seek<User>(playerId);
		if (npc == nullptr || attacker == nullptr)
			return;
		if (npc->GetStat()->IsDead())
			return;

		const uint16 remaining = npc->GetStat()->TakeDamage(PLAYER_OFFENSIVE);
		if (remaining > 0)
		{
			UserHelper::SendPlayerAttackToNpcPacket(attacker, npcId);
			return;
		}

		GSector->ForEachNeighborObject(npc->GetSectorX(), npc->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			ObjID id = object->GetObjID();
			if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;

			auto viewer = GGameObjectManager->Seek<User>(id);
			if (viewer == nullptr) return;
			auto session = viewer->GetGameSession();
			if (!session || session->_state != SOCKET_STATE::ST_INGAME) return;

			if (CanSee(id, npcId))
				UserHelper::SendNpcDiePacket(viewer, npcId);
		});

		GSector->RemoveObject(npcId, npc->RefSectorX(), npc->RefSectorY());
		npc->SetActive(false);
		npc->SetAttack(false);

		const uint32 expGain = (npc->GetType() == MONSTER_TYPE::PASSIVE) ? 3 : 5;
		attacker->GetStat()->AddExp(expGain);
		UserHelper::SendStatChangePacket(attacker);

		GTimerThread->ScheduleAfter(npcId, 10s, TIMER_EVENT_TYPE::EV_MONSTER_RESPAWN);
	}
}

