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
	explicit IoContext(IO_TYPE type) : _type(type)
	{
		::ZeroMemory(&_over, sizeof(_over));
	}

	void ResetOverlapped()
	{
		::ZeroMemory(&_over, sizeof(_over));
	}

public:
	WSAOVERLAPPED _over;
	IO_TYPE _type;
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

	void Prepare(uint32 remainData)
	{
		ASSERT_CRASH(remainData < BUF_SIZE);

		_type = IO_TYPE::IO_RECV;
		ResetOverlapped();
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
	explicit SendContext(const Packet& packet) : IoContext(IO_TYPE::IO_SEND)
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

class User;

extern AcceptContext GAcceptContext;

bool RegisterPendingIoContext(const shared_ptr<IoContext>& context);
shared_ptr<IoContext> TakePendingIoContext(IoContext* context);

class GameSession
{
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
		if (_socket == INVALID_SOCKET)
			return false;

		auto sendContext = MakeShared<SendContext>(packet);
		if (RegisterPendingIoContext(sendContext) == false)
			return false;

		const int result = ::WSASend(_socket, &sendContext->_wsaBuf, 1, nullptr, 0, &sendContext->_over, nullptr);
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

public:
	SOCKET _socket = INVALID_SOCKET;
	SOCKET_STATE _state = SOCKET_STATE::ST_FREE;
	uint32 _pendingRecvBytes = 0;
	uint32 _objectId = 0;
	mutex _sessionLock;
	weak_ptr<User> _owner;


protected:
	shared_ptr<RecvContext> _recvContext;

private:
	void CloseSocketUnsafe();
};
