#pragma once
#include "Platform.h"
#include <chrono>
#include <cstdint>

namespace algorithm {

// ─── Search configuration ─────────────────────────────────────────
struct SearchConfig {
    int32_t max_solutions = 0;
    int32_t max_depth     = 0;
    int32_t memory_mb     = 0;
    std::chrono::milliseconds max_search_time{0};
};

// ─── Forge configuration ────────────────────────────────────────────────────
struct ForgeConfig {

    bool ignore_penalty_cost = false;
    bool ignore_repair_cost  = false; // when true, skip equip+equip repair fee (+2)
    bool ignore_cost_cap     = false;
    MCE platform             = MCE::Java;
};

}; // namespace algorithm
