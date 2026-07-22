#include "AStarMemoryBudget.h"
#include <algorithm>
namespace algorithm {

// Per-entry byte costs (adjusted for real platform)
namespace {
    constexpr int64_t COST_BEST_G     = 16;   // FlatHashMap: key(8) + val(4) + occupied(1) + slack
    constexpr int64_t COST_OPEN_SET   = 88;   // SearchState in vector
    constexpr int64_t COST_STEP_POOL  = 16;   // StepNode
    constexpr int64_t COST_ITEM_POOL  = 100;  // avg Item + EnchSet capacity

    // Allocation ratios (sum = 100)
    constexpr int64_t RATIO_BEST_G     = 50;
    constexpr int64_t RATIO_OPEN_SET   = 25;
    constexpr int64_t RATIO_STEP_POOL  = 20;
    constexpr int64_t RATIO_ITEMS_POOL = 5;

    int64_t available(int64_t default_mb) noexcept {
        return default_mb > 0 ? default_mb : 2048;
    }
}

AStarMemoryBudget AStarMemoryBudget::from_memory_mb(int64_t total_mb, int32_t num_items) noexcept {
    AStarMemoryBudget b;
    if (total_mb <= 0) return b;

    // 10% reserve for OS / stack / other overhead
    constexpr double RESERVE = 0.10;
    int64_t usable = static_cast<int64_t>(static_cast<double>(total_mb) * (1.0 - RESERVE) * 1024 * 1024);

    auto entries_for = [&](int64_t ratio, int64_t cost) -> int64_t {
        if (cost == 0) return 0;
        int64_t raw = (usable * ratio / 100) / cost;
        return std::max<int64_t>(raw, num_items + 2);
    };

    b.max_explored   = entries_for(RATIO_BEST_G,     COST_BEST_G);
    b.max_open_set   = entries_for(RATIO_OPEN_SET,   COST_OPEN_SET);
    b.max_step_pool  = entries_for(RATIO_STEP_POOL,  COST_STEP_POOL);
    b.max_items_pool = entries_for(RATIO_ITEMS_POOL, COST_ITEM_POOL);

    // Pre-reserve at 50% of max each
    b.reserve_open_set   = static_cast<size_t>(b.max_open_set   / 2);
    b.reserve_step_pool  = static_cast<size_t>(b.max_step_pool  / 2);
    b.reserve_items_pool = static_cast<size_t>(b.max_items_pool / 2);

    return b;
}

AStarMemoryBudget AStarMemoryBudget::auto_detect(int32_t num_items, int64_t default_mb) noexcept {
    int64_t mb = available(default_mb);
    if (mb <= 0) mb = 2048;
    return from_memory_mb(mb, num_items);
}

} // namespace algorithm
