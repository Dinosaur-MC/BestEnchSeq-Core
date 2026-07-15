#pragma once
#include <cstdint>
#include <cstddef>

/// Memory budget for the A* search — splits an overall limit across
/// the internal pools (best_g, open_set, step_pool, items_pool).
struct AStarMemoryBudget {
    int64_t max_explored   = 0;   // best_g / 展开上限
    size_t  max_open_set   = 0;   // priority_queue 条目上限
    size_t  max_step_pool  = 0;   // StepNode 条目上限
    size_t  max_items_pool = 0;   // Item 池条目上限 (distinct Items)

    // 预分配大小 (按上限 50% 预分配)
    size_t  reserve_open_set  = 0;
    size_t  reserve_step_pool = 0;
    size_t  reserve_items_pool = 0;

    static AStarMemoryBudget from_memory_mb(int64_t memory_mb, int32_t num_items) noexcept;
    static AStarMemoryBudget auto_detect(int32_t num_items, int64_t default_mb = 2048) noexcept;
};
