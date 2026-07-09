#pragma once
#include "../IAlgorithm.h"
#include "../forge/CompactForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <deque>
#include <unordered_set>
#include <vector>

class CompactDFSAlgorithm : public IAlgorithm {
public:
    explicit CompactDFSAlgorithm(bool ignore_penalty_cost = false,
                                  bool ignore_cost_cap = false) noexcept
        : _compact_forge(ignore_penalty_cost, ignore_cost_cap) {}

    std::string_view name() const noexcept override { return "compact_dfs"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    void execute(
        const std::vector<compact::Item>& items,
        const compact::EnchReg& reg,
        const std::vector<compact::Ench>& target,
        ExecutionContext& ctx
    ) override;

private:
    struct ForgePair {
        size_t i, j;
        int32_t est_cost;
    };

    struct ItemVectorHash {
        size_t operator()(const std::vector<compact::Item>& items) const noexcept {
            size_t h = 0;
            for (const auto& item : items) {
                size_t item_hash = 0;
                for (const auto& e : item.enchs) {
                    size_t ench_hash = static_cast<size_t>(e.id)
                                     ^ (static_cast<size_t>(e.level) << 16);
                    item_hash ^= ench_hash * 0x9e3779b9;
                }
                item_hash ^= static_cast<size_t>(item.ppn) * 0x9e3779b9;
                h ^= item_hash * 0x9e3779b9 + 0x9e3779b9;
            }
            return h;
        }
    };

    struct DFSFrame {
        std::vector<compact::Item> items;
        int32_t cost_so_far{0};
        size_t pair_index{0};
        size_t saved_steps_size{0};

        compact::Item saved_base;
        compact::Item saved_sac;
        size_t base_idx{0};
        size_t sac_idx{0};
        bool has_backtrack{false};
    };

    void _dfs_iterative(ExecutionContext& ctx);
    std::vector<ForgePair> _collect_pairs(const std::vector<compact::Item>& items) const;

    bool _meets_target(const compact::Item& equipment) const;
    int32_t _heuristic(const std::vector<compact::Item>& items) const;
    int32_t _greedy_bound(const std::vector<compact::Item>& items,
                           const compact::EnchReg& reg) const;

    compact::CompactForgeEngine _compact_forge;
    const compact::EnchReg* _ench_reg{nullptr};

    std::vector<compact::Ench> _target;

    int32_t _best_cost{INT32_MAX};
    std::vector<compact::EnchStep> _best_steps;
    std::vector<compact::EnchStep> _current_steps;

    std::unordered_set<std::vector<compact::Item>, ItemVectorHash> _visited;
    std::vector<DFSFrame> _stack;
    std::deque<std::vector<ForgePair>> _frame_pairs;

    int32_t _solutions_found{0};
};
