#pragma once
#include "../IAlgorithm.h"
#include "../forge/DefaultForgeEngine.h"
#include "../forge/CompactForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <deque>
#include <vector>

/// A* algorithm using compact internal representation.
///
/// Design: during search, NO domain types are touched — only compact::Item
/// and compact::EnchReg. The CompactAdapter is used ONLY at the input
/// boundary (prepare) and output boundary (flatten_steps → EnchStepList).
class CompactAStarAlgorithm : public IAlgorithm {
public:
    explicit CompactAStarAlgorithm(ForgeConfig forge_cfg = {})
        : _compact_forge(forge_cfg.ignore_penalty_cost, forge_cfg.ignore_cost_cap) {}

    std::string_view name() const noexcept override { return "compact_astar"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override {
        static DefaultForgeEngine fallback;
        return fallback;
    }

    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;

private:
    // ─── Step node using compact::EnchStep (no domain types during search) ───
    struct CompactStepNode {
        compact::EnchStep step;
        const CompactStepNode* prev = nullptr;
    };

    // ─── State ───
    struct SearchState {
        std::vector<compact::Item> items;
        int32_t g{0};
        const CompactStepNode* steps_tail = nullptr;
    };

    // ─── Hash (items only; ignores g, steps) ───
    struct StateHash {
        size_t operator()(const SearchState& s) const noexcept {
            size_t h = s.items.size();
            for (const auto& item : s.items)
                h ^= std::hash<compact::Item>{}(item) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    // ─── Equality (items only) ───
    struct StateEqual {
        bool operator()(const SearchState& a, const SearchState& b) const noexcept {
            return a.items == b.items;
        }
    };

    // ─── Priority queue entry ───
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

    // Hot-path helpers (operate purely on compact data)
    int32_t heuristic(const std::vector<compact::Item>& items) const;
    bool meets_target(const compact::Item& equipment) const;

    compact::CompactForgeEngine _compact_forge;
    const compact::EnchReg* _ench_reg{nullptr};

    // Target enchantments extracted at execute start (compact form)
    std::vector<compact::Ench> _target;

    // Step pool (deque for pointer stability — no reallocation)
    std::deque<CompactStepNode> _step_pool;
};
