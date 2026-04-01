#include "pch.h"
#include "GameSession.h"

AcceptContext GAcceptContext;

namespace
{
	template<typename Type>
	bool SetSocketOption(SOCKET socket, int level, int option, Type value)
	{
		return SOCKET_ERROR != ::setsockopt(socket, level, option, reinterpret_cast<const char*>(&value), sizeof(value));
	}
}

GameSession::GameSession()
{
	ResetNetworkState();
}

GameSession::~GameSession()
{
	CloseSession();
}

bool GameSession::AttachSocket(SOCKET socket, uint32 sessionId)
{
	lock_guard<mutex> guard(_sessionLock);

	CloseSocketUnsafe();

	if (socket == INVALID_SOCKET)
		return false;

	_socket = socket;
	_id = sessionId;
	_state = SOCKET_STATE::ST_ALLOC;
	_pendingRecvBytes = 0;
	_sessionToken.fetch_add(1);

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
	_state = SOCKET_STATE::ST_FREE;
	_id = static_cast<uint32>(-1);
	_sessionToken.fetch_add(1);
}

void GameSession::ResetNetworkState()
{
	_socket = INVALID_SOCKET;
	_state = SOCKET_STATE::ST_FREE;
	_pendingRecvBytes = 0;
	_id = static_cast<uint32>(-1);
}

bool GameSession::PostRecv()
{
	if (_socket == INVALID_SOCKET)
		return false;

	DWORD recvFlag = 0;
	_recvContext.Prepare(_pendingRecvBytes, GetSessionToken());

	const int result = ::WSARecv(_socket, &_recvContext._wsaBuf, 1, nullptr, &recvFlag, &_recvContext._over, nullptr);
	if (result == SOCKET_ERROR)
	{
		const int errorCode = ::WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
			return false;
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
