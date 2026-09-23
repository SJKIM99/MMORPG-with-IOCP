#include "pch.h"
#include "SectorHelper.h"
#include "SubjectHelper.h"
#include "User.h"
#include "Monster.h"
#include "UserHelper.h"
#include "MonsterHelper.h"
#include "Sector.h"
#include "World/WorldRegistry.h"
#include "Zone/ZoneManager.h"
#include "Collision.h"
#include "CoreTLS.h"

namespace SectorHelper
{
	void UpdatePosition(ObjID& subjectId, float nextX, float nextZ)
	{
		auto object = ::GetGameObject<Subject>(subjectId);
		if (object == nullptr)
			return;

		// 섹터 칸 이동과 좌표 갱신을 한 번에 처리해 둘이 어긋난 상태가 남지 않게 한다.
		// Sector 격자는 Zone마다 하나씩이고 thread_local GSector로만 닿으므로,
		// 이 Zone 스레드 외에는 건드리는 쪽이 없어 락이 필요 없다.
		const bool valid = GSector->UpdateObjectSectorAndPosition(
			subjectId,
			nextX, nextZ,
			object->RefSectorX(), object->RefSectorZ(),
			object->RefX(), object->RefZ());

		if (!valid)
			return;

		// 리전은 **지금 등록한 GSector** 에서 얻는다. 좌표만으로는 알 수 없고
		// (마을 (100,100) 과 필드 (100,100) 은 다른 Zone 이다), 객체의 ZoneId 에서
		// 꺼내서도 안 된다.
		//
		// 객체의 ZoneId 를 쓰면 **닭과 달걀**이 된다. 갓 만들어진 몬스터와 방금
		// 로그인한 플레이어는 ZoneId 가 InvalidZoneId 라, 거기서 리전을 꺼내면
		// 0xFF 가 나오고 ZoneAt 이 InvalidZoneId 를 돌려준다. 그러면 객체가
		// 영원히 Zone 없이 남아 나중에 단언에서 죽는다 — 실제로 필드 Zone 이
		// 몬스터를 초기화하다 그렇게 죽었다.
		//
		// GSector 는 이 Zone 스레드 소유이고 방금 이 객체를 그 격자에 넣었으므로
		// 리전은 정의상 GSector 의 것이다.
		const RegionIndex region = GSector->GetRegion();
		const ZoneId zoneId = GWorld->ZoneAt(region, nextX, nextZ);

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

		// 현재 Zone 이 소유한 Sector 범위를 **미터**로 환산해 그 안에서만 뽑는다.
		//
		// 옛 코드는 SECTOR_RANGE(=10, 2D 타일 시절 값)를 곱했다. 이제 Sector 는
		// 리전 데이터가 정하는 32m 라, 10 을 곱하면 이 Zone 이 소유하지 않은
		// 섹터를 가리킨다. 그러면 UpdateObjectSectorAndPosition 이 실패해
		// 섹터가 (-1,-1) 로 남고, 나중에 Sector::GetObjects 의 단언에서 죽는다.
		// 실제로 그 단언(STATUS_BREAKPOINT)으로 서버가 올라오지 못했다.
		const ZoneGrid& grid = GWorld->Grid(GSector->GetRegion());
		const float cell = grid.sectorSize;

		const float minX = GSector->GetOffsetX() * cell;
		const float minZ = GSector->GetOffsetZ() * cell;
		// 리전 가장자리 Zone 은 소유 Sector 가 모자랄 수 있으므로 리전 크기로 자른다.
		const float maxX = std::min<float>(
			grid.sizeX, (GSector->GetOffsetX() + Sector::kLocalWidth) * cell) - 1.0f;
		const float maxZ = std::min<float>(
			grid.sizeZ, (GSector->GetOffsetZ() + Sector::kLocalDepth) * cell) - 1.0f;

		std::uniform_real_distribution<float> distX(minX, maxX);
		std::uniform_real_distribution<float> distZ(minZ, maxZ);
		for (int attempt = 0; attempt < 64; ++attempt)
		{
			const float x = distX(LRng);
			const float z = distZ(LRng);
			if (isCollision(ToLegacyTile(x), ToLegacyTile(z)))
				continue;

			UpdatePosition(subjectId, x, z);
			return;
		}

		// 64번 실패하면 막힌 곳뿐이라는 뜻이다. 무한 루프로 스레드를 잡아먹느니
		// 구석에라도 놓는다 — Recast 가 들어오면(4번 단계) 이 판정 자체가 사라진다.
		UpdatePosition(subjectId, minX, minZ);
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
		GSector->ForEachNeighborObjID(monster->GetSectorX(), monster->GetSectorZ(), [&](const ObjID& id)
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
		GSector->ForEachNeighborObject(player->GetSectorX(), player->GetSectorZ(), [&](const shared_ptr<Subject>& object)
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
	static void HandleZoneTransfer(const shared_ptr<User>& player, float nextX, float nextZ, ZoneId newZoneId)
	{
		const ObjID playerId = player->GetObjID();

		// 1. Transferring 플래그 설정 — 이후 Zone B 스레드가 이 플레이어 패킷을 받아도
		//    HandleEnterZone이 완료될 때까지 처리를 건너뛴다
		player->SetTransferring(true);

		// 2. Zone A 시야 목록 수집 (REMOVE 알림 기준)
		const auto oldVisible = CollectSubjects(player);

		// 3. Zone A 섹터에서 제거
		GSector->RemoveObject(const_cast<ObjID&>(playerId), player->RefSectorX(), player->RefSectorZ());

		// 4. 위치를 존 B 좌표로 갱신 (섹터 그리드에는 아직 미등록 상태)
		//    플레이어가 섹터에서 빠진 직후이므로 다른 Zone 스레드가 섹터를 통해
		//    이 플레이어를 조회할 수 없어 락 없이 SetPosition이 안전하다.
		player->SetGround(nextX, nextZ);

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
		//    FIFO는 "먼저 넣은 것이 먼저 나간다"만 보장할 뿐, HandleEnterZone이 먼저
		//    들어간다는 보장은 아니다 — 바로 위에서 라우팅을 B로 돌린 순간부터 이 push가
		//    끝나기 전까지, 워커가 이 플레이어의 패킷을 Zone B 큐에 먼저 밀어 넣을 수 있다.
		//    그래서 큐 순서에 기대지 않고 IsTransferring으로 막는다(Route.cpp의 각 핸들러 첫 검사).
		GZoneManager->EnqueueByZone(newZoneId, [playerId, nextX, nextZ]()
		{
			SectorHelper::HandleEnterZone(playerId, nextX, nextZ);
		});
	}
	// ──────────────────────────────────────────────────────────────────────────────

	void HandlePlayerMove(const shared_ptr<User>& player, float nextX, float nextZ)
	{
		if (player == nullptr)
			return;

		// Zone 경계 이동 감지 → Actor 모델 비동기 전달
		// 같은 리전 안에서의 이동이다. 리전을 넘는 이동(포탈)은 Week 2 에서
		// 목적 리전을 함께 받는 경로로 들어온다 — ZoneId 에 리전이 박혀 있어
		// 이관 코드 자체는 그대로 쓸 수 있다.
		const ZoneId curZoneId = player->GetZoneId();
		const ZoneId newZoneId = GWorld->ZoneAt(RegionOfZone(curZoneId), nextX, nextZ);
		if (newZoneId != curZoneId && GWorld->IsValidZoneId(newZoneId))
		{
			HandleZoneTransfer(player, nextX, nextZ, newZoneId);
			return;
		}

		// ── 같은 Zone 내 이동 ─────────────────────────────────────────────────────
		const float oldX     = player->GetX();
		const float oldZ     = player->GetZ();
		const ObjID playerId = player->GetObjID();

		const auto oldVisible = CollectSubjects(player);

		UpdatePosition(const_cast<ObjID&>(playerId), nextX, nextZ);
		UserHelper::SendSUBJECT_MOVE_NFY(player, static_pointer_cast<Subject>(player));

		if (oldX == player->GetX() && oldZ == player->GetZ())
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
	// 이 함수보다 먼저 도착해 처리되는 패킷이 있을 수 있다(7번 주석 참고).
	// 그쪽은 IsTransferring이 아직 true라 아무 일도 하지 않고 반환하고,
	// 마지막 줄에서 플래그를 내리는 순간부터 Zone B가 정상 처리를 시작한다.
	void HandleEnterZone(const ObjID& playerId, float nextX, float nextZ)
	{
		const auto player = ::GetGameObject<User>(playerId);
		if (player == nullptr)
			return;  // Zone Transfer 중 접속 끊김 — 안전하게 종료

		// Zone B 섹터에 등록 및 좌표 원자적 갱신
		const bool valid = GSector->UpdateObjectSectorAndPosition(
			const_cast<ObjID&>(playerId),
			nextX, nextZ,
			player->RefSectorX(), player->RefSectorZ(),
			player->RefX(), player->RefZ());

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
