#pragma once
#include "../IAlgorithm.h"
#include "../forge/ForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <deque>
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
        std::vector<compact::Item> items;
        int32_t g{0};
        const CompactStepNode* steps_tail = nullptr;
    };

    struct StateHash {
        size_t operator()(const SearchState& s) const noexcept {
            size_t h = s.items.size();
            for (const auto& item : s.items)
                h ^= std::hash<compact::Item>{}(item) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct StateEqual {
        bool operator()(const SearchState& a, const SearchState& b) const noexcept {
            return a.items == b.items;
        }
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

    ForgeEngine _compact_forge;
    const compact::EnchReg* _ench_reg{nullptr};

    std::vector<compact::Ench> _target;

    std::deque<CompactStepNode> _step_pool;
};
