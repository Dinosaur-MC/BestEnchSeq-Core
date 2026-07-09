#pragma once
#include "../IAlgorithm.h"
#include "../DefaultForgeEngine.h"
#include "../CompactForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <deque>
#include <vector>

/// A* algorithm using compact internal representation.
///
/// Uses the same admissible heuristic and search strategy as AStarAlgorithm
/// but operates on compact::Item for faster state copying, O(1) conflict
/// checking via exclusion bitmasks, and reduced memory per state.
///
/// Step recording still uses domain types (converted on-the-fly), so the
/// output format is identical to the original AStarAlgorithm.
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
    // ─── Step linked-list (domain types for output) ───
    struct StepNode {
        EnchSolution::EnchStep step;
        const StepNode* prev = nullptr;
    };

    // ─── State (compact items) ───
    struct SearchState {
        std::vector<compact::Item> items;
        int32_t g{0};
        const StepNode* steps_tail = nullptr;

        EnchStepList flatten_steps() const {
            EnchStepList result;
            std::vector<const StepNode*> nodes;
            for (auto* s = steps_tail; s; s = s->prev)
                nodes.push_back(s);
            result.reserve(nodes.size());
            for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
                result.push_back((*it)->step);
            return result;
        }
    };

    // ─── Hash for SearchState (items only; ignores g, steps) ───
    struct StateHash {
        size_t operator()(const SearchState& s) const noexcept {
            size_t h = s.items.size();
            for (const auto& item : s.items)
                h ^= std::hash<compact::Item>{}(item) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    // ─── Equality for SearchState (items only) ───
    struct StateEqual {
        bool operator()(const SearchState& a, const SearchState& b) const noexcept {
            return a.items == b.items;
        }
    };

    // ─── Priority queue entry (min-heap by f = g + h) ───
    struct PriorityState {
        SearchState state;
        int32_t f;
        bool operator>(const PriorityState& o) const { return f > o.f; }
    };

    // Allocate a StepNode from the pool.
    const StepNode* alloc_step(const StepNode* prev, EnchSolution::EnchStep step) {
        _step_pool.push_back({std::move(step), prev});
        return &_step_pool.back();
    }

    // Admissible heuristic on compact items
    int32_t heuristic(const std::vector<compact::Item>& items) const;
    bool meets_target(const compact::Item& equipment) const;

    // Allocate a StepNode recording a forge step (converts compact→domain)
    const StepNode* record_step(
        const StepNode* prev_tail,
        const compact::Item& base, const compact::Item& sacrifice,
        int32_t cost);

    compact::CompactForgeEngine _compact_forge;
    const compact::EnchReg* _ench_reg{nullptr};
    const Equipment* _equipment{nullptr};
    const AlgorithmInput* _input{nullptr};

    std::deque<StepNode> _step_pool;
};
