#pragma once
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "common/utils/FlatHashMap.hpp"
#include <chrono>
#include <cstdint>
#include <deque>
#include <vector>

#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"

namespace algorithm {
class DFSAlgorithm : public IAlgorithm {
  public:
    explicit DFSAlgorithm(ForgeConfig cfg = {}) noexcept : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "dfs"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    double evaluate(int16_t ench_count) const noexcept override;
    void execute(const AlgorithmInput &input, ExecutionContext &ctx) override;
    std::unique_ptr<IForgeEngine> get_forge_engine() const noexcept override {
        return std::make_unique<ForgeEngine>(_forge_engine);
    }
    AlgorithmMode supported_mode() const noexcept override { return AlgorithmMode::direct; }

  private:
    struct ForgePair {
        size_t i, j;
        int32_t est_cost;
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

    // ── Hash-based visited_best helpers ─────────────────────────────────
    static size_t _hash_state(const std::vector<Item> &items) noexcept;

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

    FlatHashMap<size_t, int32_t> _visited_best; // state_hash → min_g
    std::vector<DFSFrame> _stack;
    std::deque<std::vector<ForgePair>> _frame_pairs;

    int32_t _solutions_found{0};
    std::chrono::steady_clock::time_point _start_time;

    SearchDiagnostics _diag;
};

// ── Compile-time checks ─────────────────────────────────────────────────
static_assert(std::is_nothrow_destructible_v<DFSAlgorithm>,
    "DFSAlgorithm: destructor must not throw");
static_assert(sizeof(DFSAlgorithm) < 4096,
    "DFSAlgorithm: size exceeds expected range — check for member bloat");

} // namespace algorithm
