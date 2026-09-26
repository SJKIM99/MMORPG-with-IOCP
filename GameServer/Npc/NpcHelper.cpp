#include "pch.h"
#include "Npc/NpcHelper.h"
#include "User.h"
#include "UserHelper.h"
#include "GameSession.h"
#include "Sector.h"
#include "TimerThread.h"
#include "CoreTLS.h"
#include "ServerGlobal.h"
#include "Combat/CombatHelper.h"
#include "Quest/QuestHelper.h"
#include "Data/WorldData.h"
#include "Data/NpcTalk.h"
#include "Collision.h"

#include <array>
#include <queue>

namespace
{
	using WorldData::NpcKind;

	constexpr uint32_t kTickMs = 250;           // 사람이 있을 때
	constexpr uint32_t kIdleTickMs = 1000;      // 마을이 비었을 때(NPC는 멈춰 있다)
	constexpr uint32_t kTalkHoldMs = 15000;     // 말을 걸면 이만큼 멈춰 서서 바라본다
	constexpr uint32_t kPetHoldMs = 3500;       // 동물을 쓰다듬으면
	constexpr uint32_t kHealCooldownMs = 30000; // 사제 기도
	constexpr uint32_t kSoupCooldownMs = 20000; // 여관 수프
	constexpr uint32_t kSoupPrice = 30;
	constexpr int      kGreetRange = 3;
	constexpr int      kBarkAudience = 8;       // 이 거리 안에 사람이 있어야 혼잣말을 한다

	struct Greeted { ObjID user; uint32_t at; };

	struct TownNpc
	{
		const WorldData::NpcRow* row = nullptr;
		short    x = 0, y = 0;
		NpcActState state = NpcActState::Idle;
		int8_t   faceX = 0, faceY = 1;
		uint32_t nextActAt = 0, nextStepAt = 0;
		std::vector<std::pair<short, short>> path;
		uint32_t talkUntil = 0;
		ObjID    talkTo{};
		uint32_t nextBarkAt = 0;
		uint32_t greetReadyAt = 0;
		std::vector<Greeted> greeted;
	};

	bool g_ready = false;
	std::vector<TownNpc> g_npcs;
	uint32_t g_tick = 0;
	std::vector<shared_ptr<User>> g_users;   // 이번 틱에 마을 근처에 있는 확장 세션(틱마다 새로)
	const ObjID kTickId(EnumCategory::eNone, 0x4E5043544943ull);   // "NPCTIC"

	int Cheb(int ax, int ay, int bx, int by) { return (std::max)(std::abs(ax - bx), std::abs(ay - by)); }
	int Sign(int v) { return (v > 0) - (v < 0); }
	uint32_t Rand(uint32_t lo, uint32_t hi) { return hi > lo ? lo + static_cast<uint32_t>(LRng() % (hi - lo + 1)) : lo; }

	bool OnTownThread() { return g_ready && LCurrentZoneId == static_cast<ZoneId>(WorldData::kTownZoneId); }

	TownNpc* FindTownNpc(uint16_t npcId)
	{
		for (auto& n : g_npcs)
			if (n.row->id == npcId) return &n;
		return nullptr;
	}

	bool IsHuman(const TownNpc& n) { return !WorldData::IsAnimal(n.row->kind); }

	// 마을(+시야 여유) 섹터에 있는 확장 세션 사용자
	void CollectTownUsers()
	{
		g_users.clear();
		const int margin = 10;
		const short sx0 = static_cast<short>((WorldData::kTownX0 - margin) / SECTOR_RANGE);
		const short sy0 = static_cast<short>((WorldData::kTownY0 - margin) / SECTOR_RANGE);
		const short sx1 = static_cast<short>((WorldData::kTownX0 + WorldData::kTownW + margin) / SECTOR_RANGE);
		const short sy1 = static_cast<short>((WorldData::kTownY0 + WorldData::kTownH + margin) / SECTOR_RANGE);
		const Sector& sector = *GSector;
		for (short sy = sy0; sy <= sy1; ++sy)
			for (short sx = sx0; sx <= sx1; ++sx)
			{
				if (!sector.IsValidSector(sx, sy))
					continue;
				for (const ObjID& id : sector.GetObjects(sx, sy))
				{
					if (id.GetCategory<EnumCategory>() != EnumCategory::eUser)
						continue;
					auto user = ::GetGameObject<User>(id);
					if (!user) continue;
					auto session = user->GetGameSession();
					if (!session || session->m_state != SOCKET_STATE::ST_INGAME || !session->IsExtended())
						continue;
					g_users.push_back(std::move(user));
				}
			}
	}

	NPC_ENTRY EntryOf(const TownNpc& n)
	{
		NPC_ENTRY e{};
		e.npcId = n.row->id;
		e.x = n.x;
		e.y = n.y;
		e.state = static_cast<uint8_t>(n.state);
		e.faceX = n.faceX;
		e.faceY = n.faceY;
		return e;
	}

	void SendList(const shared_ptr<User>& user)
	{
		auto session = user->GetGameSession();
		if (!session) return;
		NPC_LIST_NFY_PACKET packet{};
		packet.size = sizeof(packet);
		packet.type = static_cast<char>(PacketType::NPC_LIST_NFY);
		for (const auto& n : g_npcs)
		{
			if (packet.count >= kNpcListMax) break;
			packet.npcs[packet.count++] = EntryOf(n);
		}
		session->PostSend(packet);
	}

	void BroadcastState(const TownNpc& n, uint16_t stepMs)
	{
		NPC_STATE_NFY_PACKET packet{};
		packet.size = sizeof(packet);
		packet.type = static_cast<char>(PacketType::NPC_STATE_NFY);
		packet.npc = EntryOf(n);
		packet.stepMs = stepMs;
		packet.talkTo = n.talkTo;
		for (const auto& u : g_users)
			if (auto session = u->GetGameSession())
				session->PostSend(packet);
	}

	void Bark(TownNpc& n, uint8_t line, const ObjID& toUser)
	{
		NPC_BARK_NFY_PACKET packet{};
		packet.size = sizeof(packet);
		packet.type = static_cast<char>(PacketType::NPC_BARK_NFY);
		packet.npcId = n.row->id;
		packet.line = line;
		packet.toUser = toUser;
		for (const auto& u : g_users)
			if (Cheb(u->GetX(), u->GetY(), n.x, n.y) <= GViewRange + 3)
				if (auto session = u->GetGameSession())
					session->PostSend(packet);
	}

	bool OccupiedByNpc(int x, int y, const TownNpc* self)
	{
		for (const auto& o : g_npcs)
			if (&o != self && o.x == x && o.y == y) return true;
		return false;
	}

	bool Walkable(int x, int y)
	{
		return WorldData::IsInTown(x, y) && !WorldData::IsBlocked(x, y) && WorldData::TownCell(x, y) != 'G';
	}

	// 집 주변 상자 안에서 8방향 너비 우선 탐색(모서리 끊기 없음). 경로는 시작 칸 제외.
	bool FindPath(const TownNpc& n, int tx, int ty, std::vector<std::pair<short, short>>& out)
	{
		const int r = n.row->wander + 1;
		const int x0 = n.row->x - r, y0 = n.row->y - r;
		const int w = r * 2 + 1;
		if (std::abs(tx - n.row->x) > r || std::abs(ty - n.row->y) > r || std::abs(n.x - n.row->x) > r || std::abs(n.y - n.row->y) > r)
			return false;
		std::vector<int> prev(static_cast<size_t>(w * w), -2);
		auto idx = [&](int x, int y) { return (y - y0) * w + (x - x0); };
		std::queue<std::pair<int, int>> q;
		q.push({ n.x, n.y });
		prev[static_cast<size_t>(idx(n.x, n.y))] = -1;
		static constexpr int kDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
		static constexpr int kDy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
		bool found = false;
		while (!q.empty())
		{
			const auto [cx, cy] = q.front();
			q.pop();
			if (cx == tx && cy == ty) { found = true; break; }
			for (int d = 0; d < 8; ++d)
			{
				const int nx = cx + kDx[d], ny = cy + kDy[d];
				if (nx < x0 || ny < y0 || nx >= x0 + w || ny >= y0 + w) continue;
				if (prev[static_cast<size_t>(idx(nx, ny))] != -2 || !Walkable(nx, ny)) continue;
				if (d >= 4 && (!Walkable(cx + kDx[d], cy) || !Walkable(cx, cy + kDy[d]))) continue;
				prev[static_cast<size_t>(idx(nx, ny))] = idx(cx, cy);
				q.push({ nx, ny });
			}
		}
		if (!found)
			return false;
		out.clear();
		for (int at = idx(tx, ty); at != idx(n.x, n.y) && at >= 0; at = prev[static_cast<size_t>(at)])
			out.emplace_back(static_cast<short>(x0 + at % w), static_cast<short>(y0 + at / w));
		std::reverse(out.begin(), out.end());
		if (out.size() > 14) out.resize(14);
		return !out.empty();
	}

	uint32_t IdleTime(const TownNpc& n)
	{
		switch (n.row->kind)
		{
		case NpcKind::eChild:   return Rand(300, 1600);
		case NpcKind::eDog:     return Rand(600, 2600);
		case NpcKind::eChicken: return Rand(700, 3200);
		case NpcKind::eCat:     return Rand(5000, 14000);
		case NpcKind::eBard:    return Rand(6000, 12000);
		case NpcKind::eMerchant: case NpcKind::eWeaponSmith: case NpcKind::eTeleporter: return Rand(5000, 11000);
		default:                return Rand(2500, 7000);
		}
	}

	shared_ptr<User> NearestUser(const TownNpc& n, int within)
	{
		shared_ptr<User> best;
		int bestD = within + 1;
		for (const auto& u : g_users)
		{
			if (u->GetStat()->IsDead()) continue;
			const int d = Cheb(u->GetX(), u->GetY(), n.x, n.y);
			if (d < bestD) { bestD = d; best = u; }
		}
		return best;
	}

	void PickWalk(TownNpc& n, uint32_t now)
	{
		const int r = n.row->wander;
		int tx = n.x, ty = n.y;
		bool chosen = false;
		// 개: 가까운 사람을 따라다닌다 / 아이: 가끔 다른 아이를 쫓는다
		if (n.row->kind == NpcKind::eDog && Rand(0, 99) < 65)
			if (auto u = NearestUser(n, 6))
			{
				tx = u->GetX() + static_cast<int>(Rand(0, 2)) - 1;
				ty = u->GetY() + static_cast<int>(Rand(0, 2)) - 1;
				chosen = true;
			}
		if (!chosen && n.row->kind == NpcKind::eChild && Rand(0, 99) < 45)
			for (const auto& o : g_npcs)
				if (&o != &n && o.row->kind == NpcKind::eChild)
				{
					tx = o.x + static_cast<int>(Rand(0, 2)) - 1;
					ty = o.y + static_cast<int>(Rand(0, 2)) - 1;
					chosen = true;
					break;
				}
		for (int attempt = 0; attempt < 8; ++attempt)
		{
			if (!chosen)
			{
				const int reach = n.row->kind == NpcKind::eChicken ? (std::min)(r, 2) : r;
				tx = n.row->x + static_cast<int>(Rand(0, static_cast<uint32_t>(reach * 2))) - reach;
				ty = n.row->y + static_cast<int>(Rand(0, static_cast<uint32_t>(reach * 2))) - reach;
			}
			chosen = false;
			if ((tx == n.x && ty == n.y) || !Walkable(tx, ty) || OccupiedByNpc(tx, ty, &n))
				continue;
			if (FindPath(n, tx, ty, n.path))
			{
				n.nextStepAt = now;
				return;
			}
		}
		n.nextActAt = now + IdleTime(n);   // 갈 곳이 없으면 조금 쉬었다가
	}

	void TryGreet(TownNpc& n, uint32_t now)
	{
		if (now < n.greetReadyAt)
			return;
		// 오래된 인사 기록 정리(1분)
		n.greeted.erase(std::remove_if(n.greeted.begin(), n.greeted.end(), [&](const Greeted& g) { return now - g.at > 60000; }), n.greeted.end());
		for (const auto& u : g_users)
		{
			if (u->GetStat()->IsDead() || Cheb(u->GetX(), u->GetY(), n.x, n.y) > kGreetRange)
				continue;
			const bool already = std::any_of(n.greeted.begin(), n.greeted.end(), [&](const Greeted& g) { return g.user == u->GetObjID(); });
			if (already)
				continue;
			n.greeted.push_back(Greeted{ u->GetObjID(), now });
			n.greetReadyAt = now + 8000;
			// 돌아보고 인사(동물은 반가워하는 소리)
			n.faceX = static_cast<int8_t>(Sign(u->GetX() - n.x));
			n.faceY = static_cast<int8_t>(Sign(u->GetY() - n.y));
			BroadcastState(n, 0);
			if (IsHuman(n))
				Bark(n, kNpcGreetLine, u->GetObjID());
			else
				Bark(n, static_cast<uint8_t>(NpcTalk::BarkCount(n.row->id) - 1), u->GetObjID());
			n.nextActAt = (std::max)(n.nextActAt, now + 1500);
			return;
		}
	}

	void UpdateNpc(TownNpc& n, uint32_t now)
	{
		// 대화 중: 시간이 지났거나 상대가 멀어지면 풀린다
		if (n.state == NpcActState::Talk)
		{
			bool keep = now < n.talkUntil;
			if (keep)
			{
				keep = false;
				for (const auto& u : g_users)
					if (u->GetObjID() == n.talkTo && Cheb(u->GetX(), u->GetY(), n.x, n.y) <= WorldData::kNpcInteractRange + 2)
						keep = true;
			}
			if (!keep)
			{
				n.state = NpcActState::Idle;
				n.talkTo = ObjID{};
				n.nextActAt = now + Rand(1200, 3000);
				BroadcastState(n, 0);
			}
			return;
		}
		// 걷는 중
		if (!n.path.empty())
		{
			if (now < n.nextStepAt)
				return;
			const auto [nx, ny] = n.path.front();
			if (OccupiedByNpc(nx, ny, &n) || !Walkable(nx, ny))
			{
				// 길이 막혔다(다른 NPC) — 멈추고 다시 생각한다
				n.path.clear();
				n.state = NpcActState::Idle;
				n.nextActAt = now + Rand(400, 1200);
				BroadcastState(n, 0);
				return;
			}
			n.path.erase(n.path.begin());
			GuardTownMove("npc", n.x, n.y, nx, ny);
			const int dx = nx - n.x, dy = ny - n.y;
			n.faceX = static_cast<int8_t>(Sign(dx));
			n.faceY = static_cast<int8_t>(Sign(dy));
			n.x = nx;
			n.y = ny;
			const uint32_t step = (dx != 0 && dy != 0) ? n.row->stepMs * 1414u / 1000u : n.row->stepMs;
			n.nextStepAt = now + step;
			n.state = NpcActState::Walk;
			BroadcastState(n, static_cast<uint16_t>(step));
			if (n.path.empty())
				n.nextActAt = now + step + IdleTime(n);
			return;
		}
		if (n.state == NpcActState::Walk && now >= n.nextStepAt)
		{
			n.state = NpcActState::Idle;   // 도착
			BroadcastState(n, 0);
		}
		if (n.state != NpcActState::Idle)
			return;

		TryGreet(n, now);

		// 혼잣말(보는 사람이 있을 때만)
		if (now >= n.nextBarkAt)
		{
			const int count = NpcTalk::BarkCount(n.row->id);
			if (count > 0 && NearestUser(n, kBarkAudience))
				Bark(n, static_cast<uint8_t>(Rand(0, static_cast<uint32_t>(count - 1))), ObjID{});
			const bool chatty = n.row->kind == NpcKind::eBard || n.row->kind == NpcKind::eChild || n.row->kind == NpcKind::eMerchant;
			n.nextBarkAt = now + (chatty ? Rand(9000, 20000) : Rand(16000, 40000));
		}

		if (now < n.nextActAt || n.row->wander == 0)
		{
			// 제자리 NPC도 가끔 고개를 돌린다
			if (n.row->wander == 0 && now >= n.nextActAt)
			{
				static constexpr int8_t kFace[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
				const auto& f = kFace[Rand(0, 3)];
				n.faceX = f[0];
				n.faceY = f[1];
				n.nextActAt = now + Rand(4000, 9000);
				BroadcastState(n, 0);
			}
			return;
		}
		// 다음 행동: 걷기(대부분) 또는 조금 더 쉬기
		if (Rand(0, 99) < 70)
			PickWalk(n, now);
		else
			n.nextActAt = now + IdleTime(n);
	}
}

namespace NpcHelper
{
	void InitForZone(uint16_t zoneId)
	{
		if (zoneId != static_cast<uint16_t>(WorldData::kTownZoneId))
			return;
		const uint32_t now = GetNowTime();
		g_npcs.clear();
		g_npcs.reserve(WorldData::kNpcCount);
		for (const auto& row : WorldData::kNpcs)
		{
			TownNpc n;
			n.row = &row;
			n.x = row.x;
			n.y = row.y;
			static constexpr int8_t kFace[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
			n.faceX = kFace[row.facing & 3][0];
			n.faceY = kFace[row.facing & 3][1];
			n.nextActAt = now + Rand(500, 4000);
			n.nextBarkAt = now + Rand(4000, 20000);
			g_npcs.push_back(std::move(n));
		}
		g_ready = true;
		GTimerThread->ScheduleAfter(kTickId, std::chrono::milliseconds(kTickMs), TIMER_EVENT_TYPE::EV_NPC_TICK);
		std::cout << "[Npc] " << g_npcs.size() << " town NPCs wandering (zone " << zoneId << ")\n";
	}

	void HandleTick()
	{
		if (!OnTownThread())
			return;
		++g_tick;
		const uint32_t now = GetNowTime();
		CollectTownUsers();
		// 마을에 막 들어온 사람에게 전체 목록(로그인·귀환·부활·걸어서 들어옴 모두)
		for (auto& u : g_users)
		{
			if (u->m_npcSeenTick + 3 < g_tick)
				SendList(u);
			u->m_npcSeenTick = g_tick;
		}
		// 보는 사람이 없으면 NPC도 쉰다(서버 부하 0에 가깝게)
		const bool anyone = !g_users.empty();
		if (anyone)
			for (auto& n : g_npcs)
				UpdateNpc(n, now);
		g_users.clear();
		GTimerThread->ScheduleAfter(kTickId, std::chrono::milliseconds(anyone ? kTickMs : kIdleTickMs), TIMER_EVENT_TYPE::EV_NPC_TICK);
	}

	void HandleTalk(const shared_ptr<User>& user, uint16_t npcId)
	{
		if (!user)
			return;
		auto session = user->GetGameSession();
		if (!session)
			return;
		NPC_TALK_ACK_PACKET ack{};
		ack.size = sizeof(ack);
		ack.type = static_cast<char>(PacketType::NPC_TALK_ACK);
		ack.npcId = npcId;
		TownNpc* n = OnTownThread() ? FindTownNpc(npcId) : nullptr;
		if (!n)
			ack.result = static_cast<uint8_t>(NpcTalkResult::Unknown);
		else if (user->GetStat()->IsDead() || user->IsTransferring())
			ack.result = static_cast<uint8_t>(NpcTalkResult::Busy);
		else if (Cheb(user->GetX(), user->GetY(), n->x, n->y) > WorldData::kNpcInteractRange + 1)
			ack.result = static_cast<uint8_t>(NpcTalkResult::TooFar);
		else
		{
			ack.result = static_cast<uint8_t>(NpcTalkResult::Ok);
			const uint32_t now = GetNowTime();
			n->path.clear();
			n->state = NpcActState::Talk;
			n->talkTo = user->GetObjID();
			n->talkUntil = now + (IsHuman(*n) ? kTalkHoldMs : kPetHoldMs);
			n->faceX = static_cast<int8_t>(Sign(user->GetX() - n->x));
			n->faceY = static_cast<int8_t>(Sign(user->GetY() - n->y));
			CollectTownUsers();
			BroadcastState(*n, 0);
			if (!IsHuman(*n))
				Bark(*n, static_cast<uint8_t>(Rand(0, static_cast<uint32_t>((std::max)(0, NpcTalk::BarkCount(npcId) - 1)))), user->GetObjID());
			g_users.clear();
		}
		session->PostSend(ack);
		// 대화 목표(누렁이 쓰다듬기 등) — 결과 뒤에 진행을 알린다
		if (ack.result == static_cast<uint8_t>(NpcTalkResult::Ok))
			QuestHelper::OnTalk(user, npcId);
	}

	void HandleService(const shared_ptr<User>& user, uint16_t npcId, uint8_t service)
	{
		if (!user)
			return;
		auto session = user->GetGameSession();
		if (!session)
			return;
		NPC_SERVICE_ACK_PACKET ack{};
		ack.size = sizeof(ack);
		ack.type = static_cast<char>(PacketType::NPC_SERVICE_ACK);
		ack.npcId = npcId;
		ack.service = service;
		auto reply = [&](NpcServiceResult r, uint32_t cooldownMs = 0)
		{
			ack.result = static_cast<uint8_t>(r);
			ack.cooldownMs = cooldownMs;
			session->PostSend(ack);
		};

		TownNpc* n = OnTownThread() ? FindTownNpc(npcId) : nullptr;
		if (!n || user->GetStat()->IsDead() || !user->IsExtended())
			return reply(NpcServiceResult::Invalid);
		if (Cheb(user->GetX(), user->GetY(), n->x, n->y) > WorldData::kNpcInteractRange + 1)
			return reply(NpcServiceResult::TooFar);
		const uint32_t now = GetNowTime();
		const auto stat = user->GetStat();
		const bool full = stat->GetHp() >= stat->GetMaxHp() && user->m_mp >= user->m_derived.maxMp;
		switch (static_cast<WorldData::NpcService>(service))
		{
		case WorldData::NpcService::Heal:
		{
			if (n->row->kind != NpcKind::ePriest)
				return reply(NpcServiceResult::Invalid);
			if (now < user->m_npcHealReadyAt)
				return reply(NpcServiceResult::Cooldown, user->m_npcHealReadyAt - now);
			if (full)
				return reply(NpcServiceResult::Full);
			const uint16_t before = stat->GetHp();
			stat->HealHp(stat->GetMaxHp(), stat->GetMaxHp());
			user->m_mp = user->m_derived.maxMp;
			user->m_npcHealReadyAt = now + kHealCooldownMs;
			CombatHelper::SendResource(user);
			UserHelper::BroadcastEffect(user, user->GetObjID(), EffectId::PriestHeal, user->GetX(), user->GetY(), stat->GetHp() - before);
			CollectTownUsers();
			Bark(*n, 0, user->GetObjID());
			g_users.clear();
			return reply(NpcServiceResult::Ok, kHealCooldownMs);
		}
		case WorldData::NpcService::Soup:
		{
			if (n->row->kind != NpcKind::eInnkeeper)
				return reply(NpcServiceResult::Invalid);
			if (now < user->m_npcSoupReadyAt)
				return reply(NpcServiceResult::Cooldown, user->m_npcSoupReadyAt - now);
			if (full)
				return reply(NpcServiceResult::Full);
			if (!user->GetInventory()->TrySpendAdena(kSoupPrice, AdenaReason::Service))
				return reply(NpcServiceResult::NoAdena);
			const uint16_t before = stat->GetHp();
			stat->HealHp(static_cast<uint16_t>(stat->GetMaxHp() * 40u / 100u), stat->GetMaxHp());
			user->m_mp = static_cast<uint16_t>(std::min<uint32_t>(user->m_derived.maxMp, user->m_mp + user->m_derived.maxMp * 40u / 100u));
			user->m_npcSoupReadyAt = now + kSoupCooldownMs;
			CombatHelper::SendResource(user);
			UserHelper::BroadcastEffect(user, user->GetObjID(), EffectId::PotionHeal, user->GetX(), user->GetY(), stat->GetHp() - before);
			return reply(NpcServiceResult::Ok, kSoupCooldownMs);
		}
		default:
			return reply(NpcServiceResult::Invalid);
		}
	}

	bool IsNear(uint16_t npcId, int x, int y)
	{
		if (OnTownThread())
			if (const TownNpc* n = FindTownNpc(npcId))
				return Cheb(x, y, n->x, n->y) <= WorldData::kNpcInteractRange + 1;
		const auto* row = WorldData::FindNpc(npcId);
		return row != nullptr && WorldData::IsNearNpc(*row, x, y);
	}
}
