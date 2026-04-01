#include "pch.h"
#include <iostream>
#include <atomic>
#include <mutex>
#include <windows.h>
#include <future>
#include "ThreadManager.h"
#include "User.h"
#include "GameLogicThread.h"
#include "WorkerThread.h"
#include "DBConnectionPool.h"
#include "SocketManager.h"
#include "DBThread.h"
#include "TimerThread.h"
#include "NPC.h"
#include "Collision.h"

int main()
{
	//소켓API이용해서 네트워크 초기설정 해주기
	SocketManager::Init();
	SocketManager::MakeListenSocket();
	SocketManager::Bind();
	SocketManager::Listen();
	SocketManager::CreateIocpHandle();

	NPC::InitNPC();
	User::InitializePlayers();

	//DB풀 초기화
	GDBConnectionPool->Connect(8);
	InitCollisionTile();
	//Sector 생성
	
	//작업자 스레드 생성
	const uint32 workerCount = thread::hardware_concurrency();
	for (uint32 i = 0; i < workerCount; ++i)
	{
		GThreadManager->Launch([]()
		{
			while (true)
			{
				GThreadManager->InitTLS();
				GWorkerThread->DoWork();
				GThreadManager->DestroyTLS();
			}

		});
	}

	GThreadManager->Launch([]()
	{
		GThreadManager->InitTLS();
		GGameLogicThread->Run();
		GThreadManager->DestroyTLS();
	});

	//DB스레드 생성
	for (int i = 0; i < 2; ++i)
	{
		GThreadManager->Launch([]()
		{
			GThreadManager->InitTLS();
			GDBThread->DoDataBase();
			GThreadManager->DestroyTLS();
		});
	}


	//TImer스레드 생성
	GThreadManager->Launch([]()
	{
		GThreadManager->InitTLS();
		GTimerThread->DoTimer();
		GThreadManager->DestroyTLS();
	});

	GThreadManager->Join();
}
