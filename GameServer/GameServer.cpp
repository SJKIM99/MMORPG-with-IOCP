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
#include "Zone/ZoneLayout.h"
#include "Zone/ZoneManager.h"

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
	// ==== TEMP DB TEST (Inventory 테이블 실제 ODBC 왕복 확인용) ====
	// 테스트가 끝났다고 말씀하시면 이 블록 전체와 아래 include 한 줄을 제거합니다.
	{
		if (!GDBConnectionPool->Connect(8))
		{
			// Connect가 실패하면 풀이 비어있는 채로 남는데, 그 상태에서 Pop()을 부르면
			// 영원히 대기(데드락)하므로 여기서 반드시 먼저 걸러낸다.
			std::cout << "[DBTest] DB 연결 실패 — DSN(DB_TermProject) 설정을 확인하세요.\n";
			return 1;
		}

		auto conn = GDBConnectionPool->Pop();
		const string testName = "InventoryTest";

		// Inventory.playerId -> Players.playerId FK를 만족시키기 위해 테스트 플레이어를
		// 먼저 보장한다. AddNewPlayer는 이미 있으면 무시하도록 되어 있고(IF NOT EXISTS),
		// 어느 쪽이든 항상 playerId를 돌려주므로 이 테스트를 여러 번 돌려도 안전하다.
		int playerId = 0;
		if (!conn->AddUserInfoInDataBase(testName, "test", 0, 0, 1, 0, playerId))
		{
			std::cout << "[DBTest] AddUserInfoInDataBase 실패\n";
			GDBConnectionPool->Push(conn);
			return 1;
		}
		std::cout << "[DBTest] playerId=" << playerId << "\n";

		auto printRows = [&conn, playerId]()
		{
			auto rows = conn->ExtractInventory(playerId);
			if (rows.empty())
			{
				std::cout << "[DBTest]   (행 없음)\n";
				return;
			}
			for (const auto& row : rows)
			{
				std::cout << "[DBTest]   slot=" << row._slotIndex << " item=" << row._itemId
					<< " count=" << row._count << " equipped=" << row._equipped << "\n";
			}
		};

		std::cout << "[DBTest] 1) SaveInventorySlot(신규 삽입: slot=0, item=1, count=5, equipped=0) -> "
			<< conn->SaveInventorySlot(playerId, 0, 1, 5, false) << "\n";
		std::cout << "[DBTest]    ExtractInventory 결과 (expect: slot=0 item=1 count=5 equipped=0):\n";
		printRows();

		std::cout << "[DBTest] 2) SaveInventorySlot(같은 슬롯 upsert: slot=0, item=1, count=99, equipped=1) -> "
			<< conn->SaveInventorySlot(playerId, 0, 1, 99, true) << "\n";
		std::cout << "[DBTest]    ExtractInventory 결과 (expect: 행 1개, count=99 equipped=1):\n";
		printRows();

		std::cout << "[DBTest] 3) DeleteInventorySlot(slot=0) -> "
			<< conn->DeleteInventorySlot(playerId, 0) << "\n";
		std::cout << "[DBTest]    ExtractInventory 결과 (expect: 행 없음):\n";
		printRows();

		GDBConnectionPool->Push(conn);
		std::cout << "[DBTest] done\n";
		return 0;
	}
	// ==== END TEMP DB TEST ====

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

	// 몬스터 초기화는 각 Zone 스레드의 Zone::Run() 안에서 GSector 설정 후 수행된다.
	// (메인 스레드에서 호출하면 GSector=nullptr로 크래시 발생)

	//DB풀 초기화
	GDBConnectionPool->Connect(8);
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
	for (ZoneId zoneId = 0; zoneId < static_cast<ZoneId>(ZoneLayout::ZoneCount); ++zoneId)
	{
		GThreadManager->Launch([zoneId]()
		{
			GThreadManager->InitTLS();
			GZoneManager->RunZone(zoneId);
			GThreadManager->DestroyTLS();
		});
	}

	//DB스레드 생성
	for (int i = 0; i < 8; ++i)
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

