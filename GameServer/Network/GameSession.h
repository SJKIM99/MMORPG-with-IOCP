#pragma once

constexpr int ACCEPT_BUFFER_SIZE = (sizeof(SOCKADDR_IN) + 16) * 2;

constexpr size_t ConstMaxSize(size_t lhs, size_t rhs)
{
	return (lhs > rhs) ? lhs : rhs;
}

constexpr size_t MAX_SERVER_PACKET_SIZE =
	ConstMaxSize(
		sizeof(SC_LOGIN_SUCCESS_PACKET),
		ConstMaxSize(
			sizeof(SC_ADD_OBJECT_PACKET),
			ConstMaxSize(
				sizeof(SC_REMOVE_OBJECT_PACKET),
				ConstMaxSize(
					sizeof(SC_MOVE_OBJECT_PACKET),
					ConstMaxSize(
						sizeof(SC_MONSTER_DIE_PACKET),
						ConstMaxSize(
							sizeof(SC_MONSTER_RESPAWN_PACKET),
							ConstMaxSize(
								sizeof(SC_PLAYER_ATTACK_MONSTER_PACKET),
								ConstMaxSize(
									sizeof(SC_MONSTER_ATTACK_PLAYER_PACKET),
									ConstMaxSize(
										sizeof(SC_HEAL_PACKET),
										ConstMaxSize(sizeof(SC_PLAYER_DIE_PACKET), sizeof(SC_PLAYER_RESPAWN_PACKET))
									)
								)
							)
						)
					)
				)
			)
		)
	);

class IoContext
{
public:
	WSAOVERLAPPED m_over;
	IO_TYPE m_type;

public:
	explicit IoContext(IO_TYPE type) : m_type(type)
	{
		::ZeroMemory(&m_over, sizeof(m_over));
	}

	void ResetOverlapped()
	{
		::ZeroMemory(&m_over, sizeof(m_over));
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
	char m_buffer[MAX_SERVER_PACKET_SIZE]{};

public:
	template<typename Packet>
	explicit SendContext(const Packet& packet) : IoContext(IO_TYPE::IO_SEND)
	{
		static_assert(std::is_trivially_copyable_v<Packet>, "Packet must be trivially copyable.");

		ASSERT_CRASH(packet.size <= MAX_SERVER_PACKET_SIZE);

		m_wsaBuf.buf = m_buffer;
		m_wsaBuf.len = packet.size;
		::memcpy(m_buffer, &packet, packet.size);
	}
};

class User;

extern AcceptContext GAcceptContext;

bool RegisterPendingIoContext(const shared_ptr<IoContext>& context);
shared_ptr<IoContext> TakePendingIoContext(IoContext* context);

class GameSession
{
public:
	SOCKET m_socket = INVALID_SOCKET;
	SOCKET_STATE m_state = SOCKET_STATE::ST_FREE;
	uint32_t m_pendingRecvBytes = 0;
	uint32_t m_objectId = 0;
	mutex m_sessionLock;
	weak_ptr<User> m_owner;

protected:
	shared_ptr<RecvContext> m_recvContext;

public:
	GameSession();
	virtual ~GameSession();

	void BindOwner(const shared_ptr<User>& owner);
	[[nodiscard]] shared_ptr<User> GetOwner() const;
	bool AttachSocket(SOCKET socket);
	void CloseSocket();
	void CloseSession();
	void ResetNetworkState();
	bool PostRecv();

	template<typename Packet>
	bool PostSend(const Packet& packet)
	{
		if (m_socket == INVALID_SOCKET)
			return false;

		auto sendContext = make_shared<SendContext>(packet);
		if (RegisterPendingIoContext(sendContext) == false)
			return false;

		const int result = ::WSASend(m_socket, &sendContext->m_wsaBuf, 1, nullptr, 0, &sendContext->m_over, nullptr);
		if (result == SOCKET_ERROR)
		{
			const int errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				(void)TakePendingIoContext(sendContext.get());
				return false;
			}
		}

		return true;
	}

private:
	void CloseSocketUnsafe();
};
