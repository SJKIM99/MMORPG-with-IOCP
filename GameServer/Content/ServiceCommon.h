#pragma once

#include <atomic>
#include <ctime>
#include <functional>
#include <memory>
#include <thread>

#include "Thread/GameLogicThread.h"
#include "Zone/ZoneManager.h"
#include "GameObjectManager.h"
#include "User.h"
#include "GameSession.h"

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 전역 서비스(결사 · 경매장) 공용 — docs/adr/0001-guild-auction-services.md
//
//  · 서비스 하나 = 작업 큐 하나(GameLogicThread) = 스레드 하나. 서비스 상태는 그 스레드만 만진다.
//  · Zone → 서비스: Post(람다). 서비스 → Zone: PostToOwner(사람의 ObjID, 람다) — 그 사람을 소유한 Zone 스레드에서
//    User를 받아 실행한다(그 사이 Zone을 옮겼거나 옮기는 중이면 새 Zone으로 다시 넘긴다 · 나갔으면 nullptr).
//  · 서비스 → 사람에게 알림: 세션 PostSend(세션별 송신 락)만 — 그 사람의 게임 상태는 건드리지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
namespace Service
{
	// 서비스 스레드 표시(디버그 검사용). 스레드마다 하나 — 서비스 Run이 켤 때 정한다.
	inline thread_local int LServiceId = 0;
	inline constexpr int kGuildServiceId = 1;
	inline constexpr int kAuctionServiceId = 2;

	// 요청한 사람의 정보(요청을 받은 Zone 스레드에서 만든 사본 — 서비스는 이 값만 쓴다)
	struct Ctx
	{
		int                       playerId = 0;
		ObjID                     obj{};
		std::weak_ptr<GameSession> session;
		std::string               name;
		uint8_t                   level = 1;
		uint8_t                   classId = 0;
	};

	[[nodiscard]] inline Ctx MakeCtx(const std::shared_ptr<User>& user)
	{
		Ctx c;
		c.playerId = user->GetPlayerId();
		c.obj = user->GetObjID();
		c.session = user->GetGameSession();
		c.name = user->GetName();
		c.level = user->GetStat()->GetLevel();
		c.classId = static_cast<uint8_t>(user->m_classId);
		return c;
	}

	template<typename Packet>
	void Init(Packet& p, PacketType type)
	{
		p = {};
		p.size = sizeof(Packet);
		p.type = static_cast<char>(type);
	}

	// 확장 세션이 게임 안에 있을 때만 보낸다(아무 스레드)
	template<typename Packet>
	bool SendTo(const std::weak_ptr<GameSession>& weak, const Packet& p)
	{
		const auto s = weak.lock();
		if (s == nullptr || s->m_state != SOCKET_STATE::ST_INGAME || !s->IsExtended())
			return false;
		s->PostSend(p);
		return true;
	}

	// obj를 소유한 Zone 스레드에서 fn(user)를 실행한다. 나갔으면 user = nullptr(그때는 Zone 상태를 만지지 말 것).
	inline void PostToOwner(const ObjID& obj, std::function<void(const std::shared_ptr<User>&)> fn, int hops = 0)
	{
		GZoneManager->EnqueueByObject(obj, [obj, fn = std::move(fn), hops]() mutable
		{
			auto user = ::GetGameObject<User>(obj);
			if (user != nullptr && hops < 8)
			{
				// Zone을 옮기는 중이면 새 Zone(입장 처리 뒤)으로, 소유 Zone이 아니면 지금 소유 Zone으로 다시
				if (user->IsTransferring())
				{
					const ZoneId target = user->GetZoneId();
					GZoneManager->EnqueueByZone(target, [obj, fn = std::move(fn), hops]() mutable { PostToOwner(obj, std::move(fn), hops + 1); });
					return;
				}
				if (!GZoneManager->IsCurrentThreadOwner(obj))
				{
					PostToOwner(obj, std::move(fn), hops + 1);
					return;
				}
			}
			fn(user);
		});
	}

	[[nodiscard]] inline int64_t UnixNow() noexcept { return static_cast<int64_t>(std::time(nullptr)); }
}
