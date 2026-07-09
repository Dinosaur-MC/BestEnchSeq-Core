#pragma once
#include "../IAlgorithm.h"
#include "../DefaultForgeEngine.h"
#include "../../utils/AlgorithmUtils.hpp"
#include <cstdint>
#include <deque>
#include <unordered_set>
#include <vector>

class DFSAlgorithm : public IAlgorithm {
public:
    explicit DFSAlgorithm(ForgeConfig forge_cfg = {})
        : _forge_engine(forge_cfg) {}

    std::string_view name() const noexcept override { return "dfs"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override { return _forge_engine; }

    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;

    // ── Serialization (cross-session checkpoint) ──
    bool is_resumable() const noexcept override { return true; }
    std::vector<uint8_t> serialize_state() const override;
    void deserialize_state(const std::vector<uint8_t>& data) override;

private:
    // ─── Forge pair (local to search) ───
    struct ForgePair {
        size_t i, j;
        int32_t est_cost;
    };

    // ─── State key for memoization ───
    struct StateKey {
        std::vector<int32_t> penalties;
        std::vector<int32_t> ench_ids;
        std::vector<int32_t> ench_levels;

        bool operator==(const StateKey& o) const noexcept {
            return penalties == o.penalties
                && ench_ids == o.ench_ids
                && ench_levels == o.ench_levels;
        }
    };

    struct StateKeyHash {
        size_t operator()(const StateKey& k) const noexcept {
            size_t h = 0;
            for (auto p : k.penalties)           AlgorithmUtils::hash_combine(h, (size_t)p);
            for (auto id : k.ench_ids)           AlgorithmUtils::hash_combine(h, (size_t)id);
            for (auto lv : k.ench_levels)        AlgorithmUtils::hash_combine(h, (size_t)lv);
            return h;
        }
    };

    // ─── Iterative DFS frame ───
    // Each frame corresponds to one level of the search tree.
    // The full stack is serialized for pause-and-resume support.
    struct DFSFrame {
        std::vector<ItemStack> items;
        int32_t cost_so_far{0};
        size_t pair_index{0};
        size_t saved_steps_size{0};

        // Backtrack restore (valid when has_backtrack is true)
        ItemStack saved_base;
        ItemStack saved_sac;
        size_t base_idx{0};
        size_t sac_idx{0};
        bool has_backtrack{false};
    };

    StateKey make_state_key(const std::vector<ItemStack>& items) const;

    // Core iterative search loop
    void _dfs_iterative(ExecutionContext& ctx);

    // Collect and sort forge pairs for a given item set
    std::vector<ForgePair> _collect_pairs(const std::vector<ItemStack>& items) const;

    DefaultForgeEngine _forge_engine;
    int32_t _best_cost{INT32_MAX};
    EnchStepList _best_steps;
    EnchStepList _current_steps;
    const AlgorithmInput* _input{nullptr};

    // Hash-based state memoization (simple visited set — first visit wins)
    std::unordered_set<StateKey, StateKeyHash> _visited;

    // Iterative DFS execution stack
    std::vector<DFSFrame> _stack;

    // Lazy-computed forge pairs for each frame (parallel to _stack indices)
    std::deque<std::vector<ForgePair>> _frame_pairs;

    int32_t _solutions_found{0};
    bool _state_restored{false};
};
