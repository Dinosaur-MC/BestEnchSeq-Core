#pragma once
#include "../IAlgorithm.h"
#include "../DefaultForgeEngine.h"
#include <cstdint>
#include <vector>

// ─── Hierarchical Merge Strategy ───
//
// Three-phase heuristic for large-scale enchanting problems (7+ books).
// Provides high-quality approximate solutions in < 1ms even for 14 books.
//
// Phase 1 (optional): Deduplicate — merge same-enchantment books together
//   to reduce total item count when > 7 books.
//
// Phase 2: Group by enchantment multiplier tier, then merge within each
//   group using penalty-balancing, then merge groups in multiplier order.
//
// Phase 3: Apply the merged books to equipment in multiplier order
//   (low multiplier first — cheaper enchants go on first so expensive ones
//    don't pay as much penalty).
//
// Expected error vs optimal: 5-10% (vs 15-25% for naive greedy).

class HierarchicalMergeStrategy : public IAlgorithm {
public:
    explicit HierarchicalMergeStrategy(ForgeConfig forge_cfg = {})
        : _forge_engine(forge_cfg) {}

    std::string_view name() const noexcept override { return "hierarchical"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override { return _forge_engine; }

    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;

private:
    // Merge a group of items into a single stack using penalty-balancing.
    // Returns the merged item and records steps.
    ItemStack merge_group(
        std::vector<ItemStack>& group,
        EnchStepList& steps,
        ExecutionContext& ctx);

    // Compute the effective multiplier of an item (max of its enchants' book mult)
    static int32_t effective_multiplier(const ItemStack& item);

    DefaultForgeEngine _forge_engine;
};
