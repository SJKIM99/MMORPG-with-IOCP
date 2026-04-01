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
						sizeof(SC_NPC_DIE_PACKET),
						ConstMaxSize(
							sizeof(SC_NPC_RESPAWN_PACKET),
							ConstMaxSize(
								sizeof(SC_PLAYER_ATTACK_NPC_PACKET),
								ConstMaxSize(
									sizeof(SC_NPC_ATTACK_PLAYER_PACKET),
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
	explicit IoContext(IO_TYPE type, uint64 sessionToken = 0) : _type(type), _sessionToken(sessionToken)
	{
		::ZeroMemory(&_over, sizeof(_over));
	}

	void ResetOverlapped(uint64 sessionToken = 0)
	{
		::ZeroMemory(&_over, sizeof(_over));
		_sessionToken = sessionToken;
	}

public:
	WSAOVERLAPPED _over;
	IO_TYPE _type;
	uint64 _sessionToken = 0;
};

class AcceptContext : public IoContext
{
public:
	AcceptContext() : IoContext(IO_TYPE::IO_ACCEPT) { }

public:
	char _buffer[ACCEPT_BUFFER_SIZE];
};

class RecvContext : public IoContext
{
public:
	RecvContext() : IoContext(IO_TYPE::IO_RECV)
	{
		_wsaBuf.buf = _buffer;
		_wsaBuf.len = BUF_SIZE;
	}

	void Prepare(uint32 remainData, uint64 sessionToken)
	{
		ASSERT_CRASH(remainData < BUF_SIZE);

		_type = IO_TYPE::IO_RECV;
		ResetOverlapped(sessionToken);
		_wsaBuf.buf = _buffer + remainData;
		_wsaBuf.len = BUF_SIZE - remainData;
	}

public:
	WSABUF _wsaBuf{};
	char _buffer[BUF_SIZE]{};
};

class SendContext : public IoContext
{
public:
	template<typename Packet>
	explicit SendContext(uint64 sessionToken, const Packet& packet) : IoContext(IO_TYPE::IO_SEND, sessionToken)
	{
		static_assert(std::is_trivially_copyable_v<Packet>, "Packet must be trivially copyable.");

		ASSERT_CRASH(packet.size <= MAX_SERVER_PACKET_SIZE);

		_wsaBuf.buf = _buffer;
		_wsaBuf.len = packet.size;
		::memcpy(_buffer, &packet, packet.size);
	}

public:
	WSABUF _wsaBuf{};
	char _buffer[MAX_SERVER_PACKET_SIZE]{};
};

extern AcceptContext GAcceptContext;

class GameSession
{
public:
	GameSession();
	virtual ~GameSession();

	bool AttachSocket(SOCKET socket, uint32 sessionId);
	void CloseSocket();
	void CloseSession();
	void ResetNetworkState();
	bool PostRecv();
	[[nodiscard]] uint64 GetSessionToken() const noexcept { return _sessionToken.load(); }

	template<typename Packet>
	bool PostSend(const Packet& packet)
	{
		if (_socket == INVALID_SOCKET)
			return false;

		SendContext* sendContext = xnew<SendContext>(GetSessionToken(), packet);
		const int result = ::WSASend(_socket, &sendContext->_wsaBuf, 1, nullptr, 0, &sendContext->_over, nullptr);
		if (result == SOCKET_ERROR)
		{
			const int errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				xdelete(sendContext);
				return false;
			}
		}

		return true;
	}

public:
	SOCKET _socket = INVALID_SOCKET;
	SOCKET_STATE _state = SOCKET_STATE::ST_FREE;
	uint32 _pendingRecvBytes = 0;
	uint32 _id = static_cast<uint32>(-1);
	mutex _sessionLock;
	Atomic<uint64> _sessionToken = 1;

protected:
	RecvContext _recvContext;

private:
	void CloseSocketUnsafe();
};
