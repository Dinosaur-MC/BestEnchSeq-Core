#pragma once
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/serialization/IAlgorithmSerializer.h"
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

// ─── StepTree — immutable DAG of forge steps ──────────────────────────
//
// Instead of copying the full steps vector on every forge_pair, each
// ParetoEntry stores a StepTree that shares its prefix nodes with other
// entries via shared_ptr.  The tree is only materialised into a flat
// vector when the final solution is extracted (execute()) or when
// serialising a checkpoint.

namespace algorithm {
class DPMergeStateSerializer;

class StepTree {
public:
    struct Node final {
        EnchStep              step;
        std::shared_ptr<Node> left;   // steps that produced the base item
        std::shared_ptr<Node> right;  // steps that produced the sacrifice
        size_t                depth;

        Node(EnchStep s, std::shared_ptr<Node> l,
             std::shared_ptr<Node> r, size_t d) noexcept
            : step(std::move(s)), left(std::move(l)),
              right(std::move(r)), depth(d) {}
    };

    StepTree() = default;
    explicit StepTree(std::shared_ptr<Node> root) noexcept : _root(std::move(root)) {}

    size_t size() const noexcept { return _root ? _root->depth : 0; }
    bool   empty() const noexcept { return !_root; }

    /// Materialise the tree into a flat vector (in forge order).
    std::vector<EnchStep> materialize() const {
        std::vector<EnchStep> out;
        if (!_root) return out;
        out.reserve(_root->depth);
        _materialize(out, _root.get());
        return out;
    }

    std::shared_ptr<Node> root_ptr() const noexcept { return _root; }

    /// Build a linear chain from a flat step vector (for deserialisation).
    static StepTree from_flat(const std::vector<EnchStep>& flat) {
        std::shared_ptr<Node> cur;
        for (auto it = flat.rbegin(); it != flat.rend(); ++it) {
            cur = std::make_shared<Node>(
                *it, std::move(cur), nullptr, flat.size());
        }
        return StepTree{std::move(cur)};
    }

private:
    std::shared_ptr<Node> _root;

    static void _materialize(std::vector<EnchStep>& out, const Node* n) {
        if (!n) return;
        _materialize(out, n->left.get());
        _materialize(out, n->right.get());
        out.push_back(n->step);
    }
};

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
