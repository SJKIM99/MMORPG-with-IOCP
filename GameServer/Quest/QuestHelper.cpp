#include "pch.h"
#include "Quest/QuestHelper.h"
#include "User.h"
#include "UserHelper.h"
#include "GameSession.h"
#include "GameObjectManager.h"
#include "Sector.h"
#include "CoreTLS.h"
#include "Npc/NpcHelper.h"
#include "Item/ItemTable.h"
#include "Data/QuestData.h"
#include "Data/ItemRules.h"
#include "Data/MonsterTable.h"
#include "Data/WorldData.h"

#include <ctime>

namespace
{
	using QuestData::QuestRow;
	using QuestData::ObjKind;

	template<typename Packet>
	void InitPacket(Packet& packet, PacketType type)
	{
		packet = {};
		packet.size = sizeof(Packet);
		packet.type = static_cast<char>(type);
	}

	// 반복 퀘스트 대기 시간은 벽시계(재접속 · 서버 재시작을 넘어 유지된다)
	uint32_t NowUnix() { return static_cast<uint32_t>(std::time(nullptr)); }

	bool IsDone(const User& u, uint16_t id) { return QuestData::TestBit(u.m_questDone.data(), id); }

	void SetDone(User& u, uint16_t id)
	{
		if (id > 0 && id < 256)
			u.m_questDone[id >> 3] = static_cast<uint8_t>(u.m_questDone[id >> 3] | (1u << (id & 7)));
	}

	int SlotIndexOf(const User& u, uint16_t id)
	{
		if (id == 0) return -1;
		for (size_t i = 0; i < u.m_quests.size(); ++i)
			if (u.m_quests[i].id == id) return static_cast<int>(i);
		return -1;
	}

	int FreeSlot(const User& u)
	{
		for (size_t i = 0; i < u.m_quests.size(); ++i)
			if (u.m_quests[i].id == 0) return static_cast<int>(i);
		return -1;
	}

	// 남은 대기(초). 지난 기록은 이참에 지운다.
	uint32_t CooldownRemain(User& u, uint16_t id, uint32_t now)
	{
		uint32_t remain = 0;
		auto& cds = u.m_questCooldowns;
		for (auto it = cds.begin(); it != cds.end();)
		{
			if (it->readyAtUnix <= now) { it = cds.erase(it); continue; }
			if (it->id == id) remain = (std::max)(remain, it->readyAtUnix - now);
			++it;
		}
		return remain;
	}

	int CountEquipped(User& u)
	{
		auto inventory = u.GetInventory();
		if (inventory == nullptr) return 0;
		int n = 0;
		for (int s = 1; s < static_cast<int>(EquipSlot::Count); ++s)
			if (inventory->FindEquipped(static_cast<EquipSlot>(s)) < MAX_INVENTORY_SLOTS) ++n;
		return n;
	}

	template <typename Packet>
	void Post(const shared_ptr<User>& u, const Packet& packet)
	{
		if (u == nullptr || !u->IsExtended()) return;
		if (auto session = u->GetGameSession())
			session->PostSend(packet);
	}

	void SendResult(const shared_ptr<User>& u, uint16_t questId, QuestAction action, QuestResult result, uint32_t param = 0)
	{
		QUEST_RESULT_ACK_PACKET p;
		InitPacket(p, PacketType::QUEST_RESULT_ACK);
		p.questId = questId;
		p.action = static_cast<uint8_t>(action);
		p.result = static_cast<uint8_t>(result);
		p.param = param;
		Post(u, p);
	}

	QUEST_UPDATE_INF_PACKET MakeUpdate(uint16_t questId, const uint16_t* progress, QuestEvent ev)
	{
		QUEST_UPDATE_INF_PACKET p;
		InitPacket(p, PacketType::QUEST_UPDATE_INF);
		p.questId = questId;
		p.event = static_cast<uint8_t>(ev);
		p.objIndex = 0xFF;
		p.x = p.y = -1;
		if (progress)
			for (int i = 0; i < QuestData::kMaxObjs; ++i) p.progress[i] = progress[i];
		return p;
	}

	QuestResult CanAccept(User& u, const QuestRow& q, uint32_t& param)
	{
		if (SlotIndexOf(u, q.id) >= 0) return QuestResult::NotAvailable;
		if (QuestData::IsRepeatable(q))
		{
			if (const uint32_t remain = CooldownRemain(u, q.id, NowUnix()); remain > 0)
			{
				param = remain;
				return QuestResult::Cooldown;
			}
		}
		else if (IsDone(u, q.id))
			return QuestResult::NotAvailable;
		if (q.prereq != 0 && !IsDone(u, q.prereq)) return QuestResult::NotAvailable;
		if (!QuestData::ClassCanTake(q, u.m_classId)) return QuestResult::WrongClass;
		if (u.GetStat()->GetLevel() < q.minLevel)
		{
			param = q.minLevel;
			return QuestResult::LevelTooLow;
		}
		if (FreeSlot(u) < 0) return QuestResult::LogFull;
		return QuestResult::Ok;
	}

	// 보상 희귀도: 적어도 표의 값(그리고 물건의 바닥), 가끔 한두 단계 위(제한된 무작위).
	Rarity RollRewardRarity(const ItemTableRow& row, Rarity base)
	{
		int r = (std::max)(static_cast<int>(ItemRules::FloorRarity(row)), static_cast<int>(base));
		const uint32_t roll = static_cast<uint32_t>(LRng() % 100u);
		if (roll < 4) r += 2;
		else if (roll < 24) r += 1;
		return static_cast<Rarity>((std::min)(r, static_cast<int>(Rarity::Mythic)));
	}

	// 보상을 주고 끝낸다. 가방이 가득 차면 아무것도 바꾸지 않고 false.
	bool Finish(const shared_ptr<User>& u, int slotIndex, const QuestRow& q)
	{
		auto& slot = u->m_quests[static_cast<size_t>(slotIndex)];
		const uint8_t levelBefore = u->GetStat()->GetLevel();

		// 1) 물건(실패할 수 있는 단계를 맨 앞에 — 실패하면 아무것도 바뀌지 않는다)
		const uint16_t itemId = QuestData::RewardItem(q, u->m_classId);
		Rarity rarity = Rarity::Common;
		if (itemId != 0)
		{
			const ItemTableRow* row = ItemTable::Find(itemId);
			if (row == nullptr)
				return false;
			uint64_t extra = 0;
			if (row->type == ItemType::eEquipment)
			{
				rarity = RollRewardRarity(*row, q.reward.rarity);
				auto rng = []() { return static_cast<uint32_t>(LRng()); };
				extra = ItemRules::RollInstance(*row, rarity, u->m_classId, rng).Pack();
			}
			std::vector<uint16_t> touched;
			if (!u->GetInventory()->TryAddItem(itemId, q.reward.count, &touched, 0, extra))
				return false;
		}

		// 2) 기록
		uint32_t cooldownSec = 0;
		if (QuestData::IsRepeatable(q))
		{
			cooldownSec = static_cast<uint32_t>(q.cooldownMin) * 60u;
			(void)CooldownRemain(*u, q.id, NowUnix());   // 지난 기록 정리
			u->m_questCooldowns.push_back({ q.id, NowUnix() + cooldownSec });
		}
		else
			SetDone(*u, q.id);
		const uint16_t lastProgress[QuestData::kMaxObjs] = { slot.progress[0], slot.progress[1], slot.progress[2] };
		slot = User::QuestSlot{};

		// 3) 알림(보상 내역) → 아데나 → 경험치(레벨업 연출은 완료 연출 뒤에 온다)
		const uint32_t exp = QuestData::RewardExp(q, levelBefore);
		auto p = MakeUpdate(q.id, lastProgress, QuestEvent::Completed);
		p.exp = exp;
		p.adena = q.reward.adena;
		p.itemId = itemId;
		p.itemCount = itemId != 0 ? q.reward.count : 0;
		p.rarity = static_cast<uint8_t>(rarity);
		p.cooldownSec = cooldownSec;
		Post(u, p);

		if (q.reward.adena > 0)
			(void)u->GetInventory()->AddAdena(q.reward.adena, AdenaReason::Quest);
		if (exp > 0)
			UserHelper::GainExp(u, exp);
		UserHelper::SaveProgress(u);

		if (q.id == QuestData::kFinalQuest)
			UserHelper::BroadcastWorldNotice(WorldNoticeCode::StoryComplete, q.id, 0, u->GetName());
		return true;
	}

	// 목표 하나의 진행도를 value로(목표 수를 넘지 않게). 다 이루면 — 보고할 NPC가 없는
	// 퀘스트는 — 그 자리에서 끝낸다.
	void Advance(const shared_ptr<User>& u, int slotIndex, int objIndex, int value, short x, short y)
	{
		auto& slot = u->m_quests[static_cast<size_t>(slotIndex)];
		const QuestRow* q = QuestData::Find(slot.id);
		if (q == nullptr)
			return;
		const auto& obj = q->objs[objIndex];
		const uint16_t v = static_cast<uint16_t>(std::clamp(value, 0, static_cast<int>(obj.count)));
		if (v == slot.progress[static_cast<size_t>(objIndex)])
			return;
		const bool reachedNow = v >= obj.count;
		slot.progress[static_cast<size_t>(objIndex)] = v;

		auto p = MakeUpdate(slot.id, slot.progress.data(), QuestEvent::Progress);
		p.objIndex = static_cast<uint8_t>(objIndex);
		p.x = x;
		p.y = y;
		Post(u, p);

		if (!QuestData::ObjectivesDone(*q, slot.progress.data()))
		{
			if (reachedNow)
				UserHelper::SaveProgress(u);
			return;
		}
		if (q->turnIn == 0)
		{
			if (!Finish(u, slotIndex, *q))
				SendResult(u, q->id, QuestAction::Complete, QuestResult::InventoryFull);
			return;
		}
		UserHelper::SaveProgress(u);
	}

	bool ZoneMatches(const QuestData::Objective& o, int zoneId)
	{
		return o.zone == QuestData::kAnyZone || o.zone == zoneId;
	}
}

namespace QuestHelper
{
	void SendList(const shared_ptr<User>& user)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		QUEST_LIST_INF_PACKET p;
		InitPacket(p, PacketType::QUEST_LIST_INF);
		for (const auto& s : user->m_quests)
		{
			if (s.id == 0 || p.activeCount >= kQuestListActiveMax) continue;
			auto& e = p.active[p.activeCount++];
			e.questId = s.id;
			for (int i = 0; i < QuestData::kMaxObjs; ++i) e.progress[i] = s.progress[static_cast<size_t>(i)];
		}
		std::memcpy(p.done, user->m_questDone.data(), sizeof(p.done));
		const uint32_t now = NowUnix();
		(void)CooldownRemain(*user, 0, now);   // 지난 기록 정리
		for (const auto& c : user->m_questCooldowns)
		{
			if (p.cooldownCount >= kQuestListCooldownMax) break;
			p.cooldowns[p.cooldownCount++] = QUEST_COOLDOWN{ c.id, c.readyAtUnix > now ? c.readyAtUnix - now : 0 };
		}
		Post(user, p);
	}

	void HandleAccept(const shared_ptr<User>& user, uint16_t questId)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		auto reply = [&](QuestResult r, uint32_t param = 0) { SendResult(user, questId, QuestAction::Accept, r, param); };
		const QuestRow* q = QuestData::Find(questId);
		if (q == nullptr)
			return reply(QuestResult::Unknown);
		if (user->GetStat()->IsDead() || user->IsTransferring())
			return reply(QuestResult::Busy);
		uint32_t param = 0;
		if (const QuestResult r = CanAccept(*user, *q, param); r != QuestResult::Ok)
			return reply(r, param);
		if (!NpcHelper::IsNear(q->giver, user->GetX(), user->GetY()))
			return reply(QuestResult::TooFar);

		const int si = FreeSlot(*user);
		auto& slot = user->m_quests[static_cast<size_t>(si)];
		slot = User::QuestSlot{};
		slot.id = q->id;
		// 지금 상태로 정해지는 목표(입고 있는 장비)는 받는 순간의 값에서 시작한다.
		for (int i = 0; i < QuestData::kMaxObjs; ++i)
			if (q->objs[i].kind == ObjKind::Equip)
				slot.progress[static_cast<size_t>(i)] = static_cast<uint16_t>((std::min)(CountEquipped(*user), static_cast<int>(q->objs[i].count)));

		Post(user, MakeUpdate(q->id, slot.progress.data(), QuestEvent::Accepted));
		reply(QuestResult::Ok);
		UserHelper::SaveProgress(user);
		// 이미 그 자리에 서 있는 탐험 목표 등
		OnMoved(user);
	}

	void HandleComplete(const shared_ptr<User>& user, uint16_t questId)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		auto reply = [&](QuestResult r, uint32_t param = 0) { SendResult(user, questId, QuestAction::Complete, r, param); };
		const QuestRow* q = QuestData::Find(questId);
		const int si = q != nullptr ? SlotIndexOf(*user, questId) : -1;
		if (si < 0)
			return reply(QuestResult::Unknown);
		if (user->GetStat()->IsDead() || user->IsTransferring())
			return reply(QuestResult::Busy);
		if (!QuestData::ObjectivesDone(*q, user->m_quests[static_cast<size_t>(si)].progress.data()))
			return reply(QuestResult::NotReady);
		if (q->turnIn != 0 && !NpcHelper::IsNear(q->turnIn, user->GetX(), user->GetY()))
			return reply(QuestResult::TooFar);
		if (!Finish(user, si, *q))
			return reply(QuestResult::InventoryFull);
		reply(QuestResult::Ok);
	}

	void HandleAbandon(const shared_ptr<User>& user, uint16_t questId)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		const int si = SlotIndexOf(*user, questId);
		if (si < 0)
			return SendResult(user, questId, QuestAction::Abandon, QuestResult::Unknown);
		user->m_quests[static_cast<size_t>(si)] = User::QuestSlot{};
		Post(user, MakeUpdate(questId, nullptr, QuestEvent::Abandoned));
		SendResult(user, questId, QuestAction::Abandon, QuestResult::Ok);
		UserHelper::SaveProgress(user);
	}

	void OnKill(const shared_ptr<User>& killer, const MonsterTableRow* species, short x, short y)
	{
		if (killer == nullptr || species == nullptr || !killer->IsExtended())
			return;
		const int zoneId = WorldData::RegionIdAt(x, y);
		for (size_t si = 0; si < killer->m_quests.size(); ++si)
		{
			auto& slot = killer->m_quests[si];
			if (slot.id == 0) continue;
			const QuestRow* q = QuestData::Find(slot.id);
			if (q == nullptr) continue;
			for (int oi = 0; oi < QuestData::kMaxObjs; ++oi)
			{
				const auto& o = q->objs[oi];
				auto& progress = slot.progress[static_cast<size_t>(oi)];
				if (o.kind == ObjKind::None || progress >= o.count || !ZoneMatches(o, zoneId))
					continue;
				switch (o.kind)
				{
				case ObjKind::Kill:
					if (species->id == o.target)
						Advance(killer, static_cast<int>(si), oi, progress + 1, x, y);
					break;
				case ObjKind::KillIn:
					Advance(killer, static_cast<int>(si), oi, progress + 1, x, y);
					break;
				case ObjKind::Collect:
				{
					if (species->id != o.target) break;
					// 운 보정: 실패할 때마다 확률이 조금씩 오른다(끝없이 헤매지 않게)
					auto& miss = slot.misses[static_cast<size_t>(oi)];
					const uint32_t step = (std::max)(5u, static_cast<uint32_t>(o.chance) / 3u);
					const uint32_t chance = (std::min)(100u, o.chance + miss * step);
					if (static_cast<uint32_t>(LRng() % 100u) < chance)
					{
						miss = 0;
						Advance(killer, static_cast<int>(si), oi, progress + 1, x, y);
					}
					else if (miss < 250)
						++miss;
					break;
				}
				default:
					break;
				}
				// Advance가 퀘스트를 끝냈으면(보고할 NPC 없음) 이 칸은 비었다
				if (slot.id == 0) break;
			}
		}
	}

	void OnBossKilled(uint16_t bossSpecies, short x, short y)
	{
		const int zoneId = WorldData::RegionIdAt(x, y);
		const int range = QuestData::kBossShareRange;
		std::vector<shared_ptr<User>> nearby;
		const Sector& sector = *GSector;
		for (int sy = (y - range) / SECTOR_RANGE; sy <= (y + range) / SECTOR_RANGE; ++sy)
			for (int sx = (x - range) / SECTOR_RANGE; sx <= (x + range) / SECTOR_RANGE; ++sx)
			{
				if (!sector.IsValidSector(static_cast<short>(sx), static_cast<short>(sy)))
					continue;
				for (const ObjID& id : sector.GetObjects(static_cast<short>(sx), static_cast<short>(sy)))
				{
					if (id.GetCategory<EnumCategory>() != EnumCategory::eUser)
						continue;
					auto user = ::GetGameObject<User>(id);
					if (user == nullptr || !user->IsExtended() || user->IsTransferring())
						continue;
					if ((std::max)(std::abs(user->GetX() - x), std::abs(user->GetY() - y)) > range)
						continue;
					nearby.push_back(std::move(user));
				}
			}

		for (const auto& user : nearby)
		{
			for (size_t si = 0; si < user->m_quests.size(); ++si)
			{
				const auto& slot = user->m_quests[si];
				if (slot.id == 0) continue;
				const QuestRow* q = QuestData::Find(slot.id);
				if (q == nullptr) continue;
				for (int oi = 0; oi < QuestData::kMaxObjs; ++oi)
				{
					const auto& o = q->objs[oi];
					if (o.kind == ObjKind::Boss && o.target == bossSpecies && ZoneMatches(o, zoneId) && slot.progress[static_cast<size_t>(oi)] < o.count)
					{
						Advance(user, static_cast<int>(si), oi, o.count, x, y);
						break;
					}
				}
			}
		}
	}

	void OnTalk(const shared_ptr<User>& user, uint16_t npcId)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		for (size_t si = 0; si < user->m_quests.size(); ++si)
		{
			const auto& slot = user->m_quests[si];
			if (slot.id == 0) continue;
			const QuestRow* q = QuestData::Find(slot.id);
			if (q == nullptr) continue;
			for (int oi = 0; oi < QuestData::kMaxObjs && slot.id != 0; ++oi)
			{
				const auto& o = q->objs[oi];
				if (o.kind == ObjKind::Talk && o.target == npcId && slot.progress[static_cast<size_t>(oi)] < o.count)
					Advance(user, static_cast<int>(si), oi, o.count, user->GetX(), user->GetY());
			}
		}
	}

	void OnMoved(const shared_ptr<User>& user)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		const int x = user->GetX(), y = user->GetY();
		for (size_t si = 0; si < user->m_quests.size(); ++si)
		{
			const auto& slot = user->m_quests[si];
			if (slot.id == 0) continue;
			const QuestRow* q = QuestData::Find(slot.id);
			if (q == nullptr) continue;
			for (int oi = 0; oi < QuestData::kMaxObjs && slot.id != 0; ++oi)
			{
				const auto& o = q->objs[oi];
				if (o.kind != ObjKind::Explore || slot.progress[static_cast<size_t>(oi)] >= o.count)
					continue;
				const QuestData::Poi* poi = QuestData::FindPoi(o.target);
				if (poi != nullptr && QuestData::InPoi(*poi, x, y))
					Advance(user, static_cast<int>(si), oi, o.count, static_cast<short>(poi->x), static_cast<short>(poi->y));
			}
		}
	}

	void OnEquipChanged(const shared_ptr<User>& user)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		int equipped = -1;
		for (size_t si = 0; si < user->m_quests.size(); ++si)
		{
			const auto& slot = user->m_quests[si];
			if (slot.id == 0) continue;
			const QuestRow* q = QuestData::Find(slot.id);
			if (q == nullptr) continue;
			for (int oi = 0; oi < QuestData::kMaxObjs && slot.id != 0; ++oi)
			{
				if (q->objs[oi].kind != ObjKind::Equip)
					continue;
				if (equipped < 0)
					equipped = CountEquipped(*user);
				Advance(user, static_cast<int>(si), oi, equipped, -1, -1);
			}
		}
	}
}
