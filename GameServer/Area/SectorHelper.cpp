#include "pch.h"
#include "SectorHelper.h"
#include "SubjectHelper.h"
#include "User.h"
#include "Monster.h"
#include "UserHelper.h"
#include "MonsterHelper.h"
#include "Sector.h"
#include "Collision.h"

namespace SectorHelper
{
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

			if (session->m_state != ST_INGAME) return;
			if (!SubjectHelper::CanSee(id, monsterId)) return;
			visiblePlayers.push_back(id);
		});

		return visiblePlayers;
	}

	void BroadcastMonsterVisibilityDelta(
		ObjID monsterId,
		const std::vector<ObjID>& oldList,
		const std::vector<ObjID>& newList)
	{
		const auto inOld = [&](const ObjID& id) {
			return std::find(oldList.begin(), oldList.end(), id) != oldList.end();
		};
		const auto inNew = [&](const ObjID& id) {
			return std::find(newList.begin(), newList.end(), id) != newList.end();
		};

		for (const ObjID& id : newList)
		{
			auto viewer = GGameObjectManager->Seek<Subject>(id);
			if (viewer == nullptr)
				continue;

			if (!inOld(id)) UserHelper::SendAddPlayerPacket(viewer, monsterId);
			else            UserHelper::SendMovePacket(viewer, monsterId);
		}

		for (const ObjID& id : oldList)
		{
			auto viewer = GGameObjectManager->Seek<Subject>(id);
			if (viewer == nullptr)
				continue;

			if (!inNew(id))
				UserHelper::SendRemovePlayerPacket(viewer, monsterId);
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
				if (!session || session->m_state != SOCKET_STATE::ST_INGAME) return;
				if (!SubjectHelper::CanSee(playerId, id)) return;

				if (isRespawn) UserHelper::SendRespawnPlayerPacket(user, playerId);
				else           UserHelper::SendAddPlayerPacket(user, playerId);

				UserHelper::SendAddPlayerPacket(player, id);
			}
			else if (id.GetCategory<EnumCategory>() == EnumCategory::eMonster)
			{
				if (!SubjectHelper::CanSee(playerId, id)) return;
				UserHelper::SendAddPlayerPacket(player, id);
				MonsterHelper::WakeUpMonster(id, playerId);
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
			if (!SubjectHelper::CanSee(id, clientId)) return;

			if (id.GetCategory<EnumCategory>() == EnumCategory::eUser)
			{
				auto otherPlayer = GGameObjectManager->Seek<User>(id);
				if (otherPlayer == nullptr) return;

				auto otherSession = otherPlayer->GetGameSession();
				if (!otherSession || otherSession->m_state != SOCKET_STATE::ST_INGAME) return;

				UserHelper::SendAddPlayerPacket(otherPlayer, myPlayer->GetObjID());
				UserHelper::SendAddPlayerPacket(myPlayer, id);
			}
			else
			{
				UserHelper::SendAddPlayerPacket(myPlayer, id);
				MonsterHelper::WakeUpMonster(id, clientId);
			}
		});
	}
}
