#include "pch.h"
#include "GameSession.h"
#include "User.h"

AcceptContext GAcceptContext;

namespace
{
	mutex GPendingIoLock;
	unordered_map<IoContext*, shared_ptr<IoContext>> GPendingIoContexts;

	template<typename Type>
	bool SetSocketOption(SOCKET socket, int level, int option, Type value)
	{
		return SOCKET_ERROR != ::setsockopt(socket, level, option, reinterpret_cast<const char*>(&value), sizeof(value));
	}
}

bool RegisterPendingIoContext(const shared_ptr<IoContext>& context)
{
	if (context == nullptr)
		return false;

	scoped_lock lock(GPendingIoLock);
	return GPendingIoContexts.emplace(context.get(), context).second;
}

shared_ptr<IoContext> TakePendingIoContext(IoContext* context)
{
	if (context == nullptr)
		return nullptr;

	scoped_lock lock(GPendingIoLock);
	auto found = GPendingIoContexts.find(context);
	if (found == GPendingIoContexts.end())
		return nullptr;

	auto owned = std::move(found->second);
	GPendingIoContexts.erase(found);
	return owned;
}

GameSession::GameSession() : m_recvContext(make_shared<RecvContext>())
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

	CloseSocketUnsafe();

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
	CloseSocketUnsafe();
}

void GameSession::CloseSession()
{
	lock_guard<mutex> guard(m_sessionLock);
	CloseSocketUnsafe();
}

void GameSession::ResetNetworkState()
{
	m_socket = INVALID_SOCKET;
	m_pendingRecvBytes = 0;
}

bool GameSession::PostRecv()
{
	if (m_socket == INVALID_SOCKET || m_recvContext == nullptr)
		return false;

	DWORD recvFlag = 0;
	m_recvContext->Prepare(m_pendingRecvBytes);
	if (RegisterPendingIoContext(m_recvContext) == false)
		return false;

	const int result = ::WSARecv(m_socket, &m_recvContext->m_wsaBuf, 1, nullptr, &recvFlag, &m_recvContext->m_over, nullptr);
	if (result == SOCKET_ERROR)
	{
		const int errorCode = ::WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			(void)TakePendingIoContext(m_recvContext.get());
			return false;
		}
	}

	return true;
}

void GameSession::CloseSocketUnsafe()
{
	if (m_socket != INVALID_SOCKET)
	{
		::shutdown(m_socket, SD_BOTH);
		::closesocket(m_socket);
	}

	m_socket = INVALID_SOCKET;
	m_pendingRecvBytes = 0;
}
