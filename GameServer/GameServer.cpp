#include "pch.h"
#include <iostream>
#include <atomic>
#include <mutex>
#include <new>
#include <windows.h>
#include <psapi.h>
#include <dbghelp.h>
#include <future>
#include <filesystem>
#include <iomanip>
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
#include "World/NavMeshBuilder.h"

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

int main(int argc, char** argv)
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

	// --- 프로토콜 레이아웃 출력 -------------------------------------------------
	//
	// GameServer.exe --protocol
	//
	// 클라이언트(C#)와 봇이 **같은 바이트 수로** 읽어야 하므로, 서버가 직접
	// 찍어 주는 값을 유일한 근거로 삼는다. 지형 해시를 세 구현이 대조하는 것과
	// 같은 발상이다 — 사람이 손으로 센 오프셋은 반드시 한 번은 틀린다.
	for (int i = 1; i < argc; ++i)
	{
		if (::strcmp(argv[i], "--protocol") != 0)
			continue;

		cout << "packet                       type  size  fields(offset:size)" << endl;

		const auto line = [](const char* name, int type, size_t size)
		{
			cout << "  " << std::left << std::setw(27) << name << std::right
				<< std::setw(5) << type << std::setw(6) << size;
		};

		line("USER_INPUT_REQ", static_cast<int>(PacketType::USER_INPUT_REQ),
			sizeof(USER_INPUT_REQ_PACKET));
		cout << "  flags:" << offsetof(USER_INPUT_REQ_PACKET, flags)
			<< " seq:" << offsetof(USER_INPUT_REQ_PACKET, seq)
			<< " move_x:" << offsetof(USER_INPUT_REQ_PACKET, move_x)
			<< " move_z:" << offsetof(USER_INPUT_REQ_PACKET, move_z)
			<< " yaw:" << offsetof(USER_INPUT_REQ_PACKET, yaw) << endl;

		line("USER_ENTER_WORLD_ACK", static_cast<int>(PacketType::USER_ENTER_WORLD_ACK),
			sizeof(USER_ENTER_WORLD_ACK_PACKET));
		cout << "  region:" << offsetof(USER_ENTER_WORLD_ACK_PACKET, region)
			<< " id:" << offsetof(USER_ENTER_WORLD_ACK_PACKET, id)
			<< " x:" << offsetof(USER_ENTER_WORLD_ACK_PACKET, x)
			<< " yaw:" << offsetof(USER_ENTER_WORLD_ACK_PACKET, yaw)
			<< " hp:" << offsetof(USER_ENTER_WORLD_ACK_PACKET, hp) << endl;

		line("SUBJECT_SPAWN_NFY", static_cast<int>(PacketType::SUBJECT_SPAWN_NFY),
			sizeof(SUBJECT_SPAWN_NFY_PACKET));
		cout << "  kind:" << offsetof(SUBJECT_SPAWN_NFY_PACKET, subject_kind)
			<< " id:" << offsetof(SUBJECT_SPAWN_NFY_PACKET, id)
			<< " x:" << offsetof(SUBJECT_SPAWN_NFY_PACKET, x)
			<< " name:" << offsetof(SUBJECT_SPAWN_NFY_PACKET, name)
			<< " itemId:" << offsetof(SUBJECT_SPAWN_NFY_PACKET, itemId) << endl;

		line("SUBJECT_TRANSFORM_NFY", static_cast<int>(PacketType::SUBJECT_TRANSFORM_NFY),
			sizeof(SUBJECT_TRANSFORM_NFY_PACKET));
		cout << "  state:" << offsetof(SUBJECT_TRANSFORM_NFY_PACKET, state)
			<< " id:" << offsetof(SUBJECT_TRANSFORM_NFY_PACKET, id)
			<< " x:" << offsetof(SUBJECT_TRANSFORM_NFY_PACKET, x)
			<< " yaw:" << offsetof(SUBJECT_TRANSFORM_NFY_PACKET, yaw)
			<< " time:" << offsetof(SUBJECT_TRANSFORM_NFY_PACKET, server_time) << endl;

		line("USER_MOVE_CORRECTION_NFY", static_cast<int>(PacketType::USER_MOVE_CORRECTION_NFY),
			sizeof(USER_MOVE_CORRECTION_NFY_PACKET));
		cout << "  reason:" << offsetof(USER_MOVE_CORRECTION_NFY_PACKET, reason)
			<< " seq:" << offsetof(USER_MOVE_CORRECTION_NFY_PACKET, seq)
			<< " x:" << offsetof(USER_MOVE_CORRECTION_NFY_PACKET, x)
			<< " yaw:" << offsetof(USER_MOVE_CORRECTION_NFY_PACKET, yaw) << endl;

		cout << endl
			<< "  ObjID " << sizeof(ObjID) << "바이트, NAME_SIZE " << NAME_SIZE
			<< ", MAX_CLIENT_PACKET_SIZE " << MAX_CLIENT_PACKET_SIZE << endl;
		return 0;
	}

	// --- 내비메시 오프라인 빌드 모드 -------------------------------------------
	//
	// GameServer.exe --build-navmesh
	//
	// 7장 — "런타임에 내비메시를 빌드하지 않는다. 바이너리를 로드한다."
	// 여기서 구운 .navmesh 를 커밋하고, 평소 기동은 그걸 읽기만 한다.
	// 별도 툴 프로젝트를 만들지 않은 이유는 NavMeshBuilder.h 주석 참고.
	for (int i = 1; i < argc; ++i)
	{
		if (::strcmp(argv[i], "--build-navmesh") != 0)
			continue;

		int failed = 0;
		for (const char* regionId : { "town", "field_01" })
		{
			const std::string bin = RegionData::FindRegionFile(regionId);
			if (bin.empty())
			{
				cout << "[navmesh] " << regionId << ".bin 이 없다 — build_region.py 먼저" << endl;
				++failed;
				continue;
			}

			std::filesystem::path obj(bin);
			obj.replace_extension(".obj");
			std::filesystem::path nav(bin);
			nav.replace_extension(".navmesh");

			if (!std::filesystem::exists(obj))
			{
				cout << "[navmesh] " << obj.string()
					<< " 가 없다 — tools/build_collision.py 먼저" << endl;
				++failed;
				continue;
			}

			std::string error;
			const NavMeshBuilder::Config config;
			if (!NavMeshBuilder::BuildFromObj(obj.string(), nav.string(), config, error))
			{
				cout << "[navmesh] " << regionId << " 빌드 실패: " << error << endl;
				++failed;
			}
		}
		return failed == 0 ? 0 : 1;
	}

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

