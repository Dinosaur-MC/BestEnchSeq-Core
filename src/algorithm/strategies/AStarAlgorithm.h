#pragma once
#include "../IAlgorithm.h"
#include "../forge/ForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

/// A* algorithm using compact internal representation.
class AStarAlgorithm : public IAlgorithm {
public:
    explicit AStarAlgorithm(ForgeConfig cfg = {}) noexcept
        : _compact_forge(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "compact_astar"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    void execute(
        const std::vector<compact::Item>& items,
        const compact::EnchReg& reg,
        const std::vector<compact::Ench>& target,
        ExecutionContext& ctx
    ) override;

private:
    struct CompactStepNode {
        compact::EnchStep step;
        const CompactStepNode* prev = nullptr;
    };

    struct SearchState {
        std::shared_ptr<const std::vector<compact::Item>> items;
        int32_t g{0};
        const CompactStepNode* steps_tail = nullptr;
    };

    struct PriorityState {
        SearchState state;
        int32_t f;
        bool operator>(const PriorityState& o) const { return f > o.f; }
    };

    const CompactStepNode* alloc_step(const CompactStepNode* prev,
                                       compact::EnchStep step) {
        _step_pool.push_back({std::move(step), prev});
        return &_step_pool.back();
    }

    int32_t heuristic(const std::vector<compact::Item>& items) const;
    bool meets_target(const compact::Item& equipment) const;
    int32_t _greedy_bound(const std::vector<compact::Item>& items,
                           const compact::EnchReg& reg) const;

    ForgeEngine _compact_forge;
    const compact::EnchReg* _ench_reg{nullptr};

    std::vector<compact::Ench> _target;

    // Best complete-solution cost found so far (INT32_MAX = none yet).
    // Used for pruning: any state with g >= _best_solution_cost cannot
    // lead to a better solution and can be safely removed from best_g.
    int32_t _best_solution_cost{INT32_MAX};

    std::deque<CompactStepNode> _step_pool;
};
