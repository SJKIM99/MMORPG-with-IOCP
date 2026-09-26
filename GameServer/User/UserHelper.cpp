#include "pch.h"
#include "Combat/CombatHelper.h"
#include "Data/MoveRule.h"
#include "UserHelper.h"
#include "Combat/SkillEx.h"
#include "Combat/SkillGrow.h"
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
#include "Item/ItemHelper.h"
#include "Item/ItemTableData.h"
#include "Data/ItemRules.h"
#include "Data/LevelTable.h"
#include "Data/MonsterTable.h"
#include "Data/WorldData.h"
#include "Content/ContentHelper.h"
#include "Quest/QuestHelper.h"
#include "MonsterHelper.h"
#include "GameSessionManager.h"
#include "Zone/ZoneLayout.h"

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

	// 확장 프로토콜 세션이면서 게임 안에 있는 유저인가(Subject가 User일 때만 true)
	bool IsExtendedViewer(const Subject::SharedPtr& subject)
	{
		if (subject == nullptr || subject->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
			return false;
		const auto session = static_pointer_cast<User>(subject)->GetGameSession();
		return session != nullptr && session->m_state == SOCKET_STATE::ST_INGAME && session->IsExtended();
	}

	uint8_t BuildSubjectExFlags(const shared_ptr<User>& user)
	{
		const uint32_t now = GetNowTime();
		uint8_t flags = 0;
		if (user->IsPurple(now))                   flags |= SUBJECT_EX_PURPLE;
		if (user->IsHasted(now))                   flags |= SUBJECT_EX_HASTE;
		if (user->GetStat()->IsDead())             flags |= SUBJECT_EX_DEAD;
		if (WorldData::IsSafeZone(user->GetX(), user->GetY())) flags |= SUBJECT_EX_SAFE;
		return flags;
	}

	// 몬스터 처치 드롭을 놓을 칸 — 죽은 칸부터 시작해 주변으로 퍼뜨린다(한 칸에 여러 개가
	// 겹치면 줍기 대상이 가려지므로). 같은 Zone, 막히지 않은 칸, 안전지대 밖만 쓴다.
	bool PickDropTile(short originX, short originY, int index, short& outX, short& outY)
	{
		static constexpr int kOffsets[][2] = {
			{ 0, 0 }, { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 },
			{ 2, 0 }, { 0, 2 }, { -2, 0 }, { 0, -2 }, { 2, 1 }, { 1, 2 }, { -2, -1 }, { -1, -2 },
		};
		const ZoneId originZone = ZoneLayout::GetZoneIdByWorld(originX, originY);
		int found = 0;
		for (const auto& off : kOffsets)
		{
			const int x = originX + off[0];
			const int y = originY + off[1];
			if (!ZoneLayout::IsValidWorldPosition(x, y) || isCollision(static_cast<short>(x), static_cast<short>(y)))
				continue;
			if (ZoneLayout::GetZoneIdByWorld(x, y) != originZone || WorldData::IsSafeZone(x, y))
				continue;
			if (found++ == index)
			{
				outX = static_cast<short>(x);
				outY = static_cast<short>(y);
				return true;
			}
		}
		return false;
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
		// 예전엔 항상 0이던 필드 — 이 걸음에 걸린 시간(ms, 0 = 순간이동). 봇은 읽지 않는다.
		packet.move_time = target->GetLastStepMs();

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
		const EnumCategory category = target->GetObjID().GetCategory<EnumCategory>();
		if (category == EnumCategory::eMonster)
		{
			const auto monster = static_pointer_cast<Monster>(target);
			packet.monster_type = static_cast<char>(monster->GetType());
		}
		else if (category == EnumCategory::eUser)
		{
			// 지금 장착 중인 무기 — 새로 시야에 들어온 플레이어를 처음 그릴 때부터
			// 바로 보이게 한다. 이후 장착/탈착이 바뀌면 SUBJECT_EQUIP_CHANGE_NFY로 갱신.
			const auto user = static_pointer_cast<User>(target);
			packet.itemId = static_cast<uint16_t>(user->GetInventory()->GetEquippedItemId());
		}
		else if (category == EnumCategory::eItem)
		{
			const auto item = static_pointer_cast<Item>(target);
			packet.itemId = item->GetItemTableId();
		}
		packet.id = target->GetObjID();
		packet.x = target->GetX();
		packet.y = target->GetY();
		::strncpy_s(packet.name, NAME_SIZE, target->GetName().c_str(), _TRUNCATE);

		session->PostSend(packet);

		// 확장 세션에는 레벨/성향치/강화 수치 같은 추가 정보를 바로 이어서 보낸다.
		// (몬스터는 종족이 ObjID KIND에 실려 있어 추가 정보가 필요 없다 — 다만 이미
		//  깎인 HP로 시야에 들어오면 HP 바를 맞추기 위해 보낸다.)
		if (session->IsExtended())
		{
			if (category == EnumCategory::eUser)
			{
				SendSUBJECT_INFO_EX_NFY(sender, target);
				SendPLAYER_LOOK_NFY(sender, static_pointer_cast<User>(target));
			}
			else if (category == EnumCategory::eItem)
			{
				// 강화됐거나 희귀도가 일반보다 높은 장비는 바닥에서도 빛나야 한다(빛기둥 색).
				const auto equipment = dynamic_pointer_cast<EquipmentItem>(target);
				if (equipment != nullptr)
				{
					const ItemTableRow* row = ItemTableData::Find(equipment->GetItemTableId());
					const bool rare = row != nullptr && ItemRules::EffectiveRarity(*row, equipment->GetExtra()) != Rarity::Common;
					if (equipment->GetEnchant() > 0 || rare)
						SendSUBJECT_INFO_EX_NFY(sender, target);
				}
			}
			else if (category == EnumCategory::eMonster)
			{
				if (target->GetStat()->GetHp() < target->GetStat()->GetMaxHp())
					SendSUBJECT_INFO_EX_NFY(sender, target);
			}
		}
	}

	void SendSUBJECT_EQUIP_CHANGE_NFY(Subject::SharedPtr sender, const ObjID& targetId, uint16_t itemId)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		SUBJECT_EQUIP_CHANGE_NFY_PACKET packet;
		InitializePacket(packet, PacketType::SUBJECT_EQUIP_CHANGE_NFY);
		packet.id     = targetId;
		packet.itemId = itemId;

		session->PostSend(packet);
	}

	void BroadcastEquipChange(const shared_ptr<User>& player)
	{
		if (player == nullptr)
			return;

		const uint16_t itemId = static_cast<uint16_t>(player->GetInventory()->GetEquippedItemId());
		const ObjID playerId = player->GetObjID();

		GSector->ForEachNeighborObject(player->GetSectorX(), player->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
				return;

			auto viewer = static_pointer_cast<User>(object);
			auto session = viewer->GetGameSession();
			if (!session || session->m_state != SOCKET_STATE::ST_INGAME)
				return;
			if (!SubjectHelper::CanSee(object, player))
				return;

			SendSUBJECT_EQUIP_CHANGE_NFY(viewer, playerId, itemId);
			// 확장 세션에는 전 부위 외형(방어구·망토·희귀도 빛)도 함께
			if (session->IsExtended())
				SendPLAYER_LOOK_NFY(viewer, player);
		});

		// 장비가 바뀌면 능력치(공격력·방어·최대 HP·이동속도…)도 바뀐다 — 본인에게 새 값.
		if (player->IsExtended())
		{
			CombatHelper::SendStats(player);
			CombatHelper::SendResource(player);
			QuestHelper::OnEquipChanged(player);   // "N부위 착용" 목표
		}
	}

	void SendPLAYER_LOOK_NFY(Subject::SharedPtr viewer, const shared_ptr<User>& player)
	{
		if (player == nullptr || !IsExtendedViewer(viewer))
			return;
		auto session = GetSession(viewer);
		if (!session)
			return;

		PLAYER_LOOK_NFY_PACKET packet;
		InitializePacket(packet, PacketType::PLAYER_LOOK_NFY);
		packet.id = player->GetObjID();
		auto inventory = player->GetInventory();
		const auto& slots = inventory->GetSlots();
		for (int part = 1; part <= kLookSlots; ++part)
		{
			const uint16_t slotIndex = inventory->FindEquipped(static_cast<EquipSlot>(part));
			if (slotIndex >= MAX_INVENTORY_SLOTS)
				continue;
			const auto& item = slots.at(slotIndex);
			const ItemTableRow* row = ItemTableData::Find(item->GetItemTableId());
			if (row == nullptr)
				continue;
			packet.items[part - 1] = item->GetItemTableId();
			packet.rarity[part - 1] = static_cast<uint8_t>(ItemRules::EffectiveRarity(*row, inventory->GetSlotExtra(slotIndex)));
		}
		packet.enchant = inventory->GetEquippedEnchant();
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

		if (session->IsExtended() && target->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eUser)
			SendSUBJECT_INFO_EX_NFY(sender, target);
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

	void SendITEM_USE_ACK(Subject::SharedPtr sender, uint16_t slotIndex, bool success)
	{
		auto session = GetSession(sender);
		if (!session)
			return;

		auto user = static_pointer_cast<User>(sender);
		if (user == nullptr)
			return;

		ITEM_USE_ACK_PACKET packet;
		InitializePacket(packet, PacketType::ITEM_USE_ACK);
		packet.success = success ? 1 : 0;
		packet.slot.slotIndex = slotIndex;

		if (success)
		{
			const auto& slots = user->GetInventory()->GetSlots();
			const auto it = slots.find(slotIndex);
			// it가 end()인 경우(마지막 1개를 소비해 슬롯이 비워짐)도 FillItemSlotData가
			// nullptr을 받아 itemId=0, count=0으로 정상 처리한다(ITEM_DISCARD_ACK와 같은 관례).
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
		ContentHelper::SaveAlignment(target, true);
		if (target->IsExtended())
			SaveProgress(target);   // 드롭 천장 카운터 등
		return true;
	}

	// ── V2 드롭(확장 세션이 잡은 몬스터) ─────────────────────────────────────────
	// 장비에는 희귀도·옵션을 굴려 붙이고(Data/ItemRules.h), 단계별로 방어구·장신구·
	// 직업 무기를 한 번 더 굴린다. 레거시 세션(STRESS_TEST 봇)이 잡은 몬스터는 예전 표 그대로다.
	namespace
	{
		struct GearPick { ItemTableId id; uint16_t weight; };
		constexpr ItemTableId kClassWeapon = 0xFFFF;   // "잡은 사람 직업의 이 단계 무기" 자리표시

		constexpr GearPick kGearT1[] = { { 29, 10 }, { 32, 10 }, { 37, 10 }, { 39, 10 }, { 41, 8 }, { 43, 6 }, { 45, 6 } };
		constexpr GearPick kGearT2[] = { { 30, 10 }, { 33, 10 }, { 35, 8 }, { 37, 5 }, { 39, 5 }, { 41, 5 }, { 43, 5 }, { 45, 5 }, { kClassWeapon, 12 } };
		constexpr GearPick kGearT3[] = { { 30, 6 }, { 33, 6 }, { 36, 8 }, { 38, 10 }, { 40, 10 }, { 42, 8 }, { 44, 6 }, { 46, 6 }, { kClassWeapon, 10 } };
		constexpr GearPick kGearT4[] = { { 31, 8 }, { 34, 8 }, { 38, 6 }, { 40, 6 }, { 42, 6 }, { 44, 6 }, { 46, 6 },
			{ 47, 2 }, { 48, 2 }, { 49, 2 }, { 50, 2 }, { 51, 2 }, { 52, 2 }, { 53, 2 }, { 54, 2 }, { 55, 2 }, { 56, 2 }, { 57, 2 }, { 58, 2 },
			{ kClassWeapon, 8 } };
		constexpr GearPick kGearT5[] = { { 31, 8 }, { 34, 8 }, { 42, 5 }, { 44, 6 }, { 46, 6 },
			{ 47, 3 }, { 48, 3 }, { 49, 3 }, { 50, 3 }, { 51, 3 }, { 52, 3 }, { 53, 3 }, { 54, 3 }, { 55, 3 }, { 56, 3 }, { 57, 3 }, { 58, 3 },
			{ kClassWeapon, 10 } };
		constexpr GearPick kGearBoss[] = { { 31, 6 }, { 34, 6 },
			{ 47, 5 }, { 48, 5 }, { 49, 5 }, { 50, 5 }, { 51, 5 }, { 52, 5 }, { 53, 5 }, { 54, 5 }, { 55, 5 }, { 56, 5 }, { 57, 5 }, { 58, 5 },
			{ kClassWeapon, 12 } };
		// 처치 한 번에 방어구·장신구·직업 무기가 하나 더 나올 확률(%) — 단계 0은 1단계와 같다
		constexpr uint32_t kGearChance[6] = { 8, 8, 9, 10, 11, 12 };

		ItemTableId ClassWeaponFor(ClassId cls, int tier)
		{
			const int t = std::clamp(tier, 1, 5);
			switch (cls)
			{
			case ClassId::Engraver:
			{
				static constexpr ItemTableId kStaff[6] = { 19, 19, 21, 22, 23, 24 };
				return kStaff[t];
			}
			case ClassId::NightHunter:
			{
				static constexpr ItemTableId kDagger[6] = { 20, 20, 25, 26, 27, 28 };
				return kDagger[t];
			}
			default:
			{
				static constexpr ItemTableId kSwordA[6] = { 2, 2, 3, 4, 5, 6 };
				static constexpr ItemTableId kSwordB[6] = { 2, 2, 3, 16, 17, 18 };
				return (LRng() % 2u) ? kSwordA[t] : kSwordB[t];
			}
			}
		}

		// 예전 표가 굴린 무기가 내 직업 무기 종류가 아니면(각인술사에게 검 등) 60% 확률로
		// 같은 단계의 직업 무기로 바꿔 준다 — "주울 만한 것"이 더 자주 떨어지게.
		ItemTableId MaybeClassSwap(ItemTableId id, ClassId cls)
		{
			const ItemTableRow* row = ItemTableData::Find(id);
			if (row == nullptr || ItemTableData::SlotOf(*row) != EquipSlot::Weapon)
				return id;
			const WeaponKind want = cls == ClassId::Engraver ? WeaponKind::Staff : (cls == ClassId::NightHunter ? WeaponKind::Dagger : WeaponKind::None);
			if (want == WeaponKind::None || row->weaponKind == want)
				return id;
			return (LRng() % 100u) < 60u ? ClassWeaponFor(cls, row->tier) : id;
		}

		template <size_t N>
		ItemTableId PickGear(const GearPick (&pool)[N], ClassId cls, int tier)
		{
			uint32_t total = 0;
			for (const auto& g : pool) total += g.weight;
			uint32_t roll = total > 0 ? static_cast<uint32_t>(LRng()) % total : 0;
			for (const auto& g : pool)
			{
				if (roll < g.weight)
					return g.id == kClassWeapon ? ClassWeaponFor(cls, tier) : g.id;
				roll -= g.weight;
			}
			return pool[0].id;
		}

		ItemTableId PickGearForTier(int tier, bool boss, ClassId cls)
		{
			if (boss) return PickGear(kGearBoss, cls, 5);
			switch (std::clamp(tier, 1, 5))
			{
			case 1: return PickGear(kGearT1, cls, 1);
			case 2: return PickGear(kGearT2, cls, 2);
			case 3: return PickGear(kGearT3, cls, 3);
			case 4: return PickGear(kGearT4, cls, 4);
			default: return PickGear(kGearT5, cls, 5);
			}
		}

		// 장비라면 희귀도·옵션을 굴려 붙인다(잡은 사람의 희귀품 발견 · 천장 반영).
		void RollInstanceFor(const shared_ptr<User>& finder, const Item::SharedPtr& item, int tier, bool boss)
		{
			const auto equipment = dynamic_pointer_cast<EquipmentItem>(item);
			if (equipment == nullptr)
				return;
			const ItemTableRow* row = ItemTableData::Find(item->GetItemTableId());
			if (row == nullptr)
				return;
			auto rng = []() { return static_cast<uint32_t>(LRng()); };
			const Rarity rarity = ItemRules::RollRarity(*row, tier, boss, finder->m_gearEx.itemFind,
				finder->m_pityRare, finder->m_pityHeroic, rng);
			equipment->SetExtra(ItemRules::RollInstance(*row, rarity, finder->m_classId, rng).Pack());
			// 천장 카운터가 0으로 돌아간 순간을 남긴다(그 사이 값은 로그아웃 때 저장)
			if (static_cast<int>(rarity) >= static_cast<int>(Rarity::Rare))
				SaveProgress(finder);
		}
	}

	// 몬스터 처치 시 드롭을 굴려 몬스터가 죽은 자리 주변에 필드 아이템으로
	// 떨어뜨린다(인벤토리에 즉시 들어가지 않는다) — 처치자가
	// ItemHelper::kLootPriorityDuration(10초) 동안 독점적으로 주울 수 있고, 그 뒤
	// 30초가 될 때까지는 누구나 주울 수 있다가 소멸한다. AttackMonster가 이미
	// attacker를 소유한 Zone 스레드 위에서 실행 중이므로(패킷 핸들러 ->
	// HandleAttack/SkillAttack -> 여기), 그 전제 위에서만 성립하는
	// ItemHelper::SpawnFieldItem을 안전하게 바로 호출할 수 있다.
	//
	// 확장: 종족별 드롭 테이블(DropTable 번호)과 굴림 횟수(보스는 여러 번), 그로시 드롭.
	void HandleItemDrop(const shared_ptr<User>& attacker, const MonsterTableRow* species, short x, short y)
	{
		int tileIndex = 0;
		auto spawnAtNextTile = [&](const Item::SharedPtr& item)
		{
			short dropX = x, dropY = y;
			if (!PickDropTile(x, y, tileIndex++, dropX, dropY))
			{
				dropX = x;
				dropY = y;
			}
			ItemHelper::SpawnFieldItem(item, dropX, dropY, attacker->GetObjID());
		};

		const bool v2 = attacker->IsExtended();
		const int tier = species == nullptr ? 1 : (species->dropTable >= 1 && species->dropTable <= 5 ? species->dropTable : 5);
		const bool boss = species != nullptr && species->boss;

		// 그로시 — 리니지처럼 바닥에 떨어진 "물건"이고, 주우면 지갑으로 들어간다.
		if (species != nullptr && species->adenaMax > 0 && (LRng() % 100) < species->adenaChance)
		{
			std::uniform_int_distribution<uint32_t> amountRoll(species->adenaMin, species->adenaMax);
			uint32_t rolled = amountRoll(LRng);
			if (v2 && attacker->m_gearEx.adenaFind > 0)   // 장비 옵션 "그로시 획득"
				rolled = rolled * (100u + attacker->m_gearEx.adenaFind) / 100u;
			const uint16_t amount = static_cast<uint16_t>(std::min<uint32_t>(rolled, 65535));
			if (amount > 0)
			{
				Item::SharedPtr adena = MakeNewItem(ItemTableData::Id::Adena, amount);
				ASSERT_CRASH(adena != nullptr);
				spawnAtNextTile(adena);
			}
		}

		const uint8_t tableId = species != nullptr ? species->dropTable : 1;
		const uint8_t rolls   = species != nullptr ? species->dropRolls : 1;
		for (uint8_t i = 0; i < rolls; ++i)
		{
			const DropTable::RollResult roll = DropTable::Roll(tableId);
			if (!roll.hasItem)
				continue;

			const ItemTableId itemId = v2 ? MaybeClassSwap(roll.itemId, attacker->m_classId) : roll.itemId;
			Item::SharedPtr item = MakeNewItem(itemId, roll.count);
			// DropTable은 시작 시점에 ItemTable과 대조 검증을 마쳤으므로(DropTable.cpp의
			// ValidateDropTable) 여기서 실패할 수 없다 — 실패한다면 그 자체가 불변조건 위반이다.
			ASSERT_CRASH(item != nullptr);
			if (v2)
				RollInstanceFor(attacker, item, tier, boss);
			spawnAtNextTile(item);
		}

		// V2: 방어구·장신구·직업 무기 추가 굴림(보스는 여러 개)
		if (v2)
		{
			const int pieces = boss ? 3 : ((LRng() % 100u) < kGearChance[tier] ? 1 : 0);
			for (int i = 0; i < pieces; ++i)
			{
				Item::SharedPtr item = MakeNewItem(PickGearForTier(tier, boss, attacker->m_classId), 1);
				ASSERT_CRASH(item != nullptr);
				RollInstanceFor(attacker, item, tier, boss);
				spawnAtNextTile(item);
			}
		}
	}

	void GainExp(const shared_ptr<User>& player, uint32_t amount)
	{
		if (player == nullptr)
			return;

		const uint8_t gained = player->GetStat()->AddExp(amount);
		if (gained > 0)
		{
			// 레벨업: 최대 HP/기본 공격력 재계산 + HP 가득(연출상 "레벨업 = 부활의 순간")
			player->ApplyLevelStats(true);
			if (player->IsExtended())
			{
				CombatHelper::SendStats(player);
				CombatHelper::SendResource(player);
			}
		}

		// 레거시 알림은 예전과 같은 조건(경험치가 바뀔 때마다)으로 보낸다.
		SendUSER_STAT_CHANGE_INF(player);

		if (player->IsExtended())
		{
			SendUSER_INFO_EX_INF(player);
			if (gained > 0)
			{
				SkillGrow::SendTree(player);   // 기술 점수 +2(레벨마다)
				SendSystemEx(player, SystemMessageCode::LevelUp, player->GetStat()->GetLevel());
				BroadcastEffect(player, player->GetObjID(), EffectId::LevelUp, player->GetX(), player->GetY(), player->GetStat()->GetLevel());
			}
		}

		// 주변 확장 세션에는 레벨/HP가 바뀐 모습을 다시 알린다(레벨업 때만).
		if (gained > 0)
			BroadcastInfoEx(player);
	}

	void AttackMonster(ObjID& monsterId, ObjID& playerId, int damage, uint8_t damageFlags,
		uint16_t skillId, uint8_t comboId, uint8_t flags2, uint8_t hitIndex)
	{
		auto monster = ::GetGameObject<Monster>(monsterId);
		auto attacker = ::GetGameObject<User>(playerId);
		if (monster == nullptr || attacker == nullptr)
			return;
		if (monster->GetStat()->IsDead())
			return;

		const uint16_t clamped = static_cast<uint16_t>(std::clamp(damage, 1, 65535));
		// Phase 2-E 허수아비: 맞기만 하고 죽지 않는다(HP를 다시 채움) · 반격 · 경험치 · 전리품 없음
		if (monster->IsDummy())
		{
			BroadcastDamage(monster, playerId, clamped, damageFlags, skillId, comboId, flags2, hitIndex);
			monster->GetStat()->SetHp(monster->GetStat()->GetMaxHp());
			return;
		}
		const uint16_t remaining = monster->GetStat()->TakeDamage(clamped);
		if (remaining > 0)
		{
			SendUSER_ATTACK_ACK(attacker, monster, clamped);
			BroadcastDamage(monster, playerId, clamped, damageFlags, skillId, comboId, flags2, hitIndex);
			// 리니지처럼 맞으면 반격한다(선공 몬스터가 아니어도 때린 사람을 쫓는다).
			MonsterHelper::Provoke(monster, attacker);
			return;
		}

		// RemoveObject는 섹터 좌표만 지우고 월드 좌표(x,y)는 건드리지 않지만,
		// "죽은 자리"라는 의도를 코드에서 바로 알아보도록 미리 값으로 떼어둔다.
		const short deathX = monster->GetX();
		const short deathY = monster->GetY();

		BroadcastDamage(monster, playerId, clamped, static_cast<uint8_t>(damageFlags | DAMAGE_KILLED), skillId, comboId, flags2, hitIndex);
		monster->Status().Clear();

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
		monster->ClearProvoke();
		monster->ClearViewList(); // 다음 리스폰 시 oldList가 빈 상태로 시작하도록 초기화

		const MonsterTableRow* species = monster->GetSpecies();
		const uint32_t expGain = (species != nullptr) ? species->exp
			: ((monster->GetType() == MONSTER_TYPE::PASSIVE) ? 3 : 5);
		GainExp(attacker, expGain);
		ContentHelper::OnMonsterKilled(attacker, monster);
		HandleItemDrop(attacker, species, deathX, deathY);

		const bool isBoss = species != nullptr && species->boss;
		// V2 퀘스트: 처치 · 수집(막타), 보스는 가까이 있던 모두
		QuestHelper::OnKill(attacker, species, deathX, deathY);
		if (isBoss)
			QuestHelper::OnBossKilled(species->id, deathX, deathY);
		GTimerThread->ScheduleAfter(monsterId, isBoss ? std::chrono::seconds(WorldData::kBossRespawnSeconds) : std::chrono::seconds(10),
			TIMER_EVENT_TYPE::EV_MONSTER_RESPAWN);
	}

	void SkillAttack(ObjID& playerId)
	{
		auto player = ::GetGameObject<User>(playerId);
		if (player == nullptr || player->GetStat()->IsDead())
			return;
		// 안전지대(마을)에서는 전투 행동을 하지 않는다.
		if (WorldData::IsSafeZone(player->GetX(), player->GetY()))
		{
			SendSystemEx(player, SystemMessageCode::SafeZoneNoCombat);
			return;
		}

		const int px = player->GetX();
		const int py = player->GetY();

		// 확장 세션에는 시전 이펙트를 먼저 알린다(맞은 몬스터가 없어도 기술은 보인다).
		BroadcastEffect(player, playerId, EffectId::SkillCross, static_cast<short>(px), static_cast<short>(py), 0);

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

		const int damage = static_cast<int>(CombatRule::SkillDamage(player->GetStat()->GetOffensive()));
		for (ObjID& monsterId : targets)
			AttackMonster(monsterId, playerId, damage, DAMAGE_SKILL);
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

		// 마을 안에서는 휘두르기만 보이고 피해는 없다.
		if (WorldData::IsSafeZone(attacker->GetX(), attacker->GetY()))
			return;

		// 장비·강화 보너스가 반영된 현재 공격력(Inventory::RecomputeOffensive가 항상 최신으로
		// 유지)에 ±15% 편차와 치명타를 대상마다 따로 굴린다.
		const uint32_t attack = attacker->GetStat()->GetOffensive();
		for (ObjID& monsterId : monsterTargets)
		{
			bool crit = false;
			const uint32_t damage = CombatRule::RollDamage(attack, LRng() % 1001, LRng() % 100, crit);
			AttackMonster(monsterId, attackerId, static_cast<int>(damage), crit ? DAMAGE_CRIT : 0);
		}
	}

	void BroadcastChat(Subject::SharedPtr sender, const char mess[])
	{
		// '&'로 시작하는 채팅은 전체 채팅(확장 세션 전용 기능)
		const auto senderUser = static_pointer_cast<User>(sender);
		if (mess[0] == '&' && senderUser != nullptr && senderUser->IsExtended())
		{
			BroadcastWorldChat(senderUser, mess + 1);
			return;
		}

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

		// 레벨이 오를수록 회복량이 늘고, 마을(안전지대)에서는 두 배로 회복한다.
		const bool inSafeZone = WorldData::IsSafeZone(player->GetX(), player->GetY());
		const uint16_t before = player->GetStat()->GetHp();
		if (player->IsExtended())
		{
			// V2: 능력치 기반 회복. 전투에서 벗어나 4초가 지나면 세 배(쉬면 빨리 찬다).
			const uint32_t now = GetNowTime();
			const bool resting = now - player->m_lastCombatAt > 4000;
			const uint32_t mul = inSafeZone ? 4u : (resting ? 3u : 1u);
			player->GetStat()->HealHp(static_cast<uint16_t>(std::min<uint32_t>(65535, player->m_derived.hpRegen * mul)), player->GetStat()->GetMaxHp());
			const uint16_t mpBefore = player->m_mp;
			player->m_mp = static_cast<uint16_t>(std::min<uint32_t>(player->m_derived.maxMp, player->m_mp + player->m_derived.mpRegen * mul));
			if (player->GetStat()->GetHp() != before || player->m_mp != mpBefore)
				CombatHelper::SendResource(player);
			GTimerThread->ScheduleAfter(playerId, 2500ms, TIMER_EVENT_TYPE::EV_HEAL);
			return;
		}
		player->GetStat()->HealHp(LevelTable::RegenPerTick(player->GetStat()->GetLevel(), inSafeZone), player->GetStat()->GetMaxHp());
		if (player->GetStat()->GetHp() != before || !player->IsExtended())
			SendUSER_HEAL_INF(player);  // 확장 세션에는 실제로 바뀌었을 때만(가득 찬 상태면 생략)
		GTimerThread->ScheduleAfter(playerId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
	}

	void HandleRespawn(const ObjID& playerId, uint32_t deathSeq)
	{
		auto player = ::GetGameObject<User>(playerId);
		if (player == nullptr)
			return;
		// 그 사이에 이미 리스타트했거나(살아 있음), 다시 죽어서 새 타이머가 걸렸다면 무시한다.
		if (!player->GetStat()->IsDead() || player->m_deathSeq != deathSeq)
			return;

		// 확장 세션(새 클라이언트)은 리니지처럼 마을에서 부활한다. 보통은 사망 창의
		// "마을에서 다시 시작" 버튼(USER_RESTART_REQ)이 먼저 오고, 이 타이머는 그걸
		// 누르지 않았을 때의 안전장치다.
		if (player->IsExtended())
		{
			ContentHelper::RestartInTown(player);
			return;
		}

		// 레거시(봇) 세션은 예전 동작 그대로 — 같은 Zone 안의 무작위 위치에서 부활.
		// 봇이 전부 마을 Zone으로 모이면 기존 부하 분산 조건이 깨지기 때문이다.
		SectorHelper::GetRandomPosition(const_cast<ObjID&>(playerId));
		player->GetStat()->SetDead(false);
		player->GetStat()->SetHp(player->GetStat()->GetMaxHp());

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

	void HandleGetUserInfo(const shared_ptr<GameSession>& session, const DB_USER_INFO& userInfo, const vector<DB_ITEM_INFO>& items,
		const std::vector<uint8_t>& progress, const std::vector<DB_SKILL_INFO>& skills)
	{
		if (session == nullptr || session->m_state != SOCKET_STATE::ST_ALLOC)
			return;

		auto player = std::make_shared<User>();
		player->SetObjID(EnumCategory::eUser, static_cast<uint64_t>(session->m_objectId));
		player->InitInstance();
		player->SetName(userInfo._name);
		player->SetPlayerId(userInfo._playerId);
		player->GetStat()->SetLevel(std::clamp<uint8_t>(userInfo._level, 1, LevelTable::kMaxLevel));
		player->GetStat()->SetExp(userInfo._exp);
		player->SetAlignment(userInfo._alignment);
		player->m_savedAlignment = userInfo._alignment;
		player->SetPkCount(userInfo._pkCount);
		// GameObjectManager에 공개되기 전(아직 이 스레드만 접근 가능한 시점)에 복원한다.
		player->GetInventory()->LoadFromDB(items);
		player->GetInventory()->LoadAdena(userInfo._adena);
		// 레벨에 맞는 최대 HP/공격력으로 맞추고 HP를 채운다(LoadFromDB가 장착 무기까지 복원한 뒤).
		player->ApplyLevelStats(true);

		if (!GGameObjectManager->Insert(player->GetObjID(), player))
			return;

		session->BindOwner(player);
		player->SetGameSession(session);
		session->m_state = SOCKET_STATE::ST_INGAME;
		// V2: 직업·포인트 복원 후 능력치를 다시 계산(세션이 붙은 뒤라 확장 공식이 적용된다).
		if (player->IsExtended())
		{
			// V2 기록이 없는 기존 캐릭터는 이번 접속에서 고른 직업으로 시작한다(한 번만).
			if (!player->DeserializeProgress(progress))
			{
				player->m_classId = static_cast<ClassId>(ClassTable::IsValid(session->GetNewClassId()) ? session->GetNewClassId() : 0);
				if (player->m_classId != ClassId::Knight)
				{
					// 새 직업의 초보자 무기를 쥐여 준다(기사는 가진 검을 그대로 쓴다).
					std::vector<uint16_t> touched;
					if (player->GetInventory()->TryAddItem(ClassTable::Get(player->m_classId).starterWeapon, 1, &touched) && !touched.empty())
						(void)player->GetInventory()->TryEquip(touched.front());
				}
				SaveProgress(player);
			}
			player->RecomputeStats(true);
			// Phase 2-D 기술 레벨 · 갈래(직업이 정해진 뒤 — 다른 직업의 기록은 버린다)
			(void)SkillGrow::Load(player, skills);
		}

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
		if (player->IsExtended())
		{
			SendSERVER_CONFIG_INF(player);
			SendITEM_LIST_EX_ACK(player);
			SendUSER_INFO_EX_INF(player);
			SendADENA_INF(player, 0, AdenaReason::Init);
			CombatHelper::SendStats(player);
			CombatHelper::SendResource(player);
			QuestHelper::SendList(player);
			SkillGrow::SendTree(player);
		}
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
		player->ApplyLevelStats(true);

		if (!GGameObjectManager->Insert(player->GetObjID(), player))
			return;

		session->BindOwner(player);
		player->SetGameSession(session);
		session->m_state = SOCKET_STATE::ST_INGAME;

		ObjID objId = player->GetObjID();
		const bool extended = player->IsExtended();
		if (extended)
		{
			// 로그인 화면에서 고른 직업으로 시작한다.
			player->m_classId = static_cast<ClassId>(ClassTable::IsValid(session->GetNewClassId()) ? session->GetNewClassId() : 0);
			player->RecomputeStats(true);
			SaveProgress(player);
		}
		if (extended && LCurrentZoneId == static_cast<ZoneId>(WorldData::kTownZoneId))
		{
			// 새 클라이언트의 신규 캐릭터는 마을 광장에서 시작한다(DBThread가 이미
			// 마을 Zone 스레드로 보냈다).
			const auto& spawn = WorldData::kTownSpawnPoints[LRng() % WorldData::kTownSpawnPointCount];
			SectorHelper::UpdatePosition(objId, spawn.x, spawn.y);
		}
		else
		{
			SectorHelper::GetRandomPosition(objId);
		}

		SendUSER_LOGIN_ACK(player);
		if (extended)
		{
			SendSERVER_CONFIG_INF(player);
			SendITEM_LIST_EX_ACK(player);
			// 리니지의 "초보자 지원" — 물약/두루마리/무기/그로시를 쥐여 준다.
			ContentHelper::GiveStarterKit(player);
			player->RecomputeStats(true);
			SendUSER_INFO_EX_INF(player);
			CombatHelper::SendStats(player);
			CombatHelper::SendResource(player);
			QuestHelper::SendList(player);
			SkillGrow::SendTree(player);
		}

		GTimerThread->ScheduleAfter(objId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
		SectorHelper::NotifyPlayerEnteredWorld(objId, false);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// 확장 프로토콜 송신
	// ─────────────────────────────────────────────────────────────────────────
	void SendUSER_INFO_EX_INF(const shared_ptr<User>& user)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		auto session = user->GetGameSession();
		if (!session)
			return;

		const uint32_t now = GetNowTime();
		USER_INFO_EX_INF_PACKET packet;
		InitializePacket(packet, PacketType::USER_INFO_EX_INF);
		packet.level     = user->GetStat()->GetLevel();
		packet.exp       = user->GetStat()->GetExp();
		packet.expToNext = LevelTable::ExpToNext(packet.level);
		packet.hp        = user->GetStat()->GetHp();
		packet.maxhp     = user->GetStat()->GetMaxHp();
		packet.attack    = user->GetStat()->GetOffensive();
		packet.adena     = user->GetInventory()->GetAdena();
		packet.alignment = user->GetAlignment();
		packet.pkCount   = user->GetPkCount();
		packet.flags     = BuildSubjectExFlags(user);
		packet.hasteRemainMs = user->IsHasted(now) ? (user->m_hasteUntil - now) : 0;

		session->PostSend(packet);
	}

	void SendSUBJECT_INFO_EX_NFY(Subject::SharedPtr viewer, Subject::SharedPtr target)
	{
		if (target == nullptr || !IsExtendedViewer(viewer))
			return;
		auto session = GetSession(viewer);
		if (!session)
			return;

		SUBJECT_INFO_EX_NFY_PACKET packet;
		InitializePacket(packet, PacketType::SUBJECT_INFO_EX_NFY);
		packet.id = target->GetObjID();

		const EnumCategory category = target->GetObjID().GetCategory<EnumCategory>();
		if (category == EnumCategory::eUser)
		{
			const auto user = static_pointer_cast<User>(target);
			packet.level     = user->GetStat()->GetLevel();
			packet.alignment = user->GetAlignment();
			packet.flags     = BuildSubjectExFlags(user);
			packet.enchant   = user->GetInventory()->GetEquippedEnchant();
			packet.hp        = user->GetStat()->GetHp();
			packet.maxhp     = user->GetStat()->GetMaxHp();
			packet.classId   = static_cast<uint8_t>(user->m_classId);
			// 장착 무기의 희귀도(무기 빛 색)
			const auto inventory = user->GetInventory();
			const uint16_t weaponSlot = inventory->FindEquipped(EquipSlot::Weapon);
			if (weaponSlot < MAX_INVENTORY_SLOTS)
			{
				if (const ItemTableRow* row = ItemTableData::Find(inventory->GetEquippedItemId()))
					packet.weaponGrade = static_cast<uint8_t>(ItemRules::EffectiveRarity(*row, inventory->GetSlotExtra(weaponSlot)));
			}
		}
		else if (category == EnumCategory::eMonster)
		{
			// Item에는 Stat이 없으므로(Item.h 주석) 몬스터/유저일 때만 GetStat()을 부른다.
			packet.level = target->GetStat()->GetLevel();
			packet.hp    = target->GetStat()->GetHp();
			packet.maxhp = target->GetStat()->GetMaxHp();
		}
		else if (category == EnumCategory::eItem)
		{
			if (const auto equipment = dynamic_pointer_cast<EquipmentItem>(target))
			{
				packet.enchant = equipment->GetEnchant();
				// 필드 장비의 희귀도(빛기둥 색). 옵션은 주워야 보인다.
				if (const ItemTableRow* row = ItemTableData::Find(equipment->GetItemTableId()))
					packet.weaponGrade = static_cast<uint8_t>(ItemRules::EffectiveRarity(*row, equipment->GetExtra()));
			}
		}

		session->PostSend(packet);
	}

	void BroadcastInfoEx(const shared_ptr<User>& player)
	{
		if (player == nullptr)
			return;

		if (player->IsExtended())
			SendSUBJECT_INFO_EX_NFY(player, player);

		// 죽어 있으면 섹터에 없으므로(Sector에서 제거됨) 주변 뷰어도 없다.
		if (player->GetSectorX() < 0)
			return;

		GSector->ForEachNeighborObject(player->GetSectorX(), player->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (object->GetObjID() == player->GetObjID())
				return;
			if (!IsExtendedViewer(object) || !SubjectHelper::CanSee(object, player))
				return;
			SendSUBJECT_INFO_EX_NFY(object, player);
		});
	}

	void SendITEM_LIST_EX_ACK(const shared_ptr<User>& user)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		auto session = user->GetGameSession();
		if (!session)
			return;

		ITEM_LIST_EX_ACK_PACKET packet;
		InitializePacket(packet, PacketType::ITEM_LIST_EX_ACK);

		uint8_t slotCount = 0;
		for (const auto& [slotIndex, item] : user->GetInventory()->GetSlots())
		{
			if (slotCount >= MAX_INVENTORY_SLOTS)
				break;
			user->GetInventory()->FillSlotEx(slotIndex, packet.items[slotCount]);
			++slotCount;
		}
		packet.slotCount = slotCount;

		session->PostSend(packet);

		// V2: 희귀도·옵션이 있는 슬롯만 이어서 보낸다(클라는 스냅샷을 받을 때 전부 0으로 비운다).
		for (const auto& [slotIndex, item] : user->GetInventory()->GetSlots())
			if (user->GetInventory()->GetSlotExtra(slotIndex) != 0)
				SendITEM_DETAIL_INF(user, slotIndex);
	}

	void SendITEM_DETAIL_INF(const shared_ptr<User>& user, uint16_t slotIndex)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		auto session = user->GetGameSession();
		if (!session)
			return;

		ITEM_DETAIL_INF_PACKET packet;
		InitializePacket(packet, PacketType::ITEM_DETAIL_INF);
		packet.slotIndex = slotIndex;
		const auto& slots = user->GetInventory()->GetSlots();
		const auto it = slots.find(slotIndex);
		packet.itemId = it != slots.end() ? it->second->GetItemTableId() : 0;
		packet.extra = user->GetInventory()->GetSlotExtra(slotIndex);
		session->PostSend(packet);
	}

	void HandleItemSort(const shared_ptr<User>& player)
	{
		if (player == nullptr || !player->IsExtended() || player->IsTransferring())
			return;
		const uint32_t now = GetNowTime();
		if (now - player->m_lastSortAt < 800)
			return;   // 도배 방지(정리는 수십 개의 슬롯 저장을 만든다)
		player->m_lastSortAt = now;
		(void)player->GetInventory()->TrySort();
	}

	void SendITEM_SLOT_EX_INF(const shared_ptr<User>& user, uint16_t slotIndex)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		auto session = user->GetGameSession();
		if (!session)
			return;

		ITEM_SLOT_EX_INF_PACKET packet;
		InitializePacket(packet, PacketType::ITEM_SLOT_EX_INF);
		user->GetInventory()->FillSlotEx(slotIndex, packet.slot);

		session->PostSend(packet);
	}

	void SendADENA_INF(const shared_ptr<User>& user, int32_t delta, AdenaReason reason)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		auto session = user->GetGameSession();
		if (!session)
			return;

		ADENA_INF_PACKET packet;
		InitializePacket(packet, PacketType::ADENA_INF);
		packet.adena  = user->GetInventory()->GetAdena();
		packet.delta  = delta;
		packet.reason = static_cast<uint8_t>(reason);

		session->PostSend(packet);
	}

	void SendENCHANT_RESULT_ACK(const shared_ptr<User>& user, EnchantResult result, uint16_t targetSlot, uint16_t itemId, uint8_t before, uint8_t after)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		auto session = user->GetGameSession();
		if (!session)
			return;

		ENCHANT_RESULT_ACK_PACKET packet;
		InitializePacket(packet, PacketType::ENCHANT_RESULT_ACK);
		packet.result        = static_cast<uint8_t>(result);
		packet.targetSlot    = targetSlot;
		packet.itemId        = itemId;
		packet.enchantBefore = before;
		packet.enchantAfter  = after;

		session->PostSend(packet);
	}

	void SendSHOP_RESULT_ACK(const shared_ptr<User>& user, ShopKind kind, ShopResult result, uint32_t seq, uint16_t itemId, uint16_t count, int32_t adenaDelta)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		auto session = user->GetGameSession();
		if (!session)
			return;

		SHOP_RESULT_ACK_PACKET packet;
		InitializePacket(packet, PacketType::SHOP_RESULT_ACK);
		packet.kind       = static_cast<uint8_t>(kind);
		packet.result     = static_cast<uint8_t>(result);
		packet.seq        = seq;
		packet.itemId     = itemId;
		packet.count      = count;
		packet.adenaDelta = adenaDelta;

		session->PostSend(packet);
	}

	void SendSystemEx(const shared_ptr<User>& user, SystemMessageCode code, int32_t param1, int32_t param2)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		SendSYSTEM_MESSAGE_INF(user, code, param1, param2);
	}

	void BroadcastDamage(const Subject::SharedPtr& victim, const ObjID& attackerId, int32_t damage, uint8_t flags,
		uint16_t skillId, uint8_t comboId, uint8_t flags2, uint8_t hitIndex)
	{
		if (victim == nullptr || victim->GetSectorX() < 0)
			return;

		SUBJECT_DAMAGE_NFY_PACKET packet;
		InitializePacket(packet, PacketType::SUBJECT_DAMAGE_NFY);
		packet.victimId   = victim->GetObjID();
		packet.attackerId = attackerId;
		packet.hp         = victim->GetStat()->GetHp();
		packet.maxhp      = victim->GetStat()->GetMaxHp();
		packet.damage     = damage;
		packet.flags      = flags;
		packet.skillId    = skillId;
		packet.comboId    = comboId;
		packet.flags2     = flags2;
		packet.hitIndex   = hitIndex;

		GSector->ForEachNeighborObject(victim->GetSectorX(), victim->GetSectorY(), [&](const shared_ptr<Subject>& object)
		{
			if (!IsExtendedViewer(object) || !SubjectHelper::CanSee(object, victim))
				return;
			if (auto session = GetSession(object))
				session->PostSend(packet);
		});
	}

	void BroadcastEffect(const Subject::SharedPtr& center, const ObjID& subjectId, EffectId effect, short x, short y, int32_t param)
	{
		if (center == nullptr)
			return;

		EFFECT_NFY_PACKET packet;
		InitializePacket(packet, PacketType::EFFECT_NFY);
		packet.subjectId = subjectId;
		packet.effectId  = static_cast<uint16_t>(effect);
		packet.x         = x;
		packet.y         = y;
		packet.param     = param;

		// 섹터에서 빠져 있는(죽은/이동 중) 대상이면 자기 자신에게만 보낸다.
		if (center->GetSectorX() < 0)
		{
			if (IsExtendedViewer(center))
				if (auto session = GetSession(center))
					session->PostSend(packet);
			return;
		}

		const short sectorX = static_cast<short>(x / SECTOR_RANGE);
		const short sectorY = static_cast<short>(y / SECTOR_RANGE);
		const bool inThisZone = GSector->IsValidSector(sectorX, sectorY);
		GSector->ForEachNeighborObject(inThisZone ? sectorX : center->GetSectorX(), inThisZone ? sectorY : center->GetSectorY(),
			[&](const shared_ptr<Subject>& object)
		{
			if (!IsExtendedViewer(object))
				return;
			if (abs(object->GetX() - x) > GViewRange || abs(object->GetY() - y) > GViewRange)
				return;
			if (auto session = GetSession(object))
				session->PostSend(packet);
		});
	}

	void BroadcastWorldNotice(WorldNoticeCode code, int32_t param1, int32_t param2, const std::string& name)
	{
		WORLD_NOTICE_INF_PACKET packet;
		InitializePacket(packet, PacketType::WORLD_NOTICE_INF);
		packet.code   = static_cast<uint16_t>(code);
		packet.param1 = param1;
		packet.param2 = param2;
		::strncpy_s(packet.name, NAME_SIZE, name.c_str(), _TRUNCATE);

		for (const auto& session : GSessionManager->SnapshotSessions())
		{
			if (session == nullptr || session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				continue;
			session->PostSend(packet);
		}
	}

	void BroadcastWorldChat(const shared_ptr<User>& sender, const char mess[])
	{
		// 도배 방지: 전체 채팅은 3초에 한 번
		const uint32_t now = GetNowTime();
		if (sender->m_lastWorldChatTime != 0 && now < sender->m_lastWorldChatTime + 3000)
		{
			SendSystemEx(sender, SystemMessageCode::ItemCooldown, static_cast<int32_t>(sender->m_lastWorldChatTime + 3000 - now));
			return;
		}
		sender->m_lastWorldChatTime = now;

		SC_CHAT_EX_PACKET packet;
		InitializePacket(packet, PacketType::SC_CHAT_EX);
		packet.channel  = static_cast<uint8_t>(ChatChannel::World);
		packet.senderId = sender->GetObjID();
		::strncpy_s(packet.senderName, NAME_SIZE, sender->GetName().c_str(), _TRUNCATE);
		::strncpy_s(packet.mess, CHAT_SIZE, mess, _TRUNCATE);

		for (const auto& session : GSessionManager->SnapshotSessions())
		{
			if (session == nullptr || session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
				continue;
			session->PostSend(packet);
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// V2 이동 — 순번 이동 + 토큰 버킷 검증(Data/MoveRule.h)
	// ─────────────────────────────────────────────────────────────────────────
	void SendUSER_MOVE_EX_ACK(const shared_ptr<User>& user, uint16_t seq, MoveResult result, uint32_t stepMs)
	{
		if (user == nullptr)
			return;
		auto session = user->GetGameSession();
		if (!session)
			return;
		USER_MOVE_EX_ACK_PACKET packet;
		InitializePacket(packet, PacketType::USER_MOVE_EX_ACK);
		packet.seq = seq;
		packet.x = user->GetX();
		packet.y = user->GetY();
		packet.result = static_cast<uint8_t>(result);
		packet.stepMs = static_cast<uint16_t>(stepMs > 65535u ? 65535u : stepMs);
		session->PostSend(packet);
	}

	void SendSERVER_CONFIG_INF(const shared_ptr<User>& user)
	{
		if (user == nullptr)
			return;
		auto session = user->GetGameSession();
		if (!session || !session->IsExtended())
			return;
		SERVER_CONFIG_INF_PACKET packet;
		InitializePacket(packet, PacketType::SERVER_CONFIG_INF);
		packet.protocolVersion = PROTOCOL_EX_VERSION;
		packet.viewRange = static_cast<uint8_t>(GViewRange);
		packet.baseStepMs = static_cast<uint16_t>(MoveRule::kBaseStepMs);
		packet.diagPermille = static_cast<uint16_t>(MoveRule::kDiagPermille);
		packet.moveSpeedPercent = static_cast<uint16_t>(user->GetMoveSpeedPercent(GetNowTime()));
		session->PostSend(packet);
	}

	void HandleMoveEx(const shared_ptr<User>& player, uint8_t dir, uint16_t seq)
	{
		if (player == nullptr)
			return;
		if (!MoveRule::IsValidDir(dir) || player->GetStat()->IsDead() || player->IsTransferring())
		{
			SendUSER_MOVE_EX_ACK(player, seq, MoveResult::Busy, 0);
			return;
		}

		const uint32_t now = GetNowTime();
		const uint32_t cost = MoveRule::StepMs(dir, player->GetMoveSpeedPercent(now));

		// 토큰 버킷: 늦게 온 패킷이 잃은 시간은 kCatchupMs까지 되찾고, kSlackMs까지는
		// 일찍 와도 받아 준다. 그보다 빠르면(속도 핵/시계 조작) 거절하고 좌표를 되돌린다.
		const uint32_t earliest = now > MoveRule::kCatchupMs ? now - MoveRule::kCatchupMs : 0;
		const uint32_t start = (std::max)(player->m_moveClock, earliest);
		if (start > now + MoveRule::kSlackMs)
		{
			SendUSER_MOVE_EX_ACK(player, seq, MoveResult::TooFast, cost);
			return;
		}
		player->m_moveClock = start + cost;
		player->m_lastMoveTime = now;

		short x = player->GetX();
		short y = player->GetY();
		SubjectHelper::MovePositionByDirection(x, y, static_cast<char>(dir));
		if (dir == 2 || dir == 4 || dir == 6) player->SetFacingLeft(true);
		else if (dir == 3 || dir == 5 || dir == 7) player->SetFacingLeft(false);

		player->SetLastStepMs(cost);
		SectorHelper::HandlePlayerMove(player, x, y, false);
		SendUSER_MOVE_EX_ACK(player, seq, MoveResult::Ok, cost);
		SkillEx::OnMoved(player);   // Phase 2-B: 걸으면 캐스팅 · 차지 · (움직일 수 없는) 홀딩이 끊긴다
		// 탐험 목표(존을 넘어가는 걸음이면 새 Zone 스레드가 소유 — 다음 걸음에서 본다)
		if (!player->IsTransferring())
			QuestHelper::OnMoved(player);
	}

	void SaveProgress(const shared_ptr<User>& user)
	{
		if (user == nullptr || user->GetPlayerId() == 0 || !user->IsExtended())
			return;
		GDBThread->RequestSaveProgress(user->GetPlayerId(), user->SerializeProgress());
	}
}
