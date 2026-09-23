#include "pch.h"
#include <iostream>
#include <atomic>
#include <mutex>
#include <new>
#include <windows.h>
#include <psapi.h>
#include <dbghelp.h>
#include <future>
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "dbghelp.lib")
#include "ThreadManager.h"
#include "User.h"
#include "Monster.h"
#include "GameLogicThread.h"
#include "WorkerThread.h"
#include "DBConnectionPool.h"
#include "SocketManager.h"
#include "DBThread.h"
#include "TimerThread.h"
#include "Collision.h"
#include "World/WorldRegistry.h"
#include "Zone/ZoneManager.h"
#include "World/RegionData.h"
#include "World/WorldRegistry.h"
#include "World/WorldSelfTest.h"

static void WriteCrashLog(const char* tag, DWORD exCode, void* exAddr, EXCEPTION_POINTERS* ep = nullptr)
{
	PROCESS_MEMORY_COUNTERS pmc{};
	pmc.cb = sizeof(pmc);
	GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));

	const HMODULE base = GetModuleHandle(nullptr);
	const uintptr_t rva = exAddr
		? (uintptr_t)exAddr - (uintptr_t)base
		: 0;

	FILE* f = nullptr;
	fopen_s(&f, "crash.log", "a");
	if (!f) return;

	fprintf(f, "[%s] ExceptionCode=0x%08X  Address=%p\n", tag, exCode, exAddr);
	fprintf(f, "  ModuleBase : %p\n", (void*)base);
	fprintf(f, "  RVA        : 0x%08X\n", (DWORD)rva);
	fprintf(f, "  WorkingSet : %zu MB\n", pmc.WorkingSetSize / (1024 * 1024));
	fprintf(f, "  Commit     : %zu MB\n", pmc.PagefileUsage  / (1024 * 1024));

	if (exCode == 0xC0000005)
	{
		fprintf(f, "  -> Access Violation (ASSERT_CRASH or null deref)\n");
		if (ep)
		{
			const ULONG_PTR isWrite  = ep->ExceptionRecord->ExceptionInformation[0];
			const ULONG_PTR faultPtr = ep->ExceptionRecord->ExceptionInformation[1];
			fprintf(f, "  AV %s fault-addr=%p%s\n",
				isWrite ? "WRITE" : "READ",
				(void*)faultPtr,
				faultPtr == 0 ? "  (ASSERT_CRASH)" : "");
		}
	}
	else if (exCode == 0xC00000FD)
		fprintf(f, "  -> Stack Overflow\n");
	else if (exCode == 0xC0000017)
		fprintf(f, "  -> No Memory (VirtualAlloc failed)\n");

	fclose(f);

	// Mini-dump: open crash.dmp in Visual Studio to see exact source + call stack
	if (ep)
	{
		HANDLE hFile = CreateFileA("crash.dmp", GENERIC_WRITE, 0,
			nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			MINIDUMP_EXCEPTION_INFORMATION mei{};
			mei.ThreadId          = GetCurrentThreadId();
			mei.ExceptionPointers = ep;
			mei.ClientPointers    = FALSE;
			MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
				hFile, MiniDumpWithFullMemoryInfo, &mei, nullptr, nullptr);
			CloseHandle(hFile);
		}
	}
}

int main()
{
	std::set_new_handler([]() noexcept {
		WriteCrashLog("OOM", 0, nullptr);
		std::set_new_handler(nullptr);
	});

	SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
		const DWORD code = ep->ExceptionRecord->ExceptionCode;
		void* addr       = ep->ExceptionRecord->ExceptionAddress;
		WriteCrashLog("CRASH", code, addr, ep);
		return EXCEPTION_CONTINUE_SEARCH;
	});

	//소켓API이용해서 네트워크 초기설정 해주기
	SocketManager::Init();
	SocketManager::MakeListenSocket();
	SocketManager::Bind();
	SocketManager::Listen();
	SocketManager::CreateIocpHandle();

	InitCollisionTile();

	// --- 월드(리전 동시 상주) ------------------------------------------------
	//
	// Week 1 의 3번 단계. 마을과 필드를 **같은 프로세스에** 올린다.
	// 좌표만으로는 Zone 이 정해지지 않으므로 ZoneId 에 리전을 박았다
	// (World/WorldRegistry.h 참고).
	static WorldRegistry world;
	{
		std::string error;
		if (!world.Load({ "town", "field_01" }, error))
		{
			cout << "[world] 리전 로드 실패: " << error << endl;
			return -1;
		}
		GWorld = &world;
		world.PrintSummary();

		// Sector/Zone 수학 전수 검사. 여기서 실패하면 그 위에 얹는 이동·시야가
		// 전부 조용히 틀리므로 아예 올리지 않는다.
		if (WorldSelfTest::Run(world) != 0)
		{
			cout << "[world] 자가검사 실패 — 서버를 올리지 않는다" << endl;
			return -1;
		}

		// 리전을 읽고 검사한 **뒤에** Zone 을 만든다. Zone 은 리전 크기를
		// 알아야 자기 Sector 격자를 잡을 수 있다.
		GZoneManager->BuildZones();
	}

	//DB풀 초기화 — DB 샤드(스레드) 하나가 동시에 커넥션 하나를 쓰므로 같은 수로 맞춘다.
	GDBConnectionPool->Connect(DBThread::kShardCount);
	//Sector 생성
	
	//작업자 스레드 생성
	const uint32_t workerCount = thread::hardware_concurrency();
	for (uint32_t i = 0; i < workerCount * 2; ++i)
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

	// defaultThread: Zone 없이 Enqueue되는 작업(로그인 전 세션 정리 등) 처리
	GThreadManager->Launch([]()
	{
		GThreadManager->InitTLS();
		GGameLogicThread->Run();
		GThreadManager->DestroyTLS();
	});

	// Zone 전용 로직 스레드 시작 (Zone당 독립 GameLogicThread)
	for (const ZoneId zoneId : GWorld->AllZoneIds())
	{
		GThreadManager->Launch([zoneId]()
		{
			GThreadManager->InitTLS();
			GZoneManager->RunZone(zoneId);
			GThreadManager->DestroyTLS();
		});
	}

	//DB스레드 생성 — 스레드 하나가 샤드 하나를 전담한다(DBThread.h 참고).
	// 샤드마다 담당 스레드가 정확히 하나씩 있어야 하므로 kShardCount를 그대로 쓴다.
	for (int shardIndex = 0; shardIndex < DBThread::kShardCount; ++shardIndex)
	{
		GThreadManager->Launch([shardIndex]()
		{
			GThreadManager->InitTLS();
			GDBThread->DoDataBase(shardIndex);
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

