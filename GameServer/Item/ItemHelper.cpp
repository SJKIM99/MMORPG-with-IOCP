#include "pch.h"
#include "ItemHelper.h"

#include "GameObjectManager.h"
#include "Sector.h"
#include "SectorHelper.h"
#include "SubjectHelper.h"
#include "TimerThread.h"
#include "User.h"
#include "UserHelper.h"
#include "Zone/ZoneManager.h"

namespace
{
	// 필드 아이템 인스턴스에만 쓰이는 카운터. eItem 카테고리 안에서만 유일하면
	// 되므로(ObjID는 카테고리+이 값의 조합), User/Monster의 ID 범위와 겹칠 걱정이
	// 없다 — 절대 재사용하지 않고 계속 증가만 하므로, "이미 지운 아이템의 ID를
	// 다른 아이템이 물려받아 헷갈리는" 일도 없다.
	std::atomic<uint64_t> GNextFieldItemId{ 1 };

	// GameObjectManager/Sector/ZoneManager 등록을 지우고 주변에 사라졌다고
	// 알린다 - 습득 성공 시와 자동 소멸 시 둘 다에서 쓰는 공통 정리 로직이다.
	void RemoveFieldItem(const Item::SharedPtr& item)
	{
		ObjID itemId = item->GetObjID();

		GSector->ForEachNeighborObject(item->GetSectorX(), item->GetSectorY(), [&](const shared_ptr<Subject>& viewer)
		{
			if (viewer->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
				return;

			auto user = static_pointer_cast<User>(viewer);
			auto session = user->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME)
				return;
			if (!SubjectHelper::CanSee(viewer, item))
				return;

			UserHelper::SendSUBJECT_REMOVE_NFY(user, itemId);
		});

		GSector->RemoveObject(itemId, item->RefSectorX(), item->RefSectorY());
		GZoneManager->RemoveObject(item->GetObjID());
		GGameObjectManager->Delete(item->GetObjID());
	}
}

namespace ItemHelper
{
	void SpawnFieldItem(const Item::SharedPtr& item, short x, short y)
	{
		ASSERT_CRASH(item != nullptr);
		ASSERT_CRASH(item->IsOnGround());  // 호출 전에 Inventory에서 이미 빠져나와 있어야 한다

		const uint64_t instanceId = GNextFieldItemId.fetch_add(1, std::memory_order_relaxed);
		item->SetObjID(EnumCategory::eItem, instanceId);

		ASSERT_CRASH(GGameObjectManager->Insert(item->GetObjID(), item));

		ObjID itemId = item->GetObjID();
		SectorHelper::UpdatePosition(itemId, x, y);

		GSector->ForEachNeighborObject(item->GetSectorX(), item->GetSectorY(), [&](const shared_ptr<Subject>& viewer)
		{
			if (viewer->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
				return;

			auto user = static_pointer_cast<User>(viewer);
			auto session = user->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME)
				return;
			if (!SubjectHelper::CanSee(viewer, item))
				return;

			UserHelper::SendSUBJECT_ADD_NFY(user, item);
		});

		GTimerThread->ScheduleAfter(item->GetObjID(), 30s, TIMER_EVENT_TYPE::EV_ITEM_DESPAWN);
	}

	void TryPickupAt(const shared_ptr<User>& player)
	{
		if (player == nullptr)
			return;

		Item::SharedPtr target;
		GSector->ForEachNeighborObject(player->GetSectorX(), player->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (target != nullptr)
				return;  // 이미 하나 찾음 - 한 번의 습득 요청은 아이템 하나만 대상으로 한다
			if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eItem)
				return;
			if (object->GetX() != player->GetX() || object->GetY() != player->GetY())
				return;

			target = static_pointer_cast<Item>(object);
		});

		if (target == nullptr)
		{
			UserHelper::SendITEM_PICKUP_ACK(player, false);
			return;
		}

		// 여기서 target은 아직 필드에 그대로 남아있는 상태다 - TryAddItem이
		// 실패해도(인벤토리가 꽉 참) 손댄 게 없으니 되돌릴 필요조차 없다.
		// 성공했을 때만, 바로 아래에서 필드에서 지운다. 이 함수 전체가 한
		// Zone 스레드 위에서 끊김 없이 실행되므로, "찾았는데 다른 요청이 먼저
		// 가져갔다" 같은 중간 상태가 존재하지 않는다.
		std::vector<uint16_t> touchedSlots;
		if (!player->GetInventory()->TryAddItem(target->GetItemTableId(), target->GetCount(), &touchedSlots))
		{
			UserHelper::SendITEM_PICKUP_ACK(player, false);
			UserHelper::SendSYSTEM_MESSAGE_INF(player, SystemMessageCode::InventoryFull,
				static_cast<int32_t>(target->GetItemTableId()), static_cast<int32_t>(target->GetCount()));
			return;
		}

		RemoveFieldItem(target);

		UserHelper::SendITEM_PICKUP_ACK(player, true);
		for (uint16_t slotIndex : touchedSlots)
			UserHelper::SendITEM_ACQUIRE_INF(player, slotIndex);
	}

	void HandleDespawn(const ObjID& itemId)
	{
		auto item = ::GetGameObject<Item>(itemId);
		if (item == nullptr)
			return;  // 이미 습득되었음 - 정상 경로, 아무 것도 하지 않는다

		RemoveFieldItem(item);
	}
}
