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

GameSession::GameSession() : _recvContext(MakeShared<RecvContext>())
{
	ResetNetworkState();
}

GameSession::~GameSession()
{
	CloseSession();
}

void GameSession::BindOwner(const shared_ptr<User>& owner)
{
	_owner = owner;
}

shared_ptr<User> GameSession::GetOwner() const
{
	return _owner.lock();
}

bool GameSession::AttachSocket(SOCKET socket)
{
	lock_guard<mutex> guard(_sessionLock);

	CloseSocketUnsafe();

	if (socket == INVALID_SOCKET)
		return false;

	_socket = socket;
	_pendingRecvBytes = 0;

	BOOL noDelay = TRUE;
	SetSocketOption(_socket, IPPROTO_TCP, TCP_NODELAY, noDelay);

	return true;
}

void GameSession::CloseSocket()
{
	lock_guard<mutex> guard(_sessionLock);
	CloseSocketUnsafe();
}

void GameSession::CloseSession()
{
	lock_guard<mutex> guard(_sessionLock);
	CloseSocketUnsafe();
}

void GameSession::ResetNetworkState()
{
	_socket = INVALID_SOCKET;
	_pendingRecvBytes = 0;
}

bool GameSession::PostRecv()
{
	if (_socket == INVALID_SOCKET || _recvContext == nullptr)
		return false;

	DWORD recvFlag = 0;
	_recvContext->Prepare(_pendingRecvBytes);
	if (RegisterPendingIoContext(_recvContext) == false)
		return false;

	const int result = ::WSARecv(_socket, &_recvContext->_wsaBuf, 1, nullptr, &recvFlag, &_recvContext->_over, nullptr);
	if (result == SOCKET_ERROR)
	{
		const int errorCode = ::WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			(void)TakePendingIoContext(_recvContext.get());
			return false;
		}
	}

	return true;
}

void GameSession::CloseSocketUnsafe()
{
	if (_socket != INVALID_SOCKET)
	{
		::shutdown(_socket, SD_BOTH);
		::closesocket(_socket);
	}

	_socket = INVALID_SOCKET;
	_pendingRecvBytes = 0;
}
