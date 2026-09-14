#pragma once
#include <atomic>

inline std::atomic<double> g_meshing_time_sum = 0;
inline std::atomic<double> g_meshing_count = 0;
inline std::atomic<double> g_generating_time_sum = 0;
inline std::atomic<double> g_generating_count = 0;
inline std::atomic_bool g_mesh_naive = false;