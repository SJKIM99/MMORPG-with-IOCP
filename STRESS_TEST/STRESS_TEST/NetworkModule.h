#pragma once
#include <atomic>

constexpr int GRAPH_HISTORY_SIZE = 300;

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
