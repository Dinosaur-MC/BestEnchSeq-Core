#pragma once
#include "../IAlgorithm.h"
#include "../DefaultForgeEngine.h"
#include <cstdint>
#include <vector>

class DFSAlgorithm : public IAlgorithm {
public:
    explicit DFSAlgorithm(ForgeConfig forge_cfg = {})
        : _forge_engine(forge_cfg) {}

    std::string_view name() const noexcept override { return "dfs"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override { return _forge_engine; }

    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;

private:
    // Returns lower bound cost estimate for remaining work (admissible heuristic).
    // For each target enchantment not fully satisfied by current, computes
    // (missing_level * book_multiplier), ignoring conflicts and penalty costs.
    int32_t lower_bound(const EnchSet& current, const EnchSet& target) const;

    // Check if item meets all requirements of the target.
    bool meets_target(const ItemStack& item, const ItemStack& target) const;

    // Core recursive DFS with branch-and-bound pruning.
    // Tries all forge pair orderings, pruning when cost_so_far + lower_bound >= _best_cost.
    void dfs(std::vector<ItemStack>& items, int32_t cost_so_far, ExecutionContext& ctx);

    DefaultForgeEngine _forge_engine;
    int32_t _best_cost;
    EnchStepList _best_steps;
    EnchStepList _current_steps;
    const AlgorithmInput* _input;
};
