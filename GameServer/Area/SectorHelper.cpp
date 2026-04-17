#include "pch.h"
#include "SectorHelper.h"
#include "SubjectHelper.h"
#include "User.h"
#include "Monster.h"
#include "UserHelper.h"
#include "MonsterHelper.h"
#include "Sector.h"
#include "Collision.h"
#include "CoreTLS.h"

namespace SectorHelper
{
	void UpdatePosition(ObjID& subjectId, short nextX, short nextY)
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

	void GetRandomPosition(ObjID& subjectId)
	{
		std::uniform_int_distribution<short> distX(0, W_WIDTH - 1);
		std::uniform_int_distribution<short> distY(0, W_HEIGHT - 1);
		while (true)
		{
			const short x = distX(LRng);
			const short y = distY(LRng);
			if (isCollision(x, y))
				continue;

			UpdatePosition(subjectId, x, y);
			return;
		}
	}

	std::vector<ObjID> CollectUsers(ObjID& monsterId)
	{
		std::vector<ObjID> nearUsers;
		nearUsers.reserve(16);
		const auto monster = ::GetGameObject<Monster>(monsterId);
		if (monster == nullptr)
			return nearUsers;

		// ForEachNeighborObject 대신 ObjID 직접 순회:
		// 카테고리 필터를 먼저 적용해 몬스터 ObjID에 대한
		// GetGameObject 조회와 shared_ptr atomic 증가를 생략한다.
		GSector->ForEachNeighborObjID(monster->GetSectorX(), monster->GetSectorY(), [&](const ObjID& id)
		{
			if (id.GetCategory() != EnumCategory::eUser) return;
			const auto user = ::GetGameObject<User>(id);
			if (!user) return;
			const auto session = user->GetGameSession();
			if (!session || session->m_state != ST_INGAME) return;
			if (!SubjectHelper::CanSee(user, monster)) return;
			nearUsers.push_back(id);
		});

		return nearUsers;
	}

	std::vector<ObjID> CollectSubjects(const shared_ptr<User>& player)
	{
		std::vector<ObjID> visibleSubjects;
		if (player == nullptr)
			return visibleSubjects;

		visibleSubjects.reserve(32);
		GSector->ForEachNeighborObject(player->GetSectorX(), player->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			const ObjID objectId = object->GetObjID();
			if (objectId == player->GetObjID())
				return;
			if (!SubjectHelper::CanSee(player, object))
				return;

			if (objectId.GetCategory<EnumCategory>() == EnumCategory::eUser)
			{
				auto user = static_pointer_cast<User>(object);
				auto session = user->GetGameSession();
				if (!session || session->m_state != SOCKET_STATE::ST_INGAME)
					return;
			}

			visibleSubjects.push_back(objectId);
		});

		return visibleSubjects;
	}

	// 가시 목록이 최대 VIEW_RANGE 범위 내 오브젝트 수(소규모)이므로 선형 탐색이 충분히 빠름
	void Replace(ObjID monsterId, const std::vector<ObjID>& oldList, const std::vector<ObjID>& newList)
	{
		const auto monster = ::GetGameObject<Subject>(monsterId);

		auto inList = [](const std::vector<ObjID>& list, const ObjID& id) -> bool
		{
			return std::find(list.begin(), list.end(), id) != list.end();
		};

		for (const ObjID& id : newList)
		{
			auto viewer = ::GetGameObject<Subject>(id);
			if (viewer == nullptr)
				continue;

			if (!inList(oldList, id)) UserHelper::SendSUBJECT_ADD_NFY(viewer, monster);
			else                      UserHelper::SendSUBJECT_MOVE_NFY(viewer, monster);
		}

		for (const ObjID& id : oldList)
		{
			auto viewer = ::GetGameObject<Subject>(id);
			if (viewer == nullptr)
				continue;

			if (!inList(newList, id))
				UserHelper::SendSUBJECT_REMOVE_NFY(viewer, monsterId);
		}
	}

	void NotifyPlayerEnteredWorld(ObjID& playerId, bool isRespawn)
	{
		const auto player = ::GetGameObject<User>(playerId);
		if (player == nullptr)
			return;

		const auto visibleSubjects = CollectSubjects(player);
		for (const ObjID& id : visibleSubjects)
		{
			auto object = ::GetGameObject<Subject>(id);
			if (object == nullptr)
				continue;

			if (id.GetCategory<EnumCategory>() == EnumCategory::eUser)
			{
				auto user = static_pointer_cast<User>(object);

				if (isRespawn) UserHelper::SendSUBJECT_RESPAWN_NFY(user, playerId);
				else           UserHelper::SendSUBJECT_ADD_NFY(user, static_pointer_cast<Subject>(player));

				UserHelper::SendSUBJECT_ADD_NFY(player, object);
			}
			else if (id.GetCategory<EnumCategory>() == EnumCategory::eMonster)
			{
				UserHelper::SendSUBJECT_ADD_NFY(player, object);
				ObjID monsterId = id;
				MonsterHelper::WakeUpMonster(monsterId, playerId);
			}
		}
	}

	void HandlePlayerMove(const shared_ptr<User>& player, short nextX, short nextY)
	{
		if (player == nullptr)
			return;

		const short oldX     = player->GetX();
		const short oldY     = player->GetY();
		const ObjID playerId = player->GetObjID();

		const auto oldVisible = CollectSubjects(player);

		UpdatePosition(const_cast<ObjID&>(playerId), nextX, nextY);
		UserHelper::SendSUBJECT_MOVE_NFY(player, static_pointer_cast<Subject>(player));

		if (oldX == player->GetX() && oldY == player->GetY())
			return;

		const auto newVisible = CollectSubjects(player);

		auto inList = [](const std::vector<ObjID>& list, const ObjID& id) -> bool
		{
			return std::find(list.begin(), list.end(), id) != list.end();
		};

		// Pass 1: newVisible 순회
		//   - 플레이어 관점: 시야에 새로 들어온 오브젝트 → OBJECT_ADD_INF, 몬스터 → WakeUp
		//   - 이웃 유저 관점: 플레이어가 시야에 새로 들어옴 → OBJECT_ADD_INF,
		//                     이미 있었던 경우        → OBJECT_MOVE_INF
		for (const ObjID& id : newVisible)
		{
			auto object = ::GetGameObject<Subject>(id);
			if (object == nullptr)
				continue;

			const bool isNew = !inList(oldVisible, id);

			if (isNew)
				UserHelper::SendSUBJECT_ADD_NFY(player, object);

			if (id.GetCategory<EnumCategory>() == EnumCategory::eMonster)
			{
				ObjID monsterId = id;
				ObjID viewerId  = playerId;
				MonsterHelper::WakeUpMonster(monsterId, viewerId);
			}
			else if (id.GetCategory<EnumCategory>() == EnumCategory::eUser)
			{
				auto viewer = static_pointer_cast<User>(object);
				if (isNew) UserHelper::SendSUBJECT_ADD_NFY(viewer, static_pointer_cast<Subject>(player));
				else       UserHelper::SendSUBJECT_MOVE_NFY(viewer, static_pointer_cast<Subject>(player));
			}
		}

		// Pass 2: oldVisible 순회
		//   - 플레이어 관점: 시야에서 사라진 오브젝트 → OBJECT_REMOVE_INF
		//   - 이웃 유저 관점: 플레이어가 시야에서 사라짐 → OBJECT_REMOVE_INF
		for (const ObjID& id : oldVisible)
		{
			if (inList(newVisible, id))
				continue;

			UserHelper::SendSUBJECT_REMOVE_NFY(player, id);

			if (id.GetCategory<EnumCategory>() == EnumCategory::eUser)
			{
				auto viewer = ::GetGameObject<User>(id);
				if (viewer == nullptr)
					continue;
				UserHelper::SendSUBJECT_REMOVE_NFY(viewer, playerId);
			}
		}
	}
}
