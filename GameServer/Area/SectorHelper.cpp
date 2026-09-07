#include "pch.h"
#include "SectorHelper.h"
#include "SubjectHelper.h"
#include "User.h"
#include "Monster.h"
#include "UserHelper.h"
#include "MonsterHelper.h"
#include "Sector.h"
#include "Zone/ZoneLayout.h"
#include "Zone/ZoneManager.h"
#include "Collision.h"
#include "CoreTLS.h"

namespace SectorHelper
{
	void UpdatePosition(ObjID& subjectId, short nextX, short nextY)
	{
		auto object = ::GetGameObject<Subject>(subjectId);
		if (object == nullptr)
			return;

		// 섹터 칸 이동과 좌표 갱신을 한 번에 처리해 둘이 어긋난 상태가 남지 않게 한다.
		// Sector 격자는 Zone마다 하나씩이고 thread_local GSector로만 닿으므로,
		// 이 Zone 스레드 외에는 건드리는 쪽이 없어 락이 필요 없다.
		const bool valid = GSector->UpdateObjectSectorAndPosition(
			subjectId,
			nextX, nextY,
			object->RefSectorX(), object->RefSectorY(),
			object->RefX(), object->RefY());

		if (!valid)
			return;

		const ZoneId zoneId = ZoneLayout::GetZoneIdByWorld(nextX, nextY);

		// 좌표가 바뀌어도 Zone은 대부분 그대로다. 몬스터는 아예 Zone을 벗어날 수
		// 없고(RandomMove가 존 밖 걸음을 거부하고, 어그로 추격은 ZONE_BOUNDARY_MARGIN
		// 안에서 되돌아간다), 플레이어도 500x500칸짜리 Zone을 넘는 일은 드물다.
		// 그런데 UpdateObjectZone은 전역 뮤텍스를 잡으므로, 같은 값을 다시 쓰는
		// 호출 때문에 Zone 스레드 16개가 이동 한 번마다 이 락 하나에서 다시 만난다.
		// 실제로 Zone이 바뀐 경우에만 갱신한다.
		//
		// m_zoneId를 "맵에 이미 반영된 값"으로 믿어도 되는 이유 — 아래 두 줄을 항상
		// 함께 실행하므로 m_zoneId는 곧 이 오브젝트에 대해 마지막으로 맵에 쓴 값이다.
		// 맵에서 빠지는 경로(접속 종료, 필드 아이템 습득/소멸)는 GameObjectManager에서도
		// 함께 지우므로 위 GetGameObject가 nullptr을 반환해 여기까지 오지 못하고,
		// 맵에 새로 들어오는 객체(로그인 User, InitForZone의 Monster, MakeNewItem으로
		// 만든 Item)는 전부 새로 할당되어 m_zoneId가 InvalidZoneId다. 따라서
		// "맵에는 없는데 m_zoneId만 우연히 맞아 갱신을 건너뛰는" 상태가 생기지 않는다.
		if (object->GetZoneId() != zoneId)
		{
			object->SetZoneId(zoneId);
			GZoneManager->UpdateObjectZone(subjectId, zoneId);
		}

		// 세션 쪽은 원자적 저장이라 락이 없다. IOCP 워커가 EnqueueBySession으로
		// 읽는 값이므로 조건을 걸지 않고 항상 최신으로 맞춰둔다.
		if (subjectId.GetCategory<EnumCategory>() == EnumCategory::eUser)
		{
			const auto user = static_pointer_cast<User>(object);
			if (const auto session = user->GetGameSession(); session != nullptr)
				session->SetRoutingZoneId(zoneId);
		}
	}

	void GetRandomPosition(ObjID& subjectId)
	{
		ASSERT_CRASH(GSector != nullptr);

		// 현재 Zone의 섹터 오프셋에서 월드 좌표 범위를 계산하여
		// 이 Zone 소속 Sector 범위 안에서만 랜덤 배치한다.
		const short minX = static_cast<short>(GSector->GetOffsetX() * SECTOR_RANGE);
		const short maxX = static_cast<short>((GSector->GetOffsetX() + Sector::kLocalWidth)  * SECTOR_RANGE - 1);
		const short minY = static_cast<short>(GSector->GetOffsetY() * SECTOR_RANGE);
		const short maxY = static_cast<short>((GSector->GetOffsetY() + Sector::kLocalHeight) * SECTOR_RANGE - 1);

		std::uniform_int_distribution<short> distX(minX, maxX);
		std::uniform_int_distribution<short> distY(minY, maxY);
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
				// isRespawn=true: player teleported to a random position, not walked in.
				// Pass as forceWake so aggro monsters skip the WAKE_RANGE check and
				// activate immediately for all monsters within VIEW_RANGE.
				MonsterHelper::WakeUpMonster(monsterId, playerId, isRespawn);
			}
			else if (id.GetCategory<EnumCategory>() == EnumCategory::eItem)
			{
				// 필드에 떨어진 아이템 — 깨울 것도, 상대에게 나를 알릴 것도 없다.
				// 로그인/리스폰한 플레이어에게만 "여기 아이템이 있다"고 알려주면 된다.
				UserHelper::SendSUBJECT_ADD_NFY(player, object);
			}
		}
	}

	// ── Zone A 스레드에서 실행 ─────────────────────────────────────────────────────
	// 플레이어가 존 경계를 넘을 때 Zone A 측 정리를 수행하고,
	// Zone B 스레드에 HandleEnterZone을 비동기 메시지로 위임한다.
	static void HandleZoneTransfer(const shared_ptr<User>& player, short nextX, short nextY, ZoneId newZoneId)
	{
		const ObjID playerId = player->GetObjID();

		// 1. Transferring 플래그 설정 — 이후 Zone B 스레드가 이 플레이어 패킷을 받아도
		//    HandleEnterZone이 완료될 때까지 처리를 건너뛴다
		player->SetTransferring(true);

		// 2. Zone A 시야 목록 수집 (REMOVE 알림 기준)
		const auto oldVisible = CollectSubjects(player);

		// 3. Zone A 섹터에서 제거
		GSector->RemoveObject(const_cast<ObjID&>(playerId), player->RefSectorX(), player->RefSectorY());

		// 4. 위치를 존 B 좌표로 갱신 (섹터 그리드에는 아직 미등록 상태)
		//    플레이어가 섹터에서 빠진 직후이므로 다른 Zone 스레드가 섹터를 통해
		//    이 플레이어를 조회할 수 없어 락 없이 SetPosition이 안전하다.
		player->SetPosition(nextX, nextY);

		// 5. Zone A 이웃들에게 이동·제거 알림
		UserHelper::SendSUBJECT_MOVE_NFY(player, static_pointer_cast<Subject>(player));
		for (const ObjID& id : oldVisible)
		{
			UserHelper::SendSUBJECT_REMOVE_NFY(player, id);
			if (id.GetCategory<EnumCategory>() == EnumCategory::eUser)
			{
				auto viewer = ::GetGameObject<User>(id);
				if (viewer)
					UserHelper::SendSUBJECT_REMOVE_NFY(viewer, playerId);
			}
		}

		// 6. Zone 매핑 갱신 — 이 시점 이후 WorkerThread의 새 패킷은 Zone B로 라우팅됨
		player->SetZoneId(newZoneId);
		GZoneManager->UpdateObjectZone(playerId, newZoneId);
		if (const auto session = player->GetGameSession())
			session->SetRoutingZoneId(newZoneId);

		// 7. Zone B에 입장 처리 위임 (비동기 메시지 패싱)
		//    Zone B 큐는 FIFO이므로 HandleEnterZone이 새 패킷보다 반드시 먼저 처리된다
		GZoneManager->EnqueueByZone(newZoneId, [playerId, nextX, nextY]()
		{
			SectorHelper::HandleEnterZone(playerId, nextX, nextY);
		});
	}
	// ──────────────────────────────────────────────────────────────────────────────

	void HandlePlayerMove(const shared_ptr<User>& player, short nextX, short nextY)
	{
		if (player == nullptr)
			return;

		// Zone 경계 이동 감지 → Actor 모델 비동기 전달
		const ZoneId newZoneId = ZoneLayout::GetZoneIdByWorld(nextX, nextY);
		const ZoneId curZoneId = player->GetZoneId();
		if (newZoneId != curZoneId && ZoneLayout::IsValidZoneId(newZoneId))
		{
			HandleZoneTransfer(player, nextX, nextY, newZoneId);
			return;
		}

		// ── 같은 Zone 내 이동 ─────────────────────────────────────────────────────
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

		// Pass 1: 새로 보이는 오브젝트 처리
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

		// Pass 2: 시야에서 사라진 오브젝트 처리
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

	// ── Zone B 스레드에서 실행 ─────────────────────────────────────────────────────
	// HandleZoneTransfer가 비동기 메시지로 위임한 Zone B 진입 처리.
	// Zone B 큐의 FIFO 보장으로 이 함수가 새 패킷보다 반드시 먼저 실행된다.
	void HandleEnterZone(const ObjID& playerId, short nextX, short nextY)
	{
		const auto player = ::GetGameObject<User>(playerId);
		if (player == nullptr)
			return;  // Zone Transfer 중 접속 끊김 — 안전하게 종료

		// Zone B 섹터에 등록 및 좌표 원자적 갱신
		const bool valid = GSector->UpdateObjectSectorAndPosition(
			const_cast<ObjID&>(playerId),
			nextX, nextY,
			player->RefSectorX(), player->RefSectorY(),
			player->RefX(), player->RefY());

		if (!valid)
			GetRandomPosition(const_cast<ObjID&>(playerId));


		// Zone B 이웃들에게 입장 알림 + 플레이어에게 주변 오브젝트 알림
		NotifyPlayerEnteredWorld(const_cast<ObjID&>(playerId), false);

		// Transferring 해제는 NotifyPlayerEnteredWorld 이후에 수행
		// — 이 시점부터 Zone B가 이 플레이어의 모든 패킷을 정상 처리한다
		player->SetTransferring(false);
	}
	// ──────────────────────────────────────────────────────────────────────────────
}
