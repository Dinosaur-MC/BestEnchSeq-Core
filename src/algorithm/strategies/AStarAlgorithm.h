#pragma once
#include "../IAlgorithm.h"
#include "../forge/ForgeEngine.h"
#include "algorithm/components/AStarMemoryBudget.h"
#include "algorithm/components/AStarDiagnostics.h"
#include "registries/CompactedRegistries.h"
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

/// A* using Item pool + flat ID-indexed states.
class AStarAlgorithm : public IAlgorithm {
public:
    using ItemID = int32_t;
    static constexpr ItemID INVALID_ITEM_ID = -1;

    explicit AStarAlgorithm(ForgeConfig cfg = {}) noexcept
        : _compact_forge(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "astar"; }
    std::string_view version() const noexcept override { return "2.0.0"; }
    void configure(const ForgeConfig &cfg) noexcept override { _compact_forge.set_config(cfg); }

    void execute(
        const std::vector<compact::Item>& items,
        const compact::EnchReg& reg,
        const std::vector<compact::Ench>& target,
        ExecutionContext& ctx
    ) override;

    // Inject budget before execute().
    void set_budget(AStarMemoryBudget budget) noexcept { _budget = budget; }

private:
    // ─── Item pool (deduplicated) ─────────────────────────────────────────
    class ItemPool {
        std::vector<compact::Item> _items;
        // 64-bit hash → ItemID.  On the ~10^-12 chance of a hash collision,
        // a full content comparison catches the false positive.
        std::unordered_map<size_t, ItemID> _dedup;
        size_t _max_items{10'000'000};
    public:
        void set_max(size_t n) noexcept { _max_items = n; }

        ItemID add(compact::Item item) {
            size_t h = std::hash<compact::Item>{}(item);
            auto it = _dedup.find(h);
            if (it != _dedup.end()) {
                if (_items[it->second] == item)
                    return it->second;
                // Hash collision — probe forward (extremely rare).
                for (size_t probe = 1; probe < 64; ++probe) {
                    auto it2 = _dedup.find(h + probe);
                    if (it2 == _dedup.end()) break;
                    if (_items[it2->second] == item)
                        return it2->second;
                }
            }
            if (_items.size() >= _max_items) return INVALID_ITEM_ID;
            ItemID id = static_cast<ItemID>(_items.size());
            _items.push_back(std::move(item));
            _dedup.emplace(h, id);
            return id;
        }

        const compact::Item& operator[](ItemID id) const noexcept {
            return _items[static_cast<size_t>(id)];
        }

        size_t size()     const noexcept { return _items.size(); }
        size_t capacity() const noexcept { return _items.capacity(); }
        void reserve(size_t n) { _items.reserve(n); _dedup.reserve(n); }
        void clear() { _items.clear(); _dedup.clear(); }
    };

    // ─── Step node (16 bytes) ─────────────────────────────────────────────
    struct StepNode {
        int32_t prev{-1};       // parent step index
        ItemID  base_id;        // forge 前的 base Item
        ItemID  sac_id;         // forge 前的 sacrifice Item
        int32_t cost;           // 步骤消耗
    };

    // ─── Search state (flat ID array) ─────────────────────────────────────
    struct SearchState {
        int32_t  g{0};
        int32_t  step_idx{-1};
        std::vector<ItemID> ids;
    };

    // Priority queue entry (must be outside SearchState to avoid
    // incomplete-type issue at the 'state' member).
    struct PriorityEntry {
        SearchState state;
        int32_t f;
        bool operator>(const PriorityEntry& o) const { return f > o.f; }
    };

    // ─── Config cache (copied from ctx at execute start) ───────────────
    int32_t _max_solutions{0};
    std::chrono::milliseconds _max_search_time{0};

    // ─── Pool storage (all vector — contiguous) ───────────────────────────
    ItemPool _pool;
    std::vector<StepNode> _step_pool;
    std::vector<PriorityEntry> _open_heap;

    // ─── Helpers ──────────────────────────────────────────────────────────
    int32_t _heuristic(const std::vector<ItemID>& ids) const;
    bool    _meets_target(ItemID equip_id) const;
    size_t  _hash_ids(const std::vector<ItemID>& ids) const;
    int32_t _greedy_bound(const std::vector<compact::Item>& items,
                           const compact::EnchReg& reg) const;

    // ─── Config ───────────────────────────────────────────────────────────
    ForgeEngine _compact_forge;
    const compact::EnchReg* _ench_reg{nullptr};
    std::vector<compact::Ench> _target;
    int32_t _best_solution_cost{INT32_MAX};
    int32_t _solutions_found{0};
    AStarMemoryBudget _budget;

    // ─── Diagnostics (populated during execute, written on exit) ─────────
    AStarDiagnostics _diag;

    // ─── Heuristic scratch buffers (mutable, reused across calls) ─────────
    mutable std::vector<int16_t> _h_buf;        // max level per ench id
    mutable std::vector<int16_t> _h_dirty;      // ids touched in current call
};
