#include "pch.h"
#include "SocketManager.h"
#include "WorkerThread.h"
#include "GameSession.h"

SOCKET gListenSocket = NULL;
SOCKET GClientSocket = NULL;
HANDLE gHandle = NULL;

LPFN_CONNECTEX		SocketManager::ConnectEx = nullptr;
LPFN_DISCONNECTEX	SocketManager::DisconnectEx = nullptr;
LPFN_ACCEPTEX		SocketManager::AcceptEx = nullptr;

SocketManager::SocketManager()
{
}

SocketManager::~SocketManager()
{
	Clear();
}

bool SocketManager::MakeListenSocket()
{
	gListenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	return true;
}

bool SocketManager::Bind()
{
	if (!gListenSocket) return false;

	SOCKADDR_IN _serverAddr;
	::memset(&_serverAddr, 0, sizeof(_serverAddr));
	_serverAddr.sin_family = AF_INET;
	_serverAddr.sin_port = htons(PORT_NUM);
	_serverAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (::bind(gListenSocket, reinterpret_cast<sockaddr*>(&_serverAddr), sizeof(_serverAddr)))
		return false;
	return true;
}

bool SocketManager::Listen()
{
	if (!gListenSocket) return false;

	if (::listen(gListenSocket, SOMAXCONN))
		return false;
	cout << "Server Listening..." << endl;
	return true;
}

bool SocketManager::CreateIocpHandle()
{
	gHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(gListenSocket), gHandle, 9999, 0);
	GClientSocket = CreateSocket();
	GAcceptContext.ResetOverlapped();
	DWORD bytesReceived = 0;
	SocketManager::AcceptEx(gListenSocket, GClientSocket, GAcceptContext.m_buffer, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &bytesReceived, static_cast<LPOVERLAPPED>(&GAcceptContext.m_over));

	return true;
}

void SocketManager::Init()
{

	WSADATA wsaData;
	ASSERT_CRASH(::WSAStartup(MAKEWORD(2, 2), OUT & wsaData) == 0);

	SOCKET dummySocket = CreateSocket();
	ASSERT_CRASH(BindWindowsFunction(dummySocket, WSAID_CONNECTEX, reinterpret_cast<LPVOID*>(&ConnectEx)));
	ASSERT_CRASH(BindWindowsFunction(dummySocket, WSAID_DISCONNECTEX, reinterpret_cast<LPVOID*>(&DisconnectEx)));
	ASSERT_CRASH(BindWindowsFunction(dummySocket, WSAID_ACCEPTEX, reinterpret_cast<LPVOID*>(&AcceptEx)));
	Close(dummySocket);
}

void SocketManager::Clear()
{
	::WSACleanup();

}
bool SocketManager::BindWindowsFunction(SOCKET socket, GUID guid, LPVOID* fn)
{
	DWORD bytes = 0;
	return SOCKET_ERROR != ::WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), fn, sizeof(*fn), OUT & bytes, NULL, NULL);
}

SOCKET SocketManager::CreateSocket()
{
	return WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
}

void SocketManager::Close(SOCKET& socket)
{
	if (socket != INVALID_SOCKET) {
		::closesocket(socket);
		socket = INVALID_SOCKET;
	}
}
