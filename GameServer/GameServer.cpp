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
#include "Content/GuildService.h"
#include "Content/AuctionService.h"

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

int main(int argc, char* argv[])
{
	// --view-range N : 시야 범위(기본 9). 포트폴리오 부하 테스트 조건은 5.
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (std::string(argv[i]) == "--view-range")
		{
			const int range = std::atoi(argv[i + 1]);
			GViewRange = std::clamp(range, 3, SECTOR_RANGE);
		}
		else if (std::string(argv[i]) == "--port")
		{
			const int port = std::atoi(argv[i + 1]);
			if (port > 0 && port < 65536) GServerPort = port;
		}
	}
	std::cout << "View range: " << GViewRange << std::endl;
	if (GServerPort != PORT_NUM) std::cout << "Port: " << GServerPort << std::endl;

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

	//DB풀 초기화 — DB 샤드(스레드) 하나가 동시에 커넥션 하나를 쓰므로 같은 수로 맞춘다.
	const bool dbConnected = GDBConnectionPool->Connect(DBThread::kShardCount);

	// Phase 5 결사 · 경매장(docs/adr/0001): 접속을 받기 전(워커 스레드를 띄우기 전) 메인 스레드에서 DB로 서비스 상태를 만든다.
	// 못 읽으면 두 서비스를 켜지 않는다(요청에는 Busy) — 번호가 겹친 채 거래를 받지 않기 위해.
	{
		auto db = dbConnected ? GDBConnectionPool->Pop() : nullptr;   // 연결이 없으면 Pop은 기다리기만 한다
		int64_t maxListing = 0, maxSettlement = 0;
		int maxGuild = 0;
		auto guild = std::make_shared<GuildService>();
		auto auction = std::make_shared<AuctionService>();
		const bool ok = db != nullptr && db->AuctionMaxIds(maxListing, maxSettlement, maxGuild)
			&& guild->LoadFromDB(*db, maxGuild) && auction->LoadFromDB(*db, maxListing, maxSettlement);
		if (ok)
		{
			GGuildService = guild;
			GAuctionService = auction;
			std::cout << "[Phase5] guilds=" << guild->GuildCount() << " members=" << guild->MemberCount()
				<< " listings=" << auction->ListingCount() << " settlements=" << auction->SettlementCount()
				<< " hourSec=" << AuctionService::HourSec() << "\n" << std::flush;
		}
		else
			std::cout << "[Phase5] 결사 · 경매장 DB 불러오기 실패 — 두 서비스를 끈다\n" << std::flush;
		// 장애 시험의 기준: 그로시 = 지갑 + 받지 않은 대금 + 보증금 · 수수료, 물건 = 소지품 + 판매 중 + 받지 않은 물건
		uint64_t wallet = 0, settleAdena = 0, invUnits = 0, listingUnits = 0, settleUnits = 0, sink = 0;
		if (db != nullptr && db->AuditTotals(wallet, settleAdena, invUnits, listingUnits, settleUnits, sink))
			std::cout << "[Audit] adena wallet=" << wallet << " settle=" << settleAdena << " sink=" << sink
				<< " total=" << (wallet + settleAdena + sink) << " | items inv=" << invUnits << " listing=" << listingUnits
				<< " settle=" << settleUnits << " total=" << (invUnits + listingUnits + settleUnits) << "\n" << std::flush;
		if (db != nullptr)
			GDBConnectionPool->Push(db);
	}
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


	// Phase 5 서비스 스레드(결사 · 경매장) — 각자 작업 큐 하나. 1초 틱(초대 만료 · 경매 만료)은 따로 한 스레드가 넣는다
	if (GGuildService != nullptr && GAuctionService != nullptr)
	{
		GThreadManager->Launch([]()
		{
			GThreadManager->InitTLS();
			GGuildService->Run();
			GThreadManager->DestroyTLS();
		});
		GThreadManager->Launch([]()
		{
			GThreadManager->InitTLS();
			GAuctionService->Run();
			GThreadManager->DestroyTLS();
		});
		GThreadManager->Launch([]()
		{
			GThreadManager->InitTLS();
			while (true)
			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
				GGuildService->Post([]() { GGuildService->Tick(); });
				GAuctionService->Post([]() { GAuctionService->Tick(); });
			}
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

