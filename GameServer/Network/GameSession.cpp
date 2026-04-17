#include "pch.h"
#include "GameSession.h"
#include "User.h"

AcceptContext GAcceptContext;

namespace
{
	template<typename Type>
	bool SetSocketOption(SOCKET socket, int level, int option, Type value)
	{
		return SOCKET_ERROR != ::setsockopt(socket, level, option, reinterpret_cast<const char*>(&value), sizeof(value));
	}
}

GameSession::GameSession() : m_recvContext(make_unique<RecvContext>())
{
	ResetNetworkState();
}

GameSession::~GameSession()
{
	CloseSession();
}

void GameSession::BindOwner(const shared_ptr<User>& owner)
{
	m_owner = owner;
}

shared_ptr<User> GameSession::GetOwner() const
{
	return m_owner.lock();
}

bool GameSession::AttachSocket(SOCKET socket)
{
	lock_guard<mutex> guard(m_sessionLock);

	CloseSocketImpl();

	if (socket == INVALID_SOCKET)
		return false;

	m_socket = socket;
	m_pendingRecvBytes = 0;

	BOOL noDelay = TRUE;
	SetSocketOption(m_socket, IPPROTO_TCP, TCP_NODELAY, noDelay);

	return true;
}

void GameSession::CloseSocket()
{
	lock_guard<mutex> guard(m_sessionLock);
	CloseSocketImpl();
}

void GameSession::CloseSession()
{
	lock_guard<mutex> guard(m_sessionLock);
	CloseSocketImpl();
}

void GameSession::ResetNetworkState()
{
	m_socket = INVALID_SOCKET;
	m_pendingRecvBytes = 0;
	m_sendQueue.clear();
	m_sendContext.ResetPayload();
	m_sendInFlight = false;
}

bool GameSession::PostRecv()
{
	if (m_socket == INVALID_SOCKET || m_recvContext == nullptr)
		return false;

	auto self = shared_from_this();
	DWORD recvFlag = 0;
	m_recvContext->Prepare(m_pendingRecvBytes);
	m_recvContext->AttachSession(self);

	const int result = ::WSARecv(m_socket, &m_recvContext->m_wsaBuf, 1, nullptr, &recvFlag, &m_recvContext->m_over, nullptr);
	if (result == SOCKET_ERROR)
	{
		const int errorCode = ::WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			(void)m_recvContext->DetachSession();
			return false;
		}
	}

	return true;
}

bool GameSession::HandleSendCompletion(const shared_ptr<GameSession>& self, uint32_t bytesTransferred)
{
	lock_guard<mutex> guard(m_sessionLock);

	const uint32_t bufferedBytes = m_sendContext.GetBufferedBytes();
	if (bytesTransferred > bufferedBytes)
		return false;

	uint32_t remainingToConsume = bytesTransferred;
	while (remainingToConsume > 0)
	{
		if (m_sendQueue.empty())
			return false;

		auto& pending = m_sendQueue.front();
		const uint32_t pendingBytes = pending.RemainingSize();
		if (remainingToConsume < pendingBytes)
		{
			pending.Consume(remainingToConsume);
			remainingToConsume = 0;
			break;
		}

		remainingToConsume -= pendingBytes;
		m_sendQueue.pop_front();
	}

	(void)m_sendContext.DetachSession();
	m_sendContext.ResetPayload();
	m_sendInFlight = false;

	if (m_socket == INVALID_SOCKET || m_sendQueue.empty())
		return true;

	return SubmitSendBatchImpl(self);
}

void GameSession::HandleSendFailure()
{
	lock_guard<mutex> guard(m_sessionLock);
	m_sendQueue.clear();
	(void)m_sendContext.DetachSession();
	m_sendContext.ResetPayload();
	m_sendInFlight = false;
}

void GameSession::CloseSocketImpl()
{
	m_sendQueue.clear();

	if (m_socket != INVALID_SOCKET)
	{
		::shutdown(m_socket, SD_BOTH);
		::closesocket(m_socket);
	}

	m_socket = INVALID_SOCKET;
	m_pendingRecvBytes = 0;
}

bool GameSession::SubmitSendBatchImpl(const shared_ptr<GameSession>& self)
{
	if (m_socket == INVALID_SOCKET)
		return false;
	if (m_sendInFlight)
		return true;
	if (m_sendQueue.empty())
		return true;

	m_sendContext.ResetPayload();

	for (const PendingSendPacket& pending : m_sendQueue)
	{
		if (m_sendContext.AppendPacket(pending.Data(), pending.RemainingSize()) == false)
			break;
	}

	if (m_sendContext.GetPacketCount() == 0)
		return false;

	m_sendContext.AttachSession(self);
	m_sendInFlight = true;

	const int result = ::WSASend(m_socket, &m_sendContext.m_wsaBuf, 1, nullptr, 0, &m_sendContext.m_over, nullptr);
	if (result == SOCKET_ERROR)
	{
		const int errorCode = ::WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			m_sendInFlight = false;
			(void)m_sendContext.DetachSession();
			m_sendContext.ResetPayload();
			return false;
		}
	}

	return true;
}

//void GameSession::ClearPendingSendsUnsafe()
//{
//	m_sendQueue.clear();
//}
