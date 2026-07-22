#pragma once
#include "../../IAlgorithm.h"
#include "../../forge_engine/ForgeEngine.h"
#include "../../registries/EnchReg.h"
#include "../../types/ConfigTypes.h"
#include "common/utils/HashUtils.hpp"
#include <chrono>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

#include "../../diagnostics/AlgorithmDiagnostics.h"

namespace algorithm {
class DFSAlgorithm : public IAlgorithm {
  public:
    explicit DFSAlgorithm(ForgeConfig cfg = {}) noexcept : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "dfs"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    void execute(const AlgorithmInput &input, ExecutionContext &ctx) override;
    AlgorithmMode supported_mode() const noexcept override { return AlgorithmMode::direct; }

  private:
    struct ForgePair {
        size_t i, j;
        int32_t est_cost;
    };

    struct ItemVectorHash {
        size_t operator()(const std::vector<Item> &items) const noexcept {
            size_t h = items.size();
            for (const auto &item : items) {
                for (const auto &e : item.enchs) {
                    size_t eh = static_cast<size_t>(e.id) ^ (static_cast<size_t>(e.level) << 16);
                    hash_combine(h, eh);
                }
                hash_combine(h, static_cast<size_t>(item.ppn));
                hash_combine(h, static_cast<size_t>(item.dur));
            }
            return h;
        }
    };

    struct DFSFrame {
        std::vector<Item> items;
        int32_t cost_so_far{0};
        size_t pair_index{0};
        size_t saved_steps_size{0};

        Item saved_base;
        Item saved_sac;
        size_t base_idx{0};
        size_t sac_idx{0};
        bool has_backtrack{false};
    };

    void _dfs_iterative(ExecutionContext &ctx);
    std::vector<ForgePair> _collect_pairs(const std::vector<Item> &items) const;

    int32_t _heuristic(const std::vector<Item> &items) const;
    int32_t _greedy_bound(const std::vector<Item> &items, const EnchReg &reg) const;

    ForgeEngine _forge_engine;
    const EnchReg *_ench_reg{nullptr};
    SearchConfig _search_config{};

    // Heuristic scratch buffers (reused across calls, avoids per-call allocation)
    mutable std::vector<int16_t> _h_buf;
    mutable std::vector<int16_t> _h_dirty;

    std::vector<Ench> _target;

    int32_t _best_cost{INT32_MAX};
    std::vector<EnchStep> _best_steps;
    std::vector<EnchStep> _current_steps;

    std::unordered_map<std::vector<Item>, int32_t, ItemVectorHash> _visited_best;
    std::vector<DFSFrame> _stack;
    std::deque<std::vector<ForgePair>> _frame_pairs;

    int32_t _solutions_found{0};
    std::chrono::steady_clock::time_point _start_time;

    SearchDiagnostics _diag;
};
} // namespace algorithm
