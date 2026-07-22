#pragma once
#include "../../IAlgorithm.h"
#include "../../components/ItemPool.h"
#include "../../forge_engine/ForgeEngine.h"
#include "../../registries/EnchReg.h"
#include "../../serialization/IAlgorithmSerializer.h"
#include "AStarDiagnostics.h"
#include "AStarMemoryBudget.h"
#include "common/io/ByteStream.h"
#include "common/utils/FlatHashMap.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace algorithm {
class AStarStateSerializer;

/// A* using Item pool + flat ID-indexed states.
class AStarAlgorithm : public IAlgorithm {
  public:
    using ItemID                            = ItemPool::ItemID;
    static constexpr ItemID INVALID_ITEM_ID = ItemPool::INVALID_ITEM_ID;

    explicit AStarAlgorithm(ForgeConfig cfg = {});

    std::string_view name() const noexcept override { return "astar"; }
    std::string_view version() const noexcept override { return "2.0.0"; }
    void execute(const AlgorithmInput &input, ExecutionContext &ctx) override;
    AlgorithmMode supported_mode() const noexcept override { return AlgorithmMode::direct; }

    // ── Serialization support ────────────────────────────────────────────
    IAlgorithmSerializer *get_serializer() noexcept override { return _serializer.get(); }
    const IAlgorithmSerializer *get_serializer() const noexcept override { return _serializer.get(); }
    bool is_resumable() const noexcept override { return true; }

    // friend declaration — serializer accesses private _x_ helpers + state
    friend class AStarStateSerializer;

  private:
    // ─── Step node (16 bytes) ─────────────────────────────────────────────
    struct StepNode {
        int32_t prev{-1}; // parent step index
        ItemID base_id;   // forge 前的 base Item
        ItemID sac_id;    // forge 前的 sacrifice Item
        int32_t cost;     // 步骤消耗
    };

    // ─── Search state (flat ID array) ─────────────────────────────────────
    struct SearchState {
        int32_t g{0};
        int32_t h{0};   // cached heuristic (avoids recomputation at expand)
        size_t hash{0}; // cached _hash_ids (avoids recomputation on pop)
        int32_t step_idx{-1};
        std::vector<ItemID> ids;
    };

    // Priority queue entry (must be outside SearchState to avoid
    // incomplete-type issue at the 'state' member).
    struct PriorityEntry {
        SearchState state;
        int32_t f;
        bool operator>(const PriorityEntry &o) const { return f > o.f; }
    };

    // ─── Config cache (copied from ctx at execute start) ───────────────
    int32_t _max_solutions{0};
    std::chrono::milliseconds _max_search_time{0};

    // ─── Pool storage (all vector — contiguous) ───────────────────────────
    ItemPool _pool;
    std::vector<StepNode> _step_pool;
    std::vector<PriorityEntry> _open_heap;

    // ─── Helpers ──────────────────────────────────────────────────────────
    int32_t _heuristic(const std::vector<ItemID> &ids) const;
    bool _meets_target(ItemID equip_id) const;
    size_t _hash_ids(const std::vector<ItemID> &ids) const;
    int32_t _greedy_bound(const std::vector<Item> &items, const EnchReg &reg) const;
    int32_t _delta_h(int32_t parent_h, const Item &forged, const Item &sacrifice) const;

    // ─── Config ───────────────────────────────────────────────────────────
    ForgeEngine _forge_engine;
    const EnchReg *_ench_reg{nullptr};
    std::vector<Ench> _target;
    int32_t _best_solution_cost{INT32_MAX};
    int32_t _solutions_found{0};
    AStarMemoryBudget _budget;

    // ─── Diagnostics (populated during execute, written on exit) ─────────
    AStarDiagnostics _diag;

    // ─── Heuristic scratch buffers (mutable, reused across calls) ─────────
    mutable std::vector<int16_t> _h_buf;    // max level per ench id
    mutable std::vector<int16_t> _h_dirty;  // ids touched in current call
    std::vector<int16_t> _h_max;            // persistent per-enchant max (expand state)
    std::vector<int16_t> _target_level_map; // target level per ench, 0 = not target

    // ─── 序列化 ───
    std::unique_ptr<IAlgorithmSerializer> _serializer;
    bool _state_restored{false};
    bool _deserialize_ok{false};
    int64_t _explored{0};
    size_t _state_est{0}; // estimated upper bound on search states
    FlatHashMap<size_t, int32_t> _best_g;

    void _restore_and_execute(const AlgorithmInput &input, ExecutionContext &ctx);

    // ── 序列化访问器 (供 AStarStateSerializer 使用) ────────────────────────
    void _x_export_best_g(ByteStreamWriter &w) const;
    void _x_import_best_g(ByteStreamReader &r);
    int64_t _x_explored() const noexcept { return _explored; }
    void _x_set_explored(int64_t v) { _explored = v; }
};
} // namespace algorithm
