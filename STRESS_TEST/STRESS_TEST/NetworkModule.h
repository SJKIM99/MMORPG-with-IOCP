#pragma once
#include <atomic>

constexpr int GRAPH_HISTORY_SIZE = 300;
constexpr int ZONE_COUNT         = 16;   // 4x4 zone grid

enum class TestPhase : int { RAMP_UP = 0, STABLE = 1, REDUCING = 2 };

void InitializeNetwork();
void GetPointCloud(int* size, float** positions, int** states);

extern std::atomic_int        global_delay;
extern std::atomic_int        active_clients;
extern std::atomic_int        num_connections;
extern std::atomic_int        dead_clients_count;
extern std::atomic_int        delay_min_val;
extern std::atomic_int        delay_max_val;
extern std::atomic_int        delay_avg_val;
extern std::atomic<TestPhase> current_phase;

extern float g_delay_history[GRAPH_HISTORY_SIZE];
extern float g_client_history[GRAPH_HISTORY_SIZE];
extern int   g_history_index;

extern std::atomic<float> g_avg_visible_monsters;
extern std::atomic<float> g_avg_visible_players;
extern std::atomic<float> g_avg_visible_items;

// Inventory activity (bots pick up drops, drink potions, equip weapons)
extern std::atomic_int g_items_picked;
extern std::atomic_int g_potions_used;

// Zone player counts (index = zone_id 0..15, 4x4 grid)
extern std::atomic<int> g_zone_player_count[ZONE_COUNT];

// Monster tracking
extern std::atomic_int g_monster_known;   // unique monsters seen so far
extern std::atomic_int g_monster_dead;    // currently dead (awaiting respawn)

// Player dead tracking (bots + human player currently dead in-game)
extern std::atomic_int g_player_dead;
