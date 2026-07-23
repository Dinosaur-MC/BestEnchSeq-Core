#pragma once
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/components/ItemPool.h"
#include "TTTable.h"
#include "IDAStarDiagnostics.h"
#include "domain/algorithm/registries/EnchReg.h"
#include <chrono>
#include <cstdint>
#include <vector>

/// DFS branch-and-bound with cost-aware transposition table.
///
/// Exhaustive search with best_g pruning (TT tracks min g per state).
/// Combines DFS memory footprint with optimality guarantees.
namespace algorithm {

class IDAStarAlgorithm : public IAlgorithm {
public:
    using ItemID = ItemPool::ItemID;

    explicit IDAStarAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "idastar"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;
    AlgorithmMode supported_mode() const noexcept override {
        return AlgorithmMode::direct;
    }

private:
    /// Lightweight step stored on the DFS path (IDs, not full Items).
    struct IDALightStep {
        ItemID base_id;
        ItemID sac_id;
        int32_t cost;
    };

    /// Exhaustive DFS branch-and-bound with best_g pruning.
    void _dfs(std::vector<ItemID>& ids, int32_t g,
              int32_t& best_cost, ExecutionContext& ctx);

    bool _meets_target(const std::vector<ItemID>& ids) const;
    int32_t _compute_h() const;

    ItemPool _pool;
    ForgeEngine _forge_engine;
    const EnchReg* _ench_reg{nullptr};
    std::vector<Ench> _target;

    mutable std::vector<int16_t> _h_buf;
    mutable std::vector<int16_t> _h_dirty;
    std::vector<int16_t> _h_max;
    std::vector<int16_t> _target_level_map;

    TTTable _tt;
    std::vector<IDALightStep> _current_path;
    std::vector<IDALightStep> _solution_path;

    int64_t _nodes_visited{0};
    int32_t _solutions_found{0};
    int32_t _max_solutions{0};
    std::chrono::milliseconds _max_search_time{0};
    std::chrono::steady_clock::time_point _start_time;

    IDAStarDiagnostics _diag;
};

} // namespace algorithm
