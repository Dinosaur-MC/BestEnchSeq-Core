#pragma once
#include <chrono>
#include <cstdint>

// ─── Search configuration ─────────────────────────────────────────
struct SearchConfig {
    int32_t max_solutions = 0;
    int32_t max_depth = 0;
    int32_t memory_mb = 0;
    std::chrono::milliseconds max_search_time{0};
};
