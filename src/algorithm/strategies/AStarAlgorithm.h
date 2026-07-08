#pragma once
#include "../IAlgorithm.h"
#include "../DefaultForgeEngine.h"
#include <cstdint>
#include <queue>
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
    // ─── State: a multiset of items (by content identity) ───
    struct SearchState {
        std::vector<ItemStack> items;  // current multiset
        int32_t g;                     // cost so far (sum of forge costs)
        EnchStepList steps;            // forge steps taken
    };

    // ─── Hash for SearchState (items only; ignores g and steps) ───
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

    // ─── Equality for SearchState (items only, with level-aware comparison) ───
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
        int32_t f;  // g + h
        bool operator>(const PriorityState& o) const { return f > o.f; }
    };

    // Admissible heuristic: sum of missing enchantment costs (book multiplier),
    // ignoring penalty, incompatibility, and cost cap.
    int32_t heuristic(const std::vector<ItemStack>& items, const EnchSet& target) const;

    // True if item meets or exceeds all target requirements (equipment + enchants).
    bool meets_target(const ItemStack& item, const ItemStack& target) const;

    DefaultForgeEngine _forge_engine;
    const AlgorithmInput* _input;
};
