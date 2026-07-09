#pragma once
#include "../IAlgorithm.h"
#include "../DefaultForgeEngine.h"
#include "../CompactForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <deque>
#include <unordered_set>
#include <vector>

/// DFS algorithm using compact internal representation.
///
/// During search, NO domain types are touched — only compact::Item and
/// compact::EnchReg. The CompactAdapter is used ONLY at the input boundary
/// (prepare) and output boundary (step conversion to domain).
class CompactDFSAlgorithm : public IAlgorithm {
public:
    explicit CompactDFSAlgorithm(ForgeConfig forge_cfg = {})
        : _compact_forge(forge_cfg.ignore_penalty_cost, forge_cfg.ignore_cost_cap)
        , _bound_engine(forge_cfg) {}

    std::string_view name() const noexcept override { return "compact_dfs"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override {
        static DefaultForgeEngine fallback;
        return fallback;
    }

    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;

private:
    // ─── Forge pair ───
    struct ForgePair {
        size_t i, j;
        int32_t est_cost;
    };

    // ─── Hash for vector of items (visited set key) ───
    struct ItemVectorHash {
        size_t operator()(const std::vector<compact::Item>& items) const noexcept {
            size_t h = items.size();
            for (const auto& item : items)
                h ^= std::hash<compact::Item>{}(item) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    // ─── Iterative DFS frame ───
    struct DFSFrame {
        std::vector<compact::Item> items;
        int32_t cost_so_far{0};
        size_t pair_index{0};
        size_t saved_steps_size{0};

        // Backtrack restore (valid when has_backtrack is true)
        compact::Item saved_base;
        compact::Item saved_sac;
        size_t base_idx{0};
        size_t sac_idx{0};
        bool has_backtrack{false};
    };

    void _dfs_iterative(ExecutionContext& ctx, const Equipment* out_eq);
    std::vector<ForgePair> _collect_pairs(const std::vector<compact::Item>& items) const;

    // Hot-path helpers (compact only, no domain deps)
    bool _meets_target(const compact::Item& equipment) const;
    int32_t _heuristic(const compact::Item& equipment) const;

    compact::CompactForgeEngine _compact_forge;
    DefaultForgeEngine _bound_engine;
    const compact::EnchReg* _ench_reg{nullptr};

    // Target enchantments in compact form
    std::vector<compact::Ench> _target;

    int32_t _best_cost{INT32_MAX};
    EnchStepList _best_steps;
    std::vector<compact::EnchStep> _current_steps;

    std::unordered_set<std::vector<compact::Item>, ItemVectorHash> _visited;
    std::vector<DFSFrame> _stack;
    std::deque<std::vector<ForgePair>> _frame_pairs;

    int32_t _solutions_found{0};
};
