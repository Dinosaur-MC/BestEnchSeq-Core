#pragma once
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/serialization/IAlgorithmSerializer.h"
#include "domain/algorithm/components/StepTree.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
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
///
/// Performance notes (ported from bb_dp, 2026-08):
///   - The memo key is a bitmask over the canonicalised `_base_items` (direct
///     mode books are unique, so each subset maps 1:1 to a mask), replacing the
///     previous `unordered_map<ItemCollection, Frontier>` that hashed the whole
///     vector<Item> per lookup (~21% of runtime in cachegrind).
///   - n ≤ 20 uses a flat lock-free cache (`std::atomic<Frontier*>` array
///     indexed by mask) — no shared_mutex contention.  Larger n (which bails to
///     an empty frontier anyway) falls back to a mutex-protected map.
///   - `solve()` returns `const Frontier&`; the cache owns the frontiers via
///     `unique_ptr`, so addresses are stable for the whole pass (no per-hit
///     frontier copies).
class DPMergeAlgorithm : public IAlgorithm {
public:
    explicit DPMergeAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "dp_merge"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    double evaluate(int16_t ench_count) const noexcept override;
    void execute(const AlgorithmInput &input, ExecutionContext& ctx) override;
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

    // Memoisation cache: subset identified by a bitmask over the canonicalised
    // `_base_items`.  n ≤ 20 → flat lock-free array indexed by mask (all real
    // work for dp_merge is n ≤ 20 — larger inputs bail to an empty frontier);
    // otherwise a mutex-protected map fallback.
    static constexpr size_t FLAT_CACHE_MAX_BITS = 20;
    static constexpr size_t MAX_CACHE_ENTRIES = 500000;

    std::unordered_map<uint64_t, std::unique_ptr<Frontier>> _cache;
    mutable std::shared_mutex _cache_mutex;
    bool _using_flat{false};
    std::unique_ptr<std::atomic<Frontier*>[]> _flat_cache;
    size_t _flat_capacity{0};
    // Owns every published frontier for the whole pass, so cache-returned
    // references are stable.  Also used as the pass-lifetime overflow arena
    // for the map fallback.
    std::vector<std::unique_ptr<Frontier>> _owners;
    std::mutex _owners_mutex;

    ForgeEngine _forge_engine;
    const EnchReg* _ench_reg{nullptr};
    std::vector<Ench> _target;
    std::vector<Item> _base_items;  // canonicalised input; masks index into this

    AlgorithmDiagnostics _diag;

    const Frontier& solve(uint64_t mask, bool parallelize);
    const Frontier* cache_get(uint64_t mask) const noexcept;
    const Frontier& cache_put(uint64_t mask, std::unique_ptr<Frontier> f);
    void _prepare_cache(size_t n);
    static void canonicalize(std::vector<Item>& items) noexcept;

    // ── 序列化 ───
    mutable std::unique_ptr<IAlgorithmSerializer> _serializer;
};

static_assert(std::is_nothrow_destructible_v<DPMergeAlgorithm>,
    "DPMergeAlgorithm: destructor must not throw");
static_assert(sizeof(DPMergeAlgorithm) < 8192,
    "DPMergeAlgorithm: size exceeds expected range");

} // namespace algorithm
