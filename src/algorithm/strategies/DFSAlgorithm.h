#pragma once
#include "../IAlgorithm.h"
#include "../DefaultForgeEngine.h"
#include "../../utils/AlgorithmUtils.hpp"
#include <cstdint>
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
    // ─── State key for memoization ───
    // Lightweight representation of item multiset state for hashing and equality.
    // Replaces the old string-based serialization with direct integer hashing.
    struct StateKey {
        std::vector<int32_t> penalties;     // prior_penalty of each item
        std::vector<int32_t> ench_ids;      // all enchantment IDs in canonical order
        std::vector<int32_t> ench_levels;   // corresponding levels

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

    // Build a StateKey from the current item multiset.
    // Enchantments are iterated in canonical order (EnchSet is std::set, sorted
    // by Ench::operator<) so no additional sorting is needed.
    StateKey make_state_key(const std::vector<ItemStack>& items) const;

    // Core recursive DFS with branch-and-bound pruning.
    void dfs(std::vector<ItemStack>& items, int32_t cost_so_far, ExecutionContext& ctx);

    DefaultForgeEngine _forge_engine;
    int32_t _best_cost;
    EnchStepList _best_steps;
    EnchStepList _current_steps;
    const AlgorithmInput* _input;

    // Hash-based state memoization (replaces old std::unordered_set<std::string>)
    std::unordered_set<StateKey, StateKeyHash> _visited;

    // True when state was restored via deserialize_state() — execute() skips
    // re-initialization and uses the pre-populated _best_cost, _best_steps, _visited.
    bool _state_restored{false};
};
