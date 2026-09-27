#include "pch.h"
#include "ChatHelper.h"
#include "User.h"
#include "GameSession.h"
#include "GameSessionManager.h"
#include "GameObjectManager.h"
#include "Sector.h"
#include "SubjectHelper.h"
#include "UserHelper.h"
#include "Content/GuildService.h"

#include <deque>

namespace ChatHelper
{
	namespace
	{
		template<typename Packet>
		void InitializePacket(Packet& packet, PacketType type)
		{
			packet = {};
			packet.size = sizeof(Packet);
			packet.type = static_cast<char>(type);
		}

		// 최근 대화(접속 직후 20줄) — 여러 Zone 스레드가 쓰므로 락. 한 줄마다 잠깐(복사 1회).
		struct RecentLine
		{
			uint8_t  channel = 0;
			ZoneId   zone = InvalidZoneId;   // 일반 대화의 지역(전체 · 거래는 무시)
			ObjID    sender{};
			char     name[NAME_SIZE]{};
			char     mess[CHAT_SIZE]{};
		};
		std::mutex             g_recentLock;
		std::deque<RecentLine> g_recent;

		void Remember(ChatChannel ch, ZoneId zone, const shared_ptr<User>& sender, const char* text)
		{
			RecentLine line;
			line.channel = static_cast<uint8_t>(ch);
			line.zone = zone;
			line.sender = sender->GetObjID();
			::strncpy_s(line.name, NAME_SIZE, sender->GetName().c_str(), _TRUNCATE);
			::strncpy_s(line.mess, CHAT_SIZE, text, _TRUNCATE);
			lock_guard lock(g_recentLock);
			g_recent.push_back(line);
			// 지역마다 일반 대화가 섞여 있으니 넉넉히(지역 16 · 전체 · 거래)
			while (g_recent.size() > kRecentMax * 8)
				g_recent.pop_front();
		}

		uint32_t Hash(const char* s)
		{
			uint32_t h = 2166136261u;
			for (; *s; ++s) { h ^= static_cast<uint8_t>(*s); h *= 16777619u; }
			return h;
		}

		void Fill(SC_CHAT_EX_PACKET& p, uint8_t channel, const ObjID& id, const std::string& name, const char* text)
		{
			InitializePacket(p, PacketType::SC_CHAT_EX);
			p.channel = channel;
			p.senderId = id;
			::strncpy_s(p.senderName, NAME_SIZE, name.c_str(), _TRUNCATE);
			::strncpy_s(p.mess, CHAT_SIZE, text, _TRUNCATE);
		}

		bool Deliverable(const shared_ptr<GameSession>& s)
		{
			return s != nullptr && s->m_state == SOCKET_STATE::ST_INGAME && s->IsExtended();
		}

		void Reject(const shared_ptr<User>& user, SystemMessageCode code, int32_t p1 = 0, int32_t p2 = 0)
		{
			UserHelper::SendSystemEx(user, code, p1, p2);
		}
	}

	bool IsValidUtf8(const char* s, size_t n)
	{
		size_t i = 0;
		while (i < n)
		{
			const uint8_t c = static_cast<uint8_t>(s[i]);
			if (c < 0x80)
			{
				if (c < 0x20 || c == 0x7F) return false;   // 제어 문자(줄바꿈 · 탭 포함) 거부
				++i;
				continue;
			}
			int len = 0;
			uint32_t cp = 0;
			if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
			else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
			else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
			else return false;
			if (i + static_cast<size_t>(len) > n) return false;
			for (int k = 1; k < len; ++k)
			{
				const uint8_t cc = static_cast<uint8_t>(s[i + static_cast<size_t>(k)]);
				if ((cc & 0xC0) != 0x80) return false;
				cp = (cp << 6) | (cc & 0x3F);
			}
			// 너무 긴 표현 · 서로게이트 · 범위 밖
			if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)) return false;
			if (cp >= 0xD800 && cp <= 0xDFFF) return false;
			if (cp > 0x10FFFF) return false;
			i += static_cast<size_t>(len);
		}
		return true;
	}

	void HandleChatEx(const shared_ptr<User>& sender, uint8_t channel, const char* targetName, const char* mess)
	{
		if (sender == nullptr || !sender->IsExtended())
			return;
		auto session = sender->GetGameSession();
		if (!session || session->m_state != SOCKET_STATE::ST_INGAME)
			return;

		// ── 글: 복사(널 종료 보장) · 앞뒤 공백 · UTF-8 ──
		char text[CHAT_SIZE + 1]{};
		::memcpy(text, mess, CHAT_SIZE);
		size_t n = ::strnlen(text, CHAT_SIZE);
		while (n > 0 && text[n - 1] == ' ') text[--n] = '\0';
		size_t start = 0;
		while (start < n && text[start] == ' ') ++start;
		const char* body = text + start;
		const size_t len = n - start;
		const ChatChannel ch = static_cast<ChatChannel>(channel);
		const bool knownChannel = ch == ChatChannel::Normal || ch == ChatChannel::World || ch == ChatChannel::Trade
			|| ch == ChatChannel::Guild || ch == ChatChannel::WhisperOut;
		if (len == 0 || !knownChannel || !IsValidUtf8(body, len))
		{
			Reject(sender, SystemMessageCode::ChatBadText);
			return;
		}

		const uint32_t now = GetNowTime();
		// ── 토큰 버킷(5줄 / 5초) ──
		if (sender->m_chatRefillAt == 0) sender->m_chatRefillAt = now;
		if (now > sender->m_chatRefillAt)
		{
			const uint32_t add = (now - sender->m_chatRefillAt) / kBucketRefillMs;
			if (add > 0)
			{
				const uint32_t t = std::min<uint32_t>(kBucketMax, sender->m_chatTokens + add);
				sender->m_chatTokens = static_cast<uint8_t>(t);
				sender->m_chatRefillAt = t >= static_cast<uint32_t>(kBucketMax) ? now : sender->m_chatRefillAt + add * kBucketRefillMs;
			}
		}
		if (sender->m_chatTokens == 0)
		{
			const uint32_t wait = kBucketRefillMs - std::min<uint32_t>(kBucketRefillMs, now - sender->m_chatRefillAt);
			Reject(sender, SystemMessageCode::ChatTooFast, channel, static_cast<int32_t>(wait));
			return;
		}
		// ── 같은 말 되풀이(30초 안의 세 번째부터) ──
		const uint32_t h = Hash(body);
		const bool same = h == sender->m_chatLastHash && now - sender->m_chatLastAt < kRepeatWindowMs;
		if (same && sender->m_chatRepeat >= 1)
		{
			Reject(sender, SystemMessageCode::ChatRepeated);
			return;
		}
		// ── 채널 재사용 대기 · 대상 ──
		if (ch == ChatChannel::World && sender->m_lastWorldChatTime != 0 && now < sender->m_lastWorldChatTime + kWorldCooldownMs)
		{
			Reject(sender, SystemMessageCode::ChatTooFast, channel, static_cast<int32_t>(sender->m_lastWorldChatTime + kWorldCooldownMs - now));
			return;
		}
		if (ch == ChatChannel::Trade && sender->m_lastTradeChatTime != 0 && now < sender->m_lastTradeChatTime + kTradeCooldownMs)
		{
			Reject(sender, SystemMessageCode::ChatTooFast, channel, static_cast<int32_t>(sender->m_lastTradeChatTime + kTradeCooldownMs - now));
			return;
		}
		if (ch == ChatChannel::Guild && sender->m_guildId.load(std::memory_order_relaxed) == 0)
		{
			Reject(sender, SystemMessageCode::ChatNoGuild);   // 결사가 없다
			return;
		}
		shared_ptr<GameSession> target;
		shared_ptr<User> targetUser;
		if (ch == ChatChannel::WhisperOut)
		{
			char name[NAME_SIZE + 1]{};
			::memcpy(name, targetName, NAME_SIZE);
			target = GSessionManager->FindByName(name);
			targetUser = target ? target->GetOwner() : nullptr;
			if (!Deliverable(target) || targetUser == nullptr || target == session)
			{
				Reject(sender, SystemMessageCode::ChatNoTarget);
				return;
			}
		}

		// ── 받아들임: 상태 갱신 ──
		--sender->m_chatTokens;
		sender->m_chatRepeat = same ? static_cast<uint8_t>(sender->m_chatRepeat + 1) : 0;
		sender->m_chatLastHash = h;
		sender->m_chatLastAt = now;
		if (ch == ChatChannel::World) sender->m_lastWorldChatTime = now;
		if (ch == ChatChannel::Trade) sender->m_lastTradeChatTime = now;

		const ObjID senderId = sender->GetObjID();
		SC_CHAT_EX_PACKET pk;
		switch (ch)
		{
		case ChatChannel::Normal:
		{
			// 확장 세션: 같은 지역(Zone) 전체 — 보낸 사람은 섹터에서 빠져 있어도(죽음) 먼저 받는다
			Fill(pk, static_cast<uint8_t>(ChatChannel::Normal), senderId, sender->GetName(), body);
			session->PostSend(pk);
			GSector->ForEachUserInZone([&](const ObjID& id)
			{
				if (id == senderId) return;
				auto viewer = ::GetGameObject<User>(id);
				if (viewer == nullptr) return;
				auto s = viewer->GetGameSession();
				if (Deliverable(s)) s->PostSend(pk);
			});
			// 레거시 세션(봇 · 옛 클라): 예전 CS_CHAT과 같은 시야 대화
			if (sender->GetSectorX() >= 0)
				GSector->ForEachNeighborObject(sender->GetSectorX(), sender->GetSectorY(), [&](const shared_ptr<Subject>& object)
				{
					const ObjID id = object->GetObjID();
					if (id == senderId || id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;
					auto viewer = static_pointer_cast<User>(object);
					auto s = viewer->GetGameSession();
					if (!s || s->m_state != SOCKET_STATE::ST_INGAME || s->IsExtended()) return;
					if (SubjectHelper::CanSee(object, sender))
						UserHelper::SendSC_CHAT(viewer, senderId, body);
				});
			Remember(ch, LCurrentZoneId, sender, body);
			break;
		}
		case ChatChannel::World:
		case ChatChannel::Trade:
		{
			Fill(pk, channel, senderId, sender->GetName(), body);
			for (const auto& s : GSessionManager->SnapshotSessions())
				if (Deliverable(s)) s->PostSend(pk);
			Remember(ch, LCurrentZoneId, sender, body);
			break;
		}
		case ChatChannel::Guild:
		{
			// Phase 5: 결사원(접속 중)에게 — 구성원 목록의 주인인 GuildService가 보낸다(보낸 사람 포함)
			GuildHelper::RelayChat(sender, body);
			break;
		}
		case ChatChannel::WhisperOut:
		{
			// 받는 사람: "WhisperIn — 보낸 이", 보낸 사람: "WhisperOut — 받는 이"(이름은 받는 사람 것으로 맞춘다)
			Fill(pk, static_cast<uint8_t>(ChatChannel::WhisperIn), senderId, sender->GetName(), body);
			target->PostSend(pk);
			SC_CHAT_EX_PACKET echo;
			Fill(echo, static_cast<uint8_t>(ChatChannel::WhisperOut), targetUser->GetObjID(), targetUser->GetName(), body);
			session->PostSend(echo);
			break;
		}
		default:
			break;
		}
	}

	void OnEnterWorld(const shared_ptr<User>& user)
	{
		if (user == nullptr || !user->IsExtended())
			return;
		auto session = user->GetGameSession();
		if (!session)
			return;
		GSessionManager->RegisterName(user->GetName(), session);

		// 최근 대화: 전체 · 거래 + 이 지역의 일반 대화에서 마지막 20줄
		std::vector<RecentLine> lines;
		{
			lock_guard lock(g_recentLock);
			for (auto it = g_recent.rbegin(); it != g_recent.rend() && lines.size() < kRecentMax; ++it)
			{
				const bool normal = it->channel == static_cast<uint8_t>(ChatChannel::Normal);
				if (normal && it->zone != LCurrentZoneId) continue;
				lines.push_back(*it);
			}
		}
		for (auto it = lines.rbegin(); it != lines.rend(); ++it)
		{
			SC_CHAT_EX_PACKET pk;
			Fill(pk, static_cast<uint8_t>(it->channel | kChatHistoryBit), it->sender, it->name, it->mess);
			session->PostSend(pk);
		}
	}
}
