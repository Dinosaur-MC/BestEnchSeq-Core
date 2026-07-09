#pragma once
#include "../IAlgorithm.h"
#include "../forge/DefaultForgeEngine.h"
#include <cstdint>
#include <deque>
#include <vector>

class AStarAlgorithm : public IAlgorithm {
public:
    explicit AStarAlgorithm(ForgeConfig forge_cfg = {})
        : _forge_engine(forge_cfg) {}

    std::string_view name() const noexcept override { return "astar"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override { return _forge_engine; }

    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;

private:
    // ─── Step linked-list ───
    // Instead of copying EnchStepList on every state expansion (O(depth) copy),
    // we link steps backward. The full list is flattened only when a solution
    // is found. This eliminates the O(d) step-copy cost per expansion.
    struct StepNode {
        EnchSolution::EnchStep step;
        const StepNode* prev = nullptr;
    };

    // ─── State ───
    struct SearchState {
        std::vector<ItemStack> items;
        int32_t g{0};
        const StepNode* steps_tail = nullptr;

        // Flatten linked list to vector (called once per solution found)
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
            size_t h = 0;
            for (const auto& item : s.items) {
                size_t item_hash = 0;
                for (const auto& ench : item.enchantments) {
                    item_hash ^= std::hash<int32_t>{}(ench.id) * 0x9e3779b9
                              + std::hash<int32_t>{}(ench.level);
                }
                item_hash ^= std::hash<int32_t>{}(item.prior_penalty) * 0x9e3779b9;
                h ^= item_hash * 0x9e3779b9 + 0x9e3779b9;
            }
            return h;
        }
    };

    // ─── Equality for SearchState (items only) ───
    struct StateEqual {
        bool operator()(const SearchState& a, const SearchState& b) const noexcept {
            if (a.items.size() != b.items.size())
                return false;
            for (size_t i = 0; i < a.items.size(); ++i) {
                if (a.items[i].enchantments.size() != b.items[i].enchantments.size())
                    return false;
                for (const auto& ea : a.items[i].enchantments) {
                    auto it = b.items[i].enchantments.find(ea);
                    if (it == b.items[i].enchantments.end() || it->level != ea.level)
                        return false;
                }
                if (a.items[i].prior_penalty != b.items[i].prior_penalty)
                    return false;
            }
            return true;
        }
    };

    // ─── Priority queue entry (min-heap by f = g + h) ───
    struct PriorityState {
        SearchState state;
        int32_t f;
        bool operator>(const PriorityState& o) const { return f > o.f; }
    };

    // Allocate a StepNode from the pool. Nodes are never freed until the
    // search completes — safe because the pool uses std::deque (no reallocation).
    const StepNode* alloc_step(const StepNode* prev, EnchSolution::EnchStep step) {
        _step_pool.push_back({std::move(step), prev});
        return &_step_pool.back();
    }

    // Admissible heuristic
    int32_t heuristic(const std::vector<ItemStack>& items, const EnchSet& target) const;
    bool meets_target(const ItemStack& item, const ItemStack& target) const;

    DefaultForgeEngine _forge_engine;
    const AlgorithmInput* _input;

    // Pool of StepNodes for the lifetime of the search
    std::deque<StepNode> _step_pool;
};
