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
#include "Item/EquipmentItem.h"
#include "Item/DropTable.h"

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

	// ITEM_SLOT_DATA를 채우는 로직은 ITEM_LIST_ACK/ITEM_EQUIP_ACK/ITEM_UNEQUIP_ACK/
	// ITEM_SWAP_ACK가 전부 공유한다 — "슬롯을 어떻게 와이어 포맷으로 바꾸는가"를
	// 한 곳에만 두기 위함.
	void FillItemSlotData(ITEM_SLOT_DATA& data, uint16_t slotIndex, const Item::SharedPtr& item)
	{
		data.slotIndex = slotIndex;
		if (item == nullptr)
		{
			data.itemId = 0;
			data.count  = 0;
			data.equipped = 0;
			return;
		}

		auto equipment = dynamic_pointer_cast<EquipmentItem>(item);
		data.itemId   = item->GetItemTableId();
		data.count    = item->GetCount();
		data.equipped = (equipment != nullptr && equipment->IsEquipped()) ? 1 : 0;
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

	void SendITEM_LIST_ACK(Subject::SharedPtr sender)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		auto user = static_pointer_cast<User>(sender);
		if (user == nullptr)
			return;

		ITEM_LIST_ACK_PACKET packet;
		InitializePacket(packet, PacketType::ITEM_LIST_ACK);

		uint8_t slotCount = 0;
		for (const auto& [slotIndex, item] : user->GetInventory()->GetSlots())
		{
			if (slotCount >= MAX_INVENTORY_SLOTS)
				break;  // 이론상 불가능(슬롯 자체가 MAX_INVENTORY_SLOTS를 넘을 수 없음) — 방어적으로만 둠

			FillItemSlotData(packet.items[slotCount], slotIndex, item);
			++slotCount;
		}
		packet.slotCount = slotCount;

		session->PostSend(packet);
	}

	void SendITEM_EQUIP_ACK(Subject::SharedPtr sender, uint16_t slotIndex, bool success)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		auto user = static_pointer_cast<User>(sender);
		if (user == nullptr)
			return;

		ITEM_EQUIP_ACK_PACKET packet;
		InitializePacket(packet, PacketType::ITEM_EQUIP_ACK);
		packet.success = success ? 1 : 0;
		packet.slot.slotIndex = slotIndex;

		if (success)
		{
			const auto& slots = user->GetInventory()->GetSlots();
			const auto it = slots.find(slotIndex);
			// TryEquip이 true를 반환했다는 건 이 슬롯이 방금 장착된 장비라는 뜻이므로
			// 여기서 못 찾는 건 있을 수 없다 — 이 함수는 TryEquip과 같은 Zone 스레드
			// 위에서, 중간에 다른 mutate 없이 곧바로 호출되기 때문(단일 스레드 소유 모델).
			ASSERT_CRASH(it != slots.end());
			FillItemSlotData(packet.slot, slotIndex, it->second);
		}

		session->PostSend(packet);
	}

	void SendITEM_UNEQUIP_ACK(Subject::SharedPtr sender, uint16_t slotIndex, bool success)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		auto user = static_pointer_cast<User>(sender);
		if (user == nullptr)
			return;

		ITEM_UNEQUIP_ACK_PACKET packet;
		InitializePacket(packet, PacketType::ITEM_UNEQUIP_ACK);
		packet.success = success ? 1 : 0;
		packet.slot.slotIndex = slotIndex;

		if (success)
		{
			const auto& slots = user->GetInventory()->GetSlots();
			const auto it = slots.find(slotIndex);
			ASSERT_CRASH(it != slots.end());  // SendITEM_EQUIP_ACK와 같은 이유로 항상 존재해야 한다
			FillItemSlotData(packet.slot, slotIndex, it->second);
		}

		session->PostSend(packet);
	}

	void SendITEM_SWAP_ACK(Subject::SharedPtr sender, uint16_t slotIndexA, uint16_t slotIndexB, bool success)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		auto user = static_pointer_cast<User>(sender);
		if (user == nullptr)
			return;

		ITEM_SWAP_ACK_PACKET packet;
		InitializePacket(packet, PacketType::ITEM_SWAP_ACK);
		packet.success = success ? 1 : 0;
		packet.slotA.slotIndex = slotIndexA;
		packet.slotB.slotIndex = slotIndexB;

		if (success)
		{
			const auto& slots = user->GetInventory()->GetSlots();
			// 스왑 결과 어느 한쪽이 비게 될 수 있다(한쪽만 아이템이 있던 경우) — 그건
			// 정상 결과이므로 find 실패를 ASSERT_CRASH로 다루지 않고 nullptr로 넘겨
			// FillItemSlotData가 "빈 슬롯"(itemId=0, count=0)으로 채우게 한다.
			const auto itA = slots.find(slotIndexA);
			const auto itB = slots.find(slotIndexB);
			FillItemSlotData(packet.slotA, slotIndexA, itA != slots.end() ? itA->second : nullptr);
			FillItemSlotData(packet.slotB, slotIndexB, itB != slots.end() ? itB->second : nullptr);
		}

		session->PostSend(packet);
	}

	void SendITEM_ACQUIRE_INF(Subject::SharedPtr sender, uint16_t slotIndex)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		auto user = static_pointer_cast<User>(sender);
		if (user == nullptr)
			return;

		ITEM_ACQUIRE_INF_PACKET packet;
		InitializePacket(packet, PacketType::ITEM_ACQUIRE_INF);

		const auto& slots = user->GetInventory()->GetSlots();
		const auto it = slots.find(slotIndex);
		// TryAddItem이 돌려준 touchedSlots에 있던 인덱스로만 이 함수를 호출하므로,
		// 호출 시점에 그 슬롯이 비어있는 건 있을 수 없다(같은 Zone 스레드 위에서
		// 중간에 다른 mutate 없이 곧바로 호출됨 — 단일 스레드 소유 모델).
		ASSERT_CRASH(it != slots.end());
		FillItemSlotData(packet.slot, slotIndex, it->second);

		session->PostSend(packet);
	}

	void SendSYSTEM_MESSAGE_INF(Subject::SharedPtr sender, SystemMessageCode code, int32_t param1, int32_t param2)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		SYSTEM_MESSAGE_INF_PACKET packet;
		InitializePacket(packet, PacketType::SYSTEM_MESSAGE_INF);
		packet.code   = static_cast<uint16_t>(code);
		packet.param1 = param1;
		packet.param2 = param2;

		session->PostSend(packet);
	}

	void SendITEM_DISCARD_ACK(Subject::SharedPtr sender, uint16_t slotIndex, bool success)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		auto user = static_pointer_cast<User>(sender);
		if (user == nullptr)
			return;

		ITEM_DISCARD_ACK_PACKET packet;
		InitializePacket(packet, PacketType::ITEM_DISCARD_ACK);
		packet.success = success ? 1 : 0;
		packet.slot.slotIndex = slotIndex;

		if (success)
		{
			const auto& slots = user->GetInventory()->GetSlots();
			const auto it = slots.find(slotIndex);
			// it가 end()인 경우(슬롯 전량을 버려서 비워짐)도 FillItemSlotData가
			// nullptr을 받아 itemId=0, count=0으로 정상 처리한다.
			FillItemSlotData(packet.slot, slotIndex, it != slots.end() ? it->second : nullptr);
		}

		session->PostSend(packet);
	}

	void SendITEM_PICKUP_ACK(Subject::SharedPtr sender, bool success)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		ITEM_PICKUP_ACK_PACKET packet;
		InitializePacket(packet, PacketType::ITEM_PICKUP_ACK);
		packet.success = success ? 1 : 0;

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

	// 몬스터 처치 시 드롭 테이블을 굴려 성공하면 인벤토리에 채워 넣고, 결과를
	// 클라이언트에 알린다. AttackMonster가 이미 attacker를 소유한 Zone 스레드
	// 위에서 실행 중이므로(패킷 핸들러 -> HandleAttack/SkillAttack -> 여기), 그
	// 전제 위에서만 성립하는 Inventory::TryAddItem을 안전하게 바로 호출할 수 있다.
	void HandleItemDrop(const shared_ptr<User>& attacker)
	{
		const DropTable::RollResult roll = DropTable::Roll();
		if (!roll.hasItem)
			return;

		std::vector<uint16_t> touchedSlots;
		if (attacker->GetInventory()->TryAddItem(roll.itemId, roll.count, &touchedSlots))
		{
			for (uint16_t slotIndex : touchedSlots)
				SendITEM_ACQUIRE_INF(attacker, slotIndex);
		}
		else
		{
			// 인벤토리가 가득 차 드롭을 담지 못했다 — 그냥 버리지 않고 반드시 알린다.
			SendSYSTEM_MESSAGE_INF(attacker, SystemMessageCode::InventoryFull,
				static_cast<int32_t>(roll.itemId), static_cast<int32_t>(roll.count));
		}
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
		HandleItemDrop(attacker);

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

	void HandleGetUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo, const vector<DB_ITEM_INFO>& items)
	{
		if (session == nullptr || session->m_state != SOCKET_STATE::ST_ALLOC)
			return;

		auto player = std::make_shared<User>();
		player->SetObjID(EnumCategory::eUser, static_cast<uint64_t>(session->m_objectId));
		player->InitInstance();
		player->SetName(userInfo._name);
		player->SetPlayerId(userInfo._playerId);
		player->GetStat()->SetLevel(userInfo._level);
		player->GetStat()->SetExp(userInfo._exp);
		// GameObjectManager에 공개되기 전(아직 이 스레드만 접근 가능한 시점)에 복원한다.
		player->GetInventory()->LoadFromDB(items);

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
		SendITEM_LIST_ACK(player);
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
		// 계정 INSERT는 이미 DB 스레드에서 동기적으로 끝난 뒤(playerId 확보 후)
		// 이 함수가 호출된다 — 여기서 다시 저장을 요청할 필요가 없다.
		player->SetPlayerId(userInfo._playerId);

		if (!GGameObjectManager->Insert(player->GetObjID(), player))
			return;

		session->BindOwner(player);
		player->SetGameSession(session);
		session->m_state = SOCKET_STATE::ST_INGAME;

		ObjID objId = player->GetObjID();
		SectorHelper::GetRandomPosition(objId);

		SendUSER_LOGIN_ACK(player);

		GTimerThread->ScheduleAfter(objId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
		SectorHelper::NotifyPlayerEnteredWorld(objId, false);
	}
}
