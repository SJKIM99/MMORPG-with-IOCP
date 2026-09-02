#pragma once

#include "Zone/ZoneTypes.h"

constexpr int ACCEPT_BUFFER_SIZE = (sizeof(SOCKADDR_IN) + 16) * 2;

constexpr size_t ConstMaxSize(size_t lhs, size_t rhs)
{
	return (lhs > rhs) ? lhs : rhs;
}

// 패킷이 하나 늘 때마다 깊게 중첩된 ConstMaxSize(...) 트리를 손으로 다시 짜다가
// 괄호 개수를 맞추기 어려워지는 걸 피하려고, 누적 최댓값을 한 단계씩 이름 붙여
// 계산한다 — 각 줄은 항상 "지금까지의 최댓값 vs 새 패킷 하나"만 비교하므로
// 실수할 여지가 없고, 새 패킷은 끝에 한 줄만 추가하면 된다.
namespace ServerPacketSizeDetail
{
	constexpr size_t s01 = sizeof(USER_LOGIN_ACK_PACKET);
	constexpr size_t s02 = ConstMaxSize(s01, sizeof(USER_LOGIN_FAIL_ACK_PACKET));
	constexpr size_t s03 = ConstMaxSize(s02, sizeof(SUBJECT_ADD_NFY_PACKET));
	constexpr size_t s04 = ConstMaxSize(s03, sizeof(SUBJECT_REMOVE_NFY_PACKET));
	constexpr size_t s05 = ConstMaxSize(s04, sizeof(SUBJECT_MOVE_NFY_PACKET));
	constexpr size_t s06 = ConstMaxSize(s05, sizeof(SUBJECT_DIE_NFY_PACKET));
	constexpr size_t s07 = ConstMaxSize(s06, sizeof(SUBJECT_RESPAWN_NFY_PACKET));
	constexpr size_t s08 = ConstMaxSize(s07, sizeof(USER_ATTACK_ACK_PACKET));
	constexpr size_t s09 = ConstMaxSize(s08, sizeof(SUBJECT_ATTACK_NFY_PACKET));
	constexpr size_t s10 = ConstMaxSize(s09, sizeof(USER_HEAL_INF_PACKET));
	constexpr size_t s11 = ConstMaxSize(s10, sizeof(USER_STAT_CHANGE_INF_PACKET));
	constexpr size_t s12 = ConstMaxSize(s11, sizeof(PLAYER_ATTACK_NFY_PACKET));
	constexpr size_t s13 = ConstMaxSize(s12, sizeof(SC_CHAT_PACKET));
	constexpr size_t s14 = ConstMaxSize(s13, sizeof(ITEM_LIST_ACK_PACKET));
	constexpr size_t s15 = ConstMaxSize(s14, sizeof(ITEM_EQUIP_ACK_PACKET));
	constexpr size_t s16 = ConstMaxSize(s15, sizeof(ITEM_UNEQUIP_ACK_PACKET));
	constexpr size_t s17 = ConstMaxSize(s16, sizeof(ITEM_SWAP_ACK_PACKET));
	constexpr size_t s18 = ConstMaxSize(s17, sizeof(ITEM_ACQUIRE_INF_PACKET));
	constexpr size_t s19 = ConstMaxSize(s18, sizeof(SYSTEM_MESSAGE_INF_PACKET));
	constexpr size_t s20 = ConstMaxSize(s19, sizeof(ITEM_DISCARD_ACK_PACKET));
	constexpr size_t s21 = ConstMaxSize(s20, sizeof(ITEM_PICKUP_ACK_PACKET));
	constexpr size_t s22 = ConstMaxSize(s21, sizeof(SUBJECT_EQUIP_CHANGE_NFY_PACKET));
}
constexpr size_t MAX_SERVER_PACKET_SIZE = ServerPacketSizeDetail::s22;
constexpr size_t SEND_BATCH_BUFFER_SIZE = 4096;
static_assert(SEND_BATCH_BUFFER_SIZE >= MAX_SERVER_PACKET_SIZE, "Send batch buffer must fit one server packet.");

class GameSession;
class IoContext
{
public:
	WSAOVERLAPPED m_over;
	IO_TYPE m_type;
	shared_ptr<GameSession> m_session;

public:
	explicit IoContext(IO_TYPE type) : m_type(type)
	{
		::ZeroMemory(&m_over, sizeof(m_over));
	}

	void ResetOverlapped()
	{
		::ZeroMemory(&m_over, sizeof(m_over));
	}

	void AttachSession(const shared_ptr<GameSession>& session) noexcept
	{
		m_session = session;
	}

	[[nodiscard]] shared_ptr<GameSession> DetachSession() noexcept
	{
		auto session = std::move(m_session);
		m_session.reset();
		return session;
	}

	[[nodiscard]] static IoContext* FromOverlapped(WSAOVERLAPPED* over) noexcept
	{
		return over != nullptr ? CONTAINING_RECORD(over, IoContext, m_over) : nullptr;
	}
};

class AcceptContext : public IoContext
{
public:
	char m_buffer[ACCEPT_BUFFER_SIZE];

public:
	AcceptContext() : IoContext(IO_TYPE::IO_ACCEPT) { }
};

class RecvContext : public IoContext
{
public:
	WSABUF m_wsaBuf{};
	char m_buffer[BUF_SIZE]{};

public:
	RecvContext() : IoContext(IO_TYPE::IO_RECV)
	{
		m_wsaBuf.buf = m_buffer;
		m_wsaBuf.len = BUF_SIZE;
	}

	void Prepare(uint32_t remainData)
	{
		m_type = IO_TYPE::IO_RECV;
		ResetOverlapped();
		m_wsaBuf.buf = m_buffer + remainData;
		m_wsaBuf.len = BUF_SIZE - remainData;
	}
};

class SendContext : public IoContext
{
public:
	WSABUF m_wsaBuf{};
	char m_buffer[SEND_BATCH_BUFFER_SIZE]{};
	uint32_t m_bytes = 0;
	uint32_t m_packetCount = 0;

public:
	SendContext() : IoContext(IO_TYPE::IO_SEND)
	{
		m_wsaBuf.buf = m_buffer;
		m_wsaBuf.len = 0;
	}

	void ResetPayload()
	{
		m_type = IO_TYPE::IO_SEND;
		ResetOverlapped();
		m_wsaBuf.buf = m_buffer;
		m_wsaBuf.len = 0;
		m_bytes = 0;
		m_packetCount = 0;
	}

	[[nodiscard]] bool AppendPacket(const char* packet, uint16_t packetSize)
	{
		if (packet == nullptr || packetSize == 0 || m_bytes + packetSize > SEND_BATCH_BUFFER_SIZE)
			return false;

		::memcpy(m_buffer + m_bytes, packet, packetSize);
		m_bytes += packetSize;
		m_wsaBuf.len = m_bytes;
		++m_packetCount;
		return true;
	}

	[[nodiscard]] uint32_t GetPacketCount() const noexcept
	{
		return m_packetCount;
	}

	[[nodiscard]] uint32_t GetBufferedBytes() const noexcept
	{
		return m_bytes;
	}
};

struct PendingSendPacket
{
	uint16_t size = 0;
	uint16_t offset = 0;
	char buffer[MAX_SERVER_PACKET_SIZE]{};

	template<typename Packet>
	void Assign(const Packet& packet)
	{
		static_assert(std::is_trivially_copyable_v<Packet>, "Packet must be trivially copyable.");
		ASSERT_CRASH(packet.size <= MAX_SERVER_PACKET_SIZE);

		size = packet.size;
		offset = 0;
		::memcpy(buffer, &packet, size);
	}

	[[nodiscard]] const char* Data() const noexcept
	{
		return buffer + offset;
	}

	[[nodiscard]] uint16_t RemainingSize() const noexcept
	{
		return static_cast<uint16_t>(size - offset);
	}

	void Consume(uint32_t bytes)
	{
		ASSERT_CRASH(bytes <= RemainingSize());
		offset = static_cast<uint16_t>(offset + bytes);
	}
};

class User;

extern AcceptContext GAcceptContext;

class GameSession : public enable_shared_from_this<GameSession>
{
public:
	SOCKET m_socket = INVALID_SOCKET;
	SOCKET_STATE m_state = SOCKET_STATE::ST_FREE;
	uint32_t m_pendingRecvBytes = 0;
	uint32_t m_objectId = 0;
	atomic<ZoneId> m_zoneId{ InvalidZoneId };
	mutex m_sessionLock;
	weak_ptr<User> m_owner;

protected:
	unique_ptr<RecvContext> m_recvContext;
	SendContext             m_sendContext;
	deque<PendingSendPacket> m_sendQueue;
	bool                    m_sendInFlight = false;

public:
	GameSession();
	virtual ~GameSession();

	void BindOwner(const shared_ptr<User>& owner);
	[[nodiscard]] shared_ptr<User> GetOwner() const;
	[[nodiscard]] ZoneId GetZoneId() const noexcept { return m_zoneId.load(memory_order_relaxed); }
	void SetZoneId(ZoneId zoneId) noexcept { m_zoneId.store(zoneId, memory_order_relaxed); }
	bool AttachSocket(SOCKET socket);
	void CloseSocket();
	void CloseSession();
	void ResetNetworkState();
	bool PostRecv();
	bool HandleSendCompletion(const shared_ptr<GameSession>& self, uint32_t bytesTransferred);
	void HandleSendFailure();

	template<typename Packet>
	bool PostSend(const Packet& packet)
	{
		auto self = shared_from_this();
		{
			lock_guard<mutex> guard(m_sessionLock);
			if (m_socket == INVALID_SOCKET)
				return false;

			auto& pending = m_sendQueue.emplace_back();
			pending.Assign(packet);

			if (m_sendInFlight)
				return true;

			return SubmitSendBatchImpl(self);
		}
	}

private:
	void CloseSocketImpl();
	bool SubmitSendBatchImpl(const shared_ptr<GameSession>& self);
};
