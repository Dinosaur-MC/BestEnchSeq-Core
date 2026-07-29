#pragma once
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/serialization/IAlgorithmSerializer.h"
#include "domain/algorithm/components/StepTree.h"
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace algorithm {
class DPMergeStateSerializer;

/// Divide-and-conquer DP merge-order optimizer (port of https://github.com/iamcal/enchant-order).
///
/// Algorithm:
///   1. Enumerate all 2-partitions of the current item set (Catalan split).
///   2. Recursively solve each partition (memoized).
///   3. Combine sub-results via forge_into, try both base/sacrifice directions.
///   4. Bucket by (EnchSet, PPN, type): within each equivalence class, keep
///      only the cheapest entry.
class DPMergeAlgorithm : public IAlgorithm {
public:
    explicit DPMergeAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "dp_merge"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    double evaluate(int16_t ench_count) const noexcept override;
    void execute(AlgorithmInput input, ExecutionContext& ctx) override;
    std::unique_ptr<IForgeEngine> get_forge_engine() const noexcept override {
        return std::make_unique<ForgeEngine>(_forge_engine);
    }
    AlgorithmMode supported_mode() const noexcept override {
        return AlgorithmMode::direct;
    }

    // ── Serialization support ─────────────────────────────────────────
    void init(const AlgorithmInput &input, const ExecutionContext &ctx) override;
    IAlgorithmSerializer *get_serializer() noexcept override;
    const IAlgorithmSerializer *get_serializer() const noexcept override;
    bool is_resumable() const noexcept override { return true; }

    friend class DPMergeStateSerializer;

private:
    struct ParetoEntry {
        int64_t cost{0};         // total cumulative cost (levels), int64_t to prevent overflow
        uint8_t ppn{0};           // prior work penalty
        Item item;                // the resulting item
        StepTree step_tree;       // merge history (shared DAG)

        ParetoEntry() = default;
        ParetoEntry(int64_t c, uint8_t p, Item i, StepTree t)
            : cost(c), ppn(p), item(std::move(i)), step_tree(std::move(t)) {}
    };

    struct Frontier {
        std::vector<ParetoEntry> entries;

        void insert(ParetoEntry entry);
        bool empty() const { return entries.empty(); }
    };

    // Memoisation cache: item-set → Pareto frontier.
    std::unordered_map<ItemCollection, Frontier> _cache;
    mutable std::shared_mutex _cache_mutex;

    ForgeEngine _forge_engine;
    const EnchReg* _ench_reg{nullptr};
    std::vector<Ench> _target;

    AlgorithmDiagnostics _diag;

    Frontier solve(std::vector<Item> items, bool parallelize = false);
    static void canonicalize(std::vector<Item>& items) noexcept;

    // Cache size limit prevents unbounded memory growth for large N.
    static constexpr size_t MAX_CACHE_ENTRIES = 500000;

    // ── 序列化 ───
    mutable std::unique_ptr<IAlgorithmSerializer> _serializer;
};

static_assert(std::is_nothrow_destructible_v<DPMergeAlgorithm>,
    "DPMergeAlgorithm: destructor must not throw");
static_assert(sizeof(DPMergeAlgorithm) < 8192,
    "DPMergeAlgorithm: size exceeds expected range");

} // namespace algorithm
