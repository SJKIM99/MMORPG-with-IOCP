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

#include "Protocol.h"
#include "NetworkModule.h"

HANDLE g_hiocp = INVALID_HANDLE_VALUE;

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

    SOCKET        client_socket = INVALID_SOCKET;
    OverlappedEx  recv_over{};
    unsigned char packet_buf[MAX_PACKET_SIZE]{};
    int           buffered_bytes = 0;
    high_resolution_clock::time_point next_move_time{};
    high_resolution_clock::time_point next_attack_time{};
    high_resolution_clock::time_point next_skill_time{};
    high_resolution_clock::time_point next_chat_time{};
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
    constexpr int ACCEPT_DELAY = 50;

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

    void ScheduleBehavior(CLIENT& client)
    {
        const auto now = high_resolution_clock::now();
        client.next_move_time   = now + milliseconds(RandomRange(400, 600));
        client.next_attack_time = now + milliseconds(RandomRange(400, 600));
        client.next_skill_time  = now + milliseconds(RandomRange(5200, 7000));
        client.next_chat_time   = now + milliseconds(RandomRange(10000, 20000));
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
        client.next_move_time   = {};
        client.next_attack_time = {};
        client.next_skill_time  = {};
        client.next_chat_time   = {};
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

    [[nodiscard]] char ChooseMoveDirection(const CLIENT& client)
    {
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
    case static_cast<char>(PacketType::PLAYER_ATTACK_NFY):
    case static_cast<char>(PacketType::SC_CHAT):
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
    pkt.attack_time = static_cast<uint32_t>(NowMilliseconds());
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
    num_connections.fetch_add(1);
    if (!ConnectClient(currentConnections))
        DisconnectClient(currentConnections);
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
        int  connectedCount = 0;

        for (int i = 0; i < currentConnections; ++i)
        {
            auto& client = g_clients[i];
            if (!client.connected.load()) continue;

            ++connectedCount;
            if (client.dead) { ++localDead; continue; }

            totalMonsters += client.visible_monsters;
            totalPlayers  += client.visible_players;

            if (client.next_move_time <= now)
                SendMovePacket(i);
            if (client.visible_monsters > 0 && client.next_attack_time <= now)
                SendAttackPacket(i);
            if (client.visible_monsters > 0 && client.next_skill_time  <= now)
                SendSkillPacket(i);
            if (client.next_chat_time <= now)
                SendChatPacket(i);
        }

        dead_clients_count.store(localDead, memory_order_relaxed);

        if (connectedCount > 0)
        {
            const float fConn = static_cast<float>(connectedCount);
            g_avg_visible_monsters.store(static_cast<float>(totalMonsters) / fConn, memory_order_relaxed);
            g_avg_visible_players.store (static_cast<float>(totalPlayers)  / fConn, memory_order_relaxed);
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
