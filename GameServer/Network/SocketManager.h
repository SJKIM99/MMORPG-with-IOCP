#pragma once
//#include "Protocol.h"

class WorkerThread;
class GameSession;

class SocketManager
{
public:
	SocketManager();
	~SocketManager();

	static void		Init();
	static bool		MakeListenSocket();
	static bool		Bind();
	static bool		Listen();
	static bool		CreateIocpHandle();
	
	static void		Clear();

	static bool		BindWindowsFunction(SOCKET socket, GUID guid, LPVOID* fn);
	static SOCKET	CreateSocket();
	static void		Close(SOCKET& socket);

public:
	static LPFN_CONNECTEX	ConnectEx;
	static LPFN_DISCONNECTEX	DisconnectEx;
	static LPFN_ACCEPTEX		AcceptEx;
};

extern SOCKET gListenSocket;
extern SOCKET GClientSocket;
extern HANDLE gHandle;
