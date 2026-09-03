#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <WinSock2.h>
#include <winsock.h>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace std;
using namespace chrono;

extern HWND hWnd;

const static int MAX_TEST        = 500000;
const static int MAX_CLIENTS     = MAX_TEST * 2 + 10000;
const static int MAX_PACKET_SIZE = 255;
const static int MAX_BUFF_SIZE   = 255;

#pragma comment(lib, "ws2_32.lib")

// 서버/클라이언트/스트레스테스트가 각자 프로토콜을 손으로 복제해서 들고
// 있으면 하나가 바뀔 때 나머지가 조용히 어긋난다(실제로 이 파일도 PacketType
// 순서와 SUBJECT_ATTACK_NFY_PACKET 필드가 실제 서버와 어긋나 있었다) — Client와
// 동일하게 GameServer의 Protocol.h를 절대경로로 직접 참조해 단일 소스로 통일한다.
#include "C:\Repository\MMORPG-with-IOCP\MMORPG-with-IOCP\GameServer\Protocol.h"
#include "NetworkModule.h"

HANDLE g_hiocp = INVALID_HANDLE_VALUE;

// 봇이 인벤토리를 다룰 때 쓰는 상수.
// 아이템 id는 서버의 GameServer/Item/ItemTable.cpp가 원본이다 — Protocol.h에는
// 없는 값이라 여기 옮겨 적는다(클라이언트의 ITEM_ASSET_TABLE도 같은 방식).
constexpr uint16_t INVALID_SLOT        = 0xFFFF;
constexpr uint16_t HEALTH_POTION_ID    = 1;  // 유일한 소비 아이템
constexpr uint16_t FIRST_EQUIPMENT_ID  = 2;  // 2번부터는 전부 장비(검)

// ConsumeReceivedBytes()는 MAX_PACKET_SIZE를 넘는 패킷을 받으면 그 클라이언트를
// 그냥 끊어버린다. 서버는 로그인 직후 모든 접속에 ITEM_LIST_ACK(인벤토리 30칸
// 고정 배열)를 보내므로, 이게 버퍼보다 커지면 "모든 봇이 로그인 직후 조용히
// 끊기는" 형태로 부하 테스트 전체가 망가진다. 원인을 런타임에 헤매지 않도록
// 빌드 타임에 막는다 — MAX_INVENTORY_SLOTS를 늘리면 여기서 먼저 걸린다.
static_assert(sizeof(ITEM_LIST_ACK_PACKET) <= MAX_PACKET_SIZE,
    "ITEM_LIST_ACK_PACKET이 수신 버퍼보다 큽니다. MAX_PACKET_SIZE/MAX_BUFF_SIZE를 늘리세요.");

enum OPTYPE { OP_SEND, OP_RECV, OP_DO_MOVE };

high_resolution_clock::time_point last_connect_time;

struct OverlappedEx {
    WSAOVERLAPPED  over{};
    WSABUF         wsabuf{};
    unsigned char  IOCP_buf[MAX_BUFF_SIZE]{};
    OPTYPE         event_type   = OP_RECV;
    int            event_target = -1;
};

struct CLIENT {
    ObjID    id{};
    short    x = 0;
    short    y = 0;
    uint16_t maxHp = PLAYER_MAX_HP;
    uint16_t hp    = PLAYER_MAX_HP;
    uint8_t  level = 1;
    uint32_t exp   = 0;
    int      visible_monsters = 0;
    int      visible_players  = 0;
    bool     dead        = false;
    bool     facing_left = false;
    int      zone_id     = -1;
    uint64_t last_sent_move_ms = 0;
    atomic_bool connected{ false };

    // --- 인벤토리 상태 ---
    // 슬롯 30칸을 통째로 미러링하면 클라이언트 수만큼 곱해져 메모리가 크게 늘어난다.
    // 봇은 "행동할 슬롯 하나"만 알면 충분하므로, 서버가 보내주는 슬롯 알림에서
    // 포션 슬롯과 장비 슬롯만 뽑아 들고 있는다.
    uint16_t potion_slot  = INVALID_SLOT;
    uint16_t potion_count = 0;
    uint16_t equip_slot   = INVALID_SLOT;
    bool     equipped     = false;
    int      visible_items = 0;
    // 지금 주우러 가는 필드 아이템(있으면 이동이 이쪽으로 유도된다).
    ObjID    target_item_id{};
    short    target_item_x  = -1;
    short    target_item_y  = -1;
    bool     has_target_item = false;

    SOCKET        client_socket = INVALID_SOCKET;
    OverlappedEx  recv_over{};
    unsigned char packet_buf[MAX_PACKET_SIZE]{};
    int           buffered_bytes = 0;
    high_resolution_clock::time_point next_move_time{};
    high_resolution_clock::time_point next_attack_time{};
    high_resolution_clock::time_point next_skill_time{};
    high_resolution_clock::time_point next_chat_time{};
    high_resolution_clock::time_point next_pickup_time{};
    high_resolution_clock::time_point next_potion_time{};
    high_resolution_clock::time_point next_equip_time{};
    high_resolution_clock::time_point next_discard_time{};
};

array<CLIENT, MAX_CLIENTS> g_clients;

// --- Core counters (exposed via header) ---
atomic_int        num_connections   = 0;
atomic_int        active_clients    = 0;
atomic<int>       global_delay{ 0 };

// --- Extended stats (exposed via header) ---
atomic_int        dead_clients_count{ 0 };
atomic_int        delay_min_val{ INT_MAX };
atomic_int        delay_max_val{ 0 };
atomic_int        delay_avg_val{ 0 };
atomic<TestPhase> current_phase{ TestPhase::RAMP_UP };

// --- Graph history ring buffer ---
float g_delay_history[GRAPH_HISTORY_SIZE]{};
float g_client_history[GRAPH_HISTORY_SIZE]{};
int   g_history_index = 0;

// --- Per-client aggregate stats ---
atomic<float> g_avg_visible_monsters{ 0.0f };
atomic<float> g_avg_visible_players{ 0.0f };

// --- Zone player tracking (4x4 = 16 zones) ---
std::atomic<int> g_zone_player_count[ZONE_COUNT]{};

// --- Global monster state tracking ---
static std::mutex                              g_monsterMtx;
static std::unordered_map<uint64_t, uint8_t>  g_monsterMap;  // databaseID → 0:alive 1:dead
std::atomic_int g_monster_known{ 0 };
std::atomic_int g_monster_dead { 0 };

// --- Player dead tracking (all players: bots + human) ---
static std::mutex                              g_playerMtx;
static std::unordered_map<uint64_t, uint8_t>  g_playerMap;   // databaseID → 0:alive 1:dead
std::atomic_int g_player_dead{ 0 };

// --- Inventory activity (exposed via header) ---
std::atomic_int   g_items_picked{ 0 };
std::atomic_int   g_potions_used{ 0 };
std::atomic<float> g_avg_visible_items{ 0.0f };

// --- Point cloud buffers ---
float point_cloud[MAX_TEST * 2];
int   point_states[MAX_TEST];

// --- Internal-only ---
atomic_int client_to_close = 0;
vector<thread*> worker_threads;
thread test_thread;

static int GetZoneId(short x, short y) noexcept
{
    int col = static_cast<int>(x) / 500;
    int row = static_cast<int>(y) / 500;
    if (col < 0) col = 0; else if (col > 3) col = 3;
    if (row < 0) row = 0; else if (row > 3) row = 3;
    return row * 4 + col;
}

namespace
{
    constexpr int DELAY_LIMIT  = 100;
    constexpr int DELAY_LIMIT2 = 150;
    constexpr int ACCEPT_DELAY = 5;    // ms per client slot during ramp-up
    constexpr int MAX_BATCH    = 100;  // max clients to connect in one tick

    [[nodiscard]] uint64_t NowMilliseconds() noexcept
    {
        return static_cast<uint64_t>(
            duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count());
    }

    [[nodiscard]] int RandomRange(int minValue, int maxValue)
    {
        thread_local std::mt19937 generator{
            static_cast<uint32_t>(::GetTickCount64()) ^ static_cast<uint32_t>(::GetCurrentThreadId())
        };
        std::uniform_int_distribution<int> distribution(minValue, maxValue);
        return distribution(generator);
    }

    // 각 행동의 간격은 서로 다른 서버 경로에 고르게 부하를 주도록 잡았다.
    // 이동/공격(0.4~0.6초)이 가장 잦고, 스킬(5초 쿨타임)과 포션(5초 쿨타임)은
    // 서버가 강제하는 쿨타임보다 살짝 길게 잡아 대부분의 요청이 실제로 통과하게
    // 한다. 줍기는 몬스터를 잡아 아이템이 떨어진 뒤에야 의미가 있으므로 중간
    // 빈도로, 장착/버리기는 실제 플레이에서도 드문 행동이라 훨씬 길게 잡았다.
    void ScheduleBehavior(CLIENT& client)
    {
        const auto now = high_resolution_clock::now();
        client.next_move_time    = now + milliseconds(RandomRange(400, 600));
        client.next_attack_time  = now + milliseconds(RandomRange(400, 600));
        client.next_skill_time   = now + milliseconds(RandomRange(5200, 7000));
        client.next_chat_time    = now + milliseconds(RandomRange(10000, 20000));
        client.next_pickup_time  = now + milliseconds(RandomRange(800, 1400));
        client.next_potion_time  = now + milliseconds(RandomRange(5200, 7000));
        client.next_equip_time   = now + milliseconds(RandomRange(8000, 15000));
        client.next_discard_time = now + milliseconds(RandomRange(30000, 60000));
    }

    void ResetRuntimeState(CLIENT& client)
    {
        client.connected        = false;
        client.id               = ObjID{};
        client.x                = 0;
        client.y                = 0;
        client.maxHp            = PLAYER_MAX_HP;
        client.hp               = PLAYER_MAX_HP;
        client.level            = 1;
        client.exp              = 0;
        client.visible_monsters = 0;
        client.visible_players  = 0;
        client.dead             = false;
        client.facing_left      = false;
        client.zone_id          = -1;
        client.last_sent_move_ms = 0;
        client.buffered_bytes   = 0;
        client.potion_slot      = INVALID_SLOT;
        client.potion_count     = 0;
        client.equip_slot       = INVALID_SLOT;
        client.equipped         = false;
        client.visible_items    = 0;
        client.target_item_id   = ObjID{};
        client.target_item_x    = -1;
        client.target_item_y    = -1;
        client.has_target_item  = false;
        client.next_move_time    = {};
        client.next_attack_time  = {};
        client.next_skill_time   = {};
        client.next_chat_time    = {};
        client.next_pickup_time  = {};
        client.next_potion_time  = {};
        client.next_equip_time   = {};
        client.next_discard_time = {};
        ::ZeroMemory(client.packet_buf, sizeof(client.packet_buf));
    }

    void ResetForConnect(CLIENT& client)
    {
        ResetRuntimeState(client);
        ::ZeroMemory(&client.recv_over, sizeof(client.recv_over));
        client.recv_over.event_type         = OP_RECV;
        client.recv_over.wsabuf.buf         = reinterpret_cast<CHAR*>(client.recv_over.IOCP_buf);
        client.recv_over.wsabuf.len         = sizeof(client.recv_over.IOCP_buf);
    }

    void UpdateDelayEstimate(uint64_t sentMoveTimeMs)
    {
        if (sentMoveTimeMs == 0) return;

        const int delayMs = static_cast<int>(NowMilliseconds() - sentMoveTimeMs);

        // EMA for global_delay (fast, weight 7:1)
        int expected = global_delay.load(memory_order_relaxed);
        int desired;
        do {
            desired = (expected == 0) ? delayMs : ((expected * 7) + delayMs) / 8;
        } while (!global_delay.compare_exchange_weak(expected, desired, memory_order_relaxed));

        // Track min
        int curMin = delay_min_val.load(memory_order_relaxed);
        while (delayMs < curMin &&
               !delay_min_val.compare_exchange_weak(curMin, delayMs, memory_order_relaxed));

        // Track max
        int curMax = delay_max_val.load(memory_order_relaxed);
        while (delayMs > curMax &&
               !delay_max_val.compare_exchange_weak(curMax, delayMs, memory_order_relaxed));

        // Slower EMA for avg (weight 15:1)
        int expAvg = delay_avg_val.load(memory_order_relaxed);
        int desAvg;
        do {
            desAvg = (expAvg == 0) ? delayMs : ((expAvg * 15) + delayMs) / 16;
        } while (!delay_avg_val.compare_exchange_weak(expAvg, desAvg, memory_order_relaxed));
    }

    // 서버가 보내준 슬롯 하나의 최신 상태를 봇의 인벤토리 추적에 반영한다.
    // ITEM_LIST_ACK/ACQUIRE_INF/각종 ACK가 전부 같은 ITEM_SLOT_DATA를 주므로
    // 처리도 한 군데로 모은다. itemId == 0이면 "그 슬롯이 비었다"는 뜻이다.
    void ApplyInventorySlot(CLIENT& client, const ITEM_SLOT_DATA& slot)
    {
        if (slot.itemId == HEALTH_POTION_ID)
        {
            client.potion_slot  = slot.slotIndex;
            client.potion_count = slot.count;
        }
        else if (client.potion_slot == slot.slotIndex)
        {
            // 추적하던 포션 슬롯이 비었거나 다른 아이템으로 바뀌었다.
            client.potion_slot  = INVALID_SLOT;
            client.potion_count = 0;
        }

        if (slot.itemId >= FIRST_EQUIPMENT_ID)
        {
            client.equip_slot = slot.slotIndex;
            client.equipped   = (slot.equipped != 0);
        }
        else if (client.equip_slot == slot.slotIndex)
        {
            client.equip_slot = INVALID_SLOT;
            client.equipped   = false;
        }
    }

    // 주우러 갈 필드 아이템이 있으면 그쪽으로 한 칸 다가가는 방향을 고른다.
    // 서버의 줍기 판정은 "플레이어가 서 있는 바로 그 칸"만 보므로, 이렇게
    // 유도해주지 않으면 봇이 아이템 위에 올라설 일이 거의 없어 줍기 경로가
    // 사실상 테스트되지 않는다.
    [[nodiscard]] bool ChooseDirectionTowardItem(const CLIENT& client, char& outDirection)
    {
        if (!client.has_target_item)
            return false;

        const int dx = client.target_item_x - client.x;
        const int dy = client.target_item_y - client.y;
        if (dx == 0 && dy == 0)
            return false;  // 이미 그 칸 위 — 이동 대신 줍기가 나간다

        // 방향 인코딩: 0:UP(y-) 1:DOWN(y+) 2:LEFT 3:RIGHT 4:UP-LEFT 5:UP-RIGHT
        //              6:DOWN-LEFT 7:DOWN-RIGHT
        if      (dx > 0 && dy > 0) outDirection = 7;
        else if (dx > 0 && dy < 0) outDirection = 5;
        else if (dx < 0 && dy > 0) outDirection = 6;
        else if (dx < 0 && dy < 0) outDirection = 4;
        else if (dx > 0)           outDirection = 3;
        else if (dx < 0)           outDirection = 2;
        else if (dy > 0)           outDirection = 1;
        else                       outDirection = 0;
        return true;
    }

    [[nodiscard]] char ChooseMoveDirection(const CLIENT& client)
    {
        if (char towardItem = 0; ChooseDirectionTowardItem(client, towardItem))
            return towardItem;

        array<char, 8> candidates{};
        int count = 0;

        const bool canUp    = client.y > 0;
        const bool canDown  = client.y < W_HEIGHT - 1;
        const bool canLeft  = client.x > 0;
        const bool canRight = client.x < W_WIDTH  - 1;

        if (canUp)              candidates[count++] = 0;
        if (canDown)            candidates[count++] = 1;
        if (canLeft)            candidates[count++] = 2;
        if (canRight)           candidates[count++] = 3;
        if (canUp   && canLeft) candidates[count++] = 4;
        if (canUp   && canRight)candidates[count++] = 5;
        if (canDown && canLeft) candidates[count++] = 6;
        if (canDown && canRight)candidates[count++] = 7;

        if (count == 0) return 0;
        return candidates[RandomRange(0, count - 1)];
    }
}

void error_display(const char* msg, int err_no)
{
    WCHAR* lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, err_no, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf, 0, NULL);
    std::cout << msg;
    std::wcout << L":" << lpMsgBuf << std::endl;
    MessageBox(hWnd, lpMsgBuf, L"ERROR", 0);
    LocalFree(lpMsgBuf);
}

void DisconnectClient(int clientIndex)
{
    auto& client = g_clients[clientIndex];
    const bool wasConnected = client.connected.exchange(false);

    if (client.client_socket != INVALID_SOCKET)
    {
        closesocket(client.client_socket);
        client.client_socket = INVALID_SOCKET;
    }

    if (wasConnected) {
        --active_clients;
        if (client.zone_id >= 0 && client.zone_id < ZONE_COUNT)
            g_zone_player_count[client.zone_id].fetch_sub(1, std::memory_order_relaxed);
    }

    ResetRuntimeState(client);
}

bool PostRecv(int clientIndex)
{
    auto& client = g_clients[clientIndex];
    if (client.client_socket == INVALID_SOCKET) return false;

    client.recv_over.event_type         = OP_RECV;
    client.recv_over.event_target       = clientIndex;
    client.recv_over.wsabuf.buf         = reinterpret_cast<CHAR*>(client.recv_over.IOCP_buf);
    client.recv_over.wsabuf.len         = sizeof(client.recv_over.IOCP_buf);
    ZeroMemory(&client.recv_over.over, sizeof(client.recv_over.over));

    DWORD recvFlag = 0;
    const int ret = WSARecv(client.client_socket, &client.recv_over.wsabuf, 1, nullptr,
                            &recvFlag, &client.recv_over.over, nullptr);
    if (ret == SOCKET_ERROR)
    {
        const int errNo = WSAGetLastError();
        if (errNo != WSA_IO_PENDING) return false;
    }
    return true;
}

bool SendPacket(int clientIndex, const void* packet)
{
    auto& client = g_clients[clientIndex];
    if (client.client_socket == INVALID_SOCKET || packet == nullptr) return false;

    const auto* rawPacket   = reinterpret_cast<const unsigned char*>(packet);
    const uint16_t packetSize = *reinterpret_cast<const uint16_t*>(rawPacket);
    if (packetSize == 0 || packetSize > MAX_PACKET_SIZE) return false;

    OverlappedEx* over  = new OverlappedEx;
    over->event_type    = OP_SEND;
    over->event_target  = clientIndex;
    memcpy(over->IOCP_buf, rawPacket, packetSize);
    ZeroMemory(&over->over, sizeof(over->over));
    over->wsabuf.buf    = reinterpret_cast<CHAR*>(over->IOCP_buf);
    over->wsabuf.len    = packetSize;

    const int ret = WSASend(client.client_socket, &over->wsabuf, 1, nullptr, 0,
                            &over->over, nullptr);
    if (ret == SOCKET_ERROR)
    {
        const int errNo = WSAGetLastError();
        if (errNo != WSA_IO_PENDING)
        {
            delete over;
            DisconnectClient(clientIndex);
            return false;
        }
    }
    return true;
}

bool ConnectClient(int clientIndex)
{
    auto& client = g_clients[clientIndex];
    ResetForConnect(client);

    client.client_socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                      nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (client.client_socket == INVALID_SOCKET) return false;

    SOCKADDR_IN serverAddr{};
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_port        = htons(PORT_NUM);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    const int connectResult = WSAConnect(client.client_socket,
                                         reinterpret_cast<sockaddr*>(&serverAddr),
                                         sizeof(serverAddr), nullptr, nullptr, nullptr, nullptr);
    if (connectResult != 0)
    {
        DisconnectClient(clientIndex);
        return false;
    }

    if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(client.client_socket), g_hiocp,
                               static_cast<ULONG_PTR>(clientIndex), 0) == nullptr)
    {
        DisconnectClient(clientIndex);
        return false;
    }

    if (!PostRecv(clientIndex))
    {
        DisconnectClient(clientIndex);
        return false;
    }

    USER_LOGIN_REQ_PACKET loginPacket{};
    loginPacket.size = sizeof(loginPacket);
    loginPacket.type = static_cast<char>(PacketType::USER_LOGIN_REQ);
    sprintf_s(loginPacket.name,     "bot%06d", clientIndex);
    sprintf_s(loginPacket.password, "pw%06d",  clientIndex);

    if (!SendPacket(clientIndex, &loginPacket)) return false;
    return true;
}

void ProcessPacket(int clientIndex, unsigned char packet[])
{
    auto& client = g_clients[clientIndex];

    switch (packet[2]) {
    case static_cast<char>(PacketType::USER_LOGIN_ACK):
    {
        const auto* p = reinterpret_cast<const USER_LOGIN_ACK_PACKET*>(packet);
        if (!client.connected.exchange(true))
            ++active_clients;
        client.id     = p->id;
        client.x      = p->x;
        client.y      = p->y;
        client.maxHp  = p->maxhp;
        client.hp     = p->hp;
        client.level  = p->level;
        client.exp    = p->exp;
        client.dead             = false;
        client.visible_monsters = 0;
        client.visible_players  = 0;
        client.last_sent_move_ms = 0;
        {
            const int newZone = GetZoneId(p->x, p->y);
            if (client.zone_id >= 0 && client.zone_id < ZONE_COUNT)
                g_zone_player_count[client.zone_id].fetch_sub(1, std::memory_order_relaxed);
            client.zone_id = newZone;
            g_zone_player_count[newZone].fetch_add(1, std::memory_order_relaxed);
        }
        {
            const uint64_t pid = p->id.GetDatabaseID();
            std::lock_guard<std::mutex> lk(g_playerMtx);
            g_playerMap.emplace(pid, uint8_t(0));
        }
        ScheduleBehavior(client);
        break;
    }
    case static_cast<char>(PacketType::USER_LOGIN_FAIL_ACK):
        DisconnectClient(clientIndex);
        break;

    case static_cast<char>(PacketType::SUBJECT_MOVE_NFY):
    {
        const auto* p = reinterpret_cast<const SUBJECT_MOVE_NFY_PACKET*>(packet);
        if (p->id == client.id)
        {
            client.x = p->x;
            client.y = p->y;
            UpdateDelayEstimate(client.last_sent_move_ms);
            client.last_sent_move_ms = 0;
            const int newZone = GetZoneId(p->x, p->y);
            if (newZone != client.zone_id) {
                if (client.zone_id >= 0 && client.zone_id < ZONE_COUNT)
                    g_zone_player_count[client.zone_id].fetch_sub(1, std::memory_order_relaxed);
                client.zone_id = newZone;
                g_zone_player_count[newZone].fetch_add(1, std::memory_order_relaxed);
            }
        }
        break;
    }
    case static_cast<char>(PacketType::SUBJECT_ADD_NFY):
    {
        const auto* p = reinterpret_cast<const SUBJECT_ADD_NFY_PACKET*>(packet);
        switch (p->id.GetCategory<EnumCategory>())
        {
        case EnumCategory::eMonster:
        {
            ++client.visible_monsters;
            const uint64_t mid = p->id.GetDatabaseID();
            std::lock_guard<std::mutex> lk(g_monsterMtx);
            if (g_monsterMap.emplace(mid, uint8_t(0)).second)
                g_monster_known.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        case EnumCategory::eUser:
        {
            ++client.visible_players;
            const uint64_t pid = p->id.GetDatabaseID();
            std::lock_guard<std::mutex> lk(g_playerMtx);
            auto res = g_playerMap.emplace(pid, uint8_t(0));
            if (!res.second && res.first->second == 1) {
                res.first->second = 0;
                g_player_dead.fetch_sub(1, std::memory_order_relaxed);
            }
            break;
        }
        case EnumCategory::eItem:
        {
            // 바닥에 떨어진 아이템이 시야에 들어왔다. 아직 목표가 없으면
            // 이걸 주우러 간다(이미 목표가 있으면 그쪽을 계속 쫓는다).
            ++client.visible_items;
            if (!client.has_target_item)
            {
                client.target_item_id  = p->id;
                client.target_item_x   = p->x;
                client.target_item_y   = p->y;
                client.has_target_item = true;
            }
            break;
        }
        default: break;
        }
        break;
    }
    case static_cast<char>(PacketType::SUBJECT_REMOVE_NFY):
    {
        const auto* p = reinterpret_cast<const SUBJECT_REMOVE_NFY_PACKET*>(packet);
        switch (p->id.GetCategory<EnumCategory>())
        {
        case EnumCategory::eMonster:
            client.visible_monsters = max(0, client.visible_monsters - 1); break;
        case EnumCategory::eUser:
            client.visible_players  = max(0, client.visible_players  - 1); break;
        case EnumCategory::eItem:
            // 누가 먼저 주웠거나 30초가 지나 소멸했다. 쫓던 대상이면 목표를 놓는다.
            client.visible_items = max(0, client.visible_items - 1);
            if (client.has_target_item && p->id == client.target_item_id)
                client.has_target_item = false;
            break;
        default: break;
        }
        break;
    }
    case static_cast<char>(PacketType::USER_ATTACK_ACK):
        break;

    case static_cast<char>(PacketType::SUBJECT_DIE_NFY):
    {
        const auto* p = reinterpret_cast<const SUBJECT_DIE_NFY_PACKET*>(packet);
        switch (p->id.GetCategory<EnumCategory>())
        {
        case EnumCategory::eMonster:
        {
            client.visible_monsters = max(0, client.visible_monsters - 1);
            const uint64_t mid = p->id.GetDatabaseID();
            std::lock_guard<std::mutex> lk(g_monsterMtx);
            auto it = g_monsterMap.find(mid);
            if (it != g_monsterMap.end() && it->second == 0) {
                it->second = 1;
                g_monster_dead.fetch_add(1, std::memory_order_relaxed);
            }
            break;
        }
        case EnumCategory::eUser:
        {
            const uint64_t pid = p->id.GetDatabaseID();
            {
                std::lock_guard<std::mutex> lk(g_playerMtx);
                auto it = g_playerMap.find(pid);
                if (it != g_playerMap.end() && it->second == 0) {
                    it->second = 1;
                    g_player_dead.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (p->id == client.id)
            {
                client.hp               = p->hp;
                client.dead             = true;
                client.visible_monsters = 0;
                client.visible_players  = 0;
                if (client.zone_id >= 0 && client.zone_id < ZONE_COUNT)
                    g_zone_player_count[client.zone_id].fetch_sub(1, std::memory_order_relaxed);
                client.zone_id = -1;
            }
            else { client.visible_players = max(0, client.visible_players - 1); }
            break;
        }
        default: break;
        }
        break;
    }
    case static_cast<char>(PacketType::SUBJECT_RESPAWN_NFY):
    {
        const auto* p = reinterpret_cast<const SUBJECT_RESPAWN_NFY_PACKET*>(packet);
        switch (p->id.GetCategory<EnumCategory>())
        {
        case EnumCategory::eMonster:
        {
            ++client.visible_monsters;
            const uint64_t mid = p->id.GetDatabaseID();
            std::lock_guard<std::mutex> lk(g_monsterMtx);
            auto res = g_monsterMap.emplace(mid, uint8_t(0));
            if (res.second) {
                g_monster_known.fetch_add(1, std::memory_order_relaxed);
            } else if (res.first->second == 1) {
                res.first->second = 0;
                g_monster_dead.fetch_sub(1, std::memory_order_relaxed);
            }
            break;
        }
        case EnumCategory::eUser:
        {
            const uint64_t pid = p->id.GetDatabaseID();
            {
                std::lock_guard<std::mutex> lk(g_playerMtx);
                auto it = g_playerMap.find(pid);
                if (it != g_playerMap.end() && it->second == 1) {
                    it->second = 0;
                    g_player_dead.fetch_sub(1, std::memory_order_relaxed);
                }
            }
            if (p->id == client.id)
            {
                client.x    = p->x;
                client.y    = p->y;
                client.hp   = p->hp;
                client.dead = false;
                client.visible_monsters = 0;
                client.visible_players  = 0;
                {
                    const int newZone = GetZoneId(p->x, p->y);
                    client.zone_id = newZone;
                    g_zone_player_count[newZone].fetch_add(1, std::memory_order_relaxed);
                }
                ScheduleBehavior(client);
            }
            else { ++client.visible_players; }
            break;
        }
        default: break;
        }
        break;
    }
    case static_cast<char>(PacketType::SUBJECT_ATTACK_NFY):
    {
        const auto* p = reinterpret_cast<const SUBJECT_ATTACK_NFY_PACKET*>(packet);
        client.hp = static_cast<uint16_t>(max(0, p->hp));
        break;
    }
    case static_cast<char>(PacketType::USER_HEAL_INF):
    {
        const auto* p = reinterpret_cast<const USER_HEAL_INF_PACKET*>(packet);
        client.hp = static_cast<uint16_t>(max(0, p->hp));
        break;
    }
    case static_cast<char>(PacketType::USER_STAT_CHANGE_INF):
    {
        const auto* p = reinterpret_cast<const USER_STAT_CHANGE_INF_PACKET*>(packet);
        client.level = p->level;
        client.hp    = p->hp;
        client.maxHp = p->maxhp;
        client.exp   = p->exp;
        break;
    }
    case static_cast<char>(PacketType::ITEM_LIST_ACK):
    {
        // 로그인 직후 1회. 이전 세션에서 들고 있던 아이템을 여기서 파악한다.
        const auto* p = reinterpret_cast<const ITEM_LIST_ACK_PACKET*>(packet);
        client.potion_slot  = INVALID_SLOT;
        client.potion_count = 0;
        client.equip_slot   = INVALID_SLOT;
        client.equipped     = false;
        for (int i = 0; i < (int)p->slotCount && i < MAX_INVENTORY_SLOTS; ++i)
            ApplyInventorySlot(client, p->items[i]);
        break;
    }
    case static_cast<char>(PacketType::ITEM_ACQUIRE_INF):
    {
        const auto* p = reinterpret_cast<const ITEM_ACQUIRE_INF_PACKET*>(packet);
        ApplyInventorySlot(client, p->slot);
        break;
    }
    case static_cast<char>(PacketType::ITEM_PICKUP_ACK):
    {
        // 실제 슬롯 내용은 뒤이어 오는 ITEM_ACQUIRE_INF로 온다 — 여기선 성공 여부만 센다.
        const auto* p = reinterpret_cast<const ITEM_PICKUP_ACK_PACKET*>(packet);
        if (p->success)
        {
            g_items_picked.fetch_add(1, std::memory_order_relaxed);
            client.has_target_item = false;  // 방금 주운 게 목표였을 것이다
        }
        break;
    }
    case static_cast<char>(PacketType::ITEM_USE_ACK):
    {
        const auto* p = reinterpret_cast<const ITEM_USE_ACK_PACKET*>(packet);
        if (p->success)
        {
            ApplyInventorySlot(client, p->slot);
            g_potions_used.fetch_add(1, std::memory_order_relaxed);
        }
        break;
    }
    case static_cast<char>(PacketType::ITEM_EQUIP_ACK):
    {
        const auto* p = reinterpret_cast<const ITEM_EQUIP_ACK_PACKET*>(packet);
        if (p->success) ApplyInventorySlot(client, p->slot);
        break;
    }
    case static_cast<char>(PacketType::ITEM_UNEQUIP_ACK):
    {
        const auto* p = reinterpret_cast<const ITEM_UNEQUIP_ACK_PACKET*>(packet);
        if (p->success) ApplyInventorySlot(client, p->slot);
        break;
    }
    case static_cast<char>(PacketType::ITEM_DISCARD_ACK):
    {
        const auto* p = reinterpret_cast<const ITEM_DISCARD_ACK_PACKET*>(packet);
        if (p->success) ApplyInventorySlot(client, p->slot);
        break;
    }
    case static_cast<char>(PacketType::SYSTEM_MESSAGE_INF):
    {
        // 지금은 인벤토리 가득 참 알림뿐이다. 자리가 없으면 주우러 다녀도
        // 헛수고라 목표를 놓고, 다음 줍기 시도도 뒤로 미룬다.
        const auto* p = reinterpret_cast<const SYSTEM_MESSAGE_INF_PACKET*>(packet);
        if (static_cast<SystemMessageCode>(p->code) == SystemMessageCode::InventoryFull)
        {
            client.has_target_item = false;
            client.next_pickup_time = high_resolution_clock::now() + milliseconds(RandomRange(8000, 15000));
        }
        break;
    }

    case static_cast<char>(PacketType::PLAYER_ATTACK_NFY):
    case static_cast<char>(PacketType::SC_CHAT):
    case static_cast<char>(PacketType::ITEM_SWAP_ACK):
    case static_cast<char>(PacketType::SUBJECT_EQUIP_CHANGE_NFY):
        break;  // 스트레스 테스트에서 처리 불필요 — 무시
    default:
        break;  // 알 수 없는 패킷 타입 — 연결 유지
    }
}

bool ConsumeReceivedBytes(int clientIndex, DWORD ioSize)
{
    auto& client            = g_clients[clientIndex];
    const unsigned char* cursor = client.recv_over.IOCP_buf;
    int remainingBytes      = static_cast<int>(ioSize);

    while (remainingBytes > 0)
    {
        if (client.buffered_bytes < static_cast<int>(sizeof(uint16_t)))
        {
            const int headerBytesNeeded = static_cast<int>(sizeof(uint16_t)) - client.buffered_bytes;
            const int bytesToCopy       = min(headerBytesNeeded, remainingBytes);
            memcpy(client.packet_buf + client.buffered_bytes, cursor, bytesToCopy);
            client.buffered_bytes += bytesToCopy;
            cursor                += bytesToCopy;
            remainingBytes        -= bytesToCopy;
            if (client.buffered_bytes < static_cast<int>(sizeof(uint16_t))) continue;
        }

        const uint16_t packetSize = *reinterpret_cast<const uint16_t*>(client.packet_buf);
        if (packetSize == 0 || packetSize > MAX_PACKET_SIZE) return false;

        const int packetBytesNeeded = static_cast<int>(packetSize) - client.buffered_bytes;
        const int bytesToCopy       = min(packetBytesNeeded, remainingBytes);
        memcpy(client.packet_buf + client.buffered_bytes, cursor, bytesToCopy);
        client.buffered_bytes += bytesToCopy;
        cursor                += bytesToCopy;
        remainingBytes        -= bytesToCopy;

        if (client.buffered_bytes == packetSize)
        {
            unsigned char packet[MAX_PACKET_SIZE]{};
            memcpy(packet, client.packet_buf, packetSize);
            client.buffered_bytes = 0;
            ProcessPacket(clientIndex, packet);
        }
    }
    return true;
}

void SendMovePacket(int clientIndex)
{
    auto& client = g_clients[clientIndex];
    USER_MOVE_REQ_PACKET pkt{};
    pkt.size      = sizeof(pkt);
    pkt.type      = static_cast<char>(PacketType::USER_MOVE_REQ);
    pkt.direction = ChooseMoveDirection(client);
    pkt.move_time = static_cast<uint32_t>(NowMilliseconds());

    switch (pkt.direction) {
    case 2: case 4: case 6: client.facing_left = true;  break;
    case 3: case 5: case 7: client.facing_left = false; break;
    default: break;
    }

    client.last_sent_move_ms = pkt.move_time;
    SendPacket(clientIndex, &pkt);
    client.next_move_time = high_resolution_clock::now() + milliseconds(RandomRange(400, 600));
}

void SendAttackPacket(int clientIndex)
{
    auto& client = g_clients[clientIndex];
    USER_ATTACK_REQ_PACKET pkt{};
    pkt.size        = sizeof(pkt);
    pkt.type        = static_cast<char>(PacketType::USER_ATTACK_REQ);
    pkt.facing      = client.facing_left ? 1 : 0;
    SendPacket(clientIndex, &pkt);
    client.next_attack_time = high_resolution_clock::now() + milliseconds(RandomRange(400, 600));
}

void SendSkillPacket(int clientIndex)
{
    auto& client = g_clients[clientIndex];
    USER_SKILL_REQ_PACKET pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = static_cast<char>(PacketType::USER_SKILL_REQ);
    SendPacket(clientIndex, &pkt);
    client.next_skill_time = high_resolution_clock::now() + milliseconds(RandomRange(5200, 7000));
}

// 서 있는 칸의 아이템을 줍는다(서버가 위치를 직접 보므로 좌표는 보내지 않는다).
// 목표 아이템 위에 올라섰을 때가 성공 확률이 가장 높지만, 놓친 아이템이 발밑에
// 남아있을 수도 있어서 주변에 아이템이 보이면 그냥도 한 번씩 시도한다.
void SendPickupPacket(int clientIndex)
{
    auto& client = g_clients[clientIndex];

    const bool standingOnTarget = client.has_target_item &&
                                  client.x == client.target_item_x &&
                                  client.y == client.target_item_y;
    if (!standingOnTarget && client.visible_items == 0)
    {
        // 주울 것이 안 보이면 패킷을 아끼고 조금 뒤에 다시 본다.
        client.next_pickup_time = high_resolution_clock::now() + milliseconds(RandomRange(1500, 2500));
        return;
    }

    ITEM_PICKUP_REQ_PACKET pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = static_cast<char>(PacketType::ITEM_PICKUP_REQ);
    SendPacket(clientIndex, &pkt);

    // 목표 위에 서 있을 땐 짧게(실패해도 곧 재시도), 아니면 느긋하게.
    client.next_pickup_time = high_resolution_clock::now() +
        milliseconds(standingOnTarget ? RandomRange(400, 700) : RandomRange(1500, 2500));
}

// 체력이 충분히 닳았을 때만 포션을 쓴다 — 가득 찬 상태로 마셔봐야 회복량이
// 잘려나가기만 하고, 실제 플레이 양상과도 멀어진다.
void TryUsePotion(int clientIndex)
{
    auto& client = g_clients[clientIndex];

    const bool wounded = client.maxHp > 0 && client.hp * 100 < client.maxHp * 60;
    if (client.potion_slot != INVALID_SLOT && client.potion_count > 0 && wounded)
    {
        ITEM_USE_REQ_PACKET pkt{};
        pkt.size      = sizeof(pkt);
        pkt.type      = static_cast<char>(PacketType::ITEM_USE_REQ);
        pkt.slotIndex = client.potion_slot;
        SendPacket(clientIndex, &pkt);
    }

    // 서버 쿨타임(5초)보다 살짝 길게 잡아 대부분의 요청이 실제로 통과하게 한다.
    client.next_potion_time = high_resolution_clock::now() + milliseconds(RandomRange(5200, 7000));
}

// 주운 장비를 장착하고, 가끔은 벗기도 한다(탈착 경로와 장착 상태 브로드캐스트도
// 부하 대상이라 한쪽만 계속 돌리지 않는다).
void TryEquipItem(int clientIndex)
{
    auto& client = g_clients[clientIndex];

    if (client.equip_slot != INVALID_SLOT)
    {
        if (!client.equipped)
        {
            ITEM_EQUIP_REQ_PACKET pkt{};
            pkt.size      = sizeof(pkt);
            pkt.type      = static_cast<char>(PacketType::ITEM_EQUIP_REQ);
            pkt.slotIndex = client.equip_slot;
            SendPacket(clientIndex, &pkt);
        }
        else if (RandomRange(0, 3) == 0)
        {
            ITEM_UNEQUIP_REQ_PACKET pkt{};
            pkt.size      = sizeof(pkt);
            pkt.type      = static_cast<char>(PacketType::ITEM_UNEQUIP_REQ);
            pkt.slotIndex = client.equip_slot;
            SendPacket(clientIndex, &pkt);
        }
    }

    client.next_equip_time = high_resolution_clock::now() + milliseconds(RandomRange(8000, 15000));
}

// 아주 가끔 포션 하나를 바닥에 버린다. 버리기 경로 자체를 태우는 목적도 있지만,
// 버려진 아이템이 다시 다른 봇의 줍기 대상이 되어 필드 아이템이 계속 돌게 하는
// 효과도 있다. 봇이 쓸 포션까지 말려버리지 않도록 여유가 있을 때만 버린다.
void TryDiscardItem(int clientIndex)
{
    auto& client = g_clients[clientIndex];

    if (client.potion_slot != INVALID_SLOT && client.potion_count >= 3)
    {
        ITEM_DISCARD_REQ_PACKET pkt{};
        pkt.size      = sizeof(pkt);
        pkt.type      = static_cast<char>(PacketType::ITEM_DISCARD_REQ);
        pkt.slotIndex = client.potion_slot;
        pkt.count     = 1;
        SendPacket(clientIndex, &pkt);
    }

    client.next_discard_time = high_resolution_clock::now() + milliseconds(RandomRange(30000, 60000));
}

void SendChatPacket(int clientIndex)
{
    static const char* const MESSAGES[] = {
        "hello",
        "what's your name",
        "today weather's good",
        "anyone here?",
        "nice to meet you",
        "let's fight together",
        "watch out for monsters",
    };
    constexpr int MSG_COUNT = static_cast<int>(sizeof(MESSAGES) / sizeof(MESSAGES[0]));

    auto& client = g_clients[clientIndex];
    CS_CHAT_PACKET pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = static_cast<char>(PacketType::CS_CHAT);
    ::strncpy_s(pkt.mess, MESSAGES[RandomRange(0, MSG_COUNT - 1)], CHAT_SIZE - 1);
    SendPacket(clientIndex, &pkt);
    client.next_chat_time = high_resolution_clock::now() + milliseconds(RandomRange(10000, 20000));
}

void Worker_Thread()
{
    while (true)
    {
        DWORD        ioSize        = 0;
        ULONG_PTR    completionKey = 0;
        LPOVERLAPPED rawOver       = nullptr;
        const BOOL ret = GetQueuedCompletionStatus(g_hiocp, &ioSize, &completionKey,
                                                   &rawOver, INFINITE);

        auto* over       = reinterpret_cast<OverlappedEx*>(rawOver);
        const int clientIndex = static_cast<int>(completionKey);

        if (over == nullptr) continue;

        if (ret == FALSE)
        {
            if (over->event_type == OP_SEND) delete over;
            DisconnectClient(clientIndex);
            continue;
        }

        if (ioSize == 0)
        {
            if (over->event_type == OP_SEND) delete over;
            DisconnectClient(clientIndex);
            continue;
        }

        if (over->event_type == OP_RECV)
        {
            if (!ConsumeReceivedBytes(clientIndex, ioSize) || !PostRecv(clientIndex))
                DisconnectClient(clientIndex);
        }
        else if (over->event_type == OP_SEND)
        {
            if (ioSize != over->wsabuf.len)
                DisconnectClient(clientIndex);
            delete over;
        }
        else
        {
            delete over;
        }
    }
}

void Adjust_Number_Of_Client()
{
    static int  delayMultiplier   = 1;
    static int  maxLimit          = (numeric_limits<int>::max)();
    static bool increasing        = true;
    static int  stableCount       = 0;
    constexpr int RECOVERY_STABLE_COUNT = 100;

    const int activeClientCount  = active_clients.load();
    const int currentConnections = num_connections.load();
    if (activeClientCount >= MAX_TEST || currentConnections >= MAX_CLIENTS) return;

    const auto now       = high_resolution_clock::now();
    const auto elapsedMs = duration_cast<milliseconds>(now - last_connect_time).count();
    if ((ACCEPT_DELAY * delayMultiplier) > elapsedMs) return;

    const int delaySnapshot = global_delay.load(memory_order_relaxed);

    // Phase: REDUCING — delay too high, start disconnecting
    if (delaySnapshot > DELAY_LIMIT2)
    {
        current_phase = TestPhase::REDUCING;
        if (increasing)
        {
            maxLimit   = activeClientCount;
            increasing = false;
        }
        stableCount = 0;

        if (activeClientCount < 100 || (ACCEPT_DELAY * 10) > elapsedMs) return;

        last_connect_time     = now;
        const int startIndex  = client_to_close.fetch_add(1);
        for (int attempt = 0; attempt < currentConnections; ++attempt)
        {
            const int victim = (startIndex + attempt) % currentConnections;
            if (g_clients[victim].connected.load())
            {
                DisconnectClient(victim);
                break;
            }
        }
        return;
    }

    // Phase: STABLE — delay elevated but below hard limit, slow down
    if (delaySnapshot > DELAY_LIMIT)
    {
        current_phase   = TestPhase::STABLE;
        delayMultiplier = 10;
        stableCount     = 0;
        return;
    }

    // Phase: RAMP_UP — delay is acceptable
    delayMultiplier = 1;

    if (!increasing)
    {
        ++stableCount;
        if (stableCount >= RECOVERY_STABLE_COUNT)
        {
            maxLimit    = (numeric_limits<int>::max)();
            stableCount = 0;
        }
    }

    if (maxLimit - (maxLimit / 20) < activeClientCount)
    {
        current_phase = TestPhase::STABLE;
        return;
    }

    current_phase = TestPhase::RAMP_UP;
    increasing    = true;
    last_connect_time = now;

    // batch connect: one slot per ACCEPT_DELAY ms elapsed, capped at MAX_BATCH
    const int batchSize = min(static_cast<int>(elapsedMs / ACCEPT_DELAY), MAX_BATCH);
    for (int b = 0; b < batchSize; ++b)
    {
        const int cur = num_connections.load();
        if (cur >= MAX_CLIENTS || active_clients.load() >= MAX_TEST) break;
        if (maxLimit - (maxLimit / 20) < active_clients.load()) break;
        num_connections.fetch_add(1);
        if (!ConnectClient(cur))
            DisconnectClient(cur);
    }
}

void Test_Thread()
{
    int historyCounter = 0;

    while (true)
    {
        Adjust_Number_Of_Client();

        const auto now               = high_resolution_clock::now();
        const int  currentConnections = num_connections.load();

        int  localDead    = 0;
        long long totalMonsters = 0;
        long long totalPlayers  = 0;
        long long totalItems    = 0;
        int  connectedCount = 0;

        for (int i = 0; i < currentConnections; ++i)
        {
            auto& client = g_clients[i];
            if (!client.connected.load()) continue;

            ++connectedCount;
            if (client.dead) { ++localDead; continue; }

            totalMonsters += client.visible_monsters;
            totalPlayers  += client.visible_players;
            totalItems    += client.visible_items;

            if (client.next_move_time <= now)
                SendMovePacket(i);
            if (client.visible_monsters > 0 && client.next_attack_time <= now)
                SendAttackPacket(i);
            if (client.visible_monsters > 0 && client.next_skill_time  <= now)
                SendSkillPacket(i);
            if (client.next_chat_time <= now)
                SendChatPacket(i);

            // 인벤토리 행동 — 몬스터를 잡아 아이템이 떨어져야 의미가 생기므로,
            // 위의 전투 행동들과 자연스럽게 맞물려 돌아간다.
            if (client.next_pickup_time <= now)
                SendPickupPacket(i);
            if (client.next_potion_time <= now)
                TryUsePotion(i);
            if (client.next_equip_time <= now)
                TryEquipItem(i);
            if (client.next_discard_time <= now)
                TryDiscardItem(i);
        }

        dead_clients_count.store(localDead, memory_order_relaxed);

        if (connectedCount > 0)
        {
            const float fConn = static_cast<float>(connectedCount);
            g_avg_visible_monsters.store(static_cast<float>(totalMonsters) / fConn, memory_order_relaxed);
            g_avg_visible_players.store (static_cast<float>(totalPlayers)  / fConn, memory_order_relaxed);
            g_avg_visible_items.store   (static_cast<float>(totalItems)    / fConn, memory_order_relaxed);
        }

        // Append history sample every ~500 ms (50 * 10ms)
        if (++historyCounter >= 50)
        {
            historyCounter  = 0;
            const int idx   = g_history_index;
            g_delay_history [idx] = static_cast<float>(global_delay.load(memory_order_relaxed));
            g_client_history[idx] = static_cast<float>(active_clients.load(memory_order_relaxed));
            g_history_index       = (idx + 1) % GRAPH_HISTORY_SIZE;
        }

        this_thread::sleep_for(10ms);
    }
}

void InitializeNetwork()
{
    for (auto& client : g_clients)
    {
        client.connected     = false;
        client.client_socket = INVALID_SOCKET;
        ResetRuntimeState(client);
    }

    num_connections    = 0;
    client_to_close    = 0;
    active_clients     = 0;
    global_delay       = 0;
    dead_clients_count = 0;
    delay_min_val      = INT_MAX;
    delay_max_val      = 0;
    delay_avg_val      = 0;
    current_phase      = TestPhase::RAMP_UP;
    g_history_index    = 0;
    g_avg_visible_monsters.store(0.0f);
    g_avg_visible_players.store(0.0f);
    g_avg_visible_items.store(0.0f);
    g_items_picked.store(0, std::memory_order_relaxed);
    g_potions_used.store(0, std::memory_order_relaxed);
    memset(g_delay_history,  0, sizeof(g_delay_history));
    memset(g_client_history, 0, sizeof(g_client_history));
    for (auto& z : g_zone_player_count) z.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(g_monsterMtx);
        g_monsterMap.clear();
    }
    g_monster_known.store(0, std::memory_order_relaxed);
    g_monster_dead.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(g_playerMtx);
        g_playerMap.clear();
    }
    g_player_dead.store(0, std::memory_order_relaxed);
    last_connect_time  = high_resolution_clock::now();

    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);

    g_hiocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);

    const unsigned int workerCount = max(2u, thread::hardware_concurrency());
    for (unsigned int i = 0; i < workerCount; ++i)
        worker_threads.push_back(new thread{ Worker_Thread });

    test_thread = thread{ Test_Thread };
}

void GetPointCloud(int* size, float** positions, int** states)
{
    int index                    = 0;
    const int currentConnections = num_connections.load();

    for (int i = 0; i < currentConnections; ++i)
    {
        if (!g_clients[i].connected.load()) continue;
        point_cloud [index * 2]     = static_cast<float>(g_clients[i].x);
        point_cloud [index * 2 + 1] = static_cast<float>(g_clients[i].y);
        point_states[index]          = g_clients[i].dead ? 1 : 0;
        ++index;
    }

    *size      = index;
    *positions = point_cloud;
    *states    = point_states;
}
